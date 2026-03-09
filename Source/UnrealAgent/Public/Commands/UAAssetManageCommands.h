// Copyright KuoYu. All Rights Reserved.

#pragma once

#include "Commands/UACommandBase.h"

/**
 * 资产管理操作命令 — 补全资产的"增删改"操作。
 *
 * 支持的方法:
 *   - create_asset:    创建新资产（Material, Blueprint, MaterialInstance, DataTable 等）
 *   - duplicate_asset: 复制资产
 *   - rename_asset:    重命名/移动资产
 *   - delete_asset:    删除资产（带引用安全检查）
 *   - save_asset:      保存资产或全部保存
 *   - create_folder:   创建文件夹
 */
class UAAssetManageCommands : public UACommandBase
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
	bool ExecuteCreateAsset(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool ExecuteDuplicateAsset(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool ExecuteRenameAsset(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool ExecuteDeleteAsset(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool ExecuteSaveAsset(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool ExecuteCreateFolder(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
};
