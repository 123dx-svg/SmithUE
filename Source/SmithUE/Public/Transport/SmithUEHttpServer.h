#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "HAL/ThreadSafeBool.h"
#include "SmithUEHttpServer.generated.h"

class FSocket;
class FSmithUEHttpServerRunnable;
class FRunnableThread;

UCLASS()
class SMITHUE_API USmithUEHttpServer : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	USmithUEHttpServer();
	virtual ~USmithUEHttpServer();

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void StartServer();
	void StopServer();

	/** Write (or re-write) the portfile for this server instance.
	 *  If bForce is false, skips the write when the file already exists.
	 *  Preserves atomic write + current-user-only ACL.
	 *  Safe to call from the game thread only. */
	void EnsurePortFileWritten(bool bForce = false);
	bool IsRunning() const { return bIsRunning; }
	uint16 GetBoundPort() const { return BoundPort; }

	/** Set to true on the GameThread once AssetRegistry finishes loading. Safe to read from any thread. */
	FThreadSafeBool bIsReady;

	/** True while a PIE session is active. Updated on the GameThread via PIE delegates,
	 *  read from the HTTP worker thread in the /ready handler. */
	FThreadSafeBool bPIEActive;

	/** Plugin version cached at StartServer (game thread, IPluginManager-only).
	 *  Immutable after init — safe to read from the HTTP worker thread. */
	FString PluginVersion = TEXT("unknown");
	FString InstanceId;

private:
	TSharedPtr<FSocket> ListenerSocket;
	FSmithUEHttpServerRunnable* Runnable;
	FRunnableThread* ServerThread;
	bool bIsRunning;
	uint16 Port;
	uint16 BoundPort = 0;
	FString PortFilePath;

	/** Handle for the FTSTicker delegate that polls asset-registry readiness. */
	FTSTicker::FDelegateHandle ReadyTickerHandle;

	/** Portfile heartbeat — re-writes the portfile if it disappears externally. */
	FTSTicker::FDelegateHandle HeartbeatTickerHandle;

	/** Handles for the PIE begin/end delegates that drive bPIEActive. */
	FDelegateHandle PIEBeginHandle;
	FDelegateHandle PIEEndHandle;
};
