// Copyright KuoYu. All Rights Reserved.

#include "Commands/UAEventCommands.h"
#include "UAEventCache.h"
#include "UnrealAgent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/DateTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogUAEventCommands, Log, All);

namespace UAEventHelper
{
	static TSharedPtr<FJsonObject> MakeProp(const FString& Type, const FString& Desc)
	{
		auto P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("type"), Type);
		P->SetStringField(TEXT("description"), Desc);
		return P;
	}

	static TSharedPtr<FJsonObject> MakeInputSchema(
		TArray<TPair<FString, TSharedPtr<FJsonObject>>> Props,
		TArray<FString> RequiredFields = {})
	{
		auto Schema = MakeShared<FJsonObject>();
		Schema->SetStringField(TEXT("type"), TEXT("object"));
		auto Properties = MakeShared<FJsonObject>();
		for (auto& P : Props) { Properties->SetObjectField(P.Key, P.Value); }
		Schema->SetObjectField(TEXT("properties"), Properties);
		if (RequiredFields.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> Req;
			for (auto& R : RequiredFields) { Req.Add(MakeShared<FJsonValueString>(R)); }
			Schema->SetArrayField(TEXT("required"), Req);
		}
		return Schema;
	}
}

// ==================== GetSupportedMethods ====================

TArray<FString> UAEventCommands::GetSupportedMethods() const
{
	return {
		TEXT("get_recent_events"),
		TEXT("get_events_since"),
	};
}

// ==================== GetToolSchema ====================

TSharedPtr<FJsonObject> UAEventCommands::GetToolSchema(const FString& MethodName) const
{
	using namespace UAEventHelper;

	if (MethodName == TEXT("get_recent_events"))
	{
		return MakeToolSchema(TEXT("get_recent_events"),
			TEXT("获取最近的编辑器事件。事件类型: SelectionChanged, AssetEditorOpened, AssetEditorClosed, PIEStarted, PIEStopped, AssetSaved, LevelChanged, UndoRedo"),
			MakeInputSchema({
				{TEXT("count"), MakeProp(TEXT("integer"), TEXT("返回事件数量，默认 20，最大 200"))},
				{TEXT("type_filter"), MakeProp(TEXT("string"), TEXT("按事件类型过滤，可选"))},
			}, {}));
	}
	if (MethodName == TEXT("get_events_since"))
	{
		return MakeToolSchema(TEXT("get_events_since"),
			TEXT("获取指定时间之后的编辑器事件"),
			MakeInputSchema({
				{TEXT("since"), MakeProp(TEXT("string"), TEXT("ISO 8601 时间字符串 (如 2026-03-09T10:00:00)"))},
				{TEXT("type_filter"), MakeProp(TEXT("string"), TEXT("按事件类型过滤，可选"))},
			}, {TEXT("since")}));
	}
	return nullptr;
}

// ==================== Execute dispatcher ====================

bool UAEventCommands::Execute(
	const FString& MethodName,
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	if (MethodName == TEXT("get_recent_events")) return ExecuteGetRecentEvents(Params, OutResult, OutError);
	if (MethodName == TEXT("get_events_since"))  return ExecuteGetEventsSince(Params, OutResult, OutError);

	OutError = FString::Printf(TEXT("Unknown method: %s"), *MethodName);
	return false;
}

// ==================== EventsToJsonArray ====================

TArray<TSharedPtr<FJsonValue>> UAEventCommands::EventsToJsonArray(const TArray<FUAEvent>& Events)
{
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FUAEvent& Event : Events)
	{
		auto Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("type"), UAEventCache::EventTypeToString(Event.Type));
		Obj->SetStringField(TEXT("timestamp"), Event.Timestamp.ToIso8601());
		if (Event.Data.IsValid())
		{
			Obj->SetObjectField(TEXT("data"), Event.Data);
		}
		Arr.Add(MakeShared<FJsonValueObject>(Obj));
	}
	return Arr;
}

// ==================== get_recent_events ====================

bool UAEventCommands::ExecuteGetRecentEvents(
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	int32 Count = 20;
	Params->TryGetNumberField(TEXT("count"), Count);
	Count = FMath::Clamp(Count, 1, 200);

	FString TypeFilter;
	Params->TryGetStringField(TEXT("type_filter"), TypeFilter);

	// 校验 type_filter 有效性
	if (!TypeFilter.IsEmpty())
	{
		EUAEventType TempType;
		if (!UAEventCache::StringToEventType(TypeFilter, TempType))
		{
			OutError = FString::Printf(TEXT("Unknown event type: '%s'. Valid types: SelectionChanged, AssetEditorOpened, AssetEditorClosed, PIEStarted, PIEStopped, AssetSaved, LevelChanged, UndoRedo"), *TypeFilter);
			return false;
		}
	}

	TArray<FUAEvent> Events = UAEventCache::Get().GetRecentEvents(Count, TypeFilter);

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetArrayField(TEXT("events"), EventsToJsonArray(Events));
	OutResult->SetNumberField(TEXT("count"), Events.Num());
	return true;
}

// ==================== get_events_since ====================

bool UAEventCommands::ExecuteGetEventsSince(
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString SinceStr;
	if (!Params->TryGetStringField(TEXT("since"), SinceStr))
	{
		OutError = TEXT("'since' is required (ISO 8601 format)");
		return false;
	}

	FDateTime Since;
	if (!FDateTime::ParseIso8601(*SinceStr, Since))
	{
		OutError = FString::Printf(TEXT("Invalid ISO 8601 date: %s"), *SinceStr);
		return false;
	}

	FString TypeFilter;
	Params->TryGetStringField(TEXT("type_filter"), TypeFilter);

	// 校验 type_filter 有效性
	if (!TypeFilter.IsEmpty())
	{
		EUAEventType TempType;
		if (!UAEventCache::StringToEventType(TypeFilter, TempType))
		{
			OutError = FString::Printf(TEXT("Unknown event type: '%s'. Valid types: SelectionChanged, AssetEditorOpened, AssetEditorClosed, PIEStarted, PIEStopped, AssetSaved, LevelChanged, UndoRedo"), *TypeFilter);
			return false;
		}
	}

	TArray<FUAEvent> Events = UAEventCache::Get().GetEventsSince(Since, TypeFilter);

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetArrayField(TEXT("events"), EventsToJsonArray(Events));
	OutResult->SetNumberField(TEXT("count"), Events.Num());
	return true;
}
