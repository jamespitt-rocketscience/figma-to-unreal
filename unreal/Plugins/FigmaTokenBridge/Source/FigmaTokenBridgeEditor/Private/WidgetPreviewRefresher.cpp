// Copyright Rocket Science.

#include "WidgetPreviewRefresher.h"

#include "DesignTokenLibrary.h"
#include "FigmaTokenBridgeLog.h"

#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditor.h"

void FWidgetPreviewRefresher::Register()
{
	Handle = UDesignTokenLibrary::OnDesignTokensChanged().AddLambda([]()
	{
		RefreshOpenWidgetEditors();
	});
}

void FWidgetPreviewRefresher::Unregister()
{
	UDesignTokenLibrary::OnDesignTokensChanged().Remove(Handle);
	Handle.Reset();
}

int32 FWidgetPreviewRefresher::RefreshOpenWidgetEditors()
{
	UAssetEditorSubsystem* Editors = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
	if (!Editors)
	{
		return 0;
	}

	int32 Refreshed = 0;
	for (UObject* Asset : Editors->GetAllEditedAssets())
	{
		if (!Cast<UWidgetBlueprint>(Asset))
		{
			continue;
		}

		IAssetEditorInstance* Editor = Editors->FindEditorForAsset(Asset, /*bFocusIfOpen=*/ false);
		if (Editor && Editor->GetEditorName() == FName(TEXT("WidgetBlueprintEditor")))
		{
			static_cast<FWidgetBlueprintEditor*>(Editor)->RefreshPreview();
			++Refreshed;
		}
	}

	if (Refreshed > 0)
	{
		UE_LOG(LogFigmaTokens, Log, TEXT("Refreshed %d open widget editor(s)."), Refreshed);
	}
	return Refreshed;
}
