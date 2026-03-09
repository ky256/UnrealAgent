// Copyright KuoYu. All Rights Reserved.

#include "Commands/UAMaterialCommands.h"
#include "UnrealAgent.h"

#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionCustom.h"
#include "UObject/UObjectIterator.h"
#include "Engine/Texture.h"

// ==================== Schema helpers ====================

namespace UAMaterialHelper
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

TArray<FString> UAMaterialCommands::GetSupportedMethods() const
{
	return {
		TEXT("get_material_graph"),
		TEXT("create_material_expression"),
		TEXT("delete_material_expression"),
		TEXT("connect_material_property"),
		TEXT("connect_material_expressions"),
		TEXT("set_expression_value"),
		TEXT("recompile_material"),
		TEXT("layout_material_expressions"),
		TEXT("get_material_parameters"),
		TEXT("set_material_instance_param"),
		TEXT("set_material_property"),
	};
}

// ==================== GetToolSchema ====================

TSharedPtr<FJsonObject> UAMaterialCommands::GetToolSchema(const FString& MethodName) const
{
	using namespace UAMaterialHelper;

	if (MethodName == TEXT("get_material_graph"))
	{
		return MakeToolSchema(TEXT("get_material_graph"),
			TEXT("Get complete material node graph: all expressions, connections, and material properties"),
			MakeInputSchema(
				{{TEXT("asset_path"), MakeProp(TEXT("string"), TEXT("Material asset path, e.g. /Game/Materials/M_Base"))}},
				{TEXT("asset_path")}
			));
	}
	if (MethodName == TEXT("create_material_expression"))
	{
		return MakeToolSchema(TEXT("create_material_expression"),
			TEXT("Create a material expression node. Common: MaterialExpressionConstant, MaterialExpressionConstant3Vector, MaterialExpressionScalarParameter, MaterialExpressionVectorParameter, MaterialExpressionTextureSample, MaterialExpressionAdd, MaterialExpressionMultiply, MaterialExpressionLerp, MaterialExpressionCustom"),
			MakeInputSchema({
				{TEXT("asset_path"), MakeProp(TEXT("string"), TEXT("Material asset path"))},
				{TEXT("expression_class"), MakeProp(TEXT("string"), TEXT("Expression class without U prefix"))},
				{TEXT("node_pos_x"), MakeProp(TEXT("integer"), TEXT("X position. Default 0"))},
				{TEXT("node_pos_y"), MakeProp(TEXT("integer"), TEXT("Y position. Default 0"))},
			}, {TEXT("asset_path"), TEXT("expression_class")}));
	}
	if (MethodName == TEXT("delete_material_expression"))
	{
		return MakeToolSchema(TEXT("delete_material_expression"),
			TEXT("Delete a material expression by index from get_material_graph"),
			MakeInputSchema({
				{TEXT("asset_path"), MakeProp(TEXT("string"), TEXT("Material asset path"))},
				{TEXT("expression_index"), MakeProp(TEXT("integer"), TEXT("Index of expression"))},
			}, {TEXT("asset_path"), TEXT("expression_index")}));
	}
	if (MethodName == TEXT("connect_material_property"))
	{
		return MakeToolSchema(TEXT("connect_material_property"),
			TEXT("Connect expression output to material property (BaseColor, Metallic, Roughness, Normal, etc)"),
			MakeInputSchema({
				{TEXT("asset_path"), MakeProp(TEXT("string"), TEXT("Material asset path"))},
				{TEXT("expression_index"), MakeProp(TEXT("integer"), TEXT("Source expression index"))},
				{TEXT("output_name"), MakeProp(TEXT("string"), TEXT("Output pin name. Empty for default"))},
				{TEXT("property"), MakeProp(TEXT("string"), TEXT("MP_BaseColor, MP_Metallic, MP_Specular, MP_Roughness, MP_Normal, MP_EmissiveColor, MP_Opacity, MP_OpacityMask, MP_AmbientOcclusion"))},
			}, {TEXT("asset_path"), TEXT("expression_index"), TEXT("property")}));
	}
	if (MethodName == TEXT("connect_material_expressions"))
	{
		return MakeToolSchema(TEXT("connect_material_expressions"),
			TEXT("Connect output of one expression to input of another"),
			MakeInputSchema({
				{TEXT("asset_path"), MakeProp(TEXT("string"), TEXT("Material asset path"))},
				{TEXT("from_index"), MakeProp(TEXT("integer"), TEXT("Source expression index"))},
				{TEXT("from_output"), MakeProp(TEXT("string"), TEXT("Output pin name. Empty for default"))},
				{TEXT("to_index"), MakeProp(TEXT("integer"), TEXT("Destination expression index"))},
				{TEXT("to_input"), MakeProp(TEXT("string"), TEXT("Input pin name. Empty for default"))},
			}, {TEXT("asset_path"), TEXT("from_index"), TEXT("to_index")}));
	}
	if (MethodName == TEXT("set_expression_value"))
	{
		return MakeToolSchema(TEXT("set_expression_value"),
			TEXT("Set expression property. Constant:'R', Constant3Vector:'Constant' {r,g,b,a}, ScalarParameter:'DefaultValue', VectorParameter:'DefaultValue' {r,g,b,a}, TextureSample:'texture_path'"),
			MakeInputSchema({
				{TEXT("asset_path"), MakeProp(TEXT("string"), TEXT("Material asset path"))},
				{TEXT("expression_index"), MakeProp(TEXT("integer"), TEXT("Expression index"))},
				{TEXT("property_name"), MakeProp(TEXT("string"), TEXT("Property name"))},
				{TEXT("value"), MakeProp(TEXT("string"), TEXT("JSON value string"))},
			}, {TEXT("asset_path"), TEXT("expression_index"), TEXT("property_name"), TEXT("value")}));
	}
	if (MethodName == TEXT("recompile_material"))
	{
		return MakeToolSchema(TEXT("recompile_material"), TEXT("Recompile material after graph changes"),
			MakeInputSchema({{TEXT("asset_path"), MakeProp(TEXT("string"), TEXT("Material asset path"))}}, {TEXT("asset_path")}));
	}
	if (MethodName == TEXT("layout_material_expressions"))
	{
		return MakeToolSchema(TEXT("layout_material_expressions"), TEXT("Auto-arrange expression nodes in grid"),
			MakeInputSchema({{TEXT("asset_path"), MakeProp(TEXT("string"), TEXT("Material asset path"))}}, {TEXT("asset_path")}));
	}
	if (MethodName == TEXT("get_material_parameters"))
	{
		return MakeToolSchema(TEXT("get_material_parameters"), TEXT("Get all parameter names (scalar, vector, texture, static_switch)"),
			MakeInputSchema({{TEXT("asset_path"), MakeProp(TEXT("string"), TEXT("Material or MaterialInstance path"))}}, {TEXT("asset_path")}));
	}
	if (MethodName == TEXT("set_material_instance_param"))
	{
		return MakeToolSchema(TEXT("set_material_instance_param"), TEXT("Set parameter value on MaterialInstanceConstant"),
			MakeInputSchema({
				{TEXT("asset_path"), MakeProp(TEXT("string"), TEXT("MaterialInstanceConstant path"))},
				{TEXT("param_name"), MakeProp(TEXT("string"), TEXT("Parameter name"))},
				{TEXT("param_type"), MakeProp(TEXT("string"), TEXT("scalar, vector, texture, static_switch"))},
				{TEXT("value"), MakeProp(TEXT("string"), TEXT("Value string"))},
			}, {TEXT("asset_path"), TEXT("param_name"), TEXT("param_type"), TEXT("value")}));
	}
	if (MethodName == TEXT("set_material_property"))
	{
		return MakeToolSchema(TEXT("set_material_property"), TEXT("Set material property: BlendMode, ShadingModel, TwoSided, OpacityMaskClipValue"),
			MakeInputSchema({
				{TEXT("asset_path"), MakeProp(TEXT("string"), TEXT("Material asset path"))},
				{TEXT("property_name"), MakeProp(TEXT("string"), TEXT("BlendMode, ShadingModel, TwoSided, OpacityMaskClipValue"))},
				{TEXT("value"), MakeProp(TEXT("string"), TEXT("Value string"))},
			}, {TEXT("asset_path"), TEXT("property_name"), TEXT("value")}));
	}
	return nullptr;
}

// ==================== Execute dispatcher ====================

bool UAMaterialCommands::Execute(const FString& MethodName, const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError)
{
	if (MethodName == TEXT("get_material_graph")) return ExecuteGetMaterialGraph(Params, OutResult, OutError);
	if (MethodName == TEXT("create_material_expression")) return ExecuteCreateExpression(Params, OutResult, OutError);
	if (MethodName == TEXT("delete_material_expression")) return ExecuteDeleteExpression(Params, OutResult, OutError);
	if (MethodName == TEXT("connect_material_property")) return ExecuteConnectToProperty(Params, OutResult, OutError);
	if (MethodName == TEXT("connect_material_expressions")) return ExecuteConnectExpressions(Params, OutResult, OutError);
	if (MethodName == TEXT("set_expression_value")) return ExecuteSetExpressionValue(Params, OutResult, OutError);
	if (MethodName == TEXT("recompile_material")) return ExecuteRecompileMaterial(Params, OutResult, OutError);
	if (MethodName == TEXT("layout_material_expressions")) return ExecuteLayoutExpressions(Params, OutResult, OutError);
	if (MethodName == TEXT("get_material_parameters")) return ExecuteGetMaterialParameters(Params, OutResult, OutError);
	if (MethodName == TEXT("set_material_instance_param")) return ExecuteSetInstanceParam(Params, OutResult, OutError);
	if (MethodName == TEXT("set_material_property")) return ExecuteSetMaterialProperty(Params, OutResult, OutError);
	OutError = FString::Printf(TEXT("Unknown method: %s"), *MethodName);
	return false;
}

// ==================== Helpers ====================

UMaterial* UAMaterialCommands::LoadMaterialFromPath(const FString& AssetPath, FString& OutError)
{
	UObject* Obj = StaticLoadObject(UMaterial::StaticClass(), nullptr, *AssetPath);
	UMaterial* Mat = Obj ? Cast<UMaterial>(Obj) : nullptr;
	if (!Mat) { OutError = FString::Printf(TEXT("Material not found: %s"), *AssetPath); }
	return Mat;
}

UMaterialInstanceConstant* UAMaterialCommands::LoadMaterialInstanceFromPath(const FString& AssetPath, FString& OutError)
{
	UObject* Obj = StaticLoadObject(UMaterialInstanceConstant::StaticClass(), nullptr, *AssetPath);
	UMaterialInstanceConstant* MIC = Obj ? Cast<UMaterialInstanceConstant>(Obj) : nullptr;
	if (!MIC) { OutError = FString::Printf(TEXT("MaterialInstanceConstant not found: %s"), *AssetPath); }
	return MIC;
}

UMaterialExpression* UAMaterialCommands::FindExpressionByIndex(UMaterial* Material, int32 Index, FString& OutError)
{
	auto Exprs = Material->GetExpressions();
	if (Index < 0 || Index >= Exprs.Num()) { OutError = FString::Printf(TEXT("Index %d out of range [0,%d)"), Index, Exprs.Num()); return nullptr; }
	return Exprs[Index];
}

bool UAMaterialCommands::ParseMaterialProperty(const FString& PropertyName, EMaterialProperty& OutProperty)
{
	static TMap<FString, EMaterialProperty> Map = {
		{TEXT("MP_BaseColor"), MP_BaseColor}, {TEXT("MP_Metallic"), MP_Metallic},
		{TEXT("MP_Specular"), MP_Specular}, {TEXT("MP_Roughness"), MP_Roughness},
		{TEXT("MP_Normal"), MP_Normal}, {TEXT("MP_EmissiveColor"), MP_EmissiveColor},
		{TEXT("MP_Opacity"), MP_Opacity}, {TEXT("MP_OpacityMask"), MP_OpacityMask},
		{TEXT("MP_WorldPositionOffset"), MP_WorldPositionOffset}, {TEXT("MP_AmbientOcclusion"), MP_AmbientOcclusion},
	};
	if (auto* Found = Map.Find(PropertyName)) { OutProperty = *Found; return true; }
	return false;
}

TSharedPtr<FJsonObject> UAMaterialCommands::ExpressionToJson(UMaterialExpression* Expr, int32 Index)
{
	auto Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(TEXT("index"), Index);
	Obj->SetStringField(TEXT("class"), Expr->GetClass()->GetName());
	Obj->SetStringField(TEXT("desc"), Expr->GetDescription());
	int32 PosX = 0, PosY = 0;
	UMaterialEditingLibrary::GetMaterialExpressionNodePosition(Expr, PosX, PosY);
	Obj->SetNumberField(TEXT("pos_x"), PosX);
	Obj->SetNumberField(TEXT("pos_y"), PosY);
	TArray<FString> InputNames = UMaterialEditingLibrary::GetMaterialExpressionInputNames(Expr);
	TArray<TSharedPtr<FJsonValue>> InputArr;
	for (const auto& N : InputNames) { InputArr.Add(MakeShared<FJsonValueString>(N)); }
	Obj->SetArrayField(TEXT("inputs"), InputArr);
	if (auto* SP = Cast<UMaterialExpressionScalarParameter>(Expr)) { Obj->SetStringField(TEXT("parameter_name"), SP->ParameterName.ToString()); Obj->SetNumberField(TEXT("default_value"), SP->DefaultValue); }
	else if (auto* VP = Cast<UMaterialExpressionVectorParameter>(Expr)) {
		Obj->SetStringField(TEXT("parameter_name"), VP->ParameterName.ToString());
		auto C = MakeShared<FJsonObject>(); C->SetNumberField(TEXT("r"), VP->DefaultValue.R); C->SetNumberField(TEXT("g"), VP->DefaultValue.G); C->SetNumberField(TEXT("b"), VP->DefaultValue.B); C->SetNumberField(TEXT("a"), VP->DefaultValue.A); Obj->SetObjectField(TEXT("default_value"), C);
	}
	else if (auto* C1 = Cast<UMaterialExpressionConstant>(Expr)) { Obj->SetNumberField(TEXT("value"), C1->R); }
	else if (auto* C3 = Cast<UMaterialExpressionConstant3Vector>(Expr)) {
		auto C = MakeShared<FJsonObject>(); C->SetNumberField(TEXT("r"), C3->Constant.R); C->SetNumberField(TEXT("g"), C3->Constant.G); C->SetNumberField(TEXT("b"), C3->Constant.B); C->SetNumberField(TEXT("a"), C3->Constant.A); Obj->SetObjectField(TEXT("constant"), C);
	}
	return Obj;
}

// ==================== get_material_graph ====================

bool UAMaterialCommands::ExecuteGetMaterialGraph(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath)) { OutError = TEXT("'asset_path' is required"); return false; }

	UMaterial* Mat = LoadMaterialFromPath(AssetPath, OutError);
	if (!Mat) return false;

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetStringField(TEXT("asset_path"), AssetPath);
	OutResult->SetStringField(TEXT("material_name"), Mat->GetName());

	// Material global properties (顶层，方便 AI 直接访问)
	OutResult->SetStringField(TEXT("blend_mode"), StaticEnum<EBlendMode>()->GetNameStringByValue((int64)Mat->BlendMode));
	OutResult->SetStringField(TEXT("shading_model"), StaticEnum<EMaterialShadingModel>()->GetNameStringByValue((int64)Mat->GetShadingModels().GetFirstShadingModel()));
	OutResult->SetBoolField(TEXT("two_sided"), Mat->IsTwoSided());
	OutResult->SetNumberField(TEXT("opacity_mask_clip_value"), Mat->GetOpacityMaskClipValue());
	OutResult->SetStringField(TEXT("material_domain"), StaticEnum<EMaterialDomain>()->GetNameStringByValue((int64)Mat->MaterialDomain));

	// All expressions
	auto Expressions = Mat->GetExpressions();
	TArray<TSharedPtr<FJsonValue>> ExprArr;
	for (int32 i = 0; i < Expressions.Num(); i++)
	{
		if (Expressions[i]) ExprArr.Add(MakeShared<FJsonValueObject>(ExpressionToJson(Expressions[i], i)));
	}
	OutResult->SetArrayField(TEXT("expressions"), ExprArr);
	OutResult->SetNumberField(TEXT("expression_count"), ExprArr.Num());

	// Material property connections (which expression feeds BaseColor, Normal, etc.)
	auto ConnObj = MakeShared<FJsonObject>();
	TArray<TPair<FString, EMaterialProperty>> PropertyList = {
		{TEXT("BaseColor"), MP_BaseColor}, {TEXT("Metallic"), MP_Metallic},
		{TEXT("Specular"), MP_Specular}, {TEXT("Roughness"), MP_Roughness},
		{TEXT("Normal"), MP_Normal}, {TEXT("EmissiveColor"), MP_EmissiveColor},
		{TEXT("Opacity"), MP_Opacity}, {TEXT("OpacityMask"), MP_OpacityMask},
		{TEXT("AmbientOcclusion"), MP_AmbientOcclusion},
	};
	for (auto& Prop : PropertyList)
	{
		UMaterialExpression* InputNode = UMaterialEditingLibrary::GetMaterialPropertyInputNode(Mat, Prop.Value);
		if (InputNode)
		{
			FString OutputName = UMaterialEditingLibrary::GetMaterialPropertyInputNodeOutputName(Mat, Prop.Value);
			int32 FoundIdx = Expressions.IndexOfByKey(InputNode);
			auto PC = MakeShared<FJsonObject>();
			PC->SetNumberField(TEXT("expression_index"), FoundIdx);
			PC->SetStringField(TEXT("output_name"), OutputName);
			ConnObj->SetObjectField(Prop.Key, PC);
		}
	}
	OutResult->SetObjectField(TEXT("property_connections"), ConnObj);

	// Expression-to-expression connections
	TArray<TSharedPtr<FJsonValue>> ConnArr;
	for (int32 i = 0; i < Expressions.Num(); i++)
	{
		if (!Expressions[i]) continue;
		TArray<UMaterialExpression*> Inputs = UMaterialEditingLibrary::GetInputsForMaterialExpression(Mat, Expressions[i]);
		TArray<FString> InputNames = UMaterialEditingLibrary::GetMaterialExpressionInputNames(Expressions[i]);
		for (int32 j = 0; j < Inputs.Num() && j < InputNames.Num(); j++)
		{
			if (!Inputs[j]) continue;
			int32 FromIdx = Expressions.IndexOfByKey(Inputs[j]);
			FString OutName;
			UMaterialEditingLibrary::GetInputNodeOutputNameForMaterialExpression(Expressions[i], Inputs[j], OutName);
			auto C = MakeShared<FJsonObject>();
			C->SetNumberField(TEXT("from_index"), FromIdx);
			C->SetStringField(TEXT("from_output"), OutName);
			C->SetNumberField(TEXT("to_index"), i);
			C->SetStringField(TEXT("to_input"), InputNames[j]);
			ConnArr.Add(MakeShared<FJsonValueObject>(C));
		}
	}
	OutResult->SetArrayField(TEXT("expression_connections"), ConnArr);

	return true;
}

// ==================== create_material_expression ====================

bool UAMaterialCommands::ExecuteCreateExpression(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError)
{
	FString AssetPath, ExprClassName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath)) { OutError = TEXT("'asset_path' required"); return false; }
	if (!Params->TryGetStringField(TEXT("expression_class"), ExprClassName)) { OutError = TEXT("'expression_class' required"); return false; }

	UMaterial* Mat = LoadMaterialFromPath(AssetPath, OutError);
	if (!Mat) return false;

	FString FullClassName = FString::Printf(TEXT("U%s"), *ExprClassName);
	UClass* ExprClass = FindFirstObject<UClass>(*FullClassName, EFindFirstObjectOptions::NativeFirst);
	if (!ExprClass) ExprClass = FindFirstObject<UClass>(*ExprClassName, EFindFirstObjectOptions::NativeFirst);
	if (!ExprClass || !ExprClass->IsChildOf(UMaterialExpression::StaticClass()))
	{
		OutError = FString::Printf(TEXT("Invalid expression class: %s"), *ExprClassName);
		return false;
	}

	int32 PosX = 0, PosY = 0;
	Params->TryGetNumberField(TEXT("node_pos_x"), PosX);
	Params->TryGetNumberField(TEXT("node_pos_y"), PosY);

	UMaterialExpression* NewExpr = UMaterialEditingLibrary::CreateMaterialExpression(Mat, ExprClass, PosX, PosY);
	if (!NewExpr) { OutError = TEXT("Failed to create expression"); return false; }

	int32 NewIndex = Mat->GetExpressions().IndexOfByKey(NewExpr);
	OutResult = MakeShared<FJsonObject>();
	OutResult->SetBoolField(TEXT("success"), true);
	OutResult->SetNumberField(TEXT("expression_index"), NewIndex);
	OutResult->SetStringField(TEXT("expression_class"), NewExpr->GetClass()->GetName());
	return true;
}

// ==================== delete_material_expression ====================

bool UAMaterialCommands::ExecuteDeleteExpression(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError)
{
	FString AssetPath; int32 ExprIndex = -1;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath)) { OutError = TEXT("'asset_path' required"); return false; }
	if (!Params->TryGetNumberField(TEXT("expression_index"), ExprIndex)) { OutError = TEXT("'expression_index' required"); return false; }

	UMaterial* Mat = LoadMaterialFromPath(AssetPath, OutError);
	if (!Mat) return false;
	UMaterialExpression* Expr = FindExpressionByIndex(Mat, ExprIndex, OutError);
	if (!Expr) return false;

	FString ClassName = Expr->GetClass()->GetName();
	UMaterialEditingLibrary::DeleteMaterialExpression(Mat, Expr);

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetBoolField(TEXT("success"), true);
	OutResult->SetStringField(TEXT("message"), FString::Printf(TEXT("Deleted %s at index %d"), *ClassName, ExprIndex));
	return true;
}

// ==================== connect_material_property ====================

bool UAMaterialCommands::ExecuteConnectToProperty(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError)
{
	FString AssetPath, OutputName, PropertyStr; int32 ExprIndex = -1;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath)) { OutError = TEXT("'asset_path' required"); return false; }
	if (!Params->TryGetNumberField(TEXT("expression_index"), ExprIndex)) { OutError = TEXT("'expression_index' required"); return false; }
	if (!Params->TryGetStringField(TEXT("property"), PropertyStr)) { OutError = TEXT("'property' required"); return false; }
	Params->TryGetStringField(TEXT("output_name"), OutputName);

	EMaterialProperty MatProp;
	if (!ParseMaterialProperty(PropertyStr, MatProp)) { OutError = FString::Printf(TEXT("Invalid property: %s"), *PropertyStr); return false; }

	UMaterial* Mat = LoadMaterialFromPath(AssetPath, OutError);
	if (!Mat) return false;
	UMaterialExpression* Expr = FindExpressionByIndex(Mat, ExprIndex, OutError);
	if (!Expr) return false;

	bool bOk = UMaterialEditingLibrary::ConnectMaterialProperty(Expr, OutputName, MatProp);
	OutResult = MakeShared<FJsonObject>();
	OutResult->SetBoolField(TEXT("success"), bOk);
	OutResult->SetStringField(TEXT("message"), bOk ? FString::Printf(TEXT("Connected %d -> %s"), ExprIndex, *PropertyStr) : TEXT("Connect failed"));
	return bOk;
}

// ==================== connect_material_expressions ====================

bool UAMaterialCommands::ExecuteConnectExpressions(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError)
{
	FString AssetPath, FromOutput, ToInput; int32 FromIdx = -1, ToIdx = -1;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath)) { OutError = TEXT("'asset_path' required"); return false; }
	if (!Params->TryGetNumberField(TEXT("from_index"), FromIdx)) { OutError = TEXT("'from_index' required"); return false; }
	if (!Params->TryGetNumberField(TEXT("to_index"), ToIdx)) { OutError = TEXT("'to_index' required"); return false; }
	Params->TryGetStringField(TEXT("from_output"), FromOutput);
	Params->TryGetStringField(TEXT("to_input"), ToInput);

	UMaterial* Mat = LoadMaterialFromPath(AssetPath, OutError);
	if (!Mat) return false;
	UMaterialExpression* FromExpr = FindExpressionByIndex(Mat, FromIdx, OutError);
	if (!FromExpr) return false;
	UMaterialExpression* ToExpr = FindExpressionByIndex(Mat, ToIdx, OutError);
	if (!ToExpr) return false;

	bool bOk = UMaterialEditingLibrary::ConnectMaterialExpressions(FromExpr, FromOutput, ToExpr, ToInput);
	OutResult = MakeShared<FJsonObject>();
	OutResult->SetBoolField(TEXT("success"), bOk);
	OutResult->SetStringField(TEXT("message"), bOk ? FString::Printf(TEXT("%d -> %d"), FromIdx, ToIdx) : TEXT("Connect failed"));
	return bOk;
}

// ==================== set_expression_value ====================

bool UAMaterialCommands::ExecuteSetExpressionValue(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError)
{
	FString AssetPath, PropName, ValueStr; int32 ExprIndex = -1;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath)) { OutError = TEXT("'asset_path' required"); return false; }
	if (!Params->TryGetNumberField(TEXT("expression_index"), ExprIndex)) { OutError = TEXT("'expression_index' required"); return false; }
	if (!Params->TryGetStringField(TEXT("property_name"), PropName)) { OutError = TEXT("'property_name' required"); return false; }
	if (!Params->TryGetStringField(TEXT("value"), ValueStr)) { OutError = TEXT("'value' required"); return false; }

	UMaterial* Mat = LoadMaterialFromPath(AssetPath, OutError);
	if (!Mat) return false;
	UMaterialExpression* Expr = FindExpressionByIndex(Mat, ExprIndex, OutError);
	if (!Expr) return false;

	bool bSuccess = false;

	// Special: UMaterialExpressionCustom properties
	if (auto* CustomExpr = Cast<UMaterialExpressionCustom>(Expr))
	{
		if (PropName == TEXT("Code"))
		{
			CustomExpr->Code = ValueStr;
			bSuccess = true;
		}
		else if (PropName == TEXT("Description"))
		{
			CustomExpr->Description = ValueStr;
			bSuccess = true;
		}
		else if (PropName == TEXT("OutputType"))
		{
			// CMOT_Float1..CMOT_Float4, CMOT_MaterialAttributes
			static TMap<FString, ECustomMaterialOutputType> OT = {
				{TEXT("CMOT_Float1"), CMOT_Float1}, {TEXT("CMOT_Float2"), CMOT_Float2},
				{TEXT("CMOT_Float3"), CMOT_Float3}, {TEXT("CMOT_Float4"), CMOT_Float4},
				{TEXT("CMOT_MaterialAttributes"), CMOT_MaterialAttributes},
			};
			if (auto* Found = OT.Find(ValueStr)) { CustomExpr->OutputType = *Found; bSuccess = true; }
			else { OutError = FString::Printf(TEXT("Invalid OutputType: %s"), *ValueStr); return false; }
		}
		else if (PropName.StartsWith(TEXT("InputName")))
		{
			// InputName or InputName[N] — 设置 Custom 节点的输入名称
			int32 InputIdx = 0;
			if (PropName.Contains(TEXT("[")))
			{
				FString IdxStr;
				PropName.Split(TEXT("["), nullptr, &IdxStr);
				IdxStr.Split(TEXT("]"), &IdxStr, nullptr);
				InputIdx = FCString::Atoi(*IdxStr);
			}
			// 自动扩展 Inputs 数组（如果索引超出范围）
			while (InputIdx >= CustomExpr->Inputs.Num())
			{
				FCustomInput NewInput;
				NewInput.InputName = FName(TEXT("Input"));
				CustomExpr->Inputs.Add(NewInput);
			}
			if (InputIdx >= 0 && InputIdx < CustomExpr->Inputs.Num())
			{
				CustomExpr->Inputs[InputIdx].InputName = FName(*ValueStr);
				bSuccess = true;
			}
			else { OutError = FString::Printf(TEXT("Input index %d out of range [0,%d)"), InputIdx, CustomExpr->Inputs.Num()); return false; }
		}
		// 如果不是 Custom 专属属性，继续走通用逻辑
		if (bSuccess)
		{
			OutResult = MakeShared<FJsonObject>();
			OutResult->SetBoolField(TEXT("success"), true);
			OutResult->SetStringField(TEXT("message"), FString::Printf(TEXT("Set %s = %s on CustomExpression %d"), *PropName, *ValueStr, ExprIndex));
			return true;
		}
	}

	// Special: texture_path for TextureSample
	if (PropName == TEXT("texture_path"))
	{
		UMaterialExpressionTextureSample* TS = Cast<UMaterialExpressionTextureSample>(Expr);
		if (!TS) { OutError = TEXT("Not a TextureSample"); return false; }
		UTexture* Tex = Cast<UTexture>(StaticLoadObject(UTexture::StaticClass(), nullptr, *ValueStr));
		if (!Tex) { OutError = FString::Printf(TEXT("Texture not found: %s"), *ValueStr); return false; }
		TS->Texture = Tex;
		bSuccess = true;
	}
	// Color JSON {r,g,b,a}
	else if (ValueStr.StartsWith(TEXT("{")))
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ValueStr);
		TSharedPtr<FJsonObject> ColorJson;
		if (FJsonSerializer::Deserialize(Reader, ColorJson) && ColorJson.IsValid())
		{
			FLinearColor Color(
				ColorJson->GetNumberField(TEXT("r")),
				ColorJson->GetNumberField(TEXT("g")),
				ColorJson->GetNumberField(TEXT("b")),
				ColorJson->HasField(TEXT("a")) ? ColorJson->GetNumberField(TEXT("a")) : 1.0f);
			if (auto* C3 = Cast<UMaterialExpressionConstant3Vector>(Expr)) { C3->Constant = Color; bSuccess = true; }
			else if (auto* C4 = Cast<UMaterialExpressionConstant4Vector>(Expr)) { C4->Constant = Color; bSuccess = true; }
			else if (auto* VP = Cast<UMaterialExpressionVectorParameter>(Expr)) { VP->DefaultValue = Color; bSuccess = true; }
			else { OutError = TEXT("Expression does not support color values"); return false; }
		}
	}
	else if (FCString::IsNumeric(*ValueStr))
	{
		float FloatVal = FCString::Atof(*ValueStr);
		if (auto* C1 = Cast<UMaterialExpressionConstant>(Expr)) { C1->R = FloatVal; bSuccess = true; }
		else if (auto* SP = Cast<UMaterialExpressionScalarParameter>(Expr)) { SP->DefaultValue = FloatVal; bSuccess = true; }
		else
		{
			FProperty* Prop = Expr->GetClass()->FindPropertyByName(FName(*PropName));
			if (Prop)
			{
				if (FFloatProperty* FP = CastField<FFloatProperty>(Prop)) { FP->SetPropertyValue_InContainer(Expr, FloatVal); bSuccess = true; }
				else if (FDoubleProperty* DP = CastField<FDoubleProperty>(Prop)) { DP->SetPropertyValue_InContainer(Expr, (double)FloatVal); bSuccess = true; }
			}
		}
	}
	else
	{
		// String value - set FName or FString property
		FProperty* Prop = Expr->GetClass()->FindPropertyByName(FName(*PropName));
		if (Prop)
		{
			if (FNameProperty* NP = CastField<FNameProperty>(Prop)) { NP->SetPropertyValue_InContainer(Expr, FName(*ValueStr)); bSuccess = true; }
			else if (FStrProperty* StrP = CastField<FStrProperty>(Prop)) { StrP->SetPropertyValue_InContainer(Expr, ValueStr); bSuccess = true; }
		}
	}

	if (!bSuccess && OutError.IsEmpty()) { OutError = FString::Printf(TEXT("Failed to set '%s' on %s"), *PropName, *Expr->GetClass()->GetName()); return false; }

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetBoolField(TEXT("success"), true);
	OutResult->SetStringField(TEXT("message"), FString::Printf(TEXT("Set %s = %s on expression %d"), *PropName, *ValueStr, ExprIndex));
	return true;
}

// ==================== recompile_material ====================

bool UAMaterialCommands::ExecuteRecompileMaterial(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath)) { OutError = TEXT("'asset_path' required"); return false; }
	UMaterial* Mat = LoadMaterialFromPath(AssetPath, OutError);
	if (!Mat) return false;

	UMaterialEditingLibrary::RecompileMaterial(Mat);

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetBoolField(TEXT("success"), true);
	OutResult->SetStringField(TEXT("message"), FString::Printf(TEXT("Recompiled: %s"), *AssetPath));
	return true;
}

// ==================== layout_material_expressions ====================

bool UAMaterialCommands::ExecuteLayoutExpressions(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath)) { OutError = TEXT("'asset_path' required"); return false; }
	UMaterial* Mat = LoadMaterialFromPath(AssetPath, OutError);
	if (!Mat) return false;

	UMaterialEditingLibrary::LayoutMaterialExpressions(Mat);

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetBoolField(TEXT("success"), true);
	OutResult->SetStringField(TEXT("message"), TEXT("Layout applied"));
	return true;
}

// ==================== get_material_parameters ====================

bool UAMaterialCommands::ExecuteGetMaterialParameters(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath)) { OutError = TEXT("'asset_path' required"); return false; }

	UMaterialInterface* MI = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *AssetPath));
	if (!MI) { OutError = FString::Printf(TEXT("MaterialInterface not found: %s"), *AssetPath); return false; }

	TArray<FName> ScalarNames, VectorNames, TextureNames, SwitchNames;
	UMaterialEditingLibrary::GetScalarParameterNames(MI, ScalarNames);
	UMaterialEditingLibrary::GetVectorParameterNames(MI, VectorNames);
	UMaterialEditingLibrary::GetTextureParameterNames(MI, TextureNames);
	UMaterialEditingLibrary::GetStaticSwitchParameterNames(MI, SwitchNames);

	auto ToArr = [](const TArray<FName>& Names) {
		TArray<TSharedPtr<FJsonValue>> A;
		for (const FName& N : Names) A.Add(MakeShared<FJsonValueString>(N.ToString()));
		return A;
	};

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetArrayField(TEXT("scalar_parameters"), ToArr(ScalarNames));
	OutResult->SetArrayField(TEXT("vector_parameters"), ToArr(VectorNames));
	OutResult->SetArrayField(TEXT("texture_parameters"), ToArr(TextureNames));
	OutResult->SetArrayField(TEXT("static_switch_parameters"), ToArr(SwitchNames));
	return true;
}

// ==================== set_material_instance_param ====================

bool UAMaterialCommands::ExecuteSetInstanceParam(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError)
{
	FString AssetPath, ParamName, ParamType, ValueStr;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath)) { OutError = TEXT("'asset_path' required"); return false; }
	if (!Params->TryGetStringField(TEXT("param_name"), ParamName)) { OutError = TEXT("'param_name' required"); return false; }
	if (!Params->TryGetStringField(TEXT("param_type"), ParamType)) { OutError = TEXT("'param_type' required"); return false; }
	if (!Params->TryGetStringField(TEXT("value"), ValueStr)) { OutError = TEXT("'value' required"); return false; }

	UMaterialInstanceConstant* MIC = LoadMaterialInstanceFromPath(AssetPath, OutError);
	if (!MIC) return false;

	bool bOk = false;
	FName FPN(*ParamName);

	// BUG-002/007 修复：先验证参数是否存在于父材质中
	// UMaterialEditingLibrary 的 Set*ParameterValue 在参数不存在时直接返回 false 且无信息
	UMaterialInterface* ParentMat = MIC->Parent;
	if (!ParentMat)
	{
		OutError = FString::Printf(TEXT("MaterialInstance '%s' has no parent material"), *AssetPath);
		return false;
	}

	// 检查参数是否在父材质中定义
	if (ParamType == TEXT("scalar"))
	{
		TArray<FName> ScalarNames;
		UMaterialEditingLibrary::GetScalarParameterNames(ParentMat, ScalarNames);
		if (!ScalarNames.Contains(FPN))
		{
			// 构建可用参数列表
			FString AvailableParams;
			for (const FName& N : ScalarNames) { if (!AvailableParams.IsEmpty()) AvailableParams += TEXT(", "); AvailableParams += N.ToString(); }
			OutError = FString::Printf(TEXT("Scalar parameter '%s' not found in parent material. Available scalar parameters: [%s]"), *ParamName, *AvailableParams);
			return false;
		}
		bOk = UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(MIC, FPN, FCString::Atof(*ValueStr));
	}
	else if (ParamType == TEXT("vector"))
	{
		TArray<FName> VectorNames;
		UMaterialEditingLibrary::GetVectorParameterNames(ParentMat, VectorNames);
		if (!VectorNames.Contains(FPN))
		{
			FString AvailableParams;
			for (const FName& N : VectorNames) { if (!AvailableParams.IsEmpty()) AvailableParams += TEXT(", "); AvailableParams += N.ToString(); }
			OutError = FString::Printf(TEXT("Vector parameter '%s' not found in parent material. Available vector parameters: [%s]"), *ParamName, *AvailableParams);
			return false;
		}
		TSharedRef<TJsonReader<>> Rd = TJsonReaderFactory<>::Create(ValueStr);
		TSharedPtr<FJsonObject> CJ;
		if (FJsonSerializer::Deserialize(Rd, CJ) && CJ.IsValid())
		{
			FLinearColor C(CJ->GetNumberField(TEXT("r")), CJ->GetNumberField(TEXT("g")), CJ->GetNumberField(TEXT("b")),
				CJ->HasField(TEXT("a")) ? CJ->GetNumberField(TEXT("a")) : 1.0f);
			bOk = UMaterialEditingLibrary::SetMaterialInstanceVectorParameterValue(MIC, FPN, C);
		}
		else { OutError = TEXT("Invalid vector JSON. Expected: {\"r\":1.0,\"g\":0.5,\"b\":0.0}"); return false; }
	}
	else if (ParamType == TEXT("texture"))
	{
		TArray<FName> TextureNames;
		UMaterialEditingLibrary::GetTextureParameterNames(ParentMat, TextureNames);
		if (!TextureNames.Contains(FPN))
		{
			FString AvailableParams;
			for (const FName& N : TextureNames) { if (!AvailableParams.IsEmpty()) AvailableParams += TEXT(", "); AvailableParams += N.ToString(); }
			OutError = FString::Printf(TEXT("Texture parameter '%s' not found in parent material. Available texture parameters: [%s]"), *ParamName, *AvailableParams);
			return false;
		}
		UTexture* Tex = Cast<UTexture>(StaticLoadObject(UTexture::StaticClass(), nullptr, *ValueStr));
		if (!Tex) { OutError = FString::Printf(TEXT("Texture not found: %s"), *ValueStr); return false; }
		bOk = UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(MIC, FPN, Tex);
	}
	else if (ParamType == TEXT("static_switch"))
	{
		TArray<FName> SwitchNames;
		UMaterialEditingLibrary::GetStaticSwitchParameterNames(ParentMat, SwitchNames);
		if (!SwitchNames.Contains(FPN))
		{
			FString AvailableParams;
			for (const FName& N : SwitchNames) { if (!AvailableParams.IsEmpty()) AvailableParams += TEXT(", "); AvailableParams += N.ToString(); }
			OutError = FString::Printf(TEXT("Static switch parameter '%s' not found in parent material. Available static_switch parameters: [%s]"), *ParamName, *AvailableParams);
			return false;
		}
		bOk = UMaterialEditingLibrary::SetMaterialInstanceStaticSwitchParameterValue(MIC, FPN, ValueStr.ToBool());
	}
	else { OutError = FString::Printf(TEXT("Unknown param_type: '%s'. Valid types: scalar, vector, texture, static_switch"), *ParamType); return false; }

	if (!bOk)
	{
		OutError = FString::Printf(TEXT("UE API failed to set %s parameter '%s' on '%s'. The parameter exists in the parent material but SetMaterialInstance*ParameterValue returned false. This may be an engine limitation."), *ParamType, *ParamName, *AssetPath);
		return false;
	}

	UMaterialEditingLibrary::UpdateMaterialInstance(MIC);

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetBoolField(TEXT("success"), true);
	OutResult->SetStringField(TEXT("message"), FString::Printf(TEXT("Set %s.%s = %s"), *ParamType, *ParamName, *ValueStr));
	return true;
}

// ==================== set_material_property ====================

bool UAMaterialCommands::ExecuteSetMaterialProperty(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError)
{
	FString AssetPath, PropName, ValueStr;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath)) { OutError = TEXT("'asset_path' required"); return false; }
	if (!Params->TryGetStringField(TEXT("property_name"), PropName)) { OutError = TEXT("'property_name' required"); return false; }
	if (!Params->TryGetStringField(TEXT("value"), ValueStr)) { OutError = TEXT("'value' required"); return false; }

	UMaterial* Mat = LoadMaterialFromPath(AssetPath, OutError);
	if (!Mat) return false;

	bool bOk = false;
	if (PropName == TEXT("BlendMode"))
	{
		static TMap<FString, EBlendMode> BM = {
			{TEXT("Opaque"), BLEND_Opaque}, {TEXT("Masked"), BLEND_Masked},
			{TEXT("Translucent"), BLEND_Translucent}, {TEXT("Additive"), BLEND_Additive}, {TEXT("Modulate"), BLEND_Modulate},
		};
		if (auto* M = BM.Find(ValueStr)) { Mat->BlendMode = *M; bOk = true; }
		else { OutError = FString::Printf(TEXT("Invalid BlendMode: %s"), *ValueStr); return false; }
	}
	else if (PropName == TEXT("ShadingModel"))
	{
		static TMap<FString, EMaterialShadingModel> SM = {
			{TEXT("DefaultLit"), MSM_DefaultLit}, {TEXT("Unlit"), MSM_Unlit},
			{TEXT("Subsurface"), MSM_Subsurface}, {TEXT("ClearCoat"), MSM_ClearCoat},
		};
		if (auto* M = SM.Find(ValueStr)) { Mat->SetShadingModel(*M); bOk = true; }
		else { OutError = FString::Printf(TEXT("Invalid ShadingModel: %s"), *ValueStr); return false; }
	}
	else if (PropName == TEXT("TwoSided")) { Mat->TwoSided = ValueStr.ToBool(); bOk = true; }
	else if (PropName == TEXT("OpacityMaskClipValue")) { Mat->OpacityMaskClipValue = FCString::Atof(*ValueStr); bOk = true; }
	else { OutError = FString::Printf(TEXT("Unknown property: %s"), *PropName); return false; }

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetBoolField(TEXT("success"), bOk);
	OutResult->SetStringField(TEXT("message"), FString::Printf(TEXT("Set %s = %s"), *PropName, *ValueStr));
	return true;
}
