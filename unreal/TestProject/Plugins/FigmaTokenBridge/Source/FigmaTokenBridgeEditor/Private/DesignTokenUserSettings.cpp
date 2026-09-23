// Copyright Rocket Science.

#include "DesignTokenUserSettings.h"

#include "HAL/PlatformMisc.h"

const UDesignTokenUserSettings* UDesignTokenUserSettings::Get()
{
	return GetDefault<UDesignTokenUserSettings>();
}

FString UDesignTokenUserSettings::GetAccessToken() const
{
	const FString Stored = FigmaAccessToken.TrimStartAndEnd();
	if (!Stored.IsEmpty())
	{
		return Stored;
	}
	return FPlatformMisc::GetEnvironmentVariable(TEXT("FIGMA_ACCESS_TOKEN")).TrimStartAndEnd();
}
