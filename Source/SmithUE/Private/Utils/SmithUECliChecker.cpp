// Copyright 2026, 123dx-svg. MIT License.

#include "Utils/SmithUECliChecker.h"

#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "SmithUEModule.h"
#include "SmithUESettings.h"

// ---------------------------------------------------------------------------
// Module-level constants and state (game-thread-confined writes, no lock)
// ---------------------------------------------------------------------------

// Lower bound on the smithue-cli version the plugin needs. This is only a FLOOR:
// the live npm `latest` is queried on every probe and wins whenever it is newer,
// so a CLI release no longer requires bumping this constant for machines to
// upgrade. Bump it only when the plugin genuinely requires a newer CLI (i.e. the
// floor is a compatibility contract, not a release marker).
static constexpr TCHAR kRecommendedCliVersion[] = TEXT("0.15.0");

/** Last known CLI environment state. Written only on the game thread. */
static FCliInfo GCachedCliInfo;

/** Set to true while an async check is in-flight; cleared on game thread when done. */
static FThreadSafeBool bCliCheckInFlight;

/** Set to true while an install/upgrade is in-flight; independent of the check flag. */
static FThreadSafeBool bInstallInFlight;

/** Set by CancelCliInstall(); polled by the install worker loop to terminate the subprocess. */
static FThreadSafeBool bInstallCancelRequested;

/**
 * Never set. Passed to RunBounded for probes that are time-bounded but not
 * user-cancellable, so the one cancel flag stays exclusive to the install.
 */
static FThreadSafeBool bNeverCancel;

/** Hard timeout for the install/upgrade subprocess (seconds). */
static constexpr double kInstallTimeoutSeconds = 120.0;

/**
 * Hard timeout for the `npm view` registry lookup (seconds). Kept short: it runs
 * on every probe and an unreachable registry must not stall the check.
 */
static constexpr double kRegistryQueryTimeoutSeconds = 20.0;

/**
 * Guards the automatic upgrade to once per editor session. Also stops the
 * self-retrigger loop, since a successful install re-runs CheckCliEnvironment().
 * Game thread only.
 */
static bool GAutoUpgradeAttempted = false;

// Static member definition
FOnCliCheckComplete FSmithUECliChecker::OnCliCheckComplete;

// ---------------------------------------------------------------------------
// SmithUECliInternal — pure, testable helpers
// ---------------------------------------------------------------------------

namespace SmithUECliInternal
{

TOptional<FSmithUESemver> ParseSemver(const FString& s)
{
    if (s.IsEmpty())
    {
        return {};
    }

    FString Str = s.TrimStartAndEnd();

    // Strip leading 'v' or 'V'
    if (!Str.IsEmpty() && (Str[0] == TEXT('v') || Str[0] == TEXT('V')))
    {
        Str = Str.Mid(1);
    }

    // Strip build metadata — everything from the first '+' onwards
    int32 PlusIdx = INDEX_NONE;
    if (Str.FindChar(TEXT('+'), PlusIdx))
    {
        Str = Str.Left(PlusIdx);
    }

    // Split off prerelease — everything from the first '-' onwards
    FString Core;
    FString Prerelease;
    int32 DashIdx = INDEX_NONE;
    if (Str.FindChar(TEXT('-'), DashIdx))
    {
        Core       = Str.Left(DashIdx);
        Prerelease = Str.Mid(DashIdx + 1);
    }
    else
    {
        Core = Str;
    }

    // Parse major.minor.patch
    TArray<FString> Parts;
    Core.ParseIntoArray(Parts, TEXT("."), /*bCullEmpty=*/false);
    if (Parts.Num() != 3)
    {
        return {};
    }

    for (const FString& Part : Parts)
    {
        if (Part.IsEmpty())
        {
            return {};
        }
        for (TCHAR Ch : Part)
        {
            if (!FChar::IsDigit(Ch))
            {
                return {};
            }
        }
    }

    FSmithUESemver Result;
    Result.Major      = FCString::Atoi(*Parts[0]);
    Result.Minor      = FCString::Atoi(*Parts[1]);
    Result.Patch      = FCString::Atoi(*Parts[2]);
    Result.Prerelease = MoveTemp(Prerelease);
    return Result;
}

int32 CompareSemver(const FSmithUESemver& a, const FSmithUESemver& b)
{
    if (a.Major != b.Major) { return a.Major < b.Major ? -1 : 1; }
    if (a.Minor != b.Minor) { return a.Minor < b.Minor ? -1 : 1; }
    if (a.Patch != b.Patch) { return a.Patch < b.Patch ? -1 : 1; }

    // Prerelease ordering per semver §11:
    //   a release version (empty prerelease) is HIGHER than any prerelease
    const bool aIsRelease = a.Prerelease.IsEmpty();
    const bool bIsRelease = b.Prerelease.IsEmpty();

    if (aIsRelease && bIsRelease) { return 0; }
    if (aIsRelease)               { return 1;  } // release > prerelease
    if (bIsRelease)               { return -1; } // prerelease < release

    // Both have prerelease — lexicographic comparison
    const int32 Cmp = a.Prerelease.Compare(b.Prerelease, ESearchCase::CaseSensitive);
    if (Cmp < 0) return -1;
    if (Cmp > 0) return 1;
    return 0;
}

TOptional<FString> ClassifyVersionProbe(int32 rc, const FString& out)
{
    if (rc != 0)
    {
        return {};
    }

    FString Trimmed = out.TrimStartAndEnd();
    if (Trimmed.IsEmpty())
    {
        return {};
    }

    // Must parse as valid semver to be accepted
    if (!ParseSemver(Trimmed).IsSet())
    {
        return {};
    }

    // Return trimmed, stripping any leading 'v'
    if (!Trimmed.IsEmpty() && (Trimmed[0] == TEXT('v') || Trimmed[0] == TEXT('V')))
    {
        Trimmed = Trimmed.Mid(1);
    }

    // Also strip build metadata from the returned string
    int32 PlusIdx = INDEX_NONE;
    if (Trimmed.FindChar(TEXT('+'), PlusIdx))
    {
        Trimmed = Trimmed.Left(PlusIdx);
    }

    return Trimmed;
}

TOptional<FString> ParseNpmLsCliVersion(const FString& json)
{
    if (json.IsEmpty())
    {
        return {};
    }

    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(json);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        return {};
    }

    const TSharedPtr<FJsonObject>* DepsObjPtr = nullptr;
    if (!Root->TryGetObjectField(TEXT("dependencies"), DepsObjPtr)
        || DepsObjPtr == nullptr
        || !(*DepsObjPtr).IsValid())
    {
        return {};
    }
    const TSharedPtr<FJsonObject>& DepsObj = *DepsObjPtr;

    const TSharedPtr<FJsonObject>* CliObjPtr = nullptr;
    if (!DepsObj->TryGetObjectField(TEXT("smithue-cli"), CliObjPtr)
        || CliObjPtr == nullptr
        || !(*CliObjPtr).IsValid())
    {
        return {};
    }
    const TSharedPtr<FJsonObject>& CliObj = *CliObjPtr;

    FString Version;
    if (!CliObj->TryGetStringField(TEXT("version"), Version) || Version.IsEmpty())
    {
        return {};
    }

    return Version;
}

FString ResolveTargetCliVersion(const FString& Floor, const FString& NpmLatest)
{
    const TOptional<FSmithUESemver> FloorVer  = ParseSemver(Floor);
    const TOptional<FSmithUESemver> LatestVer = ParseSemver(NpmLatest);

    if (!LatestVer.IsSet())
    {
        // Offline, npm missing, or a malformed dist-tag: fall back to the floor.
        return FloorVer.IsSet() ? Floor : FString();
    }
    if (!FloorVer.IsSet())
    {
        return NpmLatest;
    }
    return CompareSemver(LatestVer.GetValue(), FloorVer.GetValue()) > 0 ? NpmLatest : Floor;
}

bool IsCliOutdated(const FString& Installed, const FString& Target)
{
    const TOptional<FSmithUESemver> TargetVer = ParseSemver(Target);
    if (!TargetVer.IsSet())
    {
        // Nothing credible to compare against — never report a false "outdated".
        return false;
    }

    const TOptional<FSmithUESemver> InstalledVer = ParseSemver(Installed);
    if (!InstalledVer.IsSet())
    {
        // A target exists but the installed version is unreadable: treat as outdated
        // so a reinstall can repair it.
        return true;
    }

    return CompareSemver(InstalledVer.GetValue(), TargetVer.GetValue()) < 0;
}

} // namespace SmithUECliInternal

// ---------------------------------------------------------------------------
// Internal helpers (not exported)
// ---------------------------------------------------------------------------

namespace
{

/**
 * Run an executable directly via FPlatformProcess::ExecProcess (like the git
 * pattern in UpdateChecker). Stdout/stderr are captured in OutStdOut/OutStdErr.
 *
 * WHY NOT cmd.exe /c:
 *   UE ExecProcess uses DETACHED_PROCESS which breaks pipe inheritance for
 *   cmd.exe child processes — node/npm inside cmd.exe never write to the pipe,
 *   producing rc=0 but empty stdout. Calling node.exe directly (a real PE
 *   executable) works identically to how git.exe is called in UpdateChecker.
 *
 * WHY node.exe for npm:
 *   npm.cmd is a batch file — ExecProcess cannot launch .cmd files directly.
 *   The solution is to run node.exe with the npm-cli.js script path, which is
 *   the same mechanism npm.cmd uses internally.
 */
int32 RunDirect(const FString& Exe, const FString& Args, FString& OutStdOut, FString& OutStdErr)
{
    int32 rc = -1;
    FPlatformProcess::ExecProcess(*Exe, *Args, &rc, &OutStdOut, &OutStdErr);
    return rc;
}

/** Outcome of a bounded subprocess run. */
struct FBoundedRun
{
    int32 Rc          = -1;
    bool  bTimedOut   = false;
    bool  bCancelled  = false;
};

/**
 * Run an executable with a hard timeout and cooperative cancellation, capturing
 * combined stdout+stderr. Uses CreateProc (hidden) + a pipe + a poll loop so a
 * stuck network call (npm) can never block forever — unlike ExecProcess which
 * has no timeout. On timeout or cancel the whole process tree is terminated.
 *
 *   CancelFlag  : polled each tick; when true -> terminate + bCancelled.
 *   TimeoutSecs : wall-clock budget; when exceeded -> terminate + bTimedOut.
 */
FBoundedRun RunBounded(const FString& Exe, const FString& Args,
                       double TimeoutSecs, const FThreadSafeBool& CancelFlag,
                       FString& OutCombined)
{
    FBoundedRun Result;

    void* ReadPipe  = nullptr;
    void* WritePipe = nullptr;
    if (!FPlatformProcess::CreatePipe(ReadPipe, WritePipe))
    {
        OutCombined = TEXT("failed to create stdout pipe");
        return Result;
    }

    // bLaunchDetached=false, bLaunchHidden=true, bLaunchReallyHidden=true (no console flash).
    // WritePipe is inherited by the child for BOTH stdout and stderr on Windows.
    FProcHandle Proc = FPlatformProcess::CreateProc(
        *Exe, *Args, /*bLaunchDetached*/false, /*bLaunchHidden*/true, /*bLaunchReallyHidden*/true,
        /*OutProcessID*/nullptr, /*PriorityModifier*/0, /*WorkingDir*/nullptr, /*PipeWriteChild*/WritePipe);

    if (!Proc.IsValid())
    {
        FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
        OutCombined = TEXT("failed to launch subprocess");
        return Result;
    }

    const double Start = FPlatformTime::Seconds();
    while (FPlatformProcess::IsProcRunning(Proc))
    {
        OutCombined += FPlatformProcess::ReadPipe(ReadPipe);

        if (CancelFlag)
        {
            FPlatformProcess::TerminateProc(Proc, /*KillTree*/true);
            Result.bCancelled = true;
            break;
        }
        if (FPlatformTime::Seconds() - Start > TimeoutSecs)
        {
            FPlatformProcess::TerminateProc(Proc, /*KillTree*/true);
            Result.bTimedOut = true;
            break;
        }
        FPlatformProcess::Sleep(0.1f);
    }

    // Drain whatever is left in the pipe (process may have just exited).
    OutCombined += FPlatformProcess::ReadPipe(ReadPipe);

    FPlatformProcess::GetProcReturnCode(Proc, &Result.Rc);
    FPlatformProcess::CloseProc(Proc);
    FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
    return Result;
}

/**
 * Locate npm-cli.js relative to a node.exe path.
 * npm ships as node_modules/npm/bin/npm-cli.js inside the nodejs install dir.
 * Returns empty string if not found.
 */
FString FindNpmCliJs()
{
    // Search common install locations — %ProgramFiles%\nodejs is most common
    TArray<FString> Candidates;
    FString PF = FPlatformMisc::GetEnvironmentVariable(TEXT("ProgramFiles"));
    if (!PF.IsEmpty())
    {
        Candidates.Add(PF / TEXT("nodejs") / TEXT("node_modules") / TEXT("npm") / TEXT("bin") / TEXT("npm-cli.js"));
    }
    // %APPDATA%\npm\node_modules\npm\bin\npm-cli.js (user-global installs)
    FString AppData = FPlatformMisc::GetEnvironmentVariable(TEXT("APPDATA"));
    if (!AppData.IsEmpty())
    {
        Candidates.Add(AppData / TEXT("npm") / TEXT("node_modules") / TEXT("npm") / TEXT("bin") / TEXT("npm-cli.js"));
    }
    // Locate via PATH: try resolving node.exe sibling
    {
        FString NodeOut, NodeErr;
        int32 Rc = -1;
        // Use where.exe to find node.exe (where.exe IS a real exe)
        FPlatformProcess::ExecProcess(TEXT("where.exe"), TEXT("node.exe"), &Rc, &NodeOut, &NodeErr);
        if (Rc == 0 && !NodeOut.IsEmpty())
        {
            FString NodePath = NodeOut.TrimStartAndEnd();
            // Handle multiple results — take first line
            int32 NewLine = -1;
            if (NodePath.FindChar(TEXT('\n'), NewLine)) NodePath = NodePath.Left(NewLine).TrimStartAndEnd();
            FString NodeDir = FPaths::GetPath(NodePath);
            Candidates.Add(NodeDir / TEXT("node_modules") / TEXT("npm") / TEXT("bin") / TEXT("npm-cli.js"));
        }
    }
    for (const FString& C : Candidates)
    {
        if (IFileManager::Get().FileExists(*C))
        {
            return C;
        }
    }
    return FString();
}

/** Show a fire-and-forget toast notification. Must be called on the game thread. */
void ShowToast(const FString& Message, float Duration = 6.0f)
{
    FNotificationInfo Info(FText::FromString(Message));
    Info.bFireAndForget  = true;
    Info.ExpireDuration  = Duration;
    FSlateNotificationManager::Get().AddNotification(Info);
}

// ---------------------------------------------------------------------------
// SKILL deployment freshness
// ---------------------------------------------------------------------------

/** User home directory (~). Prefers %USERPROFILE%, falls back to UE's UserHomeDir(). */
FString GetUserHome()
{
    FString Home = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
    if (Home.IsEmpty())
    {
        Home = FPlatformProcess::UserHomeDir();
    }
    return Home.TrimStartAndEnd();
}

/**
 * Resolve the global node_modules dir via `node npm-cli.js root -g`.
 * Returns empty string on any failure.
 */
FString FindGlobalNodeModules(const FString& NpmCliJs)
{
    if (NpmCliJs.IsEmpty())
    {
        return FString();
    }
    FString Out, Err;
    const int32 Rc = RunDirect(TEXT("node.exe"),
        FString::Printf(TEXT("\"%s\" root -g"), *NpmCliJs), Out, Err);
    if (Rc != 0)
    {
        return FString();
    }
    FString Root = Out.TrimStartAndEnd();
    int32 NL = INDEX_NONE;
    if (Root.FindChar(TEXT('\n'), NL))
    {
        Root = Root.Left(NL).TrimStartAndEnd();
    }
    return Root;
}

/** Deployed SKILL.md under the primary agent-skills root (~/.agents). */
FString GetDeployedSkillPath()
{
    const FString Home = GetUserHome();
    if (Home.IsEmpty())
    {
        return FString();
    }
    return Home / TEXT(".agents") / TEXT("skills") / TEXT("smithue-control") / TEXT("SKILL.md");
}

/** Load a file and normalize CRLF -> LF + trim trailing whitespace, for stable content comparison. */
bool LoadNormalized(const FString& Path, FString& Out)
{
    if (!FFileHelper::LoadFileToString(Out, *Path))
    {
        return false;
    }
    Out.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
    Out = Out.TrimEnd();
    return true;
}

/**
 * Determine whether the locally-deployed SKILL.md matches the SKILL bundled in the
 * globally-installed smithue-cli. OutSourcePath receives the bundle path (copy source).
 */
ESkillState ComputeSkillState(const FString& NpmCliJs, FString& OutSourcePath)
{
    OutSourcePath.Reset();

    const FString NodeModules = FindGlobalNodeModules(NpmCliJs);
    if (NodeModules.IsEmpty())
    {
        return ESkillState::Unknown;
    }

    const FString Bundle = NodeModules / TEXT("smithue-cli") / TEXT("skill") / TEXT("SKILL.md");
    if (!IFileManager::Get().FileExists(*Bundle))
    {
        return ESkillState::Unknown;
    }
    OutSourcePath = Bundle;

    const FString Deployed = GetDeployedSkillPath();
    if (Deployed.IsEmpty())
    {
        return ESkillState::Unknown;
    }
    if (!IFileManager::Get().FileExists(*Deployed))
    {
        return ESkillState::NotDeployed;
    }

    FString BundleText, DeployedText;
    if (!LoadNormalized(Bundle, BundleText))
    {
        return ESkillState::Unknown;
    }
    if (!LoadNormalized(Deployed, DeployedText))
    {
        return ESkillState::NotDeployed;
    }

    if (!BundleText.Equals(DeployedText, ESearchCase::CaseSensitive))
    {
        return ESkillState::Stale;
    }

    // SKILL.md matches — but a PARTIAL deploy (missing references/ or scripts/) is still Stale,
    // so the "重装 SKILL" button appears and ReinstallSkill re-copies the whole bundle.
    // (Pre-0.14 deploys only wrote SKILL.md; this heals those machines.)
    // NOTE: the bundle dir was renamed reference/ -> references/ in CLI 0.15; probe both
    // names so this plugin stays correct against either an old or new installed CLI.
    const FString BundleDir   = FPaths::GetPath(Bundle);   // .../smithue-cli/skill
    const FString DeployedDir = FPaths::GetPath(Deployed); // .../smithue-control
    for (const TCHAR* Sub : { TEXT("references"), TEXT("reference"), TEXT("scripts") })
    {
        if (IFileManager::Get().DirectoryExists(*(BundleDir / Sub))
            && !IFileManager::Get().DirectoryExists(*(DeployedDir / Sub)))
        {
            return ESkillState::Stale;
        }
    }

    // Legacy residue: bundle uses the new references/ layout but the deploy still has a
    // stale reference/ dir (merge copies never delete). Flag Stale so reinstall cleans it.
    if (IFileManager::Get().DirectoryExists(*(BundleDir / TEXT("references")))
        && IFileManager::Get().DirectoryExists(*(DeployedDir / TEXT("reference"))))
    {
        return ESkillState::Stale;
    }

    return ESkillState::Synced;
}

} // anonymous namespace

/**
 * Decide whether to upgrade smithue-cli without asking. Game thread only.
 *
 * Deliberately conservative: it modifies the user's GLOBAL npm environment, so it
 * runs at most once per editor session, only when opted in, and only when there
 * is a working npm and something concrete to fix.
 */
static void MaybeAutoUpgradeCli()
{
    if (GAutoUpgradeAttempted)
    {
        return;
    }

    const USmithUESettings* Settings = GetDefault<USmithUESettings>();
    if (!Settings || !Settings->bAutoUpgradeCliOnStartup)
    {
        return;
    }

    const ECliState State = GCachedCliInfo.State;
    if (State != ECliState::Outdated && State != ECliState::NotInstalled)
    {
        return;
    }

    // No npm means no way to install; the panel already surfaces that.
    if (GCachedCliInfo.NpmVersion.IsEmpty())
    {
        UE_LOG(LogSmithUE, Warning,
               TEXT("[SmithUE CLI] auto-upgrade skipped: npm not available"));
        return;
    }

    // Set BEFORE launching: a successful install re-runs CheckCliEnvironment(),
    // which would otherwise re-enter this function.
    GAutoUpgradeAttempted = true;

    const FString Target = GCachedCliInfo.TargetVersion.IsEmpty()
        ? TEXT("latest")
        : GCachedCliInfo.TargetVersion;

    UE_LOG(LogSmithUE, Log,
           TEXT("[SmithUE CLI] auto-upgrade: state=%d installed='%s' target='%s'"),
           static_cast<int32>(State), *GCachedCliInfo.CliVersion, *Target);

    ShowToast(State == ECliState::NotInstalled
        ? FString::Printf(TEXT("SmithUE: 正在自动安装 smithue-cli %s…"), *Target)
        : FString::Printf(TEXT("SmithUE: 正在自动升级 smithue-cli %s → %s…"),
                          *GCachedCliInfo.CliVersion, *Target));

    FSmithUECliChecker::ExecuteCliInstall();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void FSmithUECliChecker::CheckCliEnvironment()
{
    // AtomicSet returns the PREVIOUS value:
    //   true  → already in-flight, bail out
    //   false → we just acquired the "lock", proceed
    if (bCliCheckInFlight.AtomicSet(true))
    {
        return;
    }

    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, []()
    {
        using namespace SmithUECliInternal;

        FCliInfo Info;
        Info.LastCheckTime = FDateTime::UtcNow();

        // 1. Probe node — call node.exe directly (real PE exe, like git.exe in UpdateChecker)
        FString NodeOut, NodeErr;
        const int32 NodeRc = RunDirect(TEXT("node.exe"), TEXT("--version"), NodeOut, NodeErr);
        UE_LOG(LogSmithUE, Log, TEXT("[SmithUE CLI] node probe: rc=%d out='%s' err='%s'"),
               NodeRc, *NodeOut.TrimStartAndEnd(), *NodeErr.TrimStartAndEnd());

        const TOptional<FString> NodeVer = ClassifyVersionProbe(NodeRc, NodeOut);
        if (!NodeVer.IsSet())
        {
            // node not found — report and exit early
            Info.State = ECliState::NoNode;
            Info.bValid = true;

            AsyncTask(ENamedThreads::GameThread, [Info]()
            {
                GCachedCliInfo = Info;
                FSmithUECliChecker::OnCliCheckComplete.Broadcast();
                bCliCheckInFlight = false;
            });
            return;
        }

        Info.NodeVersion = NodeVer.GetValue();

        // 2. Probe npm — use node.exe + npm-cli.js (avoids cmd.exe DETACHED_PROCESS pipe issue)
        FString NpmCliJs = FindNpmCliJs();
        FString NpmOut, NpmErr;
        int32 NpmRc = -1;
        if (NpmCliJs.IsEmpty())
        {
            UE_LOG(LogSmithUE, Warning, TEXT("[SmithUE CLI] npm probe: npm-cli.js not found on PATH"));
        }
        else
        {
            NpmRc = RunDirect(TEXT("node.exe"),
                FString::Printf(TEXT("\"%s\" --version"), *NpmCliJs),
                NpmOut, NpmErr);
            UE_LOG(LogSmithUE, Log, TEXT("[SmithUE CLI] npm probe: rc=%d out='%s' err='%s'"),
                   NpmRc, *NpmOut.TrimStartAndEnd(), *NpmErr.TrimStartAndEnd());
        }

        const TOptional<FString> NpmVer = ClassifyVersionProbe(NpmRc, NpmOut);
        if (NpmVer.IsSet())
        {
            Info.NpmVersion = NpmVer.GetValue();
        }

        // 3. Probe smithue-cli — use node.exe + npm-cli.js (IGNORE rc — parse JSON only)
        FString LsOut, LsErr;
        int32 LsRc = -1;
        if (!NpmCliJs.IsEmpty())
        {
            LsRc = RunDirect(TEXT("node.exe"),
                FString::Printf(TEXT("\"%s\" ls -g --depth=0 smithue-cli --json"), *NpmCliJs),
                LsOut, LsErr);
            UE_LOG(LogSmithUE, Log, TEXT("[SmithUE CLI] npm ls probe: rc=%d out='%s' err='%s'"),
                   LsRc, *LsOut.TrimStartAndEnd(), *LsErr.TrimStartAndEnd());
        }

        const TOptional<FString> CliVer = ParseNpmLsCliVersion(LsOut);

        // ----------------------------------------------------------------
        // 3.5. Ask the registry what "latest" actually is.
        //      Without this the plugin can only compare against its own hardcoded
        //      floor, which goes stale the moment a new CLI ships -- the machine
        //      then reports "Ready" forever and never upgrades.
        //      Bounded and failure-tolerant: offline simply means no latest.
        // ----------------------------------------------------------------
        FString ViewOut;
        if (!NpmCliJs.IsEmpty())
        {
            const FBoundedRun ViewRun = RunBounded(TEXT("node.exe"),
                FString::Printf(TEXT("\"%s\" view smithue-cli version --no-audit --no-fund --fetch-timeout=15000 --fetch-retries=1"), *NpmCliJs),
                kRegistryQueryTimeoutSeconds, bNeverCancel, ViewOut);
            UE_LOG(LogSmithUE, Log, TEXT("[SmithUE CLI] npm view probe: rc=%d timedout=%d out='%s'"),
                   ViewRun.Rc, ViewRun.bTimedOut ? 1 : 0, *ViewOut.TrimStartAndEnd());

            // npm prints warnings to the same combined stream, so take the last
            // line that parses as a semver rather than the whole blob.
            TArray<FString> Lines;
            ViewOut.ParseIntoArrayLines(Lines);
            for (int32 i = Lines.Num() - 1; i >= 0; --i)
            {
                const TOptional<FString> Candidate = ClassifyVersionProbe(0, Lines[i]);
                if (Candidate.IsSet())
                {
                    Info.LatestVersion = Candidate.GetValue();
                    break;
                }
            }
        }

        Info.TargetVersion = ResolveTargetCliVersion(FString(kRecommendedCliVersion), Info.LatestVersion);

        // ----------------------------------------------------------------
        // 4. Compute state
        // ----------------------------------------------------------------
        if (!CliVer.IsSet())
        {
            Info.State = ECliState::NotInstalled;
        }
        else
        {
            Info.CliVersion = CliVer.GetValue();
            Info.State = IsCliOutdated(Info.CliVersion, Info.TargetVersion)
                ? ECliState::Outdated
                : ECliState::Ready;
        }
        Info.bValid = true;

        // ----------------------------------------------------------------
        // 4.5. Compute SKILL deployment freshness (same probe, off game thread).
        //      Compares deployed ~/.agents/.../SKILL.md against the installed
        //      CLI's bundled skill/SKILL.md. Cheap file reads — safe here.
        // ----------------------------------------------------------------
        Info.SkillState = ComputeSkillState(NpmCliJs, Info.SkillSourcePath);
        UE_LOG(LogSmithUE, Log,
               TEXT("[SmithUE CLI] skill state: %d (source='%s')"),
               static_cast<int32>(Info.SkillState), *Info.SkillSourcePath);

        // ----------------------------------------------------------------
        // 5. Marshal result back to the game thread
        // ----------------------------------------------------------------
        AsyncTask(ENamedThreads::GameThread, [Info]()
        {
            GCachedCliInfo = Info;
            FSmithUECliChecker::OnCliCheckComplete.Broadcast();
            bCliCheckInFlight = false;

            // Opt-in, once per session. Must run after the flag is cleared so the
            // install's follow-up re-check is not blocked by the in-flight guard.
            MaybeAutoUpgradeCli();
        });
    });
}

void FSmithUECliChecker::ExecuteCliInstall()
{
    // Use a DEDICATED in-flight flag so a stuck install never freezes Re-check.
    if (bInstallInFlight.AtomicSet(true))
    {
        return;
    }
    bInstallCancelRequested = false;

    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, []()
    {
        // Install via node.exe + npm-cli.js (avoids cmd.exe DETACHED_PROCESS pipe issue).
        // Fast-fail flags so a bad network gives up in ~1 min instead of npm's ~15 min
        // retry budget; the 120s hard timeout below is the absolute backstop.
        const FString NpmCli = FindNpmCliJs();
        FString Combined;
        FBoundedRun Run;
        if (NpmCli.IsEmpty())
        {
            Combined = TEXT("npm-cli.js not found — is Node.js installed?");
        }
        else
        {
            const FString Args = FString::Printf(
                TEXT("\"%s\" i -g smithue-cli@latest --fetch-timeout=60000 --fetch-retries=1 --no-audit --no-fund"),
                *NpmCli);
            Run = RunBounded(TEXT("node.exe"), Args, kInstallTimeoutSeconds, bInstallCancelRequested, Combined);
        }

        UE_LOG(LogSmithUE, Log,
               TEXT("[SmithUE CLI] install: rc=%d timedOut=%d cancelled=%d out='%s'"),
               Run.Rc, Run.bTimedOut ? 1 : 0, Run.bCancelled ? 1 : 0, *Combined.TrimStartAndEnd());

        const bool bSuccess = (!Run.bTimedOut && !Run.bCancelled && Run.Rc == 0 && !NpmCli.IsEmpty());

        // Build a user-facing message off the failure mode.
        FString ToastMsg;
        if (Run.bCancelled)
        {
            ToastMsg = TEXT("smithue-cli install cancelled.");
        }
        else if (Run.bTimedOut)
        {
            ToastMsg = TEXT("smithue-cli install timed out (120s) and was aborted. "
                            "Check your network / npm registry, or run 'npm i -g smithue-cli@latest' manually. "
                            "The SmithUE plugin works fine without the CLI.");
        }
        else if (!bSuccess)
        {
            if (NpmCli.IsEmpty())
            {
                ToastMsg = TEXT("smithue-cli install failed: Node.js not found. Install Node.js first (nodejs.org).");
            }
            else if (Combined.Contains(TEXT("EACCES"))
                     || Combined.Contains(TEXT("permission"), ESearchCase::IgnoreCase))
            {
                ToastMsg = TEXT("smithue-cli install failed: permission denied. "
                                "Run your terminal as administrator or use a Node version manager (nvm, fnm).");
            }
            else if (Combined.Contains(TEXT("ETIMEDOUT"))
                     || Combined.Contains(TEXT("ECONNREFUSED"))
                     || Combined.Contains(TEXT("ENOTFOUND"))
                     || Combined.Contains(TEXT("fetch failed"), ESearchCase::IgnoreCase)
                     || Combined.Contains(TEXT("network"), ESearchCase::IgnoreCase))
            {
                ToastMsg = TEXT("smithue-cli install failed: network error. "
                                "Check your connection / npm registry, or try 'npx smithue-cli' (no install). "
                                "The SmithUE plugin works fine without the CLI.");
            }
            else
            {
                ToastMsg = TEXT("smithue-cli install failed. "
                                "Run 'npm i -g smithue-cli@latest' manually for details.");
            }
        }

        AsyncTask(ENamedThreads::GameThread, [bSuccess, ToastMsg]()
        {
            bInstallInFlight = false;
            if (bSuccess)
            {
                // Refresh state so the button flips to "up to date" automatically.
                FSmithUECliChecker::CheckCliEnvironment();
            }
            else
            {
                ShowToast(ToastMsg, 9.0f);
            }
        });
    });
}

void FSmithUECliChecker::CancelCliInstall()
{
    // The install worker loop polls this flag and terminates the subprocess tree.
    bInstallCancelRequested = true;
}

ECliState FSmithUECliChecker::GetState()
{
    return GCachedCliInfo.State;
}

FString FSmithUECliChecker::GetNodeVersion()
{
    return GCachedCliInfo.NodeVersion;
}

FString FSmithUECliChecker::GetNpmVersion()
{
    return GCachedCliInfo.NpmVersion;
}

FString FSmithUECliChecker::GetCliVersion()
{
    return GCachedCliInfo.CliVersion;
}

FString FSmithUECliChecker::GetLatestCliVersion()
{
    return GCachedCliInfo.LatestVersion;
}

FString FSmithUECliChecker::GetTargetCliVersion()
{
    return GCachedCliInfo.TargetVersion;
}

bool FSmithUECliChecker::HasAttemptedAutoUpgrade()
{
    return GAutoUpgradeAttempted;
}

FDateTime FSmithUECliChecker::GetLastCheckTime()
{
    return GCachedCliInfo.LastCheckTime;
}

bool FSmithUECliChecker::IsCheckInFlight()
{
    return bCliCheckInFlight;
}

bool FSmithUECliChecker::IsInstallInFlight()
{
    return bInstallInFlight;
}

ESkillState FSmithUECliChecker::GetSkillState()
{
    return GCachedCliInfo.SkillState;
}

void FSmithUECliChecker::ReinstallSkill()
{
    // Copy the SKILL bundled in the *currently installed* CLI to the local agent-skill
    // dirs. Pure local file copy — fast enough to run synchronously on the game thread.
    const FString Source = GCachedCliInfo.SkillSourcePath;
    if (Source.IsEmpty() || !IFileManager::Get().FileExists(*Source))
    {
        ShowToast(TEXT("无法重装 SKILL：未找到已安装 smithue-cli 自带的 SKILL.md。请先安装 / 升级 CLI。"), 8.0f);
        return;
    }

    const FString Home = GetUserHome();
    if (Home.IsEmpty())
    {
        ShowToast(TEXT("无法重装 SKILL：未能定位用户主目录。"), 8.0f);
        return;
    }

    // Mirror the CLI postinstall deploy targets:
    //   ~/.agents/skills   (always)
    //   ~/.claude/skills   (only if ~/.claude exists)
    //   ~/.codex/skills    (only if ~/.codex exists)
    TArray<FString> SkillsRoots;
    SkillsRoots.Add(Home / TEXT(".agents") / TEXT("skills"));
    for (const TCHAR* AgentRoot : { TEXT(".claude"), TEXT(".codex") })
    {
        const FString Root = Home / AgentRoot;
        if (IFileManager::Get().DirectoryExists(*Root))
        {
            SkillsRoots.Add(Root / TEXT("skills"));
        }
    }

    // Copy the WHOLE bundle (SKILL.md + references/ + scripts/), not just SKILL.md.
    // SkillSourcePath points at the bundle's SKILL.md; its parent dir is the bundle root.
    const FString SourceDir = FPaths::GetPath(Source);

    int32 OkCount = 0;
    for (const FString& SkillsRoot : SkillsRoots)
    {
        const FString DestDir = SkillsRoot / TEXT("smithue-control");
        IFileManager::Get().MakeDirectory(*DestDir, /*Tree*/true);
        // CopyDirectoryTree (IPlatformFile) recurses SourceDir -> DestDir (SKILL.md + references/ + scripts/).
        if (FPlatformFileManager::Get().GetPlatformFile().CopyDirectoryTree(*DestDir, *SourceDir, /*bOverwriteAllExisting*/true))
        {
            // Legacy cleanup: CopyDirectoryTree merges and never deletes, so a stale
            // reference/ dir from pre-0.15 bundles would survive — remove it when the
            // new references/ layout is what we just deployed.
            if (IFileManager::Get().DirectoryExists(*(SourceDir / TEXT("references")))
                && IFileManager::Get().DirectoryExists(*(DestDir / TEXT("reference"))))
            {
                IFileManager::Get().DeleteDirectory(*(DestDir / TEXT("reference")), /*RequireExists*/false, /*Tree*/true);
            }
            ++OkCount;
        }
        else
        {
            UE_LOG(LogSmithUE, Warning, TEXT("[SmithUE CLI] skill bundle copy failed -> '%s'"), *DestDir);
        }
    }

    if (OkCount > 0)
    {
        ShowToast(FString::Printf(
            TEXT("已重装 smithue-control SKILL（含 references/ 与 scripts/）到 %d 个目录。请重载 AI 工具以生效。"), OkCount), 8.0f);
        // Refresh state so the row flips to "已同步" and the button hides.
        FSmithUECliChecker::CheckCliEnvironment();
    }
    else
    {
        ShowToast(TEXT("重装 SKILL 失败（权限 / 路径问题）。可手动运行：npm i -g smithue-cli@latest。"), 9.0f);
    }
}
