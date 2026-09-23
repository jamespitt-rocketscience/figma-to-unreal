// Copyright Rocket Science.

#include "Modules/ModuleManager.h"

#include "DesignTokenColourRefCustomization.h"
#include "DesignTokenImporter.h"
#include "DesignTokenSettings.h"
#include "DesignTokenTypes.h"
#include "DesignTokenUserSettings.h"
#include "DesignTokens.h"
#include "FigmaTokenBridgeLog.h"
#include "FigmaTokenFetcher.h"

#include "PropertyEditorModule.h"

#include "Containers/Ticker.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/App.h"
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

		// Delayed so the editor has finished opening and Slate can show a toast.
		// The core ticker only runs in the main loop, so commandlets and
		// automation runs never get here.
		if (UDesignTokenUserSettings::Get()->bCheckForPublishedTokensOnStartup && !FApp::IsUnattended())
		{
			StartupCheckHandle = FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateLambda([](float)
				{
					CheckForPublishedTokens();
					return false;
				}),
				5.0f);
		}
	}

	virtual void ShutdownModule() override
	{
		FTSTicker::GetCoreTicker().RemoveTicker(StartupCheckHandle);

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
	FTSTicker::FDelegateHandle StartupCheckHandle;

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
			LOCTEXT("SyncTooltip", "Fetch the designer's latest publish from Figma (or read the local tokens.json) and regenerate the design token asset."),
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

	/** Guards against a second Sync click while the first fetch is in flight. */
	static inline bool bFetchInFlight = false;

	/** The startup check's "Sync now" toast, so its buttons can dismiss it. */
	static inline TWeakPtr<SNotificationItem> PendingUpdateToast;

	/**
	 * Pull the designer's latest publish from Figma when this user can. Otherwise
	 * import the local tokens.json, and say why so a stale file is not mistaken
	 * for the latest design.
	 */
	static void SyncTokens()
	{
		if (bFetchInFlight)
		{
			return;
		}

		const FString Blocker = FFigmaTokenFetcher::WhyCannotPull();
		if (!Blocker.IsEmpty())
		{
			FDesignTokenImportResult Result = FDesignTokenImporter::ImportFromSettings();
			if (UDesignTokenSettings::Get()->bPullFromFigma)
			{
				Result.Warnings.Insert(TEXT("Imported the local tokens.json, not Figma's latest publish. ") + Blocker, 0);
			}
			ShowResult(Result, FString());
			return;
		}

		FNotificationInfo Info(LOCTEXT("Fetching", "Fetching the latest publish from Figma…"));
		Info.bFireAndForget = false;
		const TSharedPtr<SNotificationItem> Pending = FSlateNotificationManager::Get().AddNotification(Info);
		if (Pending.IsValid())
		{
			Pending->SetCompletionState(SNotificationItem::CS_Pending);
		}

		bFetchInFlight = true;
		FFigmaTokenFetcher::FetchPublication([Pending](const FFigmaFetchResult& Fetched)
		{
			bFetchInFlight = false;
			if (Pending.IsValid())
			{
				Pending->Fadeout();
			}
			ImportFetched(Fetched);
		});
	}

	static void ImportFetched(const FFigmaFetchResult& Fetched)
	{
		if (!Fetched.bSuccess)
		{
			FDesignTokenImportResult Failed;
			Failed.Errors = Fetched.Errors;
			Failed.Warnings = Fetched.Warnings;
			ShowResult(Failed, FString());
			return;
		}

		FDesignTokenImportResult Result = FDesignTokenImporter::ImportFromText(
			Fetched.Publication.DocumentJson, UDesignTokenSettings::Get()->GetAbsoluteTokensFilePath());
		Result.Warnings.Append(Fetched.Warnings);

		const FFigmaPublication& P = Fetched.Publication;
		ShowResult(Result, FString::Printf(TEXT("From Figma, published %s%s."),
			*P.PublishedAt, P.PublishedBy.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" by %s"), *P.PublishedBy)));
	}

	/**
	 * Opt-in, per user. One request per launch, and the fetched publish is kept,
	 * so accepting the offer imports it without a second request. That matters on
	 * a seat with 20 reads a month.
	 */
	static void CheckForPublishedTokens()
	{
		if (!FFigmaTokenFetcher::WhyCannotPull().IsEmpty() || bFetchInFlight)
		{
			return;
		}

		bFetchInFlight = true;
		FFigmaTokenFetcher::FetchPublication([](const FFigmaFetchResult& Fetched)
		{
			bFetchInFlight = false;

			if (!Fetched.bSuccess)
			{
				// Not a toast: nobody asked for this, and a failure here must not
				// nag on every launch. Sync reports the same error when clicked.
				for (const FString& Error : Fetched.Errors)
				{
					UE_LOG(LogFigmaTokens, Warning, TEXT("Startup check for published tokens: %s"), *Error);
				}
				return;
			}

			const UDesignTokens* Active = Cast<UDesignTokens>(UDesignTokenSettings::Get()->ActiveTokens.TryLoad());
			if (Active && Active->ExportedAt == Fetched.Publication.ExportedAt)
			{
				UE_LOG(LogFigmaTokens, Log, TEXT("Design tokens are up to date with Figma (%s)."), *Active->ExportedAt);
				return;
			}

			const FFigmaPublication& P = Fetched.Publication;
			FNotificationInfo Info(LOCTEXT("NewPublish", "New design tokens have been published in Figma."));
			Info.SubText = FText::FromString(FString::Printf(TEXT("%s%s, %s"),
				P.LinkName.IsEmpty() ? TEXT("Design tokens") : *P.LinkName,
				P.PublishedBy.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" by %s"), *P.PublishedBy),
				*P.PublishedAt));
			Info.bFireAndForget = false;
			Info.ButtonDetails.Add(FNotificationButtonInfo(
				LOCTEXT("SyncNow", "Sync now"), FText::GetEmpty(),
				FSimpleDelegate::CreateLambda([Fetched]()
				{
					DismissPendingUpdateToast();
					ImportFetched(Fetched);
				}),
				SNotificationItem::CS_None));
			Info.ButtonDetails.Add(FNotificationButtonInfo(
				LOCTEXT("Later", "Later"), FText::GetEmpty(),
				FSimpleDelegate::CreateStatic(&FFigmaTokenBridgeEditorModule::DismissPendingUpdateToast),
				SNotificationItem::CS_None));

			PendingUpdateToast = FSlateNotificationManager::Get().AddNotification(Info);
		});
	}

	static void DismissPendingUpdateToast()
	{
		if (const TSharedPtr<SNotificationItem> Toast = PendingUpdateToast.Pin())
		{
			Toast->Fadeout();
		}
		PendingUpdateToast.Reset();
	}

	/** Context, when set, is a line saying where the tokens came from. */
	static void ShowResult(const FDesignTokenImportResult& Result, const FString& Context)
	{
		if (!Context.IsEmpty())
		{
			UE_LOG(LogFigmaTokens, Display, TEXT("%s"), *Context);
		}

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
			Info.SubText = Context.IsEmpty()
				? LOCTEXT("SeeLog", "See the Output Log (LogFigmaTokens) for details.")
				: FText::FromString(Context + TEXT(" See the Output Log (LogFigmaTokens) for details."));
		}
		else if (!Context.IsEmpty())
		{
			Info.SubText = FText::FromString(Context);
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
