// Copyright KuoYu. All Rights Reserved.

#include "Commands/UAAssetManageCommands.h"
#include "UnrealAgent.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "FileHelpers.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "Factories/BlueprintFactory.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Engine/Blueprint.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogUAAssetManage, Log, All);

// ==================== Schema Helpers ====================

// 清理路径：去除末尾斜杠，避免 UE LongPackageNames 校验报错
static void SanitizePackagePath(FString& Path)
{
	// 去除末尾的 / 或反斜杠
	while (Path.Len() > 1 && (Path.EndsWith(TEXT("/")) || Path.EndsWith(TEXT("\\"))))
	{
		Path.LeftChopInline(1);
	}
}

namespace UAAssetManageHelper
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

TArray<FString> UAAssetManageCommands::GetSupportedMethods() const
{
	return {
		TEXT("create_asset"),
		TEXT("duplicate_asset"),
		TEXT("rename_asset"),
		TEXT("delete_asset"),
		TEXT("save_asset"),
		TEXT("create_folder"),
	};
}

// ==================== GetToolSchema ====================

TSharedPtr<FJsonObject> UAAssetManageCommands::GetToolSchema(const FString& MethodName) const
{
	using namespace UAAssetManageHelper;

	if (MethodName == TEXT("create_asset"))
	{
		return MakeToolSchema(TEXT("create_asset"),
			TEXT("创建新资产。支持的 asset_class: Material, MaterialInstance, Blueprint"),
			MakeInputSchema({
				{TEXT("asset_name"), MakeProp(TEXT("string"), TEXT("资产名称"))},
				{TEXT("package_path"), MakeProp(TEXT("string"), TEXT("包路径，如 /Game/Materials"))},
				{TEXT("asset_class"), MakeProp(TEXT("string"), TEXT("资产类型: Material, MaterialInstance, Blueprint"))},
				{TEXT("parent_material"), MakeProp(TEXT("string"), TEXT("MaterialInstance 的父材质路径"))},
				{TEXT("parent_class"), MakeProp(TEXT("string"), TEXT("Blueprint 的父类名（如 Actor, Character, Pawn），默认 Actor"))},
			}, {TEXT("asset_name"), TEXT("package_path"), TEXT("asset_class")}));
	}
	if (MethodName == TEXT("duplicate_asset"))
	{
		return MakeToolSchema(TEXT("duplicate_asset"),
			TEXT("复制资产到新位置"),
			MakeInputSchema({
				{TEXT("source_path"), MakeProp(TEXT("string"), TEXT("源资产路径"))},
				{TEXT("dest_path"), MakeProp(TEXT("string"), TEXT("目标包路径（文件夹）"))},
				{TEXT("new_name"), MakeProp(TEXT("string"), TEXT("新资产名称"))},
			}, {TEXT("source_path"), TEXT("dest_path"), TEXT("new_name")}));
	}
	if (MethodName == TEXT("rename_asset"))
	{
		return MakeToolSchema(TEXT("rename_asset"),
			TEXT("重命名或移动资产"),
			MakeInputSchema({
				{TEXT("asset_path"), MakeProp(TEXT("string"), TEXT("当前资产路径"))},
				{TEXT("new_name"), MakeProp(TEXT("string"), TEXT("新名称，可选"))},
				{TEXT("new_path"), MakeProp(TEXT("string"), TEXT("新目标文件夹路径，可选"))},
			}, {TEXT("asset_path")}));
	}
	if (MethodName == TEXT("delete_asset"))
	{
		return MakeToolSchema(TEXT("delete_asset"),
			TEXT("删除资产。默认安全模式：如有引用则拒绝，force=true 强制删除。"),
			MakeInputSchema({
				{TEXT("asset_path"), MakeProp(TEXT("string"), TEXT("要删除的资产路径"))},
				{TEXT("force"), MakeProp(TEXT("boolean"), TEXT("是否强制删除（忽略引用检查），默认 false"))},
			}, {TEXT("asset_path")}));
	}
	if (MethodName == TEXT("save_asset"))
	{
		return MakeToolSchema(TEXT("save_asset"),
			TEXT("保存资产。传 asset_path 保存单个，传 save_all=true 保存所有脏资产。"),
			MakeInputSchema({
				{TEXT("asset_path"), MakeProp(TEXT("string"), TEXT("要保存的资产路径，可选"))},
				{TEXT("save_all"), MakeProp(TEXT("boolean"), TEXT("是否保存所有脏资产，默认 false"))},
			}, {}));
	}
	if (MethodName == TEXT("create_folder"))
	{
		return MakeToolSchema(TEXT("create_folder"),
			TEXT("在 Content Browser 中创建文件夹"),
			MakeInputSchema({
				{TEXT("folder_path"), MakeProp(TEXT("string"), TEXT("文件夹路径，如 /Game/Materials/NewFolder"))},
			}, {TEXT("folder_path")}));
	}

	return nullptr;
}

// ==================== Execute dispatcher ====================

bool UAAssetManageCommands::Execute(
	const FString& MethodName,
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	if (MethodName == TEXT("create_asset"))    return ExecuteCreateAsset(Params, OutResult, OutError);
	if (MethodName == TEXT("duplicate_asset")) return ExecuteDuplicateAsset(Params, OutResult, OutError);
	if (MethodName == TEXT("rename_asset"))    return ExecuteRenameAsset(Params, OutResult, OutError);
	if (MethodName == TEXT("delete_asset"))    return ExecuteDeleteAsset(Params, OutResult, OutError);
	if (MethodName == TEXT("save_asset"))      return ExecuteSaveAsset(Params, OutResult, OutError);
	if (MethodName == TEXT("create_folder"))   return ExecuteCreateFolder(Params, OutResult, OutError);

	OutError = FString::Printf(TEXT("Unknown method: %s"), *MethodName);
	return false;
}

// ==================== create_asset ====================

bool UAAssetManageCommands::ExecuteCreateAsset(
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString AssetName, PackagePath, AssetClass;
	if (!Params->TryGetStringField(TEXT("asset_name"), AssetName)) { OutError = TEXT("'asset_name' required"); return false; }
	if (!Params->TryGetStringField(TEXT("package_path"), PackagePath)) { OutError = TEXT("'package_path' required"); return false; }
	if (!Params->TryGetStringField(TEXT("asset_class"), AssetClass)) { OutError = TEXT("'asset_class' required"); return false; }

	// 清理路径末尾斜杠，防止 UE LongPackageNames 校验弹窗
	SanitizePackagePath(PackagePath);

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	UFactory* Factory = nullptr;
	UObject* CreatedAsset = nullptr;

	if (AssetClass == TEXT("Material"))
	{
		Factory = NewObject<UMaterialFactoryNew>();
		CreatedAsset = AssetTools.CreateAsset(AssetName, PackagePath, UMaterial::StaticClass(), Factory);
	}
	else if (AssetClass == TEXT("MaterialInstance"))
	{
		FString ParentMaterialPath;
		Params->TryGetStringField(TEXT("parent_material"), ParentMaterialPath);

		UMaterialInstanceConstantFactoryNew* MICFactory = NewObject<UMaterialInstanceConstantFactoryNew>();
		if (!ParentMaterialPath.IsEmpty())
		{
			UMaterialInterface* ParentMat = Cast<UMaterialInterface>(
				StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *ParentMaterialPath));
			if (ParentMat)
			{
				MICFactory->InitialParent = ParentMat;
			}
		}
		Factory = MICFactory;
		CreatedAsset = AssetTools.CreateAsset(AssetName, PackagePath, UMaterialInstanceConstant::StaticClass(), Factory);
	}
	else if (AssetClass == TEXT("Blueprint"))
	{
		FString ParentClassName;
		Params->TryGetStringField(TEXT("parent_class"), ParentClassName);
		if (ParentClassName.IsEmpty()) ParentClassName = TEXT("Actor");

		// 查找父类
		UClass* ParentClass = FindFirstObject<UClass>(*ParentClassName, EFindFirstObjectOptions::NativeFirst);
		if (!ParentClass)
		{
			ParentClass = FindFirstObject<UClass>(*(FString(TEXT("A")) + ParentClassName), EFindFirstObjectOptions::NativeFirst);
		}
		if (!ParentClass)
		{
			ParentClass = AActor::StaticClass();
		}

		UBlueprintFactory* BPFactory = NewObject<UBlueprintFactory>();
		BPFactory->ParentClass = ParentClass;
		Factory = BPFactory;
		CreatedAsset = AssetTools.CreateAsset(AssetName, PackagePath, UBlueprint::StaticClass(), Factory);
	}
	else
	{
		OutError = FString::Printf(TEXT("Unsupported asset_class: %s. Supported: Material, MaterialInstance, Blueprint"), *AssetClass);
		return false;
	}

	if (!CreatedAsset)
	{
		OutError = FString::Printf(TEXT("Failed to create %s asset '%s'"), *AssetClass, *AssetName);
		return false;
	}

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetBoolField(TEXT("success"), true);
	OutResult->SetStringField(TEXT("asset_path"), CreatedAsset->GetPathName());
	OutResult->SetStringField(TEXT("asset_class"), AssetClass);
	OutResult->SetStringField(TEXT("asset_name"), AssetName);

	UE_LOG(LogUAAssetManage, Log, TEXT("create_asset: %s (%s) at %s"), *AssetName, *AssetClass, *PackagePath);
	return true;
}

// ==================== duplicate_asset ====================

bool UAAssetManageCommands::ExecuteDuplicateAsset(
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString SourcePath, DestPath, NewName;
	if (!Params->TryGetStringField(TEXT("source_path"), SourcePath)) { OutError = TEXT("'source_path' required"); return false; }
	if (!Params->TryGetStringField(TEXT("dest_path"), DestPath)) { OutError = TEXT("'dest_path' required"); return false; }
	if (!Params->TryGetStringField(TEXT("new_name"), NewName)) { OutError = TEXT("'new_name' required"); return false; }

	SanitizePackagePath(DestPath);

	UObject* DuplicatedAsset = UEditorAssetLibrary::DuplicateAsset(SourcePath, DestPath / NewName);
	if (!DuplicatedAsset)
	{
		OutError = FString::Printf(TEXT("Failed to duplicate %s to %s/%s"), *SourcePath, *DestPath, *NewName);
		return false;
	}

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetBoolField(TEXT("success"), true);
	OutResult->SetStringField(TEXT("new_asset_path"), DuplicatedAsset->GetPathName());

	UE_LOG(LogUAAssetManage, Log, TEXT("duplicate_asset: %s -> %s/%s"), *SourcePath, *DestPath, *NewName);
	return true;
}

// ==================== rename_asset ====================

bool UAAssetManageCommands::ExecuteRenameAsset(
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath)) { OutError = TEXT("'asset_path' required"); return false; }

	FString NewName, NewPath;
	Params->TryGetStringField(TEXT("new_name"), NewName);
	Params->TryGetStringField(TEXT("new_path"), NewPath);

	if (NewName.IsEmpty() && NewPath.IsEmpty())
	{
		OutError = TEXT("At least one of 'new_name' or 'new_path' must be specified");
		return false;
	}

	// 构建目标路径
	FString CurrentName = FPaths::GetBaseFilename(AssetPath);
	FString CurrentDir = FPaths::GetPath(AssetPath);

	SanitizePackagePath(NewPath);
	FString TargetDir = NewPath.IsEmpty() ? CurrentDir : NewPath;
	FString TargetName = NewName.IsEmpty() ? CurrentName : NewName;
	FString TargetFullPath = TargetDir / TargetName;

	bool bSuccess = UEditorAssetLibrary::RenameAsset(AssetPath, TargetFullPath);
	if (!bSuccess)
	{
		OutError = FString::Printf(TEXT("Failed to rename %s to %s"), *AssetPath, *TargetFullPath);
		return false;
	}

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetBoolField(TEXT("success"), true);
	OutResult->SetStringField(TEXT("old_path"), AssetPath);
	OutResult->SetStringField(TEXT("new_path"), TargetFullPath);

	UE_LOG(LogUAAssetManage, Log, TEXT("rename_asset: %s -> %s"), *AssetPath, *TargetFullPath);
	return true;
}

// ==================== delete_asset ====================

bool UAAssetManageCommands::ExecuteDeleteAsset(
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath)) { OutError = TEXT("'asset_path' required"); return false; }

	bool bForce = false;
	Params->TryGetBoolField(TEXT("force"), bForce);

	// 检查资产是否存在
	if (!UEditorAssetLibrary::DoesAssetExist(AssetPath))
	{
		OutError = FString::Printf(TEXT("Asset does not exist: %s"), *AssetPath);
		return false;
	}

	// 安全检查：检查引用
	if (!bForce)
	{
		TArray<FString> Referencers;
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

		// BUG-006 修复：等待 AssetRegistry 异步索引完成，确保引用关系数据是最新的
		// 新建资产的引用关系可能尚未被异步索引到，导致引用检查误判
		if (AssetRegistry.IsLoadingAssets())
		{
			UE_LOG(LogUAAssetManage, Log, TEXT("Waiting for AssetRegistry to finish loading before reference check..."));
			AssetRegistry.WaitForCompletion();
		}

		FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(AssetPath));
		if (AssetData.IsValid())
		{
			TArray<FAssetIdentifier> RefIds;
			AssetRegistry.GetReferencers(AssetData.PackageName, RefIds);

			for (const FAssetIdentifier& RefId : RefIds)
			{
				if (RefId.PackageName != AssetData.PackageName)
				{
					Referencers.Add(RefId.PackageName.ToString());
				}
			}
		}

		if (Referencers.Num() > 0)
		{
			OutResult = MakeShared<FJsonObject>();
			OutResult->SetBoolField(TEXT("success"), false);
			OutResult->SetBoolField(TEXT("had_references"), true);
			OutResult->SetNumberField(TEXT("reference_count"), Referencers.Num());

			TArray<TSharedPtr<FJsonValue>> RefArr;
			for (const FString& Ref : Referencers)
			{
				RefArr.Add(MakeShared<FJsonValueString>(Ref));
			}
			OutResult->SetArrayField(TEXT("referencers"), RefArr);
			OutResult->SetStringField(TEXT("message"), TEXT("Asset has references. Use force=true to delete anyway."));
			return true; // 不是错误，只是拒绝
		}
	}

	bool bDeleted = UEditorAssetLibrary::DeleteAsset(AssetPath);
	if (!bDeleted)
	{
		OutError = FString::Printf(TEXT("Failed to delete: %s"), *AssetPath);
		return false;
	}

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetBoolField(TEXT("success"), true);
	OutResult->SetBoolField(TEXT("had_references"), false);
	OutResult->SetStringField(TEXT("deleted_path"), AssetPath);

	UE_LOG(LogUAAssetManage, Log, TEXT("delete_asset: %s (force=%d)"), *AssetPath, bForce);
	return true;
}

// ==================== save_asset ====================

bool UAAssetManageCommands::ExecuteSaveAsset(
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	bool bSaveAll = false;
	Params->TryGetBoolField(TEXT("save_all"), bSaveAll);

	FString AssetPath;
	Params->TryGetStringField(TEXT("asset_path"), AssetPath);

	if (!bSaveAll && AssetPath.IsEmpty())
	{
		OutError = TEXT("Must specify 'asset_path' or 'save_all=true'");
		return false;
	}

	OutResult = MakeShared<FJsonObject>();

	if (bSaveAll)
	{
		bool bSuccess = FEditorFileUtils::SaveDirtyPackages(
			/*bPromptUserToSave=*/false,
			/*bSaveMapPackages=*/true,
			/*bSaveContentPackages=*/true
		);

		OutResult->SetBoolField(TEXT("success"), bSuccess);
		OutResult->SetStringField(TEXT("message"), bSuccess ? TEXT("All dirty packages saved") : TEXT("Some packages failed to save"));
	}
	else
	{
		bool bSuccess = UEditorAssetLibrary::SaveAsset(AssetPath);
		OutResult->SetBoolField(TEXT("success"), bSuccess);
		OutResult->SetStringField(TEXT("asset_path"), AssetPath);
		if (!bSuccess)
		{
			OutError = FString::Printf(TEXT("Failed to save: %s"), *AssetPath);
			return false;
		}
	}

	return true;
}

// ==================== create_folder ====================

bool UAAssetManageCommands::ExecuteCreateFolder(
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString FolderPath;
	if (!Params->TryGetStringField(TEXT("folder_path"), FolderPath)) { OutError = TEXT("'folder_path' required"); return false; }

	SanitizePackagePath(FolderPath);

	bool bSuccess = UEditorAssetLibrary::MakeDirectory(FolderPath);
	if (!bSuccess)
	{
		OutError = FString::Printf(TEXT("Failed to create folder: %s"), *FolderPath);
		return false;
	}

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetBoolField(TEXT("success"), true);
	OutResult->SetStringField(TEXT("folder_path"), FolderPath);

	UE_LOG(LogUAAssetManage, Log, TEXT("create_folder: %s"), *FolderPath);
	return true;
}
