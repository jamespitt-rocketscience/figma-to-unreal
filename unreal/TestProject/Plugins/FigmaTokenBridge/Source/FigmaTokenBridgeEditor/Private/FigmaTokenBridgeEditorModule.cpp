// Copyright Rocket Science.

#include "Modules/ModuleManager.h"

#include "DesignTokenColourRefCustomization.h"
#include "DesignTokenImporter.h"
#include "DesignTokenSettings.h"
#include "DesignTokenTypes.h"
#include "FigmaTokenBridgeLog.h"

#include "PropertyEditorModule.h"

#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "ToolMenus.h"
// SNotificationItem and FNotificationInfo are both declared in SNotificationList.h.
// There is no SNotificationItem.h, despite the class name.
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "FigmaTokenBridgeEditor"

class FFigmaTokenBridgeEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// Make sure this project has an identity before anyone opens settings to
		// copy it into a Figma link. Generated once, then committed.
		const FString ProjectId = UDesignTokenSettings::GetOrCreateProjectId();
		UE_LOG(LogFigmaTokens, Log, TEXT("Figma Token Bridge project id: %s"), *ProjectId);

		UToolMenus::RegisterStartupCallback(
			FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FFigmaTokenBridgeEditorModule::RegisterMenus));

		// Registered against the struct rather than any widget class, so every
		// token-aware property added later gets the swatch and the alias chain
		// without another line of editor code.
		FPropertyEditorModule& PropertyModule =
			FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

		PropertyModule.RegisterCustomPropertyTypeLayout(
			FDesignTokenColourRef::StaticStruct()->GetFName(),
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(
				&FDesignTokenColourRefCustomization::MakeInstance));

		PropertyModule.NotifyCustomizationModuleChanged();
	}

	virtual void ShutdownModule() override
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);

		// Guarded: on editor shutdown PropertyEditor may already be gone, and
		// LoadModuleChecked would assert on the way out.
		if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
		{
			FPropertyEditorModule& PropertyModule =
				FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

			PropertyModule.UnregisterCustomPropertyTypeLayout(
				FDesignTokenColourRef::StaticStruct()->GetFName());

			PropertyModule.NotifyCustomizationModuleChanged();
		}
	}

private:
	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);

		UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
		if (!ToolsMenu)
		{
			return;
		}

		FToolMenuSection& Section = ToolsMenu->FindOrAddSection(
			TEXT("FigmaTokenBridge"), LOCTEXT("SectionLabel", "Figma Token Bridge"));

		Section.AddMenuEntry(
			TEXT("SyncDesignTokens"),
			LOCTEXT("SyncLabel", "Sync Design Tokens"),
			LOCTEXT("SyncTooltip", "Read the linked tokens.json and regenerate the design token asset."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateStatic(&FFigmaTokenBridgeEditorModule::SyncTokens)));

		Section.AddMenuEntry(
			TEXT("CopyProjectId"),
			LOCTEXT("CopyIdLabel", "Copy Project Id for Figma"),
			LOCTEXT("CopyIdTooltip", "Copy this project's id to the clipboard, to paste into a link in the Figma plugin."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateStatic(&FFigmaTokenBridgeEditorModule::CopyProjectId)));
	}

	/**
	 * The designer needs this value to author a link. Putting it one click away
	 * is the difference between the handshake being used and being worked around.
	 */
	static void CopyProjectId()
	{
		const FString ProjectId = UDesignTokenSettings::GetOrCreateProjectId();
		FPlatformApplicationMisc::ClipboardCopy(*ProjectId);

		FNotificationInfo Info(FText::Format(
			LOCTEXT("CopiedId", "Copied {0} — paste it into the Figma plugin when creating a link."),
			FText::FromString(ProjectId)));
		Info.ExpireDuration = 8.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}

	static void SyncTokens()
	{
		const FDesignTokenImportResult Result = FDesignTokenImporter::ImportFromSettings();

		for (const FString& Warning : Result.Warnings)
		{
			UE_LOG(LogFigmaTokens, Warning, TEXT("%s"), *Warning);
		}
		for (const FString& Error : Result.Errors)
		{
			UE_LOG(LogFigmaTokens, Error, TEXT("%s"), *Error);
		}

		UE_LOG(LogFigmaTokens, Display, TEXT("%s"), *Result.Summary());

		// The toast carries the headline; the log carries the detail, because a
		// notification that lists 171 tokens is not something anyone reads.
		FNotificationInfo Info(FText::FromString(Result.Summary()));
		Info.ExpireDuration = Result.bSuccess ? 6.0f : 12.0f;
		Info.bUseSuccessFailIcons = true;
		if (Result.Warnings.Num() || Result.Errors.Num())
		{
			Info.SubText = LOCTEXT("SeeLog", "See the Output Log (LogFigmaTokens) for details.");
		}

		const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
		if (Item.IsValid())
		{
			Item->SetCompletionState(Result.bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
			Item->ExpireAndFadeout();
		}
	}
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FFigmaTokenBridgeEditorModule, FigmaTokenBridgeEditor);
