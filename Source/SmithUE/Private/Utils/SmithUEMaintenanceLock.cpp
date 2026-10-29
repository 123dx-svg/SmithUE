#include "Utils/SmithUEMaintenanceLock.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

FSmithUEMaintenanceLock::FSmithUEMaintenanceLock(const FString& OverridePath)
{
#if PLATFORM_WINDOWS
    FString LockPath = OverridePath;
    if (LockPath.IsEmpty())
    {
        const FString LocalAppData = FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA"));
        if (LocalAppData.IsEmpty())
        {
            return;
        }
        LockPath = LocalAppData / TEXT(".smithue") / TEXT("maintenance.lock");
    }
    if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(LockPath), true))
    {
        return;
    }

    HANDLE File = ::CreateFileW(*LockPath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (File == INVALID_HANDLE_VALUE)
    {
        return;
    }

    OwnerToken = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString Owner = FString::Printf(TEXT("{\"pid\":%u,\"token\":\"%s\",\"owner\":\"editor\"}"),
        FPlatformProcess::GetCurrentProcessId(), *OwnerToken);
    const FTCHARToUTF8 Utf8(*Owner);
    DWORD Written = 0;
    if (!::WriteFile(File, Utf8.Get(), Utf8.Length(), &Written, nullptr)
        || Written != static_cast<DWORD>(Utf8.Length()))
    {
        ::CloseHandle(File);
        return;
    }
    ::FlushFileBuffers(File);
    Handle = File;
#endif
}

FSmithUEMaintenanceLock::~FSmithUEMaintenanceLock()
{
#if PLATFORM_WINDOWS
    if (Handle)
    {
        ::CloseHandle(Handle);
    }
#endif
}