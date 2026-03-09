// Copyright KuoYu. All Rights Reserved.

#include "Commands/UAScreenshotCommands.h"
#include "UnrealAgent.h"
#include "EngineUtils.h"
#include "LevelEditorViewport.h"
#include "SLevelViewport.h"
#include "LevelEditor.h"
#include "ImageUtils.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ObjectThumbnail.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"

DEFINE_LOG_CATEGORY_STATIC(LogUAScreenshot, Log, All);

namespace UAScreenshotHelper
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

TArray<FString> UAScreenshotCommands::GetSupportedMethods() const
{
	return {
		TEXT("take_screenshot"),
		TEXT("get_asset_thumbnail"),
	};
}

// ==================== GetToolSchema ====================

TSharedPtr<FJsonObject> UAScreenshotCommands::GetToolSchema(const FString& MethodName) const
{
	using namespace UAScreenshotHelper;

	if (MethodName == TEXT("take_screenshot"))
	{
		return MakeToolSchema(TEXT("take_screenshot"),
TEXT("截取编辑器截图。mode=scene 获取视口最终渲染画面（不含gizmo，推荐）; mode=viewport 截取编辑器视口（含gizmo）; mode=editor 截取编辑器UI截图（面板级精准截图）。"),
			MakeInputSchema({
{TEXT("mode"), MakeProp(TEXT("string"), TEXT("截图模式: scene（视口最终渲染画面）、viewport（编辑器视口，含gizmo）、editor（编辑器UI面板截图）。默认 scene"))},
				{TEXT("target"), MakeProp(TEXT("string"), TEXT("editor模式专用，指定截图目标: active_window（当前活跃窗口）、asset_editor（资产编辑器内容区域，如材质节点图/蓝图图）、tab（按TabID截取指定面板，如Details/Outliner/ContentBrowser）、full（UE编辑器主窗口）。默认 active_window"))},
				{TEXT("asset_path"), MakeProp(TEXT("string"), TEXT("editor模式target=asset_editor时使用，指定要截图的资产路径，如 /Game/Materials/M_Base。不传则截取最近活跃的资产编辑器"))},
				{TEXT("tab_id"), MakeProp(TEXT("string"), TEXT("editor模式target=tab时使用，面板TabID。常用值: LevelEditorSelectionDetails(Details面板), LevelEditorSceneOutliner(Outliner), ContentBrowserTab1(内容浏览器), OutputLog(输出日志), WorldSettings(世界设置)"))},
				{TEXT("quality"), MakeProp(TEXT("string"), TEXT("分辨率预设: low(512x512), medium(1024x1024), high(1280x720), ultra(1920x1080)。默认 high"))},
				{TEXT("width"), MakeProp(TEXT("integer"), TEXT("自定义宽度，覆盖 quality"))},
				{TEXT("height"), MakeProp(TEXT("integer"), TEXT("自定义高度，覆盖 quality"))},
				{TEXT("format"), MakeProp(TEXT("string"), TEXT("输出格式: png、jpg。默认 png"))},
			}, {}));
	}
	if (MethodName == TEXT("get_asset_thumbnail"))
	{
		return MakeToolSchema(TEXT("get_asset_thumbnail"),
			TEXT("获取资产缩略图并保存为图片文件"),
			MakeInputSchema({
				{TEXT("asset_path"), MakeProp(TEXT("string"), TEXT("资产路径"))},
				{TEXT("size"), MakeProp(TEXT("integer"), TEXT("缩略图尺寸，默认 256"))},
			}, {TEXT("asset_path")}));
	}
	return nullptr;
}

// ==================== Execute dispatcher ====================

bool UAScreenshotCommands::Execute(
	const FString& MethodName,
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	if (MethodName == TEXT("take_screenshot"))     return ExecuteTakeScreenshot(Params, OutResult, OutError);
	if (MethodName == TEXT("get_asset_thumbnail")) return ExecuteGetAssetThumbnail(Params, OutResult, OutError);

	OutError = FString::Printf(TEXT("Unknown method: %s"), *MethodName);
	return false;
}

// ==================== 辅助函数 ====================

void UAScreenshotCommands::GetResolutionFromQuality(const FString& Quality, int32& OutWidth, int32& OutHeight)
{
	if (Quality == TEXT("low"))        { OutWidth = 512;  OutHeight = 512;  }
	else if (Quality == TEXT("medium")) { OutWidth = 1024; OutHeight = 1024; }
	else if (Quality == TEXT("high"))   { OutWidth = 1280; OutHeight = 720;  }
	else if (Quality == TEXT("ultra"))  { OutWidth = 1920; OutHeight = 1080; }
	else                                { OutWidth = 1280; OutHeight = 720;  } // 默认 high
}

FString UAScreenshotCommands::GetScreenshotDir()
{
	FString Dir = FPaths::ProjectSavedDir() / TEXT("UnrealAgent") / TEXT("Screenshots");
	IFileManager::Get().MakeDirectory(*Dir, true);
	return Dir;
}

bool UAScreenshotCommands::SaveBitmapToPng(const TArray<FColor>& Bitmap, int32 Width, int32 Height, const FString& FilePath)
{
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

	if (!ImageWrapper.IsValid()) return false;

	if (!ImageWrapper->SetRaw(Bitmap.GetData(), Bitmap.Num() * sizeof(FColor), Width, Height, ERGBFormat::BGRA, 8))
	{
		return false;
	}

	TArray64<uint8> CompressedData = ImageWrapper->GetCompressed();
	return FFileHelper::SaveArrayToFile(CompressedData, *FilePath);
}

// ==================== take_screenshot ====================

bool UAScreenshotCommands::ExecuteTakeScreenshot(
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString Mode = TEXT("scene");
	Params->TryGetStringField(TEXT("mode"), Mode);

	FString Quality = TEXT("high");
	Params->TryGetStringField(TEXT("quality"), Quality);

	int32 Width = 0, Height = 0;
	Params->TryGetNumberField(TEXT("width"), Width);
	Params->TryGetNumberField(TEXT("height"), Height);

	// 安全上限：防止超大分辨率导致内存溢出崩溃 (BUG-008)
	constexpr int32 MaxDimension = 4096;
	if (Width > MaxDimension)
	{
		UE_LOG(LogUAScreenshot, Warning, TEXT("Width %d exceeds max %d, clamping"), Width, MaxDimension);
		Width = MaxDimension;
	}
	if (Height > MaxDimension)
	{
		UE_LOG(LogUAScreenshot, Warning, TEXT("Height %d exceeds max %d, clamping"), Height, MaxDimension);
		Height = MaxDimension;
	}

	if (Width <= 0 || Height <= 0)
	{
		GetResolutionFromQuality(Quality, Width, Height);
	}

	FString Format = TEXT("png");
	Params->TryGetStringField(TEXT("format"), Format);
	Format = Format.ToLower();

	// 生成文件名
	FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	FString Extension = (Format == TEXT("jpg")) ? TEXT("jpg") : TEXT("png");
	FString FileName = FString::Printf(TEXT("screenshot_%s.%s"), *Timestamp, *Extension);
	FString FilePath = GetScreenshotDir() / FileName;

	bool bSuccess = false;

	if (Mode == TEXT("editor"))
	{
		// ── Editor 模式：使用 Slate TakeScreenshot 截取编辑器 UI 面板 ──
		FString Target = TEXT("active_window");
		Params->TryGetStringField(TEXT("target"), Target);

		FString AssetPath;
		Params->TryGetStringField(TEXT("asset_path"), AssetPath);

		FString TabId;
		Params->TryGetStringField(TEXT("tab_id"), TabId);

		bSuccess = ExecuteEditorScreenshot(Target, AssetPath, TabId, FilePath, Width, Height, OutError);
	}
	else if (Mode == TEXT("viewport"))
	{
		// ── Viewport 模式：从编辑器活跃视口回读像素 ──
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		TSharedPtr<SLevelViewport> ActiveViewport = LevelEditorModule.GetFirstActiveLevelViewport();

		if (!ActiveViewport.IsValid())
		{
			OutError = TEXT("No active level viewport found");
			return false;
		}

		FViewport* Viewport = ActiveViewport->GetActiveViewport();
		if (!Viewport)
		{
			OutError = TEXT("Viewport is null");
			return false;
		}

		// 强制视口重绘一帧，确保帧缓冲中有最新的渲染内容
		// 不做此步骤的话，如果视口尚未绘制过（窗口被遮挡、刚启动等），ReadPixels 会返回纯黑
		Viewport->Invalidate();
		Viewport->Draw(/*bShouldPresent=*/ false);
		FlushRenderingCommands();

		TArray<FColor> Bitmap;
		if (!Viewport->ReadPixels(Bitmap))
		{
			OutError = TEXT("Failed to read viewport pixels");
			return false;
		}

		int32 ViewWidth = Viewport->GetSizeXY().X;
		int32 ViewHeight = Viewport->GetSizeXY().Y;

		// 如果视口尺寸和目标尺寸不同，这里直接使用原始尺寸（避免缩放损失）
		// Agent 可以通过缩小视口或移动相机来调整
		bSuccess = SaveBitmapToPng(Bitmap, ViewWidth, ViewHeight, FilePath);
		Width = ViewWidth;
		Height = ViewHeight;
	}
	else if (Mode == TEXT("scene"))
	{
		// ── Scene 模式：直接获取视口最终渲染画面 ──
		// 第一性原理：视口已经完成了最终渲染（含正确的曝光、后处理、色调映射），
		// 我们只需要读取它，而不是用 SceneCapture2D 重新渲染再去模拟参数。
		// 方案：GameView（隐藏gizmo）→ 重绘 → ReadPixels → 恢复 → 缩放到目标尺寸

		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		TSharedPtr<SLevelViewport> ActiveViewport = LevelEditorModule.GetFirstActiveLevelViewport();

		if (!ActiveViewport.IsValid())
		{
			OutError = TEXT("No active level viewport found");
			return false;
		}

		FLevelEditorViewportClient& ViewClient = ActiveViewport->GetLevelViewportClient();
		FViewport* Viewport = ActiveViewport->GetActiveViewport();
		if (!Viewport)
		{
			OutError = TEXT("Viewport is null");
			return false;
		}

		// 保存原始 GameView 状态，临时切换到游戏视图（隐藏 gizmo/grid/icons 等编辑器覆盖物）
		const bool bWasInGameView = ViewClient.IsInGameView();
		if (!bWasInGameView)
		{
			ViewClient.SetGameView(true);
		}

		// 强制视口重绘一帧（不 Present 到屏幕，仅更新帧缓冲）
		Viewport->Invalidate();
		Viewport->Draw(/*bShouldPresent=*/ false);
		FlushRenderingCommands();

		// 从帧缓冲读取最终渲染画面
		TArray<FColor> Bitmap;
		if (!Viewport->ReadPixels(Bitmap))
		{
			// 恢复 GameView 状态
			if (!bWasInGameView)
			{
				ViewClient.SetGameView(false);
			}
			OutError = TEXT("Failed to read viewport pixels");
			return false;
		}

		int32 ViewWidth = Viewport->GetSizeXY().X;
		int32 ViewHeight = Viewport->GetSizeXY().Y;

		// 恢复原始 GameView 状态
		if (!bWasInGameView)
		{
			ViewClient.SetGameView(false);
			Viewport->Invalidate();
		}

		UE_LOG(LogUAScreenshot, Log, TEXT("Scene capture: viewport %dx%d, target %dx%d"),
			ViewWidth, ViewHeight, Width, Height);

		// 如果视口尺寸与请求尺寸不同，缩放到目标分辨率
		if (ViewWidth != Width || ViewHeight != Height)
		{
			TArray<FColor> ResizedBitmap;
			ResizedBitmap.SetNumUninitialized(Width * Height);
			FImageUtils::ImageResize(ViewWidth, ViewHeight, Bitmap, Width, Height, ResizedBitmap, true);
			Bitmap = MoveTemp(ResizedBitmap);
		}
		else
		{
			Width = ViewWidth;
			Height = ViewHeight;
		}

		bSuccess = SaveBitmapToPng(Bitmap, Width, Height, FilePath);
	}
	else
	{
		OutError = FString::Printf(TEXT("Unsupported mode: '%s'. Valid modes: scene, viewport, editor"), *Mode);
		return false;
	}

	if (!bSuccess)
	{
		if (OutError.IsEmpty())
		{
			OutError = TEXT("Failed to save screenshot file");
		}
		UE_LOG(LogUAScreenshot, Error, TEXT("take_screenshot failed: %s"), *OutError);
		return false;
	}

	// 获取文件大小
	int64 FileSize = IFileManager::Get().FileSize(*FilePath);

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetBoolField(TEXT("success"), true);
	OutResult->SetStringField(TEXT("file_path"), FilePath);
	OutResult->SetNumberField(TEXT("width"), Width);
	OutResult->SetNumberField(TEXT("height"), Height);
	OutResult->SetNumberField(TEXT("file_size_bytes"), FileSize);
	OutResult->SetStringField(TEXT("mode"), Mode);

	UE_LOG(LogUAScreenshot, Log, TEXT("take_screenshot: %s (%dx%d, %lld bytes, mode=%s)"),
		*FilePath, Width, Height, FileSize, *Mode);
	return true;
}

// ==================== get_asset_thumbnail ====================

bool UAScreenshotCommands::ExecuteGetAssetThumbnail(
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		OutError = TEXT("'asset_path' is required");
		return false;
	}

	int32 Size = 256;
	Params->TryGetNumberField(TEXT("size"), Size);
	if (Size <= 0 || Size > 1024) Size = 256;

	// 加载资产
	UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
	if (!Asset)
	{
		OutError = FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath);
		return false;
	}

	// 获取缩略图
	FObjectThumbnail* Thumbnail = ThumbnailTools::GenerateThumbnailForObjectToSaveToDisk(Asset);
	if (!Thumbnail || Thumbnail->IsEmpty())
	{
		OutError = FString::Printf(TEXT("No thumbnail available for: %s"), *AssetPath);
		return false;
	}

	int32 ThumbWidth = Thumbnail->GetImageWidth();
	int32 ThumbHeight = Thumbnail->GetImageHeight();
	const TArray<uint8>& ThumbData = Thumbnail->AccessImageData();

	if (ThumbData.Num() == 0)
	{
		OutError = TEXT("Thumbnail image data is empty");
		return false;
	}

	// 将 BGRA 数据转为 FColor 数组
	TArray<FColor> Bitmap;
	Bitmap.SetNumUninitialized(ThumbWidth * ThumbHeight);
	FMemory::Memcpy(Bitmap.GetData(), ThumbData.GetData(),
		FMath::Min(ThumbData.Num(), (int32)(Bitmap.Num() * sizeof(FColor))));

	// 如果请求的 Size 与原始缩略图尺寸不同，进行缩放
	int32 FinalWidth = ThumbWidth;
	int32 FinalHeight = ThumbHeight;
	if (Size != ThumbWidth || Size != ThumbHeight)
	{
		TArray<FColor> ResizedBitmap;
		ResizedBitmap.SetNumUninitialized(Size * Size);
		FImageUtils::ImageResize(ThumbWidth, ThumbHeight, Bitmap, Size, Size, ResizedBitmap, true);
		Bitmap = MoveTemp(ResizedBitmap);
		FinalWidth = Size;
		FinalHeight = Size;
	}

	// 保存文件
	FString SafeName = FPaths::GetBaseFilename(AssetPath).Replace(TEXT("/"), TEXT("_"));
	FString FileName = FString::Printf(TEXT("thumb_%s_%d.png"), *SafeName, Size);
	FString FilePath = GetScreenshotDir() / FileName;

	bool bSuccess = SaveBitmapToPng(Bitmap, FinalWidth, FinalHeight, FilePath);
	if (!bSuccess)
	{
		OutError = TEXT("Failed to save thumbnail file");
		return false;
	}

	int64 FileSize = IFileManager::Get().FileSize(*FilePath);

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetBoolField(TEXT("success"), true);
	OutResult->SetStringField(TEXT("file_path"), FilePath);
	OutResult->SetNumberField(TEXT("width"), FinalWidth);
	OutResult->SetNumberField(TEXT("height"), FinalHeight);
	OutResult->SetNumberField(TEXT("file_size_bytes"), FileSize);

	UE_LOG(LogUAScreenshot, Log, TEXT("get_asset_thumbnail: %s → %s (%dx%d, requested=%d)"),
		*AssetPath, *FilePath, FinalWidth, FinalHeight, Size);
	return true;
}

// ==================== Editor UI 截图 ====================

TSharedPtr<SWidget> UAScreenshotCommands::FindTargetWidgetForEditorScreenshot(
	const FString& Target, const FString& AssetPath, const FString& InTabId, FString& OutError)
{
	if (Target == TEXT("full"))
	{
		// 截取 UE 编辑器主窗口
		TSharedPtr<SWindow> MainWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
		if (!MainWindow.IsValid())
		{
			// 回退：获取根窗口
			TArray<TSharedRef<SWindow>> AllWindows;
			FSlateApplication::Get().GetAllVisibleWindowsOrdered(AllWindows);
			if (AllWindows.Num() > 0)
			{
				MainWindow = AllWindows[0];
			}
		}

		if (!MainWindow.IsValid())
		{
			OutError = TEXT("No main editor window found");
			return nullptr;
		}
		return MainWindow;
	}

	if (Target == TEXT("asset_editor"))
	{
		// 截取资产编辑器的【内容区域】（材质节点图 / 蓝图图 等），而非整个窗口
		UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (!AssetEditorSubsystem)
		{
			OutError = TEXT("AssetEditorSubsystem is not available");
			return nullptr;
		}

		IAssetEditorInstance* EditorInstance = nullptr;

		if (!AssetPath.IsEmpty())
		{
			// 根据资产路径找到编辑器
			UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
			if (!Asset)
			{
				OutError = FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath);
				return nullptr;
			}

			EditorInstance = AssetEditorSubsystem->FindEditorForAsset(Asset, false);
			if (!EditorInstance)
			{
				OutError = FString::Printf(TEXT("No editor is open for asset: %s"), *AssetPath);
				return nullptr;
			}
		}
		else
		{
			// 没有指定资产，获取最近活跃的编辑器
			TArray<IAssetEditorInstance*> AllEditors = AssetEditorSubsystem->GetAllOpenEditors();
			if (AllEditors.Num() == 0)
			{
				OutError = TEXT("No asset editors are currently open");
				return nullptr;
			}

			// 按最后激活时间排序，取最近的
			double LatestTime = -1.0;
			for (IAssetEditorInstance* Editor : AllEditors)
			{
				double ActivationTime = Editor->GetLastActivationTime();
				if (ActivationTime > LatestTime)
				{
					LatestTime = ActivationTime;
					EditorInstance = Editor;
				}
			}
		}

		if (!EditorInstance)
		{
			OutError = TEXT("Could not find an active asset editor");
			return nullptr;
		}

		// 通过 TabManager 获取编辑器的 Owner Tab
		TSharedPtr<FTabManager> TabManager = EditorInstance->GetAssociatedTabManager();
		if (!TabManager.IsValid())
		{
			OutError = TEXT("Asset editor has no associated TabManager");
			return nullptr;
		}

		// 获取 TabManager 的 Owner Tab，截取其【内容区域】（不含 Tab 标签栏）
		TSharedPtr<SDockTab> OwnerTab = TabManager->GetOwnerTab();
		if (OwnerTab.IsValid())
		{
			// 截取整个 OwnerTab（SDockTab 在 Slate Widget 树中有确定位置，
			// FindWidgetWindow 能正确定位）
			UE_LOG(LogUAScreenshot, Log, TEXT("asset_editor: capturing OwnerTab (SDockTab)"));
			return OwnerTab;
		}

		OutError = TEXT("Asset editor TabManager has no owner tab");
		return nullptr;
	}

	if (Target == TEXT("tab"))
	{
		// 按 Tab ID 精确截取指定面板（Details、Outliner、ContentBrowser 等）
		if (InTabId.IsEmpty())
		{
			OutError = TEXT("target='tab' requires 'tab_id' parameter. Common tab IDs: "
				"LevelEditorSelectionDetails, LevelEditorSceneOutliner, ContentBrowserTab1, "
				"OutputLog, WorldSettings");
			return nullptr;
		}

		// 首先在 LevelEditor 的 TabManager 中查找
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();

		TSharedPtr<SDockTab> FoundTab;
		if (LevelEditorTabManager.IsValid())
		{
			FoundTab = LevelEditorTabManager->FindExistingLiveTab(FTabId(FName(*InTabId)));
		}

		// 如果没找到，尝试在 FGlobalTabmanager 中查找（Nomad Tab 等）
		if (!FoundTab.IsValid())
		{
			FoundTab = FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId(FName(*InTabId)));
		}

		if (!FoundTab.IsValid())
		{
			OutError = FString::Printf(TEXT("Tab '%s' not found or not currently open. "
				"Common tab IDs: LevelEditorSelectionDetails, LevelEditorSceneOutliner, "
				"ContentBrowserTab1, OutputLog, WorldSettings"), *InTabId);
			return nullptr;
		}

		// SDockTab 只是标签头按钮（很小），实际面板内容在 Docking 容器中。
		// 第一性原理分析 UE Docking 系统的 Widget 层级：
		//   SDockingTabStack (整个面板区域：标签栏 + 内容)
		//     ├── Tab Bar → SDockTab (只是标签按钮)
		//     └── Content Area → SDockTab::GetContent()
		// 
		// 策略：从 SDockTab 向上遍历 Widget 树，找到父容器（SDockingTabStack
		// 或类似的 Docking 容器），截取整个面板区域。

		// 向上遍历查找 Docking 容器
		TSharedPtr<SWidget> Current = FoundTab;
		TSharedPtr<SWidget> PanelWidget;
		for (int32 i = 0; i < 20 && Current.IsValid(); ++i)
		{
			FString TypeName = Current->GetTypeAsString();

			// SDockingTabStack 是包含整个面板的容器
			if (TypeName.Contains(TEXT("SDockingTabStack")))
			{
				PanelWidget = Current;
				break;
			}
			// SDockingArea 也是一个有效的容器（包含多个 TabStack）
			if (TypeName.Contains(TEXT("SDockingArea")) && !PanelWidget.IsValid())
			{
				PanelWidget = Current;
				// 不 break，继续找更精确的 SDockingTabStack
			}

			TSharedPtr<SWidget> Parent = Current->GetParentWidget();
			if (!Parent.IsValid() || Parent == Current)
			{
				break;
			}
			Current = Parent;
		}

		if (PanelWidget.IsValid())
		{
			return PanelWidget;
		}

		// 回退方案：使用 GetContent()
		TSharedRef<SWidget> Content = FoundTab->GetContent();
		if (Content != SNullWidget::NullWidget)
		{
			UE_LOG(LogUAScreenshot, Warning, TEXT("tab: no DockingTabStack found, falling back to GetContent() for '%s' (widget=%s)"),
				*InTabId, *Content->GetTypeAsString());
			return Content;
		}

		// 最终回退：SDockTab 自身
		UE_LOG(LogUAScreenshot, Warning, TEXT("tab: all fallbacks failed, using SDockTab itself for '%s'"), *InTabId);
		return FoundTab;
	}

	if (Target == TEXT("active_window"))
	{
		// 截取当前活跃的顶层窗口（可能是编辑器主窗口、浮动面板、弹出窗口等）
		TSharedPtr<SWindow> ActiveWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
		if (!ActiveWindow.IsValid())
		{
			OutError = TEXT("No active top-level window found. Try clicking on the window you want to capture first.");
			return nullptr;
		}
		return ActiveWindow;
	}

	// 不支持的 target
	OutError = FString::Printf(TEXT("Unsupported target: '%s'. Valid targets: active_window, asset_editor, tab, full"), *Target);
	return nullptr;
}

bool UAScreenshotCommands::ExecuteEditorScreenshot(
	const FString& Target, const FString& AssetPath, const FString& InTabId,
	const FString& FilePath, int32& OutWidth, int32& OutHeight, FString& OutError)
{
	// 1. 如果是 tab 模式，先激活目标 Tab 使其渲染到前台
	//    TakeScreenshot 截取的是屏幕上已渲染的像素，如果目标 Tab 不在前台
	//    （被同一 TabStack 中的其他 Tab 遮挡），截取的就是错误的内容。
	if (Target == TEXT("tab") && !InTabId.IsEmpty())
	{
		TSharedPtr<SDockTab> TabToActivate;

		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();
		if (LevelEditorTabManager.IsValid())
		{
			TabToActivate = LevelEditorTabManager->FindExistingLiveTab(FTabId(FName(*InTabId)));
		}
		if (!TabToActivate.IsValid())
		{
			TabToActivate = FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId(FName(*InTabId)));
		}

		if (TabToActivate.IsValid() && !TabToActivate->IsForeground())
		{
			UE_LOG(LogUAScreenshot, Log, TEXT("Activating tab '%s' to bring it to foreground before screenshot"), *InTabId);
			TabToActivate->ActivateInParent(ETabActivationCause::SetDirectly);

			// 强制 Slate 刷新布局和渲染，确保目标 Tab 的内容被完整绘制
			FSlateApplication::Get().Tick();
		}
	}

	// 2. 定位目标 Widget
	TSharedPtr<SWidget> TargetWidget = FindTargetWidgetForEditorScreenshot(Target, AssetPath, InTabId, OutError);
	if (!TargetWidget.IsValid())
	{
		return false;
	}

	// 3. 使用 FSlateApplication::TakeScreenshot 截取 Widget
	TArray<FColor> ColorData;
	FIntVector Size(0, 0, 0);

	UE_LOG(LogUAScreenshot, Log, TEXT("ExecuteEditorScreenshot: calling TakeScreenshot on widget (type=%s, target=%s)"),
		*TargetWidget->GetTypeAsString(), *Target);

	bool bCaptured = FSlateApplication::Get().TakeScreenshot(TargetWidget.ToSharedRef(), ColorData, Size);

	UE_LOG(LogUAScreenshot, Log, TEXT("ExecuteEditorScreenshot: TakeScreenshot returned bCaptured=%d, ColorData.Num()=%d, Size=(%d,%d,%d)"),
		bCaptured, ColorData.Num(), Size.X, Size.Y, Size.Z);

	if (!bCaptured || ColorData.Num() == 0)
	{
		OutError = FString::Printf(TEXT("FSlateApplication::TakeScreenshot failed — bCaptured=%d, ColorData=%d, Size=(%d,%d). Widget type: %s"),
			bCaptured, ColorData.Num(), Size.X, Size.Y, *TargetWidget->GetTypeAsString());
		return false;
	}

	OutWidth = Size.X;
	OutHeight = Size.Y;

	UE_LOG(LogUAScreenshot, Log, TEXT("Editor screenshot captured: %dx%d pixels (%d total, target=%s)"),
		OutWidth, OutHeight, ColorData.Num(), *Target);

	// 3. 保存为 PNG
	bool bSaved = SaveBitmapToPng(ColorData, OutWidth, OutHeight, FilePath);
	if (!bSaved)
	{
		OutError = FString::Printf(TEXT("Failed to save editor screenshot file (%dx%d, %d pixels)"),
			OutWidth, OutHeight, ColorData.Num());
		return false;
	}

	return true;
}
