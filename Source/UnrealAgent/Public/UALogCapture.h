// Copyright KuoYu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/OutputDevice.h"
#include "HAL/CriticalSection.h"

/**
 * 日志条目结构体
 */
struct FUALogEntry
{
	FString Text;
	FString Category;
	FString Severity;
	FDateTime Timestamp;
};

/**
 * 日志截获系统 — 截获 UE 输出日志到环形缓冲区。
 * 在插件 StartupModule 时注册，ShutdownModule 时反注册。
 * 
 * 使用方式:
 *   UALogCapture::Get().Initialize();    // 开始截获
 *   UALogCapture::Get().Shutdown();      // 停止截获
 *   UALogCapture::Get().GetRecent(50);   // 获取最近50条
 */
class UALogCapture : public FOutputDevice
{
public:
	static UALogCapture& Get();

	/** 注册为全局输出设备，开始截获日志 */
	void Initialize();

	/** 从全局输出设备列表中移除 */
	void Shutdown();

	/** 获取最近 N 条日志，可选按类别过滤 */
	TArray<FUALogEntry> GetRecent(int32 Count = 50, const FString& CategoryFilter = TEXT("")) const;

	/** 获取指定时间之后的日志 */
	TArray<FUALogEntry> GetSince(const FDateTime& Since, const FString& CategoryFilter = TEXT("")) const;

	/** 清空缓冲区 */
	void Clear();

	/** 是否已初始化 */
	bool IsInitialized() const { return bInitialized; }

protected:
	// FOutputDevice 接口
	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override;

private:
	UALogCapture() = default;
	~UALogCapture();

	/** 将 ELogVerbosity 转换为可读字符串 */
	static FString VerbosityToString(ELogVerbosity::Type Verbosity);

	mutable FCriticalSection Lock;
	TArray<FUALogEntry> Buffer;
	int32 MaxBufferSize = 500;
	bool bInitialized = false;
};
