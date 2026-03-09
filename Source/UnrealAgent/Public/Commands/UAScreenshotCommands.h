// Copyright KuoYu. All Rights Reserved.

#pragma once

#include "Commands/UACommandBase.h"

/**
 * 截图命令模块 — 视口截图 + 资产缩略图
 *
 * 支持的方法:
 *   - take_screenshot:     截取场景或编辑器窗口截图
 *   - get_asset_thumbnail: 获取资产缩略图
 */
class UAScreenshotCommands : public UACommandBase
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
	bool ExecuteTakeScreenshot(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool ExecuteGetAssetThumbnail(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);

	/** editor 模式：截取编辑器 UI 面板（材质编辑器内容区域、蓝图编辑器、Details 面板等） */
	bool ExecuteEditorScreenshot(const FString& Target, const FString& AssetPath, const FString& InTabId,
		const FString& FilePath, int32& OutWidth, int32& OutHeight, FString& OutError);

	/** 根据 target 参数定位需要截图的 SWidget（面板级精准截图） */
	TSharedPtr<SWidget> FindTargetWidgetForEditorScreenshot(const FString& Target, const FString& AssetPath, const FString& InTabId, FString& OutError);

	/** 根据 quality 字符串获取目标分辨率 */
	static void GetResolutionFromQuality(const FString& Quality, int32& OutWidth, int32& OutHeight);

	/** 保存 color 数组为 PNG 文件 */
	static bool SaveBitmapToPng(const TArray<FColor>& Bitmap, int32 Width, int32 Height, const FString& FilePath);

	/** 获取截图保存目录 */
	static FString GetScreenshotDir();
};
