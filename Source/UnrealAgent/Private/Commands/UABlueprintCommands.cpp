// Copyright KuoYu. All Rights Reserved.

#include "Commands/UABlueprintCommands.h"
#include "UnrealAgent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_MacroInstance.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"

DEFINE_LOG_CATEGORY_STATIC(LogUABlueprint, Log, All);

// ==================== Schema Helpers ====================

namespace UABlueprintHelper
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

TArray<FString> UABlueprintCommands::GetSupportedMethods() const
{
	return {
		TEXT("get_blueprint_overview"),
		TEXT("get_blueprint_graph"),
		TEXT("get_blueprint_variables"),
		TEXT("get_blueprint_functions"),
		TEXT("add_node"),
		TEXT("delete_node"),
		TEXT("connect_pins"),
		TEXT("disconnect_pin"),
		TEXT("add_variable"),
		TEXT("add_function"),
		TEXT("compile_blueprint"),
	};
}

// ==================== GetToolSchema ====================

TSharedPtr<FJsonObject> UABlueprintCommands::GetToolSchema(const FString& MethodName) const
{
	using namespace UABlueprintHelper;

	if (MethodName == TEXT("get_blueprint_overview"))
	{
		return MakeToolSchema(TEXT("get_blueprint_overview"),
			TEXT("获取蓝图概览：父类、图列表、变量、事件、接口、编译状态。用于快速了解蓝图结构。"),
			MakeInputSchema(
				{{TEXT("asset_path"), MakeProp(TEXT("string"), TEXT("蓝图资产路径，如 /Game/Blueprints/BP_Player"))}},
				{TEXT("asset_path")}
			));
	}
	if (MethodName == TEXT("get_blueprint_graph"))
	{
		return MakeToolSchema(TEXT("get_blueprint_graph"),
			TEXT("获取蓝图节点图详情：所有节点、引脚、连接关系。类似 get_material_graph 的蓝图版本。"),
			MakeInputSchema({
				{TEXT("asset_path"), MakeProp(TEXT("string"), TEXT("蓝图资产路径"))},
				{TEXT("graph_name"), MakeProp(TEXT("string"), TEXT("图名称，如 EventGraph。留空默认 EventGraph"))},
			}, {TEXT("asset_path")}));
	}
	if (MethodName == TEXT("get_blueprint_variables"))
	{
		return MakeToolSchema(TEXT("get_blueprint_variables"),
			TEXT("获取蓝图的所有变量定义列表（名称、类型、默认值、是否公开、是否复制）"),
			MakeInputSchema(
				{{TEXT("asset_path"), MakeProp(TEXT("string"), TEXT("蓝图资产路径"))}},
				{TEXT("asset_path")}
			));
	}
	if (MethodName == TEXT("get_blueprint_functions"))
	{
		return MakeToolSchema(TEXT("get_blueprint_functions"),
			TEXT("获取蓝图自定义函数签名列表（函数名、输入输出参数、是否纯函数）"),
			MakeInputSchema(
				{{TEXT("asset_path"), MakeProp(TEXT("string"), TEXT("蓝图资产路径"))}},
				{TEXT("asset_path")}
			));
	}
	if (MethodName == TEXT("add_node"))
	{
		return MakeToolSchema(TEXT("add_node"),
			TEXT("在蓝图图中添加节点。支持的 node_class: CallFunction, Event, CustomEvent, IfThenElse, VariableGet, VariableSet, MacroInstance"),
			MakeInputSchema({
				{TEXT("asset_path"), MakeProp(TEXT("string"), TEXT("蓝图资产路径"))},
				{TEXT("graph_name"), MakeProp(TEXT("string"), TEXT("图名称，留空默认 EventGraph"))},
				{TEXT("node_class"), MakeProp(TEXT("string"), TEXT("节点类型: CallFunction, Event, CustomEvent, IfThenElse, VariableGet, VariableSet, MacroInstance"))},
				{TEXT("function_name"), MakeProp(TEXT("string"), TEXT("CallFunction 时的函数名 (如 PrintString)"))},
				{TEXT("target_class"), MakeProp(TEXT("string"), TEXT("CallFunction 时的目标类 (如 KismetSystemLibrary)，留空自动查找"))},
				{TEXT("event_name"), MakeProp(TEXT("string"), TEXT("Event/CustomEvent 的事件名"))},
				{TEXT("variable_name"), MakeProp(TEXT("string"), TEXT("VariableGet/Set 的变量名"))},
				{TEXT("macro_path"), MakeProp(TEXT("string"), TEXT("MacroInstance 的宏资产路径"))},
				{TEXT("node_pos_x"), MakeProp(TEXT("integer"), TEXT("节点 X 位置，默认 0"))},
				{TEXT("node_pos_y"), MakeProp(TEXT("integer"), TEXT("节点 Y 位置，默认 0"))},
			}, {TEXT("asset_path"), TEXT("node_class")}));
	}
	if (MethodName == TEXT("delete_node"))
	{
		return MakeToolSchema(TEXT("delete_node"),
			TEXT("删除蓝图图中指定索引的节点"),
			MakeInputSchema({
				{TEXT("asset_path"), MakeProp(TEXT("string"), TEXT("蓝图资产路径"))},
				{TEXT("graph_name"), MakeProp(TEXT("string"), TEXT("图名称，留空默认 EventGraph"))},
				{TEXT("node_index"), MakeProp(TEXT("integer"), TEXT("要删除的节点索引（来自 get_blueprint_graph）"))},
			}, {TEXT("asset_path"), TEXT("node_index")}));
	}
	if (MethodName == TEXT("connect_pins"))
	{
		return MakeToolSchema(TEXT("connect_pins"),
			TEXT("连接蓝图中两个节点的引脚"),
			MakeInputSchema({
				{TEXT("asset_path"), MakeProp(TEXT("string"), TEXT("蓝图资产路径"))},
				{TEXT("graph_name"), MakeProp(TEXT("string"), TEXT("图名称，留空默认 EventGraph"))},
				{TEXT("from_node_index"), MakeProp(TEXT("integer"), TEXT("源节点索引"))},
				{TEXT("from_pin"), MakeProp(TEXT("string"), TEXT("源节点输出引脚名称"))},
				{TEXT("to_node_index"), MakeProp(TEXT("integer"), TEXT("目标节点索引"))},
				{TEXT("to_pin"), MakeProp(TEXT("string"), TEXT("目标节点输入引脚名称"))},
			}, {TEXT("asset_path"), TEXT("from_node_index"), TEXT("from_pin"), TEXT("to_node_index"), TEXT("to_pin")}));
	}
	if (MethodName == TEXT("disconnect_pin"))
	{
		return MakeToolSchema(TEXT("disconnect_pin"),
			TEXT("断开蓝图节点某个引脚的所有连接"),
			MakeInputSchema({
				{TEXT("asset_path"), MakeProp(TEXT("string"), TEXT("蓝图资产路径"))},
				{TEXT("graph_name"), MakeProp(TEXT("string"), TEXT("图名称，留空默认 EventGraph"))},
				{TEXT("node_index"), MakeProp(TEXT("integer"), TEXT("节点索引"))},
				{TEXT("pin_name"), MakeProp(TEXT("string"), TEXT("要断开连接的引脚名称"))},
			}, {TEXT("asset_path"), TEXT("node_index"), TEXT("pin_name")}));
	}
	if (MethodName == TEXT("add_variable"))
	{
		return MakeToolSchema(TEXT("add_variable"),
			TEXT("向蓝图添加新变量"),
			MakeInputSchema({
				{TEXT("asset_path"), MakeProp(TEXT("string"), TEXT("蓝图资产路径"))},
				{TEXT("variable_name"), MakeProp(TEXT("string"), TEXT("变量名"))},
				{TEXT("variable_type"), MakeProp(TEXT("string"), TEXT("类型: bool, int, float, string, vector, rotator, transform, text, name, object"))},
				{TEXT("default_value"), MakeProp(TEXT("string"), TEXT("默认值字符串，可选"))},
				{TEXT("category"), MakeProp(TEXT("string"), TEXT("变量分类，可选"))},
				{TEXT("is_exposed"), MakeProp(TEXT("boolean"), TEXT("是否公开为 Instance Editable，默认 false"))},
			}, {TEXT("asset_path"), TEXT("variable_name"), TEXT("variable_type")}));
	}
	if (MethodName == TEXT("add_function"))
	{
		return MakeToolSchema(TEXT("add_function"),
			TEXT("向蓝图添加新的自定义函数"),
			MakeInputSchema({
				{TEXT("asset_path"), MakeProp(TEXT("string"), TEXT("蓝图资产路径"))},
				{TEXT("function_name"), MakeProp(TEXT("string"), TEXT("函数名"))},
				{TEXT("is_pure"), MakeProp(TEXT("boolean"), TEXT("是否为纯函数，默认 false"))},
			}, {TEXT("asset_path"), TEXT("function_name")}));
	}
	if (MethodName == TEXT("compile_blueprint"))
	{
		return MakeToolSchema(TEXT("compile_blueprint"),
			TEXT("编译蓝图并返回编译结果（状态、错误列表、警告列表）。用于操作后的验证闭环。"),
			MakeInputSchema(
				{{TEXT("asset_path"), MakeProp(TEXT("string"), TEXT("蓝图资产路径"))}},
				{TEXT("asset_path")}
			));
	}

	return nullptr;
}

// ==================== Execute dispatcher ====================

bool UABlueprintCommands::Execute(
	const FString& MethodName,
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	if (MethodName == TEXT("get_blueprint_overview"))    return ExecuteGetBlueprintOverview(Params, OutResult, OutError);
	if (MethodName == TEXT("get_blueprint_graph"))       return ExecuteGetBlueprintGraph(Params, OutResult, OutError);
	if (MethodName == TEXT("get_blueprint_variables"))   return ExecuteGetBlueprintVariables(Params, OutResult, OutError);
	if (MethodName == TEXT("get_blueprint_functions"))   return ExecuteGetBlueprintFunctions(Params, OutResult, OutError);
	if (MethodName == TEXT("add_node"))                  return ExecuteAddNode(Params, OutResult, OutError);
	if (MethodName == TEXT("delete_node"))               return ExecuteDeleteNode(Params, OutResult, OutError);
	if (MethodName == TEXT("connect_pins"))              return ExecuteConnectPins(Params, OutResult, OutError);
	if (MethodName == TEXT("disconnect_pin"))            return ExecuteDisconnectPin(Params, OutResult, OutError);
	if (MethodName == TEXT("add_variable"))              return ExecuteAddVariable(Params, OutResult, OutError);
	if (MethodName == TEXT("add_function"))              return ExecuteAddFunction(Params, OutResult, OutError);
	if (MethodName == TEXT("compile_blueprint"))         return ExecuteCompileBlueprint(Params, OutResult, OutError);

	OutError = FString::Printf(TEXT("Unknown method: %s"), *MethodName);
	return false;
}

// ==================== Helpers ====================

UBlueprint* UABlueprintCommands::LoadBlueprintFromPath(const FString& AssetPath, FString& OutError)
{
	UObject* Obj = StaticLoadObject(UBlueprint::StaticClass(), nullptr, *AssetPath);
	UBlueprint* BP = Obj ? Cast<UBlueprint>(Obj) : nullptr;
	if (!BP) { OutError = FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath); }
	return BP;
}

UEdGraph* UABlueprintCommands::FindGraphByName(UBlueprint* Blueprint, const FString& GraphName, FString& OutError)
{
	FString TargetName = GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName;

	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);

	for (UEdGraph* Graph : AllGraphs)
	{
		if (Graph && Graph->GetName() == TargetName)
		{
			return Graph;
		}
	}

	OutError = FString::Printf(TEXT("Graph '%s' not found in blueprint %s"), *TargetName, *Blueprint->GetName());
	return nullptr;
}

UEdGraphNode* UABlueprintCommands::FindNodeByIndex(UEdGraph* Graph, int32 Index, FString& OutError)
{
	if (Index < 0 || Index >= Graph->Nodes.Num())
	{
		OutError = FString::Printf(TEXT("Node index %d out of range [0, %d)"), Index, Graph->Nodes.Num());
		return nullptr;
	}
	return Graph->Nodes[Index];
}

UEdGraphPin* UABlueprintCommands::FindPinByName(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection Direction, FString& OutError)
{
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->PinName.ToString() == PinName && Pin->Direction == Direction)
		{
			return Pin;
		}
	}

	// 如果指定方向找不到，尝试不限方向
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->PinName.ToString() == PinName)
		{
			return Pin;
		}
	}

	OutError = FString::Printf(TEXT("Pin '%s' not found on node %s"), *PinName, *Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
	return nullptr;
}

FString UABlueprintCommands::PinTypeToString(const FEdGraphPinType& PinType)
{
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) return TEXT("exec");
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean) return TEXT("bool");
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Int) return TEXT("int");
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Int64) return TEXT("int64");
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Real)
	{
		if (PinType.PinSubCategory == UEdGraphSchema_K2::PC_Float) return TEXT("float");
		if (PinType.PinSubCategory == UEdGraphSchema_K2::PC_Double) return TEXT("double");
		return TEXT("real");
	}
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_String) return TEXT("string");
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Text) return TEXT("text");
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Name) return TEXT("name");
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
	{
		if (UScriptStruct* Struct = Cast<UScriptStruct>(PinType.PinSubCategoryObject.Get()))
		{
			return Struct->GetName();
		}
		return TEXT("struct");
	}
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Object || PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject)
	{
		if (UClass* ObjClass = Cast<UClass>(PinType.PinSubCategoryObject.Get()))
		{
			return ObjClass->GetName();
		}
		return TEXT("object");
	}
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
	{
		if (UEnum* PinEnum = Cast<UEnum>(PinType.PinSubCategoryObject.Get()))
		{
			return PinEnum->GetName();
		}
		return TEXT("byte");
	}
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard) return TEXT("wildcard");
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Delegate) return TEXT("delegate");

	return PinType.PinCategory.ToString();
}

bool UABlueprintCommands::StringToPinType(const FString& TypeString, FEdGraphPinType& OutPinType)
{
	FString Lower = TypeString.ToLower();

	if (Lower == TEXT("bool"))      { OutPinType.PinCategory = UEdGraphSchema_K2::PC_Boolean; return true; }
	if (Lower == TEXT("int"))       { OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int; return true; }
	if (Lower == TEXT("int64"))     { OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int64; return true; }
	if (Lower == TEXT("float"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
		return true;
	}
	if (Lower == TEXT("double"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
		return true;
	}
	if (Lower == TEXT("string"))    { OutPinType.PinCategory = UEdGraphSchema_K2::PC_String; return true; }
	if (Lower == TEXT("text"))      { OutPinType.PinCategory = UEdGraphSchema_K2::PC_Text; return true; }
	if (Lower == TEXT("name"))      { OutPinType.PinCategory = UEdGraphSchema_K2::PC_Name; return true; }
	if (Lower == TEXT("byte"))      { OutPinType.PinCategory = UEdGraphSchema_K2::PC_Byte; return true; }

	// 常用结构体
	if (Lower == TEXT("vector"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
		return true;
	}
	if (Lower == TEXT("rotator"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
		return true;
	}
	if (Lower == TEXT("transform"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
		return true;
	}
	if (Lower == TEXT("linearcolor") || Lower == TEXT("color"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FLinearColor>::Get();
		return true;
	}

	// 通用对象引用
	if (Lower == TEXT("object"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Object;
		OutPinType.PinSubCategoryObject = UObject::StaticClass();
		return true;
	}

	return false;
}

TSharedPtr<FJsonObject> UABlueprintCommands::PinToJson(UEdGraphPin* Pin, const TMap<UEdGraphNode*, int32>& NodeIndexMap)
{
	auto PinObj = MakeShared<FJsonObject>();
	PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
	PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
	PinObj->SetStringField(TEXT("type"), PinTypeToString(Pin->PinType));
	PinObj->SetBoolField(TEXT("is_connected"), Pin->LinkedTo.Num() > 0);

	// 默认值
	if (!Pin->DefaultValue.IsEmpty())
	{
		PinObj->SetStringField(TEXT("default_value"), Pin->DefaultValue);
	}

	// 连接信息
	if (Pin->LinkedTo.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Connections;
		for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
		{
			if (!LinkedPin || !LinkedPin->GetOwningNode()) continue;
			auto ConnObj = MakeShared<FJsonObject>();
			const int32* FoundIdx = NodeIndexMap.Find(LinkedPin->GetOwningNode());
			ConnObj->SetNumberField(TEXT("node_index"), FoundIdx ? *FoundIdx : -1);
			ConnObj->SetStringField(TEXT("pin_name"), LinkedPin->PinName.ToString());
			Connections.Add(MakeShared<FJsonValueObject>(ConnObj));
		}
		PinObj->SetArrayField(TEXT("connected_to"), Connections);
	}

	return PinObj;
}

TSharedPtr<FJsonObject> UABlueprintCommands::NodeToJson(UEdGraphNode* Node, int32 Index, const TMap<UEdGraphNode*, int32>& NodeIndexMap)
{
	auto NodeObj = MakeShared<FJsonObject>();
	NodeObj->SetNumberField(TEXT("index"), Index);
	NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
	NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
	NodeObj->SetNumberField(TEXT("node_pos_x"), Node->NodePosX);
	NodeObj->SetNumberField(TEXT("node_pos_y"), Node->NodePosY);

	// 针对特定节点类型添加额外信息
	if (UK2Node_CallFunction* FuncNode = Cast<UK2Node_CallFunction>(Node))
	{
		NodeObj->SetStringField(TEXT("function_name"), FuncNode->FunctionReference.GetMemberName().ToString());
		if (UClass* MemberParent = FuncNode->FunctionReference.GetMemberParentClass())
		{
			NodeObj->SetStringField(TEXT("target_class"), MemberParent->GetName());
		}
	}
	else if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
	{
		NodeObj->SetStringField(TEXT("event_name"), EventNode->GetFunctionName().ToString());
	}
	else if (UK2Node_CustomEvent* CustomEvent = Cast<UK2Node_CustomEvent>(Node))
	{
		NodeObj->SetStringField(TEXT("event_name"), CustomEvent->CustomFunctionName.ToString());
	}
	else if (UK2Node_VariableGet* VarGet = Cast<UK2Node_VariableGet>(Node))
	{
		NodeObj->SetStringField(TEXT("variable_name"), VarGet->GetVarName().ToString());
	}
	else if (UK2Node_VariableSet* VarSet = Cast<UK2Node_VariableSet>(Node))
	{
		NodeObj->SetStringField(TEXT("variable_name"), VarSet->GetVarName().ToString());
	}

	// 引脚列表
	TArray<TSharedPtr<FJsonValue>> PinArr;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin->bHidden) continue;
		PinArr.Add(MakeShared<FJsonValueObject>(PinToJson(Pin, NodeIndexMap)));
	}
	NodeObj->SetArrayField(TEXT("pins"), PinArr);

	return NodeObj;
}

// ==================== get_blueprint_overview ====================

bool UABlueprintCommands::ExecuteGetBlueprintOverview(
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath)) { OutError = TEXT("'asset_path' is required"); return false; }

	UBlueprint* BP = LoadBlueprintFromPath(AssetPath, OutError);
	if (!BP) return false;

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetStringField(TEXT("name"), BP->GetName());
	OutResult->SetStringField(TEXT("asset_path"), AssetPath);

	// 父类
	if (BP->ParentClass)
	{
		OutResult->SetStringField(TEXT("parent_class"), BP->ParentClass->GetName());
	}

	// 蓝图类型
	OutResult->SetStringField(TEXT("blueprint_type"),
		StaticEnum<EBlueprintType>()->GetNameStringByValue(static_cast<int64>(BP->BlueprintType)));

	// 图列表
	TArray<UEdGraph*> AllGraphs;
	BP->GetAllGraphs(AllGraphs);
	TArray<TSharedPtr<FJsonValue>> GraphArr;
	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph) continue;
		auto GObj = MakeShared<FJsonObject>();
		GObj->SetStringField(TEXT("name"), Graph->GetName());

		// 判断图类型
		FString GraphType = TEXT("unknown");
		if (FBlueprintEditorUtils::IsEventGraph(Graph))
		{
			GraphType = TEXT("event_graph");
		}
		else if (FBlueprintEditorUtils::IsDelegateSignatureGraph(Graph))
		{
			GraphType = TEXT("delegate");
		}
		else
		{
			// 函数图或宏图
			for (int32 i = 0; i < BP->FunctionGraphs.Num(); ++i)
			{
				if (BP->FunctionGraphs[i] == Graph) { GraphType = TEXT("function"); break; }
			}
			if (GraphType == TEXT("unknown"))
			{
				for (int32 i = 0; i < BP->MacroGraphs.Num(); ++i)
				{
					if (BP->MacroGraphs[i] == Graph) { GraphType = TEXT("macro"); break; }
				}
			}
		}
		GObj->SetStringField(TEXT("type"), GraphType);
		GObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
		GraphArr.Add(MakeShared<FJsonValueObject>(GObj));
	}
	OutResult->SetArrayField(TEXT("graphs"), GraphArr);

	// 变量列表（简略）
	TArray<TSharedPtr<FJsonValue>> VarArr;
	for (const FBPVariableDescription& Var : BP->NewVariables)
	{
		auto VObj = MakeShared<FJsonObject>();
		VObj->SetStringField(TEXT("name"), Var.VarName.ToString());
		VObj->SetStringField(TEXT("type"), PinTypeToString(Var.VarType));
		VObj->SetBoolField(TEXT("is_exposed"),
			Var.PropertyFlags & CPF_Edit ? true : false);
		VarArr.Add(MakeShared<FJsonValueObject>(VObj));
	}
	OutResult->SetArrayField(TEXT("variables"), VarArr);

	// 接口
	TArray<TSharedPtr<FJsonValue>> InterfaceArr;
	for (const FBPInterfaceDescription& IntDesc : BP->ImplementedInterfaces)
	{
		if (IntDesc.Interface)
		{
			InterfaceArr.Add(MakeShared<FJsonValueString>(IntDesc.Interface->GetName()));
		}
	}
	OutResult->SetArrayField(TEXT("interfaces"), InterfaceArr);

	// 编译状态
	FString StatusStr;
	switch (BP->Status)
	{
	case BS_UpToDate:           StatusStr = TEXT("UpToDate"); break;
	case BS_Dirty:              StatusStr = TEXT("Dirty"); break;
	case BS_Error:              StatusStr = TEXT("Error"); break;
	case BS_BeingCreated:       StatusStr = TEXT("BeingCreated"); break;
	default:                    StatusStr = TEXT("Unknown"); break;
	}
	OutResult->SetStringField(TEXT("compile_status"), StatusStr);

	UE_LOG(LogUABlueprint, Log, TEXT("get_blueprint_overview: %s — %d graphs, %d variables"),
		*BP->GetName(), AllGraphs.Num(), BP->NewVariables.Num());
	return true;
}

// ==================== get_blueprint_graph ====================

bool UABlueprintCommands::ExecuteGetBlueprintGraph(
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath)) { OutError = TEXT("'asset_path' is required"); return false; }

	FString GraphName;
	Params->TryGetStringField(TEXT("graph_name"), GraphName);

	UBlueprint* BP = LoadBlueprintFromPath(AssetPath, OutError);
	if (!BP) return false;

	UEdGraph* Graph = FindGraphByName(BP, GraphName, OutError);
	if (!Graph) return false;

	// Pass 1: 建立节点索引映射
	TMap<UEdGraphNode*, int32> NodeIndexMap;
	for (int32 i = 0; i < Graph->Nodes.Num(); ++i)
	{
		if (Graph->Nodes[i])
		{
			NodeIndexMap.Add(Graph->Nodes[i], i);
		}
	}

	// Pass 2: 序列化节点
	TArray<TSharedPtr<FJsonValue>> NodeArr;
	for (int32 i = 0; i < Graph->Nodes.Num(); ++i)
	{
		if (Graph->Nodes[i])
		{
			NodeArr.Add(MakeShared<FJsonValueObject>(NodeToJson(Graph->Nodes[i], i, NodeIndexMap)));
		}
	}

	// 提取连接关系（去重）
	TArray<TSharedPtr<FJsonValue>> ConnArr;
	TSet<FString> SeenConnections;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output) continue;
			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (!LinkedPin || !LinkedPin->GetOwningNode()) continue;

				const int32* FromIdx = NodeIndexMap.Find(Node);
				const int32* ToIdx = NodeIndexMap.Find(LinkedPin->GetOwningNode());
				if (!FromIdx || !ToIdx) continue;

				FString ConnKey = FString::Printf(TEXT("%d:%s->%d:%s"),
					*FromIdx, *Pin->PinName.ToString(), *ToIdx, *LinkedPin->PinName.ToString());
				if (SeenConnections.Contains(ConnKey)) continue;
				SeenConnections.Add(ConnKey);

				auto ConnObj = MakeShared<FJsonObject>();
				ConnObj->SetNumberField(TEXT("from_node"), *FromIdx);
				ConnObj->SetStringField(TEXT("from_pin"), Pin->PinName.ToString());
				ConnObj->SetNumberField(TEXT("to_node"), *ToIdx);
				ConnObj->SetStringField(TEXT("to_pin"), LinkedPin->PinName.ToString());
				ConnArr.Add(MakeShared<FJsonValueObject>(ConnObj));
			}
		}
	}

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetStringField(TEXT("graph_name"), Graph->GetName());
	OutResult->SetArrayField(TEXT("nodes"), NodeArr);
	OutResult->SetNumberField(TEXT("node_count"), NodeArr.Num());
	OutResult->SetArrayField(TEXT("connections"), ConnArr);
	OutResult->SetNumberField(TEXT("connection_count"), ConnArr.Num());

	UE_LOG(LogUABlueprint, Log, TEXT("get_blueprint_graph: %s.%s — %d nodes, %d connections"),
		*BP->GetName(), *Graph->GetName(), NodeArr.Num(), ConnArr.Num());
	return true;
}
