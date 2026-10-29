#include "Transport/SmithUEHttpServer.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/RunnableThread.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "Transport/SmithUEHttpServerRunnable.h"
#include "SmithUESettings.h"
#include "SmithUEModule.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include <aclapi.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

// ============================================================
//  Portfile helpers (file-scope statics)
// ============================================================

/** Returns true if something is actively listening on 127.0.0.1:Port (500 ms timeout). */
static bool SmithUE_IsPortListening(ISocketSubsystem* SS, uint16 Port)
{
	check(SS);
	FSocket* Probe = SS->CreateSocket(NAME_Stream, TEXT("SmithUEProbe"), false);
	if (!Probe)
	{
		return false;
	}

	Probe->SetNonBlocking(true);

	FIPv4Address LoopbackAddr;
	FIPv4Address::Parse(TEXT("127.0.0.1"), LoopbackAddr);
	TSharedRef<FInternetAddr> IA = SS->CreateInternetAddr();
	IA->SetIp(LoopbackAddr.Value);
	IA->SetPort(Port);

	// Non-blocking connect — returns false immediately on EINPROGRESS
	Probe->Connect(*IA);

	const double Deadline = FPlatformTime::Seconds() + 0.5;
	bool bConnected = false;
	while (FPlatformTime::Seconds() < Deadline)
	{
		const ESocketConnectionState State = Probe->GetConnectionState();
		if (State == SCS_Connected)
		{
			bConnected = true;
			break;
		}
		if (State == SCS_ConnectionError)
		{
			break; // refused or reset — definitively not alive
		}
		// SCS_NotConnected = still pending; keep polling
		FPlatformProcess::SleepNoStats(0.01f);
	}

	Probe->Close();
	SS->DestroySocket(Probe);
	return bConnected;
}

/** Scan DirPath for *.port files; delete any whose owning process has exited or whose port is not responding. */
static void SmithUE_PruneStalePortFiles(const FString& DirPath, ISocketSubsystem* SS)
{
	TArray<FString> FileNames;
	IFileManager::Get().FindFiles(FileNames, *(DirPath + TEXT("\\*.port")), true, false);

	for (const FString& FileName : FileNames)
	{
		const FString FullPath = DirPath + TEXT("\\") + FileName;

		// Primary check: portfile name is <pid>.port (written via GetCurrentProcessId()).
		// Deleting by PID liveness correctly handles the shared fixed-port case: when all
		// editors bind the same port (e.g. 13721), a dead editor's stale portfile records
		// that same port which the live editor still answers — a pure port-listening check
		// would never prune it. PID check sidesteps this entirely.
		const FString PidStr = FPaths::GetBaseFilename(FileName);
		if (PidStr.IsNumeric())
		{
			const uint32 FilePid = static_cast<uint32>(FCString::Strtoui64(*PidStr, nullptr, 10));
			if (FilePid != 0 && !FPlatformProcess::IsApplicationRunning(FilePid))
			{
				IFileManager::Get().Delete(*FullPath);
				UE_LOG(LogSmithUE, Log, TEXT("SmithUE: pruned stale portfile %s (PID %u no longer running)"),
					*FullPath, FilePid);
				continue;
			}
		}

		// Fallback: if PID could not be parsed (unexpected filename format), or the process
		// is alive but somehow the port went away, fall back to the port-listening check.
		FString Content;
		if (!FFileHelper::LoadFileToString(Content, *FullPath))
		{
			continue;
		}

		// Extract port value: find "port":N without pulling in full JSON parser
		const int32 KeyIdx = Content.Find(TEXT("\"port\":"));
		if (KeyIdx < 0)
		{
			IFileManager::Get().Delete(*FullPath); // malformed — remove
			continue;
		}
		const FString AfterKey = Content.Mid(KeyIdx + 7).TrimStart();
		const int32 FilePort = FCString::Atoi(*AfterKey);
		if (FilePort <= 0 || FilePort > 65535)
		{
			IFileManager::Get().Delete(*FullPath);
			continue;
		}

		if (!SmithUE_IsPortListening(SS, static_cast<uint16>(FilePort)))
		{
			IFileManager::Get().Delete(*FullPath);
			UE_LOG(LogSmithUE, Log, TEXT("SmithUE: pruned stale portfile %s (port %d not responding)"),
				*FullPath, FilePort);
		}
	}
}

#if PLATFORM_WINDOWS
/**
 * Set the DACL on FilePath so only the current user's SID has access.
 * Uses PROTECTED_DACL_SECURITY_INFORMATION to block inherited parent ACEs.
 */
static void SmithUE_SetPortFileAclCurrentUserOnly(const FString& FilePath)
{
	HANDLE hToken = nullptr;
	if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &hToken))
	{
		UE_LOG(LogSmithUE, Warning, TEXT("SmithUE portfile ACL: OpenProcessToken failed (%u)"), ::GetLastError());
		return;
	}

	// First call: obtain required buffer size
	DWORD dwSize = 0;
	::GetTokenInformation(hToken, TokenUser, nullptr, 0, &dwSize);

	TArray<uint8> TokenUserBuf;
	TokenUserBuf.SetNumUninitialized(static_cast<int32>(dwSize));
	TOKEN_USER* pTokenUser = reinterpret_cast<TOKEN_USER*>(TokenUserBuf.GetData());

	if (!::GetTokenInformation(hToken, TokenUser, pTokenUser, dwSize, &dwSize))
	{
		UE_LOG(LogSmithUE, Warning, TEXT("SmithUE portfile ACL: GetTokenInformation failed (%u)"), ::GetLastError());
		::CloseHandle(hToken);
		return;
	}

	EXPLICIT_ACCESS ea = {};
	ea.grfAccessPermissions = GENERIC_ALL;
	ea.grfAccessMode        = SET_ACCESS;
	ea.grfInheritance       = NO_INHERITANCE;
	ea.Trustee.TrusteeForm  = TRUSTEE_IS_SID;
	ea.Trustee.TrusteeType  = TRUSTEE_IS_USER;
	ea.Trustee.ptstrName    = reinterpret_cast<LPTSTR>(pTokenUser->User.Sid);

	PACL pACL = nullptr;
	const DWORD AclErr = ::SetEntriesInAcl(1, &ea, nullptr, &pACL);
	if (AclErr != ERROR_SUCCESS)
	{
		UE_LOG(LogSmithUE, Warning, TEXT("SmithUE portfile ACL: SetEntriesInAcl failed (%u)"), AclErr);
		::CloseHandle(hToken);
		return;
	}

	// PROTECTED_DACL_SECURITY_INFORMATION prevents inheriting ACEs from parent directory
	const DWORD SecErr = ::SetNamedSecurityInfoW(
		const_cast<LPWSTR>(*FilePath),
		SE_FILE_OBJECT,
		DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
		nullptr, nullptr, pACL, nullptr
	);
	if (SecErr != ERROR_SUCCESS)
	{
		UE_LOG(LogSmithUE, Warning, TEXT("SmithUE portfile ACL: SetNamedSecurityInfo failed (%u)"), SecErr);
	}

	::LocalFree(pACL);
	::CloseHandle(hToken);
}
#endif // PLATFORM_WINDOWS

/**
 * Atomically write Content to FinalPath:
 *   1. Write to <FinalPath>.tmp
 *   2. MoveFileExW (MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
 *   3. Apply current-user-only ACL (Windows)
 */
static void SmithUE_WritePortFileAtomic(const FString& FinalPath, const FString& Content)
{
	const FString TmpPath = FinalPath + TEXT(".tmp");

	if (!FFileHelper::SaveStringToFile(Content, *TmpPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogSmithUE, Warning, TEXT("SmithUE: failed to write tmp portfile %s"), *TmpPath);
		return;
	}

#if PLATFORM_WINDOWS
	// Atomic rename — MOVEFILE_WRITE_THROUGH ensures data is flushed before rename returns
	const BOOL bMoved = ::MoveFileExW(
		*TmpPath,
		*FinalPath,
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
	);
	if (!bMoved)
	{
		UE_LOG(LogSmithUE, Warning, TEXT("SmithUE: MoveFileExW failed (%u) — portfile not written"), ::GetLastError());
		IFileManager::Get().Delete(*TmpPath);
		return;
	}

	SmithUE_SetPortFileAclCurrentUserOnly(FinalPath);
#else
	// Non-Windows fallback (not expected; plugin is Win64-only)
	IFileManager::Get().Move(*FinalPath, *TmpPath, /*bReplace=*/true, /*bEvenReadOnly=*/false);
#endif
}

// ============================================================
//  USmithUEHttpServer
// ============================================================

USmithUEHttpServer::USmithUEHttpServer()
	: Runnable(nullptr)
	, ServerThread(nullptr)
	, bIsRunning(false)
	, Port(0)
	, BoundPort(0)
{
}

USmithUEHttpServer::~USmithUEHttpServer()
{
}

bool USmithUEHttpServer::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningCommandlet();
}

void USmithUEHttpServer::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Read settings once
	const USmithUESettings* SmithSettings = GetDefault<USmithUESettings>();

	// Priority 3 (lowest): Project Settings
	if (SmithSettings)
	{
		Port = static_cast<uint16>(FMath::Clamp(SmithSettings->HttpBindPort, 0, 65535));
	}

	// Priority 2: SMITHUE_BIND_PORT environment variable override
	FString EnvPort = FPlatformMisc::GetEnvironmentVariable(TEXT("SMITHUE_BIND_PORT"));
	if (!EnvPort.IsEmpty())
	{
		int32 ParsedPort = FCString::Atoi(*EnvPort);
		if (ParsedPort >= 0 && ParsedPort <= MAX_uint16 && EnvPort.IsNumeric())
		{
			Port = static_cast<uint16>(ParsedPort);
		}
	}

	// Priority 1: command-line override for HTTP bind port is not implemented;
	// GetPortFromCommandLine in SmithUEHttpServerRunnable.cpp is for status reporting only.

	// Respect auto-start setting
	if (SmithSettings && !SmithSettings->bAutoStartServer)
	{
		UE_LOG(LogSmithUE, Log, TEXT("SmithUE: auto-start disabled by Project Settings"));
		return;
	}

	StartServer();
}

void USmithUEHttpServer::Deinitialize()
{
	StopServer();
	Super::Deinitialize();
}

void USmithUEHttpServer::StartServer()
{
	if (bIsRunning)
	{
		return;
	}

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (SocketSubsystem == nullptr)
	{
		UE_LOG(LogSmithUE, Error, TEXT("Failed to acquire socket subsystem for SmithUE HTTP server"));
		return;
	}

	FSocket* RawListenerSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("SmithUEHttpListener"), false);
	TSharedPtr<FSocket> NewListenerSocket;
	if (RawListenerSocket != nullptr)
	{
		NewListenerSocket = MakeShareable(RawListenerSocket, ::FSocketDeleter(SocketSubsystem));
	}

	if (!NewListenerSocket.IsValid())
	{
		UE_LOG(LogSmithUE, Error, TEXT("Failed to create SmithUE HTTP listener socket"));
		return;
	}

	NewListenerSocket->SetReuseAddr(false);
	NewListenerSocket->SetNonBlocking(true);

	FIPv4Address Address;
	FIPv4Address::Parse(TEXT("127.0.0.1"), Address);
	const FIPv4Endpoint Endpoint(Address, Port);
	bool bBound = NewListenerSocket->Bind(*Endpoint.ToInternetAddr());
	if (!bBound && Port != 0)
	{
		UE_LOG(LogSmithUE, Warning, TEXT("SmithUE port %d unavailable; allocating an independent port"), Port);
		const FIPv4Endpoint AutomaticEndpoint(Address, 0);
		bBound = NewListenerSocket->Bind(*AutomaticEndpoint.ToInternetAddr());
	}
	if (!bBound || !NewListenerSocket->Listen(5))
	{
		UE_LOG(LogSmithUE, Error, TEXT("Failed to bind/listen SmithUE HTTP server on 127.0.0.1:%d"), Port);
		NewListenerSocket.Reset();
		return;
	}

	// Retrieve OS-assigned port after successful bind
	TSharedRef<FInternetAddr> BoundAddr = SocketSubsystem->CreateInternetAddr();
	NewListenerSocket->GetAddress(*BoundAddr);
	BoundPort = static_cast<uint16>(BoundAddr->GetPort());

	InstanceId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	ListenerSocket = NewListenerSocket;
	bIsRunning = true;

	// Cache plugin version on the game thread (IPluginManager is game-thread only).
	// Immutable after this point — the /ready handler reads it from the worker thread.
	if (TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SmithUE")))
	{
		PluginVersion = Plugin->GetDescriptor().VersionName;
	}

	// Track PIE state on the game thread so the worker-thread /ready handler can
	// report pie_active without touching GEditor/UObjects.
	bPIEActive = (GEditor != nullptr && GEditor->PlayWorld != nullptr);
	if (!PIEBeginHandle.IsValid())
	{
		PIEBeginHandle = FEditorDelegates::BeginPIE.AddLambda([this](const bool /*bIsSimulating*/)
		{
			bPIEActive = true;
		});
	}
	if (!PIEEndHandle.IsValid())
	{
		PIEEndHandle = FEditorDelegates::EndPIE.AddLambda([this](const bool /*bIsSimulating*/)
		{
			bPIEActive = false;
		});
	}

	if (!IsRooted())
	{
		AddToRoot();
	}
	Runnable = new FSmithUEHttpServerRunnable(this, ListenerSocket);
	ServerThread = FRunnableThread::Create(Runnable, TEXT("SmithUEHttpServerThread"));
	if (ServerThread == nullptr)
	{
		UE_LOG(LogSmithUE, Error, TEXT("Failed to create SmithUE HTTP server thread"));
		delete Runnable;
		Runnable = nullptr;
		StopServer();
		if (IsRooted())
		{
			RemoveFromRoot();
		}
		return;
	}

	UE_LOG(LogSmithUE, Log, TEXT("SmithUE HTTP listening on 127.0.0.1:%d"), BoundPort);

	// ---- Readiness: poll AssetRegistry every 0.5 s on the GameThread ----
	{
		// Reset in case StartServer is called more than once (shouldn't happen, but be safe)
		bIsReady = false;

		ReadyTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateWeakLambda(this, [this](float /*DeltaTime*/) -> bool
			{
				check(IsInGameThread());

				FAssetRegistryModule* AssetRegistryModule =
					FModuleManager::GetModulePtr<FAssetRegistryModule>("AssetRegistry");

				if (AssetRegistryModule && !AssetRegistryModule->Get().IsLoadingAssets())
				{
					bIsReady = true;
					UE_LOG(LogSmithUE, Log, TEXT("SmithUE: editor ready (AssetRegistry finished loading)"));
					return false; // Unregister ticker
				}
				return true; // Keep ticking
			}),
			0.5f);
	}

	// ---- Portfile: prune stale files, then write new one atomically ----
	{
		const FString LocalAppData = FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA"));
		if (LocalAppData.IsEmpty())
		{
			UE_LOG(LogSmithUE, Warning, TEXT("SmithUE: LOCALAPPDATA not set; portfile skipped"));
		}
		else
		{
			const FString DirPath = LocalAppData + TEXT("\\.smithue");
			IFileManager::Get().MakeDirectory(*DirPath, true);

			// Remove portfiles whose server is no longer responding
			SmithUE_PruneStalePortFiles(DirPath, SocketSubsystem);

			// Build portfile path and store for later cleanup
			PortFilePath = DirPath + FString::Printf(TEXT("\\%u.port"), FPlatformProcess::GetCurrentProcessId());

			EnsurePortFileWritten(true);

			// Register heartbeat to re-write portfile if deleted externally
			const float HeartbeatInterval = []() -> float {
				const USmithUESettings* S = GetDefault<USmithUESettings>();
				return S ? FMath::Clamp(S->PortfileHeartbeatInterval, 1.0f, 30.0f) : 4.0f;
			}();

			HeartbeatTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateWeakLambda(this, [this](float /*DeltaTime*/) -> bool
				{
					// EnsurePortFileWritten(false) is a no-op when the file already exists,
					// so this only does work (and logs) when the file has disappeared.
					EnsurePortFileWritten(false);
					return true; // keep ticking
				}),
				HeartbeatInterval
			);

			// OnExit handles crash / abnormal termination paths
			FCoreDelegates::OnExit.AddLambda([PortFilePathCopy = PortFilePath]()
			{
				IFileManager::Get().Delete(*PortFilePathCopy);
			});
		}
	}
}

void USmithUEHttpServer::EnsurePortFileWritten(bool bForce)
{
	if (PortFilePath.IsEmpty())
	{
		return; // Not yet initialized
	}

	if (!bForce && IFileManager::Get().FileExists(*PortFilePath))
	{
		return; // File exists and we're not forced to re-write
	}

	// Resolve plugin version into the cached member on first use (game-thread only)
	if (PluginVersion.IsEmpty() || PluginVersion == TEXT("unknown"))
	{
		if (TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SmithUE")))
		{
			PluginVersion = Plugin->GetDescriptor().VersionName;
		}
	}

	// Project file path — normalise to forward slashes for JSON
	FString ProjectPath = FPaths::GetProjectFilePath();
	ProjectPath.ReplaceInline(TEXT("\\"), TEXT("/"));

	const FString JsonContent = FString::Printf(
		TEXT("{\"port\":%d,\"pid\":%u,\"project\":\"%s\",\"project_name\":\"%s\",\"started_at\":\"%s\",\"plugin_version\":\"%s\",\"instance_id\":\"%s\"}"),
		static_cast<int32>(BoundPort),
		FPlatformProcess::GetCurrentProcessId(),
		*ProjectPath,
		FApp::GetProjectName(),
		*FDateTime::UtcNow().ToIso8601(),
		*PluginVersion,
		*InstanceId
	);

	SmithUE_WritePortFileAtomic(PortFilePath, JsonContent);
	UE_LOG(LogSmithUE, Log, TEXT("SmithUE: portfile written to %s"), *PortFilePath);
}

void USmithUEHttpServer::StopServer()
{
	if (!bIsRunning)
	{
		return;
	}

	bIsRunning = false;
	bIsReady   = false;

	// Signal the runnable first so the accept loop stops and no new worker lambdas are launched.
	if (Runnable != nullptr)
	{
		Runnable->Stop();
	}

	// Remove readiness ticker if still pending
	if (ReadyTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ReadyTickerHandle);
		ReadyTickerHandle.Reset();
	}

	// Unregister heartbeat before deleting portfile to prevent a race where
	// the heartbeat fires after intentional deletion and re-creates the file.
	if (HeartbeatTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(HeartbeatTickerHandle);
		HeartbeatTickerHandle.Reset();
	}

	// Unregister PIE delegates that drive bPIEActive.
	if (PIEBeginHandle.IsValid())
	{
		FEditorDelegates::BeginPIE.Remove(PIEBeginHandle);
		PIEBeginHandle.Reset();
	}
	if (PIEEndHandle.IsValid())
	{
		FEditorDelegates::EndPIE.Remove(PIEEndHandle);
		PIEEndHandle.Reset();
	}
	bPIEActive = false;

	// Wait for in-flight workers to complete before removing discovery state or
	// destroying the runnable/server objects captured by worker lambdas.
	const double WaitStartTime = FPlatformTime::Seconds();
	constexpr double MaxWaitSeconds = 2.0;
	bool bShutdownDrainTimedOut = false;
	while (Runnable != nullptr && Runnable->GetActiveWorkerCount() > 0)
	{
		if (FPlatformTime::Seconds() - WaitStartTime > MaxWaitSeconds)
		{
			UE_LOG(LogSmithUE, Warning, TEXT("SmithUE: shutdown drain timeout — %d workers still running"),
				Runnable->GetActiveWorkerCount());
			bShutdownDrainTimedOut = true;
			break;
		}
		FPlatformProcess::Sleep(0.05f);
	}

	// Graceful shutdown: delete portfile so the slot is freed immediately
	if (!PortFilePath.IsEmpty())
	{
		IFileManager::Get().Delete(*PortFilePath);
		PortFilePath.Empty();
	}

	if (ServerThread != nullptr)
	{
		ServerThread->Kill(true);
		delete ServerThread;
		ServerThread = nullptr;
	}

	if (Runnable != nullptr && !bShutdownDrainTimedOut)
	{
		delete Runnable;
		Runnable = nullptr;
	}

	if (ListenerSocket.IsValid())
	{
		ListenerSocket.Reset();
	}

	if (!bShutdownDrainTimedOut && IsRooted())
	{
		RemoveFromRoot();
	}
}
