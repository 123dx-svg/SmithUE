#pragma once

#include "CoreMinimal.h"

class SMITHUE_API FSmithUEMaintenanceLock
{
public:
    explicit FSmithUEMaintenanceLock(const FString& OverridePath = FString());
    ~FSmithUEMaintenanceLock();

    FSmithUEMaintenanceLock(const FSmithUEMaintenanceLock&) = delete;
    FSmithUEMaintenanceLock& operator=(const FSmithUEMaintenanceLock&) = delete;

    bool IsLocked() const { return Handle != nullptr; }
    const FString& GetOwnerToken() const { return OwnerToken; }

private:
    void* Handle = nullptr;
    FString OwnerToken;
};