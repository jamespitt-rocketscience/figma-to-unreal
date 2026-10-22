// Copyright Rocket Science.

#include "Modules/ModuleManager.h"

#include "DesignTokenColourRefCustomization.h"
#include "DesignTokenImporter.h"
#include "DesignTokenSettings.h"
#include "DesignTokenTypes.h"
#include "DesignTokenUserSettings.h"
#include "DesignTokens.h"
#include "FigmaLiveLink.h"
#include "FigmaTokenBridgeLog.h"
#include "FigmaTokenBridgeReport.h"
#include "WidgetPreviewRefresher.h"
#include "FigmaTokenFetcher.h"

#include "MessageLogModule.h"
#include "PropertyEditorModule.h"

#include "Containers/Ticker.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Logging/MessageLog.h"
#include "Misc/App.h"
#include "ToolMenus.h"
// SNotificationItem and FNotificationInfo are both declared in SNotificationList.h.
// There is no SNotificationItem.h, despite the class name.
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "FigmaTokenBridgeEditor"

/** Message Log › Sygma: one page per import, kept after the toast fades. */
static const FName MessageLogName(TEXT("FigmaTokenBridge"));

class FFigmaTokenBridgeEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// Make sure this project has an identity before anyone opens settings to
		// copy it into a Figma link. Generated once, then committed.
		const FString ProjectId = UDesignTokenSettings::GetOrCreateProjectId();
		UE_LOG(LogFigmaTokens, Log, TEXT("Sygma project id: %s"), *ProjectId);

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

		WidgetPreviewRefresher.Register();

		FMessageLogInitializationOptions LogOptions;
		LogOptions.bShowPages = true;
		FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog").RegisterLogListing(
			MessageLogName, LOCTEXT("MessageLogLabel", "Sygma"), LogOptions);

		UDesignTokenUserSettings::GetMutable()->OnSettingChanged().AddRaw(
			this, &FFigmaTokenBridgeEditorModule::OnUserSettingsChanged);

		// A listener has no business in a commandlet or an automation run. Started
		// from the first main-loop tick so that a port clash can show a toast; the
		// core ticker never runs outside the main loop anyway.
		if (UDesignTokenUserSettings::Get()->bEnableLiveLink && !FApp::IsUnattended() && !IsRunningCommandlet())
		{
			LiveLinkStartHandle = FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateLambda([](float)
				{
					ApplyLiveLink(/*bAnnounce=*/ false);
					return false;
				}));
		}

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
		FTSTicker::GetCoreTicker().RemoveTicker(LiveLinkStartHandle);

		FFigmaLiveLink::Stop();
		WidgetPreviewRefresher.Unregister();
		if (UObjectInitialized())
		{
			UDesignTokenUserSettings::GetMutable()->OnSettingChanged().RemoveAll(this);
		}

		if (FModuleManager::Get().IsModuleLoaded("MessageLog"))
		{
			FModuleManager::GetModuleChecked<FMessageLogModule>("MessageLog").UnregisterLogListing(MessageLogName);
		}

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
	FTSTicker::FDelegateHandle LiveLinkStartHandle;
	FWidgetPreviewRefresher WidgetPreviewRefresher;

	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);

		UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
		if (!ToolsMenu)
		{
			return;
		}

		FToolMenuSection& Section = ToolsMenu->FindOrAddSection(
			TEXT("FigmaTokenBridge"), LOCTEXT("SectionLabel", "Sygma"));

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

		Section.AddMenuEntry(
			TEXT("ToggleLiveLink"),
			LOCTEXT("LiveLinkLabel", "Figma Live Link"),
			LOCTEXT("LiveLinkTooltip", "Let the Figma plugin on this machine push tokens straight into this editor."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateStatic(&FFigmaTokenBridgeEditorModule::ToggleLiveLink),
				FCanExecuteAction(),
				FGetActionCheckState::CreateLambda([]()
				{
					return FFigmaLiveLink::IsRunning() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})),
			EUserInterfaceActionType::ToggleButton);

		Section.AddMenuEntry(
			TEXT("CopyLiveLinkPairingCode"),
			LOCTEXT("PairingLabel", "Copy Live Link Pairing Code"),
			LOCTEXT("PairingTooltip", "Turn on the live link and copy the code to paste into the Figma plugin. Needed once per machine."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateStatic(&FFigmaTokenBridgeEditorModule::CopyLiveLinkPairingCode)));
	}

	// ------------------------------------------------------------- live link

	void OnUserSettingsChanged(UObject*, FPropertyChangedEvent& Event)
	{
		const FName Changed = Event.GetMemberPropertyName();
		if (Changed == GET_MEMBER_NAME_CHECKED(UDesignTokenUserSettings, bEnableLiveLink)
			|| Changed == GET_MEMBER_NAME_CHECKED(UDesignTokenUserSettings, LiveLinkPort)
			|| Changed == GET_MEMBER_NAME_CHECKED(UDesignTokenUserSettings, LiveLinkSecret))
		{
			ApplyLiveLink(/*bAnnounce=*/ true);
		}
	}

	/** Start, restart or stop the listener to match the user's settings. */
	static void ApplyLiveLink(bool bAnnounce)
	{
		UDesignTokenUserSettings* Settings = UDesignTokenUserSettings::GetMutable();
		if (!Settings->bEnableLiveLink)
		{
			const bool bWasRunning = FFigmaLiveLink::IsRunning();
			FFigmaLiveLink::Stop();
			if (bAnnounce && bWasRunning)
			{
				Toast(LOCTEXT("LiveLinkOff", "Figma live link is off."), true);
			}
			return;
		}

		// Clearing the secret in Editor Preferences revokes every pairing: a new
		// one is generated here, and plugins holding the old one get a 401.
		const FString Error = FFigmaLiveLink::Start(Settings->LiveLinkPort, Settings->GetOrCreateLiveLinkSecret(),
			[](const FDesignTokenImportResult& Result, const FString& Context)
			{
				ShowResult(Result, Context, TEXT("Live push from Figma"));
			});

		if (!Error.IsEmpty())
		{
			// Shown even at startup: the user turned this on, and a link that is
			// silently down looks exactly like a designer who has not pushed yet.
			UE_LOG(LogFigmaTokens, Warning, TEXT("Figma live link: %s"), *Error);
			Toast(FText::FromString(TEXT("Figma live link could not start. ") + Error), false);
			return;
		}

		if (bAnnounce)
		{
			Toast(FText::Format(LOCTEXT("LiveLinkOn", "Figma live link is on, port {0}."),
				FText::AsNumber(Settings->LiveLinkPort, &FNumberFormattingOptions::DefaultNoGrouping())), true);
		}
	}

	static void ToggleLiveLink()
	{
		UDesignTokenUserSettings* Settings = UDesignTokenUserSettings::GetMutable();
		Settings->bEnableLiveLink = !FFigmaLiveLink::IsRunning();
		Settings->SaveConfig();
		ApplyLiveLink(/*bAnnounce=*/ true);
	}

	/**
	 * One click to pair: turns the link on if it is off, then copies the code. The
	 * secret is kept, so the same code keeps working after a restart.
	 */
	static void CopyLiveLinkPairingCode()
	{
		UDesignTokenUserSettings* Settings = UDesignTokenUserSettings::GetMutable();
		if (!Settings->bEnableLiveLink)
		{
			Settings->bEnableLiveLink = true;
			Settings->SaveConfig();
		}
		if (!FFigmaLiveLink::IsRunning())
		{
			ApplyLiveLink(/*bAnnounce=*/ false);
			if (!FFigmaLiveLink::IsRunning())
			{
				return;  // ApplyLiveLink has already said why.
			}
		}

		FPlatformApplicationMisc::ClipboardCopy(
			*FFigmaLiveLink::MakePairingCode(FFigmaLiveLink::GetPort(), Settings->GetOrCreateLiveLinkSecret()));

		FNotificationInfo Info(LOCTEXT("CopiedPairing", "Copied the live link pairing code."));
		Info.SubText = LOCTEXT("CopiedPairingSub",
			"Paste it into the Figma plugin under Live link. Once per machine; it survives restarts. Treat it like a password.");
		Info.ExpireDuration = 10.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}

	static void Toast(const FText& Text, bool bSuccess)
	{
		FNotificationInfo Info(Text);
		Info.ExpireDuration = bSuccess ? 5.0f : 12.0f;
		Info.bUseSuccessFailIcons = true;
		const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
		if (Item.IsValid())
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
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
			ShowResult(Result, FString(), TEXT("Sync from the local tokens.json"));
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
			ShowResult(Failed, FString(), TEXT("Sync from Figma"));
			return;
		}

		FDesignTokenImportResult Result = FDesignTokenImporter::ImportFromText(
			Fetched.Publication.DocumentJson, UDesignTokenSettings::Get()->GetAbsoluteTokensFilePath());
		Result.Warnings.Append(Fetched.Warnings);

		const FFigmaPublication& P = Fetched.Publication;
		ShowResult(Result, FString::Printf(TEXT("From Figma, published %s%s."),
			*P.PublishedAt, P.PublishedBy.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" by %s"), *P.PublishedBy)),
			TEXT("Sync from Figma"));
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
			// Compared by content, not exportedAt: a republish of the same tokens
			// is not news, and an unchanged sync leaves the asset's exportedAt as it was.
			if (Active && !Active->ContentFingerprint.IsEmpty()
				&& Active->ContentFingerprint == FDesignTokenImporter::ContentFingerprint(Fetched.Publication.DocumentJson))
			{
				UE_LOG(LogFigmaTokens, Log, TEXT("Design tokens are up to date with Figma."));
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

	/**
	 * Source names the route ("Sync from Figma") and titles the report page.
	 * Context, when set, is a line saying who published and when.
	 */
	static void ShowResult(const FDesignTokenImportResult& Result, const FString& Context, const FString& Source)
	{
		WriteReport(Result, Context, Source);

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

		// The Message Log page stops at 50 changes; this is the full list.
		for (const FString& Line : FFigmaTokenBridgeReport::ChangeLines(Result.Changes, MAX_int32))
		{
			UE_LOG(LogFigmaTokens, Display, TEXT("%s"), *Line);
		}

		UE_LOG(LogFigmaTokens, Display, TEXT("%s"), *Result.Summary());

		// The toast carries the headline; the report carries the detail, because a
		// notification that lists 171 tokens is not something anyone reads.
		FNotificationInfo Info(FText::FromString(Result.Summary()));
		Info.ExpireDuration = Result.bSuccess ? 6.0f : 12.0f;
		Info.bUseSuccessFailIcons = true;
		if (Result.Warnings.Num() || Result.Errors.Num())
		{
			Info.SubText = Context.IsEmpty()
				? LOCTEXT("SeeReport", "Details are in the Message Log, under Sygma.")
				: FText::FromString(Context + TEXT(" Details are in the Message Log, under Sygma."));
		}
		else if (!Context.IsEmpty())
		{
			Info.SubText = FText::FromString(Context);
		}
		Info.Hyperlink = FSimpleDelegate::CreateLambda([]() { FMessageLog(MessageLogName).Open(EMessageSeverity::Info, true); });
		Info.HyperlinkText = LOCTEXT("OpenReport", "Open the import report");

		const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
		if (Item.IsValid())
		{
			Item->SetCompletionState(Result.bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
			Item->ExpireAndFadeout();
		}
	}

	/**
	 * One Message Log page per import. Unlike the toast it stays, so "what did the
	 * last sync skip, and why" has an answer after the fact. Pages make the
	 * previous imports a dropdown away.
	 */
	static void WriteReport(const FDesignTokenImportResult& Result, const FString& Context, const FString& Source)
	{
		FMessageLog Report(MessageLogName);
		Report.NewPage(FText::FromString(FString::Printf(TEXT("%s, %s"),
			*Source, *FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S")))));

		if (!Context.IsEmpty())
		{
			Report.Info(FText::FromString(Context));
		}
		for (const FString& Error : Result.Errors)
		{
			Report.Error(FText::FromString(Error));
		}
		for (const FString& Warning : Result.Warnings)
		{
			Report.Warning(FText::FromString(Warning));
		}
		for (const FString& Line : FFigmaTokenBridgeReport::ChangeLines(Result.Changes, 50))
		{
			Report.Info(FText::FromString(Line));
		}
		Report.Info(FText::FromString(Result.Summary()));
	}
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FFigmaTokenBridgeEditorModule, FigmaTokenBridgeEditor);
