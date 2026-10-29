// Copyright 2026, 123dx-svg. MIT License.
#pragma once

#include "CoreMinimal.h"

// ---------------------------------------------------------------------------
// Pure helpers — exposed in a named namespace so Automation tests can reach them
// without a FRIEND_TEST hack.
// ---------------------------------------------------------------------------
namespace SmithUECliInternal
{
    /** Parsed representation of a semantic version string. */
    struct FSmithUESemver
    {
        int32   Major      = 0;
        int32   Minor      = 0;
        int32   Patch      = 0;
        FString Prerelease; // empty = release version
    };

    /**
     * Parse a semver string.
     * - Leading 'v'/'V' is stripped.
     * - Build metadata (everything after '+') is ignored.
     * - Returns empty TOptional on any parse failure.
     */
    SMITHUE_API TOptional<FSmithUESemver> ParseSemver(const FString& s);

    /**
     * Three-way comparison.
     * Returns -1 (a < b), 0 (a == b), +1 (a > b).
     * Prerelease is lower than the equivalent release per semver spec §11.
     */
    SMITHUE_API int32 CompareSemver(const FSmithUESemver& a, const FSmithUESemver& b);

    /**
     * Classify the output of a version probe command (e.g. "node --version").
     * Returns the trimmed version string (leading 'v' stripped) if rc==0 and
     * stdout parses as a valid semver; returns empty TOptional otherwise.
     */
    SMITHUE_API TOptional<FString> ClassifyVersionProbe(int32 rc, const FString& out);

    /**
     * Parse the JSON output of "npm ls -g --depth=0 smithue-cli --json".
     * Extracts dependencies["smithue-cli"].version.
     * Return code from npm is intentionally ignored — JSON is the source of truth.
     */
    SMITHUE_API TOptional<FString> ParseNpmLsCliVersion(const FString& json);

    SMITHUE_API FString ResolveTargetCliVersion(const FString& Floor, const FString& NpmLatest);
    SMITHUE_API bool IsCliOutdated(const FString& Installed, const FString& Target);

} // namespace SmithUECliInternal

// ---------------------------------------------------------------------------
// Public types
// ---------------------------------------------------------------------------

DECLARE_MULTICAST_DELEGATE(FOnCliCheckComplete);

/** Overall CLI environment health. */
enum class ECliState : uint8
{
    NoNode,       // node.exe not found on PATH
    NotInstalled, // node found, but smithue-cli not globally installed
    Outdated,     // smithue-cli installed but below the target version
    Ready         // smithue-cli installed and meets the target version
};

/**
 * Freshness of the locally-deployed smithue-control SKILL.md, compared against
 * the SKILL.md bundled inside the globally-installed smithue-cli package.
 */
enum class ESkillState : uint8
{
    Unknown,      // cannot determine (CLI not installed / bundle not found)
    NotDeployed,  // CLI bundle exists, but no SKILL.md deployed under ~/.agents
    Stale,        // deployed SKILL.md differs or supporting directories are missing / obsolete
    Synced        // deployed SKILL.md matches the installed CLI's bundle
};

/** Snapshot of the last successful environment probe. */
struct FCliInfo
{
    FString     NodeVersion;
    FString     NpmVersion;
    FString     CliVersion;
    FString     LatestVersion;
    FString     TargetVersion;
    ECliState   State          = ECliState::NoNode;
    FDateTime   LastCheckTime;
    bool        bValid         = false;

    // SKILL deployment freshness (computed during the same probe).
    ESkillState SkillState      = ESkillState::Unknown;
    FString     SkillSourcePath; // installed CLI's bundled skill/SKILL.md (copy source for reinstall)
};

// ---------------------------------------------------------------------------
// Main class
// ---------------------------------------------------------------------------

class SMITHUE_API FSmithUECliChecker
{
public:
    /** Detect node / npm / smithue-cli environment asynchronously. Re-entrancy guarded. */
    static void CheckCliEnvironment();

    /**
     * Install or upgrade smithue-cli globally via npm.
    * Called by the install button or once per session when automatic upgrades are enabled.
     * Bounded by a hard timeout; cancellable via CancelCliInstall().
     */
    static void ExecuteCliInstall();

    /** Request cancellation of an in-flight install (terminates the npm subprocess). */
    static void CancelCliInstall();

    static ECliState GetState();
    static FString   GetNodeVersion();
    static FString   GetNpmVersion();
    static FString   GetCliVersion();
    static FString   GetLatestCliVersion();
    static FString   GetTargetCliVersion();
    static FDateTime GetLastCheckTime();
    static bool      IsCheckInFlight();
    static bool      IsInstallInFlight();

    /** Freshness of the locally-deployed smithue-control SKILL.md (from last probe). */
    static ESkillState GetSkillState();

    /**
    * Re-deploy the smithue-control bundle by copying the installed CLI's complete
    * skill directory to the local agent-skill directories (~/.agents, plus ~/.claude /
     * ~/.codex when those roots already exist). Pure file copy, no subprocess.
     * ONLY call this on an explicit user action (button press). Re-checks on success.
     * NOTE: deploys the SKILL that ships with the *currently installed* CLI — upgrade
     * the CLI first if you want the newest SKILL.
     */
    static void ReinstallSkill();

    static FOnCliCheckComplete OnCliCheckComplete;
};
