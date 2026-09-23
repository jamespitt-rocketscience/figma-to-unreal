// Copyright Rocket Science.

#include "FigmaTokenFetcher.h"

#include "DesignTokenSettings.h"
#include "DesignTokenUserSettings.h"
#include "FigmaTokenBridgeLog.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

namespace
{
	const TCHAR* TokenLocation = TEXT("Editor Preferences > Plugins > Figma Token Bridge");

	/** Turn a failed response into something a developer can act on. */
	FString DescribeFailure(const FHttpResponsePtr& Response, const FString& FileKey)
	{
		const int32 Code = Response->GetResponseCode();

		if (Code == 403)
		{
			return FString::Printf(
				TEXT("Figma refused the access token (403). Check the token in %s, and that it has the file_content:read scope."),
				TokenLocation);
		}
		if (Code == 404)
		{
			return FString::Printf(
				TEXT("Figma file '%s' was not found (404). Check the file key in Project Settings, and that your Figma account can open the file."),
				*FileKey);
		}
		if (Code == 429)
		{
			const FString RetryAfter = Response->GetHeader(TEXT("Retry-After"));
			const bool bLowSeat = Response->GetHeader(TEXT("X-Figma-Rate-Limit-Type")).Equals(TEXT("low"), ESearchCase::IgnoreCase);

			FString Message = TEXT("Figma's rate limit was hit (429).");
			if (!RetryAfter.IsEmpty())
			{
				Message += FString::Printf(TEXT(" Retry in %s seconds."), *RetryAfter);
			}
			if (bLowSeat)
			{
				// Worth saying outright: it looks like an outage, and waiting a
				// minute will not fix it.
				Message += TEXT(" Your token belongs to a View or Collab seat, which gets 20 file reads a month. ")
				           TEXT("Use a token from a Dev or Full seat, or sync from the committed tokens.json.");
			}
			return Message;
		}

		return FString::Printf(TEXT("Figma returned HTTP %d: %s"), Code, *Response->GetContentAsString().Left(300));
	}
}

FString FFigmaTokenFetcher::WhyCannotPull()
{
	const UDesignTokenSettings* Settings = UDesignTokenSettings::Get();
	if (!Settings || !Settings->bPullFromFigma)
	{
		return TEXT("Pull from Figma is turned off in Project Settings > Plugins > Figma Token Bridge.");
	}
	if (Settings->FigmaFileKey.TrimStartAndEnd().IsEmpty())
	{
		return TEXT("This project has no Figma file key, so there is nothing to pull from. Set it in Project Settings > Plugins > Figma Token Bridge.");
	}
	if (UDesignTokenUserSettings::Get()->GetAccessToken().IsEmpty())
	{
		return FString::Printf(
			TEXT("To pull the latest publish straight from Figma, add a Figma personal access token in %s."),
			TokenLocation);
	}
	return FString();
}

void FFigmaTokenFetcher::FetchPublication(TFunction<void(const FFigmaFetchResult&)> OnComplete)
{
	const FString Blocker = WhyCannotPull();
	if (!Blocker.IsEmpty())
	{
		FFigmaFetchResult Result;
		Result.Errors.Add(Blocker);
		OnComplete(Result);
		return;
	}

	const FString FileKey = UDesignTokenSettings::Get()->FigmaFileKey.TrimStartAndEnd();
	const FString ProjectId = UDesignTokenSettings::GetOrCreateProjectId();

	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetVerb(TEXT("GET"));
	Request->SetURL(FFigmaPublicationReader::BuildFileUrl(FileKey));
	Request->SetHeader(TEXT("X-Figma-Token"), UDesignTokenUserSettings::Get()->GetAccessToken());
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Request->SetTimeout(30.0f);

	Request->OnProcessRequestComplete().BindLambda(
		[OnComplete, FileKey, ProjectId](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			FFigmaFetchResult Result;

			if (!bConnected || !Response.IsValid())
			{
				Result.Errors.Add(TEXT("Could not reach api.figma.com. Check your connection, or turn off Pull from Figma to import the local tokens.json."));
			}
			else if (Response->GetResponseCode() != 200)
			{
				Result.Errors.Add(DescribeFailure(Response, FileKey));
			}
			else
			{
				Result.bSuccess = FFigmaPublicationReader::ReadFromFileResponse(
					Response->GetContentAsString(), ProjectId, Result.Publication, Result.Errors, Result.Warnings);
			}

			UE_LOG(LogFigmaTokens, Log, TEXT("Fetched publish from Figma file %s: %s"),
				*FileKey, Result.bSuccess ? TEXT("ok") : TEXT("failed"));
			OnComplete(Result);
		});

	Request->ProcessRequest();
}
