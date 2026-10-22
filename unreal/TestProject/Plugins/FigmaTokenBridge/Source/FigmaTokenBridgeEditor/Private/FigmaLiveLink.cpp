// Copyright Rocket Science.

#include "FigmaLiveLink.h"

#include "DesignTokenSettings.h"
#include "FigmaTokenBridgeLog.h"

#include "Dom/JsonObject.h"
#include "HttpPath.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "Misc/App.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	const TCHAR* HelloPath = TEXT("/figma-token-bridge/v1/hello");
	const TCHAR* PushPath = TEXT("/figma-token-bridge/v1/push");

	TSharedPtr<IHttpRouter> Router;
	TArray<FHttpRouteHandle> Routes;
	int32 BoundPort = 0;
	FString ActiveSecret;
	FFigmaLiveLink::FOnImported OnImportedCallback;

	FString ToJson(const TSharedRef<FJsonObject>& Object)
	{
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Object, Writer);
		return Out;
	}

	TArray<TSharedPtr<FJsonValue>> ToJsonArray(const TArray<FString>& Lines)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FString& Line : Lines)
		{
			Out.Add(MakeShared<FJsonValueString>(Line));
		}
		return Out;
	}

	/**
	 * The plugin's UI runs in a sandboxed iframe whose origin is "null", so every
	 * response has to allow any origin. That is safe only because nothing is
	 * answered without the secret. Chromium also asks before a page may reach a
	 * private address (Private Network Access), hence the last header.
	 */
	void AddCorsHeaders(FHttpServerResponse& Response)
	{
		Response.Headers.Add(TEXT("Access-Control-Allow-Origin"), { TEXT("*") });
		Response.Headers.Add(TEXT("Access-Control-Allow-Methods"), { TEXT("GET, POST, OPTIONS") });
		Response.Headers.Add(TEXT("Access-Control-Allow-Headers"), { TEXT("authorization, content-type") });
		Response.Headers.Add(TEXT("Access-Control-Allow-Private-Network"), { TEXT("true") });
		Response.Headers.Add(TEXT("Access-Control-Max-Age"), { TEXT("600") });
	}

	TUniquePtr<FHttpServerResponse> JsonResponse(EHttpServerResponseCodes Code, const FString& Body)
	{
		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(Body, TEXT("application/json"));
		Response->Code = Code;
		AddCorsHeaders(*Response);
		return Response;
	}

	TUniquePtr<FHttpServerResponse> ErrorResponse(EHttpServerResponseCodes Code, const FString& Message)
	{
		const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetBoolField(TEXT("ok"), false);
		Body->SetArrayField(TEXT("errors"), ToJsonArray({ Message }));
		return JsonResponse(Code, ToJson(Body));
	}

	/**
	 * Answers the preflight and the secret check shared by every route. Returns
	 * true when the request has been answered and the route has nothing to do.
	 */
	bool AnsweredBeforeRoute(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		if (Request.Verb == EHttpServerRequestVerbs::VERB_OPTIONS)
		{
			TUniquePtr<FHttpServerResponse> Preflight = MakeUnique<FHttpServerResponse>();
			Preflight->Code = EHttpServerResponseCodes::NoContent;
			AddCorsHeaders(*Preflight);
			OnComplete(MoveTemp(Preflight));
			return true;
		}

		if (!FFigmaLiveLink::IsAuthorised(Request.Headers, ActiveSecret))
		{
			OnComplete(ErrorResponse(EHttpServerResponseCodes::Denied,
				TEXT("This editor did not recognise the pairing code. Copy a fresh one from Tools > Copy Live Link Pairing Code and pair again.")));
			return true;
		}
		return false;
	}

	bool HandleHello(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		if (AnsweredBeforeRoute(Request, OnComplete))
		{
			return true;
		}

		const UDesignTokenSettings* Settings = UDesignTokenSettings::Get();
		OnComplete(JsonResponse(EHttpServerResponseCodes::Ok, FFigmaLiveLink::MakeHelloJson(
			UDesignTokenSettings::GetOrCreateProjectId(), FApp::GetProjectName(), Settings->FigmaFileKey)));
		return true;
	}

	bool HandlePush(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		if (AnsweredBeforeRoute(Request, OnComplete))
		{
			return true;
		}

		const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num());
		const FString JsonText(Converted.Length(), Converted.Get());
		if (JsonText.IsEmpty())
		{
			OnComplete(ErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("The push carried no tokens.")));
			return true;
		}

		// HTTPServer ticks on the game thread, so importing here is safe. Same call
		// as Sync, so the handshake checks and the tokens.json write are identical.
		const FDesignTokenImportResult Result = FDesignTokenImporter::ImportFromText(
			JsonText, UDesignTokenSettings::Get()->GetAbsoluteTokensFilePath());

		if (OnImportedCallback)
		{
			OnImportedCallback(Result, FFigmaLiveLink::DescribePush(JsonText));
		}

		// 200 even when the importer refused it: the request itself was fine, and
		// the plugin shows the importer's own errors either way.
		OnComplete(JsonResponse(EHttpServerResponseCodes::Ok, FFigmaLiveLink::MakePushResultJson(Result)));
		return true;
	}
}

FString FFigmaLiveLink::Start(int32 Port, const FString& Secret, FOnImported OnImported)
{
	Stop();

	if (Secret.IsEmpty())
	{
		return TEXT("The live link has no secret yet.");
	}

	FHttpServerModule& HttpServer = FHttpServerModule::Get();

	// Listeners start disabled until someone enables them. Enabling first means
	// GetHttpRouter binds the port now and reports a failure, instead of failing
	// silently later.
	HttpServer.StartAllListeners();
	Router = HttpServer.GetHttpRouter(static_cast<uint32>(Port), /*bFailOnBindFailure=*/ true);
	if (!Router.IsValid())
	{
		return FString::Printf(
			TEXT("Could not listen on port %d. Another program, or another open Unreal project, is probably using it."), Port);
	}

	Routes.Add(Router->BindRoute(FHttpPath(HelloPath),
		EHttpServerRequestVerbs::VERB_GET | EHttpServerRequestVerbs::VERB_OPTIONS,
		FHttpRequestHandler::CreateStatic(&HandleHello)));
	Routes.Add(Router->BindRoute(FHttpPath(PushPath),
		EHttpServerRequestVerbs::VERB_POST | EHttpServerRequestVerbs::VERB_OPTIONS,
		FHttpRequestHandler::CreateStatic(&HandlePush)));

	BoundPort = Port;
	ActiveSecret = Secret;
	OnImportedCallback = MoveTemp(OnImported);

	UE_LOG(LogFigmaTokens, Log, TEXT("Figma live link listening on port %d."), Port);
	return FString();
}

void FFigmaLiveLink::Stop()
{
	if (Router.IsValid())
	{
		for (const FHttpRouteHandle& Route : Routes)
		{
			Router->UnbindRoute(Route);
		}
		UE_LOG(LogFigmaTokens, Log, TEXT("Figma live link stopped."));
	}

	Routes.Reset();
	Router.Reset();
	BoundPort = 0;
	ActiveSecret.Reset();
	OnImportedCallback = nullptr;
}

bool FFigmaLiveLink::IsRunning()
{
	return Router.IsValid();
}

int32 FFigmaLiveLink::GetPort()
{
	return BoundPort;
}

FString FFigmaLiveLink::MakePairingCode(int32 Port, const FString& Secret)
{
	return FString::Printf(TEXT("ftb1:%d:%s"), Port, *Secret);
}

bool FFigmaLiveLink::IsAuthorised(const TMap<FString, TArray<FString>>& Headers, const FString& Secret)
{
	if (Secret.IsEmpty())
	{
		return false;
	}

	const TArray<FString>* Values = Headers.Find(TEXT("authorization"));
	if (!Values || Values->Num() != 1)
	{
		return false;
	}

	FString Presented = (*Values)[0].TrimStartAndEnd();
	if (!Presented.RemoveFromStart(TEXT("Bearer "), ESearchCase::IgnoreCase))
	{
		return false;
	}
	return Presented.TrimStart().Equals(Secret, ESearchCase::CaseSensitive);
}

FString FFigmaLiveLink::MakeHelloJson(const FString& ProjectId, const FString& ProjectName, const FString& FileKey)
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetBoolField(TEXT("ok"), true);
	Body->SetStringField(TEXT("projectId"), ProjectId);
	Body->SetStringField(TEXT("projectName"), ProjectName);
	Body->SetStringField(TEXT("fileKey"), FileKey);
	return ToJson(Body);
}

FString FFigmaLiveLink::MakePushResultJson(const FDesignTokenImportResult& Result)
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetBoolField(TEXT("ok"), Result.bSuccess);
	Body->SetStringField(TEXT("summary"), Result.Summary());
	Body->SetNumberField(TEXT("added"), Result.Added);
	Body->SetNumberField(TEXT("updated"), Result.Updated);
	Body->SetNumberField(TEXT("unchanged"), Result.Unchanged);
	Body->SetNumberField(TEXT("deprecated"), Result.Deprecated);
	Body->SetNumberField(TEXT("renamed"), Result.Renamed);
	Body->SetArrayField(TEXT("warnings"), ToJsonArray(Result.Warnings));
	Body->SetArrayField(TEXT("errors"), ToJsonArray(Result.Errors));
	return ToJson(Body);
}

FString FFigmaLiveLink::DescribePush(const FString& JsonText)
{
	FString By;
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(JsonText);
	if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
	{
		const TSharedPtr<FJsonObject>* Source = nullptr;
		if (Root->TryGetObjectField(TEXT("source"), Source) && Source)
		{
			(*Source)->TryGetStringField(TEXT("exportedBy"), By);
		}
	}

	return By.IsEmpty()
		? FString(TEXT("Pushed live from Figma."))
		: FString::Printf(TEXT("Pushed live from Figma by %s."), *By);
}
