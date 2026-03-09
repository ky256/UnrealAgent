// Copyright KuoYu. All Rights Reserved.

#pragma once

#include "Commands/UACommandBase.h"

/**
 * 通用属性读写命令 — 基于 UE 反射系统，实现任意 UObject 属性的读写。
 *
 * 支持的方法:
 *   - get_property:    通过属性路径读取 Actor/Component 的任意属性值
 *   - set_property:    通过属性路径设置 Actor/Component 的任意属性值
 *   - list_properties: 列出 Actor 或组件的所有可编辑属性
 */
class UAPropertyCommands : public UACommandBase
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
	bool ExecuteGetProperty(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool ExecuteSetProperty(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool ExecuteListProperties(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);

	/** 根据名称查找场景中的 Actor */
	AActor* FindActorByName(const FString& ActorName) const;

	/** 根据名称查找 Actor 上的组件 */
	UActorComponent* FindComponentByName(AActor* Actor, const FString& ComponentName) const;

	/**
	 * 解析属性路径，获取目标对象和属性。
	 * 路径格式: "ComponentName.PropertyName" 或 "ComponentName.StructProp.Field"
	 * 如果只有一段，则直接在 Actor 上查找属性。
	 */
	bool ResolvePropertyPath(
		AActor* Actor,
		const FString& PropertyPath,
		UObject*& OutObject,
		FProperty*& OutProperty,
		void*& OutValuePtr,
		FString& OutError
	) const;

	/** 将 FProperty 值序列化为 JSON Value */
	TSharedPtr<FJsonValue> PropertyToJsonValue(FProperty* Property, const void* ValuePtr) const;

	/** 从 JSON Value 反序列化到 FProperty */
	bool JsonValueToProperty(FProperty* Property, void* ValuePtr, const TSharedPtr<FJsonValue>& JsonValue, FString& OutError) const;
};
