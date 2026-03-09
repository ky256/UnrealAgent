// Copyright KuoYu. All Rights Reserved.

#include "UAEventCache.h"
#include "UnrealAgent.h"
#include "Editor.h"
#include "Selection.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/Package.h"
#include "UObject/ObjectSaveContext.h"

DEFINE_LOG_CATEGORY_STATIC(LogUAEventCache, Log, All);

UAEventCache& UAEventCache::Get()
{
	static UAEventCache Instance;
	return Instance;
}

UAEventCache::~UAEventCache()
{
	if (bInitialized)
	{
		Shutdown();
	}
}

// ==================== Initialize / Shutdown ====================

void UAEventCache::Initialize()
{
	if (bInitialized) return;

	UE_LOG(LogUAEventCache, Log, TEXT("UAEventCache initializing..."));

	// 1. Selection 变更
	SelectionChangedHandle = USelection::SelectionChangedEvent.AddRaw(
		this, &UAEventCache::OnSelectionChanged);

	// 2. 资产编辑器打开/关闭
	if (GEditor)
	{
		UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (AssetEditorSubsystem)
		{
			AssetOpenedHandle = AssetEditorSubsystem->OnAssetOpenedInEditor().AddRaw(
				this, &UAEventCache::OnAssetOpenedInEditor);
			AssetClosedHandle = AssetEditorSubsystem->OnAssetClosedInEditor().AddRaw(
				this, &UAEventCache::OnAssetClosedInEditor);
		}
	}

	// 3. PIE 开始/结束
	PIEStartedHandle = FEditorDelegates::PostPIEStarted.AddRaw(
		this, &UAEventCache::OnPIEStarted);
	PIEEndedHandle = FEditorDelegates::EndPIE.AddRaw(
		this, &UAEventCache::OnPIEEnded);

	// 4. 包保存事件
	PackageSavedHandle = UPackage::PackageSavedWithContextEvent.AddRaw(
		this, &UAEventCache::OnPackageSaved);

	// 5. 地图变更
	MapChangedHandle = FEditorDelegates::MapChange.AddRaw(
		this, &UAEventCache::OnMapChanged);

	// 6. Undo/Redo
	UndoRedoHandle = FEditorDelegates::PostUndoRedo.AddRaw(
		this, &UAEventCache::OnPostUndoRedo);

	bInitialized = true;
	UE_LOG(LogUAEventCache, Log, TEXT("UAEventCache initialized — 7 delegates registered."));
}

void UAEventCache::Shutdown()
{
	if (!bInitialized) return;

	UE_LOG(LogUAEventCache, Log, TEXT("UAEventCache shutting down..."));

	USelection::SelectionChangedEvent.Remove(SelectionChangedHandle);

	if (GEditor)
	{
		UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (AssetEditorSubsystem)
		{
			AssetEditorSubsystem->OnAssetOpenedInEditor().Remove(AssetOpenedHandle);
			AssetEditorSubsystem->OnAssetClosedInEditor().Remove(AssetClosedHandle);
		}
	}

	FEditorDelegates::PostPIEStarted.Remove(PIEStartedHandle);
	FEditorDelegates::EndPIE.Remove(PIEEndedHandle);
	UPackage::PackageSavedWithContextEvent.Remove(PackageSavedHandle);
	FEditorDelegates::MapChange.Remove(MapChangedHandle);
	FEditorDelegates::PostUndoRedo.Remove(UndoRedoHandle);

	bInitialized = false;
	UE_LOG(LogUAEventCache, Log, TEXT("UAEventCache shutdown complete."));
}

// ==================== 查询接口 ====================

TArray<FUAEvent> UAEventCache::GetRecentEvents(int32 Count, const FString& TypeFilter) const
{
	FScopeLock ScopeLock(&Lock);

	EUAEventType FilterType = EUAEventType::SelectionChanged;
	bool bHasFilter = !TypeFilter.IsEmpty() && StringToEventType(TypeFilter, FilterType);

	TArray<FUAEvent> Result;

	// 从尾部向前取，返回最新的
	for (int32 i = EventBuffer.Num() - 1; i >= 0 && Result.Num() < Count; --i)
	{
		if (bHasFilter && EventBuffer[i].Type != FilterType) continue;
		Result.Add(EventBuffer[i]);
	}

	return Result;
}

TArray<FUAEvent> UAEventCache::GetEventsSince(const FDateTime& Since, const FString& TypeFilter) const
{
	FScopeLock ScopeLock(&Lock);

	EUAEventType FilterType = EUAEventType::SelectionChanged;
	bool bHasFilter = !TypeFilter.IsEmpty() && StringToEventType(TypeFilter, FilterType);

	TArray<FUAEvent> Result;

	for (int32 i = EventBuffer.Num() - 1; i >= 0; --i)
	{
		if (EventBuffer[i].Timestamp < Since) break; // 时间有序，可以提前退出
		if (bHasFilter && EventBuffer[i].Type != FilterType) continue;
		Result.Add(EventBuffer[i]);
	}

	// 反转，使结果按时间正序
	Algo::Reverse(Result);
	return Result;
}

void UAEventCache::Clear()
{
	FScopeLock ScopeLock(&Lock);
	EventBuffer.Empty();
}

// ==================== 内部：推送事件 ====================

void UAEventCache::PushEvent(EUAEventType Type, TSharedPtr<FJsonObject> Data)
{
	FScopeLock ScopeLock(&Lock);

	FUAEvent Event;
	Event.Type = Type;
	Event.Timestamp = FDateTime::Now();
	Event.Data = Data ? Data : MakeShared<FJsonObject>();

	if (EventBuffer.Num() >= MaxBufferSize)
	{
		EventBuffer.RemoveAt(0);
	}
	EventBuffer.Add(MoveTemp(Event));
}

// ==================== Delegate 回调 ====================

void UAEventCache::OnSelectionChanged(UObject* InObject)
{
	auto Data = MakeShared<FJsonObject>();

	if (GEditor)
	{
		USelection* Selection = GEditor->GetSelectedActors();
		if (Selection)
		{
			TArray<TSharedPtr<FJsonValue>> ActorArr;
			for (int32 i = 0; i < Selection->Num(); ++i)
			{
				UObject* Obj = Selection->GetSelectedObject(i);
				if (Obj)
				{
					ActorArr.Add(MakeShared<FJsonValueString>(Obj->GetName()));
				}
			}
			Data->SetArrayField(TEXT("selected_actors"), ActorArr);
			Data->SetNumberField(TEXT("count"), ActorArr.Num());
		}
	}

	PushEvent(EUAEventType::SelectionChanged, Data);
}

void UAEventCache::OnAssetOpenedInEditor(UObject* Asset, IAssetEditorInstance* EditorInstance)
{
	if (!Asset) return;

	auto Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Data->SetStringField(TEXT("asset_name"), Asset->GetName());
	Data->SetStringField(TEXT("asset_class"), Asset->GetClass()->GetName());
	if (EditorInstance)
	{
		Data->SetStringField(TEXT("editor_name"), EditorInstance->GetEditorName().ToString());
	}

	PushEvent(EUAEventType::AssetEditorOpened, Data);
	UE_LOG(LogUAEventCache, Verbose, TEXT("Event: AssetEditorOpened — %s"), *Asset->GetName());
}

void UAEventCache::OnAssetClosedInEditor(UObject* Asset, IAssetEditorInstance* EditorInstance)
{
	if (!Asset) return;

	auto Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Data->SetStringField(TEXT("asset_name"), Asset->GetName());

	PushEvent(EUAEventType::AssetEditorClosed, Data);
	UE_LOG(LogUAEventCache, Verbose, TEXT("Event: AssetEditorClosed — %s"), *Asset->GetName());
}

void UAEventCache::OnPIEStarted(const bool bIsSimulating)
{
	auto Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("is_simulating"), bIsSimulating);

	PushEvent(EUAEventType::PIEStarted, Data);
	UE_LOG(LogUAEventCache, Log, TEXT("Event: PIEStarted (simulating=%d)"), bIsSimulating);
}

void UAEventCache::OnPIEEnded(const bool bIsSimulating)
{
	auto Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("is_simulating"), bIsSimulating);

	PushEvent(EUAEventType::PIEStopped, Data);
	UE_LOG(LogUAEventCache, Log, TEXT("Event: PIEStopped (simulating=%d)"), bIsSimulating);
}

void UAEventCache::OnPackageSaved(const FString& PackageFileName, UPackage* Package, FObjectPostSaveContext Context)
{
	auto Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("package_name"), Package ? Package->GetName() : TEXT("Unknown"));
	Data->SetStringField(TEXT("file_name"), PackageFileName);

	PushEvent(EUAEventType::AssetSaved, Data);
	UE_LOG(LogUAEventCache, Verbose, TEXT("Event: AssetSaved — %s"),
		Package ? *Package->GetName() : TEXT("Unknown"));
}

void UAEventCache::OnMapChanged(uint32 MapChangeFlags)
{
	auto Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("flags"), MapChangeFlags);

	// 解释 flags
	FString FlagStr;
	if (MapChangeFlags & MapChangeEventFlags::NewMap) FlagStr += TEXT("NewMap ");
	if (MapChangeFlags & MapChangeEventFlags::MapRebuild) FlagStr += TEXT("MapRebuild ");
	Data->SetStringField(TEXT("flags_str"), FlagStr.TrimEnd());

	PushEvent(EUAEventType::LevelChanged, Data);
	UE_LOG(LogUAEventCache, Verbose, TEXT("Event: LevelChanged — flags=%d"), MapChangeFlags);
}

void UAEventCache::OnPostUndoRedo()
{
	PushEvent(EUAEventType::UndoRedo);
}

// ==================== 类型转换工具 ====================

FString UAEventCache::EventTypeToString(EUAEventType Type)
{
	switch (Type)
	{
	case EUAEventType::SelectionChanged:  return TEXT("SelectionChanged");
	case EUAEventType::AssetEditorOpened: return TEXT("AssetEditorOpened");
	case EUAEventType::AssetEditorClosed: return TEXT("AssetEditorClosed");
	case EUAEventType::PIEStarted:        return TEXT("PIEStarted");
	case EUAEventType::PIEStopped:        return TEXT("PIEStopped");
	case EUAEventType::AssetSaved:        return TEXT("AssetSaved");
	case EUAEventType::LevelChanged:      return TEXT("LevelChanged");
	case EUAEventType::UndoRedo:          return TEXT("UndoRedo");
	default: return TEXT("Unknown");
	}
}

bool UAEventCache::StringToEventType(const FString& Str, EUAEventType& OutType)
{
	if (Str == TEXT("SelectionChanged"))  { OutType = EUAEventType::SelectionChanged; return true; }
	if (Str == TEXT("AssetEditorOpened")) { OutType = EUAEventType::AssetEditorOpened; return true; }
	if (Str == TEXT("AssetEditorClosed")) { OutType = EUAEventType::AssetEditorClosed; return true; }
	if (Str == TEXT("PIEStarted"))        { OutType = EUAEventType::PIEStarted; return true; }
	if (Str == TEXT("PIEStopped"))        { OutType = EUAEventType::PIEStopped; return true; }
	if (Str == TEXT("AssetSaved"))        { OutType = EUAEventType::AssetSaved; return true; }
	if (Str == TEXT("LevelChanged"))      { OutType = EUAEventType::LevelChanged; return true; }
	if (Str == TEXT("UndoRedo"))          { OutType = EUAEventType::UndoRedo; return true; }
	return false;
}
