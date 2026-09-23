// Copyright Rocket Science.

#include "FigmaPublication.h"

#include "DesignTokenSettings.h"

#include "Dom/JsonObject.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

const TCHAR* FFigmaPublicationReader::Namespace = TEXT("figmaTokenBridge");
const TCHAR* FFigmaPublicationReader::Schema = TEXT("figma-token-bridge-publish/1");

namespace
{
	const TCHAR* ManifestPrefix = TEXT("manifest/");

	const TCHAR* RepublishAdvice =
		TEXT("Ask a designer to open Figma Token Bridge and click Publish to Unreal again.");

	FString DescribeProject(const FFigmaPublication& P)
	{
		return FString::Printf(TEXT("'%s' (%s)"),
			P.ProjectName.IsEmpty() ? TEXT("unnamed") : *P.ProjectName, *P.ProjectId);
	}
}

FString FFigmaPublicationReader::BuildFileUrl(const FString& FileKey)
{
	// depth=1 returns the document and its pages but no layers, which keeps a
	// large design file's response small. The shared data sits on the document.
	return FString::Printf(TEXT("https://api.figma.com/v1/files/%s?depth=1&plugin_data=shared"),
		*FGenericPlatformHttp::UrlEncode(FileKey.TrimStartAndEnd()));
}

FString FFigmaPublicationReader::Fnv1a32(const FString& Text)
{
	uint32 Hash = 0x811c9dc5u;
	for (const TCHAR C : Text)
	{
		Hash ^= static_cast<uint32>(C);
		Hash *= 0x01000193u;
	}
	return FString::Printf(TEXT("%08x"), Hash);
}

bool FFigmaPublicationReader::ReadFromFileResponse(
	const FString& ResponseJson,
	const FString& ProjectId,
	FFigmaPublication& OutPublication,
	TArray<FString>& OutErrors,
	TArray<FString>& OutWarnings)
{
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ResponseJson), Root) || !Root.IsValid())
	{
		OutErrors.Add(TEXT("Figma's response was not valid JSON."));
		return false;
	}

	FString FileName;
	Root->TryGetStringField(TEXT("name"), FileName);

	const TSharedPtr<FJsonObject>* Document = nullptr;
	const TSharedPtr<FJsonObject>* Shared = nullptr;
	const TSharedPtr<FJsonObject>* Entries = nullptr;

	if (!Root->TryGetObjectField(TEXT("document"), Document) || !Document
		|| !(*Document)->TryGetObjectField(TEXT("sharedPluginData"), Shared) || !Shared
		|| !(*Shared)->TryGetObjectField(Namespace, Entries) || !Entries)
	{
		OutErrors.Add(FString::Printf(
			TEXT("Nothing has been published to Unreal from Figma file '%s' yet. %s"),
			*FileName, RepublishAdvice));
		return false;
	}

	// --- find every manifest, and the ones aimed at this project ------------
	const FString Mine = UDesignTokenSettings::NormaliseProjectId(ProjectId);
	TArray<FFigmaPublication> All;
	TArray<FFigmaPublication> ForMe;
	TMap<FString, TArray<FString>> ChunksByLink;
	TMap<FString, int32> LengthByLink;
	TMap<FString, FString> ChecksumByLink;

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Entries)->Values)
	{
		if (!Entry.Key.StartsWith(ManifestPrefix, ESearchCase::CaseSensitive))
		{
			continue;
		}

		FString ManifestText;
		TSharedPtr<FJsonObject> Manifest;
		if (!Entry.Value.IsValid() || !Entry.Value->TryGetString(ManifestText)
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ManifestText), Manifest)
			|| !Manifest.IsValid())
		{
			OutWarnings.Add(FString::Printf(TEXT("Ignored an unreadable publish entry '%s'."), *Entry.Key));
			continue;
		}

		FString ManifestSchema;
		Manifest->TryGetStringField(TEXT("schema"), ManifestSchema);
		if (ManifestSchema != Schema)
		{
			OutWarnings.Add(FString::Printf(
				TEXT("Ignored a publish in format '%s'; this plugin reads '%s'. Update whichever side is older."),
				*ManifestSchema, Schema));
			continue;
		}

		FFigmaPublication P;
		P.FileName = FileName;
		Manifest->TryGetStringField(TEXT("linkId"), P.LinkId);
		Manifest->TryGetStringField(TEXT("linkName"), P.LinkName);
		Manifest->TryGetStringField(TEXT("projectId"), P.ProjectId);
		Manifest->TryGetStringField(TEXT("projectName"), P.ProjectName);
		Manifest->TryGetStringField(TEXT("fileKey"), P.FileKey);
		Manifest->TryGetStringField(TEXT("publishId"), P.PublishId);
		Manifest->TryGetStringField(TEXT("publishedAt"), P.PublishedAt);
		Manifest->TryGetStringField(TEXT("publishedBy"), P.PublishedBy);
		Manifest->TryGetStringField(TEXT("exportedAt"), P.ExportedAt);
		Manifest->TryGetNumberField(TEXT("publishedCount"), P.PublishedCount);

		TArray<FString> ChunkKeys;
		Manifest->TryGetStringArrayField(TEXT("chunks"), ChunkKeys);
		ChunksByLink.Add(P.LinkId, ChunkKeys);

		int32 Length = INDEX_NONE;
		Manifest->TryGetNumberField(TEXT("length"), Length);
		LengthByLink.Add(P.LinkId, Length);

		FString Checksum;
		Manifest->TryGetStringField(TEXT("checksum"), Checksum);
		ChecksumByLink.Add(P.LinkId, Checksum);

		All.Add(P);
		if (!Mine.IsEmpty() && UDesignTokenSettings::NormaliseProjectId(P.ProjectId) == Mine)
		{
			ForMe.Add(P);
		}
	}

	if (ForMe.Num() == 0)
	{
		if (All.Num() == 0)
		{
			OutErrors.Add(FString::Printf(
				TEXT("Nothing has been published to Unreal from Figma file '%s' yet. %s"),
				*FileName, RepublishAdvice));
			return false;
		}

		// Name what IS published, so whoever hits this can tell whether the link
		// has the wrong id or simply has not been published yet.
		TArray<FString> Others;
		for (const FFigmaPublication& P : All)
		{
			Others.Add(DescribeProject(P));
		}
		OutErrors.Add(FString::Printf(
			TEXT("Figma file '%s' has publishes for %s, but none for this project (%s). ")
			TEXT("Check that a link in the Figma plugin uses this project's id, then publish it."),
			*FileName, *FString::Join(Others, TEXT(", ")), *ProjectId));
		return false;
	}

	// ISO-8601 timestamps sort correctly as strings.
	ForMe.Sort([](const FFigmaPublication& A, const FFigmaPublication& B) { return A.PublishedAt > B.PublishedAt; });
	if (ForMe.Num() > 1)
	{
		TArray<FString> Names;
		for (const FFigmaPublication& P : ForMe)
		{
			Names.Add(FString::Printf(TEXT("'%s'"), *P.LinkName));
		}
		OutWarnings.Add(FString::Printf(
			TEXT("%d Figma links target this project (%s). Using the most recent publish, '%s'. Delete the extra links to remove the ambiguity."),
			ForMe.Num(), *FString::Join(Names, TEXT(", ")), *ForMe[0].LinkName));
	}

	OutPublication = ForMe[0];

	// --- reassemble and verify --------------------------------------------
	const TArray<FString>& ChunkKeys = ChunksByLink.FindChecked(OutPublication.LinkId);
	if (ChunkKeys.Num() == 0)
	{
		OutErrors.Add(FString::Printf(TEXT("The publish for '%s' lists no data. %s"),
			*OutPublication.LinkName, RepublishAdvice));
		return false;
	}

	FString Text;
	for (const FString& Key : ChunkKeys)
	{
		FString Part;
		if (!(*Entries)->TryGetStringField(Key, Part) || Part.IsEmpty())
		{
			// Most likely two designers published at the same moment and one
			// clean-up removed the other's chunks. Publishing again fixes it.
			OutErrors.Add(FString::Printf(TEXT("The publish for '%s' is incomplete: '%s' is missing. %s"),
				*OutPublication.LinkName, *Key, RepublishAdvice));
			return false;
		}
		Text += Part;
	}

	if (Text.Len() != LengthByLink.FindChecked(OutPublication.LinkId)
		|| Fnv1a32(Text) != ChecksumByLink.FindChecked(OutPublication.LinkId))
	{
		OutErrors.Add(FString::Printf(TEXT("The publish for '%s' failed its checksum, so it was not imported. %s"),
			*OutPublication.LinkName, RepublishAdvice));
		return false;
	}

	OutPublication.DocumentJson = MoveTemp(Text);
	return true;
}

FString FFigmaPublicationReader::PrettyPrint(const FString& CompactJson)
{
	FString Out;
	Out.Reserve(CompactJson.Len() * 2);

	int32 Depth = 0;
	bool bInString = false;
	bool bEscaped = false;

	auto NewLine = [&Out, &Depth]()
	{
		Out.AppendChar(TEXT('\n'));
		for (int32 i = 0; i < Depth; ++i)
		{
			Out.Append(TEXT("  "));
		}
	};

	auto NextSignificant = [&CompactJson](int32 From) -> int32
	{
		while (From < CompactJson.Len() && FChar::IsWhitespace(CompactJson[From]))
		{
			++From;
		}
		return From;
	};

	for (int32 i = 0; i < CompactJson.Len(); ++i)
	{
		const TCHAR C = CompactJson[i];

		if (bInString)
		{
			Out.AppendChar(C);
			if (bEscaped)          { bEscaped = false; }
			else if (C == '\\')    { bEscaped = true; }
			else if (C == '"')     { bInString = false; }
			continue;
		}

		switch (C)
		{
		case '"':
			bInString = true;
			Out.AppendChar(C);
			break;

		case '{':
		case '[':
		{
			// JSON.stringify writes empty containers as {} and [], not split
			// across lines.
			const TCHAR Close = C == '{' ? '}' : ']';
			const int32 Next = NextSignificant(i + 1);
			Out.AppendChar(C);
			if (Next < CompactJson.Len() && CompactJson[Next] == Close)
			{
				Out.AppendChar(Close);
				i = Next;
			}
			else
			{
				++Depth;
				NewLine();
			}
			break;
		}

		case '}':
		case ']':
			--Depth;
			NewLine();
			Out.AppendChar(C);
			break;

		case ',':
			Out.AppendChar(C);
			NewLine();
			break;

		case ':':
			Out.Append(TEXT(": "));
			break;

		default:
			if (!FChar::IsWhitespace(C))
			{
				Out.AppendChar(C);
			}
			break;
		}
	}

	Out.AppendChar(TEXT('\n'));
	return Out;
}
