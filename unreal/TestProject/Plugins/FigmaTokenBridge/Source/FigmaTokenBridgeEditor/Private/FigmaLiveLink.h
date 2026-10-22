// Copyright Rocket Science.

#pragma once

#include "CoreMinimal.h"
#include "DesignTokenImporter.h"

/**
 * Lets the Figma plugin push tokens straight into this editor, on this machine.
 *
 * Publish and Sync stay the route between machines: designers often don't run
 * Unreal. This is the quick loop for whoever has Figma and the editor open side
 * by side. A push goes through the same importer and the same tokens.json as
 * Sync, so both handshake checks still run and the Git diff is still the review.
 *
 * Unity's Figma connector pairs through a token that changes on every editor
 * restart. Here the port is fixed and the secret lives in per-user settings, so
 * a designer pairs once per machine and it keeps working.
 *
 * Listens on the engine's HTTPServer, which binds to localhost unless the
 * project overrides [HTTPServer.Listeners] DefaultBindAddress. The secret is
 * what keeps anything else on the machine, a web page included, from pushing.
 */
class FFigmaLiveLink
{
public:
	/** The plugin's manifest allows exactly this port. Change both together. */
	static constexpr int32 DefaultPort = 30871;

	/** Called on the game thread after every push, with a line saying where it came from. */
	using FOnImported = TFunction<void(const FDesignTokenImportResult& Result, const FString& Context)>;

	/**
	 * Start listening, or move to a new port or secret. Returns an empty string on
	 * success, or why it could not start (most often: the port is taken).
	 */
	static FString Start(int32 Port, const FString& Secret, FOnImported OnImported);

	/**
	 * Unbind our routes. The engine has no way to close one listener without
	 * closing every module's, so the port stays open and answers 404.
	 */
	static void Stop();

	static bool IsRunning();
	static int32 GetPort();

	// ---- pure, split out for the automation tests ----

	/** What the designer pastes into the plugin once: "ftb1:<port>:<secret>". */
	static FString MakePairingCode(int32 Port, const FString& Secret);

	/** True when the headers carry "Authorization: Bearer <Secret>". Header keys arrive lower-cased. */
	static bool IsAuthorised(const TMap<FString, TArray<FString>>& Headers, const FString& Secret);

	/** The /hello body: enough for the plugin to match this editor to one of its links. */
	static FString MakeHelloJson(const FString& ProjectId, const FString& ProjectName, const FString& FileKey);

	/** The /push body: the import result, as the plugin shows it to the designer. */
	static FString MakePushResultJson(const FDesignTokenImportResult& Result);

	/** "Pushed live from Figma by <who>." from the export's source.exportedBy, when present. */
	static FString DescribePush(const FString& JsonText);
};
