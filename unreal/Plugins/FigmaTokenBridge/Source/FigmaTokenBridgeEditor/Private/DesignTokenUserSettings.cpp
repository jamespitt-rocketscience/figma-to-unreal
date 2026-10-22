// Copyright Rocket Science.

#include "DesignTokenUserSettings.h"

#include "HAL/PlatformMisc.h"
#include "Misc/Guid.h"

const UDesignTokenUserSettings* UDesignTokenUserSettings::Get()
{
	return GetDefault<UDesignTokenUserSettings>();
}

UDesignTokenUserSettings* UDesignTokenUserSettings::GetMutable()
{
	return GetMutableDefault<UDesignTokenUserSettings>();
}

FString UDesignTokenUserSettings::GetOrCreateLiveLinkSecret()
{
	LiveLinkSecret.TrimStartAndEndInline();
	if (LiveLinkSecret.IsEmpty())
	{
		// Two random GUIDs: 64 hex characters, and nothing a person has to type.
		LiveLinkSecret = FGuid::NewGuid().ToString(EGuidFormats::Digits) + FGuid::NewGuid().ToString(EGuidFormats::Digits);
		SaveConfig();
	}
	return LiveLinkSecret;
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
