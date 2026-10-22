// Copyright Rocket Science.

#pragma once

#include "CoreMinimal.h"

/**
 * Repaints open widget editors after an import changes tokens.
 *
 * Token-aware widgets resolve their colour in SynchronizeProperties, which an
 * open widget editor's preview only runs when it is rebuilt. Without this, the
 * 2026-10-22 walkthrough showed the token swatch turn purple in Details while
 * the preview stayed green until someone clicked Compile.
 */
class FWidgetPreviewRefresher
{
public:
	void Register();
	void Unregister();

	/** Rebuild the preview of every open widget editor. Returns how many were refreshed. */
	static int32 RefreshOpenWidgetEditors();

private:
	FDelegateHandle Handle;
};
