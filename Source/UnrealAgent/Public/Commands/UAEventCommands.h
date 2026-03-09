// Copyright KuoYu. All Rights Reserved.

#pragma once

#include "Commands/UACommandBase.h"

/**
 * 事件查询命令模块 — 查询 UAEventCache 中缓存的编辑器事件
 *
 * 支持的方法:
 *   - get_recent_events: 获取最近 N 条事件
 *   - get_events_since:  获取指定时间后的事件
 */
class UAEventCommands : public UACommandBase
{
public:
	virtual TArray<FString> GetSupportedMethods() const override;
	virtual TSharedPtr<FJsonObject> GetToolSchema(const FString& MethodName) const override;
	virtual bool Execute(
		const FString& MethodName,
		const TSharedPtr<FJsonObject>& Params,
		TSharedPtr<FJsonObject>& OutResult,
		FString& OutError
	) override;

private:
	bool ExecuteGetRecentEvents(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool ExecuteGetEventsSince(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);

	/** 将事件数组转为 JSON 数组 */
	static TArray<TSharedPtr<FJsonValue>> EventsToJsonArray(const TArray<struct FUAEvent>& Events);
};
