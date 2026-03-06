// Copyright KuoYu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commands/UACommandBase.h"

/**
 * 材质编辑命令组
 * 提供材质节点的创建、删除、连接、查询以及材质实例参数的管理功能
 */
class UAMaterialCommands : public UACommandBase
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
	// ========== 材质信息查询 ==========
	/** 获取材质的完整节点图信息（节点列表、连接关系、材质属性） */
	bool ExecuteGetMaterialGraph(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);

	// ========== 节点操作 ==========
	/** 创建材质表达式节点 */
	bool ExecuteCreateExpression(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);

	/** 删除材质表达式节点 */
	bool ExecuteDeleteExpression(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);

	// ========== 连接操作 ==========
	/** 连接节点到材质属性输出（BaseColor, Metallic 等） */
	bool ExecuteConnectToProperty(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);

	/** 连接两个节点之间的输入/输出 */
	bool ExecuteConnectExpressions(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);

	// ========== 节点属性操作 ==========
	/** 通过反射设置节点的属性值 */
	bool ExecuteSetExpressionValue(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);

	// ========== 编译与布局 ==========
	/** 重新编译材质 */
	bool ExecuteRecompileMaterial(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);

	/** 自动排列材质节点布局 */
	bool ExecuteLayoutExpressions(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);

	// ========== 材质参数查询 ==========
	/** 获取材质参数列表（Scalar/Vector/Texture/StaticSwitch） */
	bool ExecuteGetMaterialParameters(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);

	// ========== 材质实例操作 ==========
	/** 设置材质实例参数值 */
	bool ExecuteSetInstanceParam(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);

	// ========== 材质全局属性 ==========
	/** 设置材质全局属性（BlendMode, ShadingModel, TwoSided 等） */
	bool ExecuteSetMaterialProperty(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);

	// ========== 辅助方法 ==========
	/** 根据资产路径加载 UMaterial 对象 */
	UMaterial* LoadMaterialFromPath(const FString& AssetPath, FString& OutError);

	/** 根据资产路径加载 UMaterialInstanceConstant 对象 */
	class UMaterialInstanceConstant* LoadMaterialInstanceFromPath(const FString& AssetPath, FString& OutError);

	/** 将 EMaterialProperty 字符串转为枚举值 */
	bool ParseMaterialProperty(const FString& PropertyName, EMaterialProperty& OutProperty);

	/** 根据索引在材质中查找表达式节点 */
	UMaterialExpression* FindExpressionByIndex(UMaterial* Material, int32 Index, FString& OutError);

	/** 构建单个表达式节点的 JSON 描述 */
	TSharedPtr<FJsonObject> ExpressionToJson(UMaterialExpression* Expression, int32 Index);
};
