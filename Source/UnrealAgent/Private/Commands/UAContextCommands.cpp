// Copyright KuoYu. All Rights Reserved.

#include "Commands/UAContextCommands.h"
#include "UnrealAgent.h"
#include "UALogCapture.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "AssetRegistry/AssetData.h"
#include "EditorUtilityLibrary.h"

DEFINE_LOG_CATEGORY_STATIC(LogUAContext, Log, All);

// ====================================================================
// GetSupportedMethods
// ====================================================================

TArray<FString> UAContextCommands::GetSupportedMethods() const
{
	return {
		TEXT("get_open_editors"),
		TEXT("get_selected_assets"),
		TEXT("get_browser_path"),
		TEXT("get_message_log"),
		TEXT("get_output_log"),
	};
}

// ====================================================================
// GetToolSchema
// ====================================================================

TSharedPtr<FJsonObject> UAContextCommands::GetToolSchema(const FString& MethodName) const
{
	if (MethodName == TEXT("get_open_editors"))
	{
		return MakeToolSchema(
			TEXT("get_open_editors"),
			TEXT("获取当前打开的所有资产编辑器列表。"
				"返回每个编辑器的 asset_path、asset_name、asset_class、editor_name。"
				"用于理解用户当前正在编辑什么。")
		);
	}

	if (MethodName == TEXT("get_selected_assets"))
	{
		return MakeToolSchema(
			TEXT("get_selected_assets"),
			TEXT("获取 Content Browser 中当前选中的资产列表。"
				"返回 path、name、class。")
		);
	}

	if (MethodName == TEXT("get_browser_path"))
	{
		return MakeToolSchema(
			TEXT("get_browser_path"),
			TEXT("获取 Content Browser 当前浏览的文件夹路径。")
		);
	}

	if (MethodName == TEXT("get_message_log"))
	{
		TSharedPtr<FJsonObject> InputSchema = MakeShared<FJsonObject>();
		InputSchema->SetStringField(TEXT("type"), TEXT("object"));

		TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

		TSharedPtr<FJsonObject> CategoryProp = MakeShared<FJsonObject>();
		CategoryProp->SetStringField(TEXT("type"), TEXT("string"));
		CategoryProp->SetStringField(TEXT("description"),
			TEXT("日志类别过滤（如 BlueprintLog、PIE、MaterialEditor 等），留空返回所有类别。"));
		Properties->SetObjectField(TEXT("category"), CategoryProp);

		TSharedPtr<FJsonObject> CountProp = MakeShared<FJsonObject>();
		CountProp->SetStringField(TEXT("type"), TEXT("integer"));
		CountProp->SetStringField(TEXT("description"),
			TEXT("返回的日志条数，默认 50，最大 200。"));
		Properties->SetObjectField(TEXT("count"), CountProp);

		TSharedPtr<FJsonObject> SeverityProp = MakeShared<FJsonObject>();
		SeverityProp->SetStringField(TEXT("type"), TEXT("string"));
		SeverityProp->SetStringField(TEXT("description"),
			TEXT("严重级别过滤：Error, Warning, Log, Display 等。留空返回所有级别。"));
		Properties->SetObjectField(TEXT("severity"), SeverityProp);

		InputSchema->SetObjectField(TEXT("properties"), Properties);

		return MakeToolSchema(
			TEXT("get_message_log"),
			TEXT("获取最近的消息日志（包含编译错误、警告等）。"
				"可按类别和严重级别过滤。"
				"适合在编译/操作后检查是否有错误。"),
			InputSchema
		);
	}

	if (MethodName == TEXT("get_output_log"))
	{
		TSharedPtr<FJsonObject> InputSchema = MakeShared<FJsonObject>();
		InputSchema->SetStringField(TEXT("type"), TEXT("object"));

		TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

		TSharedPtr<FJsonObject> CountProp = MakeShared<FJsonObject>();
		CountProp->SetStringField(TEXT("type"), TEXT("integer"));
		CountProp->SetStringField(TEXT("description"),
			TEXT("返回的日志条数，默认 50，最大 200。"));
		Properties->SetObjectField(TEXT("count"), CountProp);

		TSharedPtr<FJsonObject> FilterProp = MakeShared<FJsonObject>();
		FilterProp->SetStringField(TEXT("type"), TEXT("string"));
		FilterProp->SetStringField(TEXT("description"),
			TEXT("文本过滤关键字，只返回包含此关键字的日志行。"));
		Properties->SetObjectField(TEXT("filter"), FilterProp);

		InputSchema->SetObjectField(TEXT("properties"), Properties);

		return MakeToolSchema(
			TEXT("get_output_log"),
			TEXT("获取输出日志最近 N 条。可按关键字过滤。"),
			InputSchema
		);
	}

	return nullptr;
}

// ====================================================================
// Execute
// ====================================================================

bool UAContextCommands::Execute(
	const FString& MethodName,
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	if (MethodName == TEXT("get_open_editors"))    return ExecuteGetOpenEditors(Params, OutResult, OutError);
	if (MethodName == TEXT("get_selected_assets")) return ExecuteGetSelectedAssets(Params, OutResult, OutError);
	if (MethodName == TEXT("get_browser_path"))    return ExecuteGetBrowserPath(Params, OutResult, OutError);
	if (MethodName == TEXT("get_message_log"))     return ExecuteGetMessageLog(Params, OutResult, OutError);
	if (MethodName == TEXT("get_output_log"))      return ExecuteGetOutputLog(Params, OutResult, OutError);

	OutError = FString::Printf(TEXT("Unknown method: %s"), *MethodName);
	return false;
}

// ====================================================================
// get_open_editors — 获取当前打开的资产编辑器
// ====================================================================

bool UAContextCommands::ExecuteGetOpenEditors(
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	if (!GEditor)
	{
		OutError = TEXT("GEditor is not available");
		return false;
	}

	UAssetEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!Subsystem)
	{
		OutError = TEXT("AssetEditorSubsystem is not available");
		return false;
	}

	TArray<UObject*> EditedAssets = Subsystem->GetAllEditedAssets();

	TArray<TSharedPtr<FJsonValue>> EditorList;
	for (UObject* Asset : EditedAssets)
	{
		if (!Asset)
		{
			continue;
		}

		TSharedPtr<FJsonObject> EditorObj = MakeShared<FJsonObject>();
		EditorObj->SetStringField(TEXT("asset_path"), Asset->GetPathName());
		EditorObj->SetStringField(TEXT("asset_name"), Asset->GetName());
		EditorObj->SetStringField(TEXT("asset_class"), Asset->GetClass()->GetName());

		IAssetEditorInstance* EditorInstance = Subsystem->FindEditorForAsset(Asset, false);
		if (EditorInstance)
		{
			EditorObj->SetStringField(TEXT("editor_name"), EditorInstance->GetEditorName().ToString());
		}
		else
		{
			EditorObj->SetStringField(TEXT("editor_name"), TEXT("Unknown"));
		}

		EditorList.Add(MakeShared<FJsonValueObject>(EditorObj));
	}

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetArrayField(TEXT("editors"), EditorList);
	OutResult->SetNumberField(TEXT("count"), EditorList.Num());

	UE_LOG(LogUAContext, Log, TEXT("get_open_editors: found %d open editors"), EditorList.Num());
	return true;
}

// ====================================================================
// get_selected_assets — Content Browser 选中项
// ====================================================================

bool UAContextCommands::ExecuteGetSelectedAssets(
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	TArray<FAssetData> SelectedAssets = UEditorUtilityLibrary::GetSelectedAssetData();

	TArray<TSharedPtr<FJsonValue>> AssetList;
	for (const FAssetData& AssetData : SelectedAssets)
	{
		TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
		AssetObj->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
		AssetObj->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
		AssetObj->SetStringField(TEXT("class"), AssetData.AssetClassPath.GetAssetName().ToString());

		AssetList.Add(MakeShared<FJsonValueObject>(AssetObj));
	}

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetArrayField(TEXT("assets"), AssetList);
	OutResult->SetNumberField(TEXT("count"), AssetList.Num());

	UE_LOG(LogUAContext, Log, TEXT("get_selected_assets: found %d selected assets"), AssetList.Num());
	return true;
}

// ====================================================================
// get_browser_path — Content Browser 当前路径
// ====================================================================

bool UAContextCommands::ExecuteGetBrowserPath(
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	IContentBrowserSingleton& ContentBrowser = ContentBrowserModule.Get();

	TArray<FString> SelectedPaths;
	ContentBrowser.GetSelectedPathViewFolders(SelectedPaths);

	OutResult = MakeShared<FJsonObject>();

	if (SelectedPaths.Num() > 0)
	{
		OutResult->SetStringField(TEXT("current_path"), SelectedPaths[0]);
	}
	else
	{
		OutResult->SetStringField(TEXT("current_path"), TEXT("/Game"));
	}

	TArray<TSharedPtr<FJsonValue>> PathsArray;
	for (const FString& Path : SelectedPaths)
	{
		PathsArray.Add(MakeShared<FJsonValueString>(Path));
	}
	OutResult->SetArrayField(TEXT("paths"), PathsArray);

	UE_LOG(LogUAContext, Log, TEXT("get_browser_path: %d paths"), SelectedPaths.Num());
	return true;
}

// ====================================================================
// get_message_log — 从 UALogCapture 获取日志
// ====================================================================

bool UAContextCommands::ExecuteGetMessageLog(
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	if (!UALogCapture::Get().IsInitialized())
	{
		OutError = TEXT("UALogCapture is not initialized");
		return false;
	}

	int32 Count = 50;
	if (Params->HasField(TEXT("count")))
	{
		Count = static_cast<int32>(Params->GetNumberField(TEXT("count")));
		Count = FMath::Clamp(Count, 1, 200);
	}

	FString CategoryFilter;
	if (Params->HasField(TEXT("category")))
	{
		CategoryFilter = Params->GetStringField(TEXT("category"));
	}

	FString SeverityFilter;
	if (Params->HasField(TEXT("severity")))
	{
		SeverityFilter = Params->GetStringField(TEXT("severity"));
	}

	TArray<FUALogEntry> Entries = UALogCapture::Get().GetRecent(Count, CategoryFilter);

	// 额外按严重级别过滤
	TArray<TSharedPtr<FJsonValue>> MessageList;
	for (const FUALogEntry& Entry : Entries)
	{
		if (!SeverityFilter.IsEmpty() && !Entry.Severity.Equals(SeverityFilter, ESearchCase::IgnoreCase))
		{
			continue;
		}

		TSharedPtr<FJsonObject> MsgObj = MakeShared<FJsonObject>();
		MsgObj->SetStringField(TEXT("text"), Entry.Text);
		MsgObj->SetStringField(TEXT("category"), Entry.Category);
		MsgObj->SetStringField(TEXT("severity"), Entry.Severity);
		MsgObj->SetStringField(TEXT("timestamp"), Entry.Timestamp.ToString());

		MessageList.Add(MakeShared<FJsonValueObject>(MsgObj));
	}

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetArrayField(TEXT("messages"), MessageList);
	OutResult->SetNumberField(TEXT("count"), MessageList.Num());

	return true;
}

// ====================================================================
// get_output_log — 输出日志最近 N 条（可按关键字过滤）
// ====================================================================

bool UAContextCommands::ExecuteGetOutputLog(
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	if (!UALogCapture::Get().IsInitialized())
	{
		OutError = TEXT("UALogCapture is not initialized");
		return false;
	}

	int32 Count = 50;
	if (Params->HasField(TEXT("count")))
	{
		Count = static_cast<int32>(Params->GetNumberField(TEXT("count")));
		Count = FMath::Clamp(Count, 1, 200);
	}

	FString TextFilter;
	if (Params->HasField(TEXT("filter")))
	{
		TextFilter = Params->GetStringField(TEXT("filter"));
	}

	// 获取较多条目，然后在文本层面过滤
	TArray<FUALogEntry> Entries = UALogCapture::Get().GetRecent(
		TextFilter.IsEmpty() ? Count : Count * 3  // 预取更多用于过滤
	);

	TArray<TSharedPtr<FJsonValue>> LineList;
	for (const FUALogEntry& Entry : Entries)
	{
		if (LineList.Num() >= Count)
		{
			break;
		}

		// 文本过滤
		if (!TextFilter.IsEmpty() && !Entry.Text.Contains(TextFilter))
		{
			continue;
		}

		TSharedPtr<FJsonObject> LineObj = MakeShared<FJsonObject>();
		LineObj->SetStringField(TEXT("text"), Entry.Text);
		LineObj->SetStringField(TEXT("category"), Entry.Category);
		LineObj->SetStringField(TEXT("verbosity"), Entry.Severity);

		LineList.Add(MakeShared<FJsonValueObject>(LineObj));
	}

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetArrayField(TEXT("lines"), LineList);
	OutResult->SetNumberField(TEXT("count"), LineList.Num());

	return true;
}
