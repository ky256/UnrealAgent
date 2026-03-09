// Copyright KuoYu. All Rights Reserved.

#pragma once

#include "Commands/UACommandBase.h"

/**
 * 蓝图编辑命令组 — 镜像 UAMaterialCommands 的设计模式。
 * 提供蓝图节点图的查询、节点创建/删除、引脚连接/断开、变量/函数管理、编译等功能。
 *
 * 支持的方法:
 *   查询:
 *     - get_blueprint_overview:    蓝图概览（图列表、变量、事件等）
 *     - get_blueprint_graph:       节点图详情（节点+连接）
 *     - get_blueprint_variables:   变量定义列表
 *     - get_blueprint_functions:   函数签名列表
 *   操作:
 *     - add_node:             添加蓝图节点
 *     - delete_node:          删除蓝图节点
 *     - connect_pins:         连接蓝图引脚
 *     - disconnect_pin:       断开蓝图引脚
 *     - add_variable:         添加变量
 *     - add_function:         添加自定义函数
 *     - compile_blueprint:    编译蓝图 + 返回错误
 */
class UABlueprintCommands : public UACommandBase
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
	// ========== 蓝图查询 ==========
	bool ExecuteGetBlueprintOverview(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool ExecuteGetBlueprintGraph(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool ExecuteGetBlueprintVariables(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool ExecuteGetBlueprintFunctions(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);

	// ========== 节点操作 ==========
	bool ExecuteAddNode(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool ExecuteDeleteNode(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);

	// ========== 连接操作 ==========
	bool ExecuteConnectPins(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool ExecuteDisconnectPin(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);

	// ========== 变量/函数操作 ==========
	bool ExecuteAddVariable(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool ExecuteAddFunction(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);

	// ========== 编译 ==========
	bool ExecuteCompileBlueprint(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);

	// ========== 辅助方法 ==========
	/** 根据资产路径加载 UBlueprint */
	class UBlueprint* LoadBlueprintFromPath(const FString& AssetPath, FString& OutError);

	/** 在蓝图中查找指定名称的图 */
	class UEdGraph* FindGraphByName(UBlueprint* Blueprint, const FString& GraphName, FString& OutError);

	/** 根据索引在图中查找节点 */
	class UEdGraphNode* FindNodeByIndex(UEdGraph* Graph, int32 Index, FString& OutError);

	/** 根据名称在节点上查找引脚 */
	class UEdGraphPin* FindPinByName(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection Direction, FString& OutError);

	/** 将单个节点序列化为 JSON */
	TSharedPtr<FJsonObject> NodeToJson(UEdGraphNode* Node, int32 Index, const TMap<UEdGraphNode*, int32>& NodeIndexMap);

	/** 将引脚序列化为 JSON */
	TSharedPtr<FJsonObject> PinToJson(UEdGraphPin* Pin, const TMap<UEdGraphNode*, int32>& NodeIndexMap);

	/** 将 FEdGraphPinType 转为可读的类型字符串 */
	static FString PinTypeToString(const struct FEdGraphPinType& PinType);

	/** 将类型字符串解析为 FEdGraphPinType */
	static bool StringToPinType(const FString& TypeString, struct FEdGraphPinType& OutPinType);
};
