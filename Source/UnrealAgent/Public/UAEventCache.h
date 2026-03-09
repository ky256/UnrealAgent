// Copyright KuoYu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"

/**
 * 事件类型枚举
 */
enum class EUAEventType : uint8
{
	SelectionChanged,      // Actor 选择变更
	AssetEditorOpened,     // 打开了资产编辑器
	AssetEditorClosed,     // 关闭了资产编辑器
	PIEStarted,            // PIE 开始
	PIEStopped,            // PIE 结束
	AssetSaved,            // 资产/包保存
	LevelChanged,          // 关卡/地图变更
	UndoRedo,              // 撤销/重做
};

/**
 * 事件条目
 */
struct FUAEvent
{
	EUAEventType Type;
	FDateTime Timestamp;
	TSharedPtr<FJsonObject> Data;
};

/**
 * UAEventCache — 编辑器事件缓存系统。
 *
 * 在插件 StartupModule 时调用 Initialize() 注册 Delegate，
 * ShutdownModule 时调用 Shutdown() 反注册。
 * 将关键编辑器事件缓存到环形队列，供 Agent 按需查询。
 */
class UAEventCache
{
public:
	static UAEventCache& Get();

	/** 注册所有 Delegate，开始监听事件 */
	void Initialize();

	/** 反注册所有 Delegate */
	void Shutdown();

	/** 获取最近 N 条事件，可选类型过滤 */
	TArray<FUAEvent> GetRecentEvents(int32 Count = 20, const FString& TypeFilter = TEXT("")) const;

	/** 获取指定时间之后的所有事件 */
	TArray<FUAEvent> GetEventsSince(const FDateTime& Since, const FString& TypeFilter = TEXT("")) const;

	/** 清空缓冲区 */
	void Clear();

	/** 是否已初始化 */
	bool IsInitialized() const { return bInitialized; }

	/** 事件类型枚举转字符串 */
	static FString EventTypeToString(EUAEventType Type);

	/** 字符串转事件类型枚举，失败返回 false */
	static bool StringToEventType(const FString& Str, EUAEventType& OutType);

private:
	UAEventCache() = default;
	~UAEventCache();

	/** 向缓冲区添加一条事件 */
	void PushEvent(EUAEventType Type, TSharedPtr<FJsonObject> Data = nullptr);

	// ── Delegate 回调 ──
	void OnSelectionChanged(UObject* InObject);
	void OnAssetOpenedInEditor(UObject* Asset, class IAssetEditorInstance* EditorInstance);
	void OnAssetClosedInEditor(UObject* Asset, class IAssetEditorInstance* EditorInstance);
	void OnPIEStarted(const bool bIsSimulating);
	void OnPIEEnded(const bool bIsSimulating);
	void OnPackageSaved(const FString& PackageFileName, UPackage* Package, FObjectPostSaveContext Context);
	void OnMapChanged(uint32 MapChangeFlags);
	void OnPostUndoRedo();

	mutable FCriticalSection Lock;
	TArray<FUAEvent> EventBuffer;
	int32 MaxBufferSize = 200;
	bool bInitialized = false;

	// Delegate 句柄
	FDelegateHandle SelectionChangedHandle;
	FDelegateHandle AssetOpenedHandle;
	FDelegateHandle AssetClosedHandle;
	FDelegateHandle PIEStartedHandle;
	FDelegateHandle PIEEndedHandle;
	FDelegateHandle PackageSavedHandle;
	FDelegateHandle MapChangedHandle;
	FDelegateHandle UndoRedoHandle;
};
