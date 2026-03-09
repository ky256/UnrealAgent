// Copyright KuoYu. All Rights Reserved.
// UABlueprintCommands 操作方法实现：add_node, delete_node, connect/disconnect, add_variable/function, compile

#include "Commands/UABlueprintCommands.h"
#include "UnrealAgent.h"
#include "Engine/Blueprint.h"
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
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetStringLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Actor.h"

// ==================== add_node ====================

bool UABlueprintCommands::ExecuteAddNode(
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath)) { OutError = TEXT("'asset_path' is required"); return false; }

	FString NodeClass;
	if (!Params->TryGetStringField(TEXT("node_class"), NodeClass)) { OutError = TEXT("'node_class' is required"); return false; }

	FString GraphName;
	Params->TryGetStringField(TEXT("graph_name"), GraphName);

	UBlueprint* BP = LoadBlueprintFromPath(AssetPath, OutError);
	if (!BP) return false;

	UEdGraph* Graph = FindGraphByName(BP, GraphName, OutError);
	if (!Graph) return false;

	int32 PosX = 0, PosY = 0;
	Params->TryGetNumberField(TEXT("node_pos_x"), PosX);
	Params->TryGetNumberField(TEXT("node_pos_y"), PosY);

	FScopedTransaction Transaction(FText::FromString(
		FString::Printf(TEXT("Agent: Add %s Node"), *NodeClass)));

	UEdGraphNode* NewNode = nullptr;

	if (NodeClass == TEXT("CallFunction"))
	{
		FString FunctionName;
		if (!Params->TryGetStringField(TEXT("function_name"), FunctionName))
		{
			OutError = TEXT("CallFunction requires 'function_name'");
			return false;
		}

		FString TargetClassName;
		Params->TryGetStringField(TEXT("target_class"), TargetClassName);

		// 查找函数
		UFunction* Function = nullptr;
		if (!TargetClassName.IsEmpty())
		{
			UClass* TargetClass = FindFirstObject<UClass>(*TargetClassName, EFindFirstObjectOptions::NativeFirst);
			if (!TargetClass)
			{
				// 尝试加 U 前缀
				TargetClass = FindFirstObject<UClass>(*(FString(TEXT("U")) + TargetClassName), EFindFirstObjectOptions::NativeFirst);
			}
			if (!TargetClass)
			{
				// 尝试加 A 前缀
				TargetClass = FindFirstObject<UClass>(*(FString(TEXT("A")) + TargetClassName), EFindFirstObjectOptions::NativeFirst);
			}
			if (TargetClass)
			{
				Function = TargetClass->FindFunctionByName(FName(*FunctionName));
			}
		}

		if (!Function)
		{
			// 广搜：在常用类中查找
			static TArray<UClass*> SearchClasses;
			if (SearchClasses.Num() == 0)
			{
				SearchClasses.Add(UKismetSystemLibrary::StaticClass());
				SearchClasses.Add(UKismetMathLibrary::StaticClass());
				SearchClasses.Add(UKismetStringLibrary::StaticClass());
				SearchClasses.Add(UGameplayStatics::StaticClass());
				SearchClasses.Add(AActor::StaticClass());
			}
			for (UClass* SearchClass : SearchClasses)
			{
				Function = SearchClass->FindFunctionByName(FName(*FunctionName));
				if (Function) break;
			}
		}

		if (!Function)
		{
			OutError = FString::Printf(TEXT("Function '%s' not found"), *FunctionName);
			return false;
		}

		UK2Node_CallFunction* FuncNode = NewObject<UK2Node_CallFunction>(Graph);
		FuncNode->SetFromFunction(Function);
		FuncNode->NodePosX = PosX;
		FuncNode->NodePosY = PosY;
		Graph->AddNode(FuncNode, true, false);
		FuncNode->AllocateDefaultPins();
		NewNode = FuncNode;
	}
	else if (NodeClass == TEXT("CustomEvent"))
	{
		FString EventName;
		if (!Params->TryGetStringField(TEXT("event_name"), EventName))
		{
			OutError = TEXT("CustomEvent requires 'event_name'");
			return false;
		}

		UK2Node_CustomEvent* EventNode = NewObject<UK2Node_CustomEvent>(Graph);
		EventNode->CustomFunctionName = FName(*EventName);
		EventNode->NodePosX = PosX;
		EventNode->NodePosY = PosY;
		Graph->AddNode(EventNode, true, false);
		EventNode->AllocateDefaultPins();
		NewNode = EventNode;
	}
	else if (NodeClass == TEXT("IfThenElse"))
	{
		UK2Node_IfThenElse* BranchNode = NewObject<UK2Node_IfThenElse>(Graph);
		BranchNode->NodePosX = PosX;
		BranchNode->NodePosY = PosY;
		Graph->AddNode(BranchNode, true, false);
		BranchNode->AllocateDefaultPins();
		NewNode = BranchNode;
	}
	else if (NodeClass == TEXT("VariableGet"))
	{
		FString VarName;
		if (!Params->TryGetStringField(TEXT("variable_name"), VarName))
		{
			OutError = TEXT("VariableGet requires 'variable_name'");
			return false;
		}

		UK2Node_VariableGet* VarGetNode = NewObject<UK2Node_VariableGet>(Graph);
		VarGetNode->VariableReference.SetSelfMember(FName(*VarName));
		VarGetNode->NodePosX = PosX;
		VarGetNode->NodePosY = PosY;
		Graph->AddNode(VarGetNode, true, false);
		VarGetNode->AllocateDefaultPins();
		NewNode = VarGetNode;
	}
	else if (NodeClass == TEXT("VariableSet"))
	{
		FString VarName;
		if (!Params->TryGetStringField(TEXT("variable_name"), VarName))
		{
			OutError = TEXT("VariableSet requires 'variable_name'");
			return false;
		}

		UK2Node_VariableSet* VarSetNode = NewObject<UK2Node_VariableSet>(Graph);
		VarSetNode->VariableReference.SetSelfMember(FName(*VarName));
		VarSetNode->NodePosX = PosX;
		VarSetNode->NodePosY = PosY;
		Graph->AddNode(VarSetNode, true, false);
		VarSetNode->AllocateDefaultPins();
		NewNode = VarSetNode;
	}
	else
	{
		OutError = FString::Printf(TEXT("Unsupported node_class: %s. Supported: CallFunction, CustomEvent, IfThenElse, VariableGet, VariableSet"), *NodeClass);
		return false;
	}

	if (!NewNode)
	{
		OutError = TEXT("Failed to create node");
		return false;
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

	// 建立索引映射用于返回引脚信息
	TMap<UEdGraphNode*, int32> NodeIndexMap;
	for (int32 i = 0; i < Graph->Nodes.Num(); ++i)
	{
		if (Graph->Nodes[i]) NodeIndexMap.Add(Graph->Nodes[i], i);
	}

	int32 NewIndex = Graph->Nodes.IndexOfByKey(NewNode);

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetBoolField(TEXT("success"), true);
	OutResult->SetNumberField(TEXT("node_index"), NewIndex);
	OutResult->SetStringField(TEXT("node_title"), NewNode->GetNodeTitle(ENodeTitleType::ListView).ToString());

	// 返回引脚信息
	TArray<TSharedPtr<FJsonValue>> PinArr;
	for (UEdGraphPin* Pin : NewNode->Pins)
	{
		if (!Pin || Pin->bHidden) continue;
		PinArr.Add(MakeShared<FJsonValueObject>(PinToJson(Pin, NodeIndexMap)));
	}
	OutResult->SetArrayField(TEXT("pins"), PinArr);

	return true;
}

// ==================== delete_node ====================

bool UABlueprintCommands::ExecuteDeleteNode(
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath)) { OutError = TEXT("'asset_path' is required"); return false; }

	int32 NodeIndex = -1;
	if (!Params->TryGetNumberField(TEXT("node_index"), NodeIndex)) { OutError = TEXT("'node_index' is required"); return false; }

	FString GraphName;
	Params->TryGetStringField(TEXT("graph_name"), GraphName);

	UBlueprint* BP = LoadBlueprintFromPath(AssetPath, OutError);
	if (!BP) return false;

	UEdGraph* Graph = FindGraphByName(BP, GraphName, OutError);
	if (!Graph) return false;

	UEdGraphNode* Node = FindNodeByIndex(Graph, NodeIndex, OutError);
	if (!Node) return false;

	FString NodeTitle = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();

	FScopedTransaction Transaction(FText::FromString(
		FString::Printf(TEXT("Agent: Delete Node %s"), *NodeTitle)));

	// 断开所有连接
	Node->BreakAllNodeLinks();
	Graph->RemoveNode(Node);

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetBoolField(TEXT("success"), true);
	OutResult->SetStringField(TEXT("deleted_node_title"), NodeTitle);
	return true;
}

// ==================== connect_pins ====================

bool UABlueprintCommands::ExecuteConnectPins(
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath)) { OutError = TEXT("'asset_path' is required"); return false; }

	int32 FromNodeIdx = -1, ToNodeIdx = -1;
	FString FromPinName, ToPinName;
	if (!Params->TryGetNumberField(TEXT("from_node_index"), FromNodeIdx)) { OutError = TEXT("'from_node_index' is required"); return false; }
	if (!Params->TryGetStringField(TEXT("from_pin"), FromPinName)) { OutError = TEXT("'from_pin' is required"); return false; }
	if (!Params->TryGetNumberField(TEXT("to_node_index"), ToNodeIdx)) { OutError = TEXT("'to_node_index' is required"); return false; }
	if (!Params->TryGetStringField(TEXT("to_pin"), ToPinName)) { OutError = TEXT("'to_pin' is required"); return false; }

	FString GraphName;
	Params->TryGetStringField(TEXT("graph_name"), GraphName);

	UBlueprint* BP = LoadBlueprintFromPath(AssetPath, OutError);
	if (!BP) return false;

	UEdGraph* Graph = FindGraphByName(BP, GraphName, OutError);
	if (!Graph) return false;

	UEdGraphNode* FromNode = FindNodeByIndex(Graph, FromNodeIdx, OutError);
	if (!FromNode) return false;
	UEdGraphNode* ToNode = FindNodeByIndex(Graph, ToNodeIdx, OutError);
	if (!ToNode) return false;

	UEdGraphPin* FromPin = FindPinByName(FromNode, FromPinName, EGPD_Output, OutError);
	if (!FromPin) return false;
	UEdGraphPin* ToPin = FindPinByName(ToNode, ToPinName, EGPD_Input, OutError);
	if (!ToPin) return false;

	FScopedTransaction Transaction(FText::FromString(TEXT("Agent: Connect Pins")));

	const UEdGraphSchema* Schema = Graph->GetSchema();
	if (!Schema)
	{
		OutError = TEXT("Graph has no schema");
		return false;
	}

	// 检查兼容性
	FPinConnectionResponse Response = Schema->CanCreateConnection(FromPin, ToPin);
	if (Response.Response == CONNECT_RESPONSE_DISALLOW)
	{
		OutError = FString::Printf(TEXT("Cannot connect: %s"), *Response.Message.ToString());
		return false;
	}

	bool bSuccess = Schema->TryCreateConnection(FromPin, ToPin);
	if (!bSuccess)
	{
		OutError = TEXT("TryCreateConnection failed");
		return false;
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetBoolField(TEXT("success"), true);
	OutResult->SetStringField(TEXT("message"),
		FString::Printf(TEXT("Connected %d.%s -> %d.%s"), FromNodeIdx, *FromPinName, ToNodeIdx, *ToPinName));
	return true;
}

// ==================== disconnect_pin ====================

bool UABlueprintCommands::ExecuteDisconnectPin(
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath)) { OutError = TEXT("'asset_path' is required"); return false; }

	int32 NodeIndex = -1;
	FString PinName;
	if (!Params->TryGetNumberField(TEXT("node_index"), NodeIndex)) { OutError = TEXT("'node_index' is required"); return false; }
	if (!Params->TryGetStringField(TEXT("pin_name"), PinName)) { OutError = TEXT("'pin_name' is required"); return false; }

	FString GraphName;
	Params->TryGetStringField(TEXT("graph_name"), GraphName);

	UBlueprint* BP = LoadBlueprintFromPath(AssetPath, OutError);
	if (!BP) return false;

	UEdGraph* Graph = FindGraphByName(BP, GraphName, OutError);
	if (!Graph) return false;

	UEdGraphNode* Node = FindNodeByIndex(Graph, NodeIndex, OutError);
	if (!Node) return false;

	// 查找引脚（不限方向）
	UEdGraphPin* Pin = nullptr;
	for (UEdGraphPin* P : Node->Pins)
	{
		if (P && P->PinName.ToString() == PinName)
		{
			Pin = P;
			break;
		}
	}
	if (!Pin)
	{
		OutError = FString::Printf(TEXT("Pin '%s' not found on node %d"), *PinName, NodeIndex);
		return false;
	}

	FScopedTransaction Transaction(FText::FromString(TEXT("Agent: Disconnect Pin")));

	int32 DisconnectedCount = Pin->LinkedTo.Num();
	Pin->BreakAllPinLinks();

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetBoolField(TEXT("success"), true);
	OutResult->SetNumberField(TEXT("disconnected_count"), DisconnectedCount);
	return true;
}

// ==================== get_blueprint_variables ====================

bool UABlueprintCommands::ExecuteGetBlueprintVariables(
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath)) { OutError = TEXT("'asset_path' is required"); return false; }

	UBlueprint* BP = LoadBlueprintFromPath(AssetPath, OutError);
	if (!BP) return false;

	TArray<TSharedPtr<FJsonValue>> VarArr;
	for (const FBPVariableDescription& Var : BP->NewVariables)
	{
		auto VObj = MakeShared<FJsonObject>();
		VObj->SetStringField(TEXT("name"), Var.VarName.ToString());
		VObj->SetStringField(TEXT("type"), PinTypeToString(Var.VarType));
		VObj->SetStringField(TEXT("category"), Var.Category.ToString());
		VObj->SetBoolField(TEXT("is_exposed"), (Var.PropertyFlags & CPF_Edit) != 0);
		VObj->SetBoolField(TEXT("is_replicated"), (Var.PropertyFlags & CPF_Net) != 0);
		VObj->SetBoolField(TEXT("is_read_only"), (Var.PropertyFlags & CPF_BlueprintReadOnly) != 0);
		VObj->SetBoolField(TEXT("is_array"), Var.VarType.IsArray());

		// 默认值
		if (!Var.DefaultValue.IsEmpty())
		{
			VObj->SetStringField(TEXT("default_value"), Var.DefaultValue);
		}

		// 元数据 tooltip（必须先检查 Key 是否存在，GetMetaData 在 Key 不存在时会 check 断言失败）
		if (Var.HasMetaData(FBlueprintMetadata::MD_Tooltip))
		{
			const FString& Tooltip = Var.GetMetaData(FBlueprintMetadata::MD_Tooltip);
			if (!Tooltip.IsEmpty())
			{
				VObj->SetStringField(TEXT("tooltip"), Tooltip);
			}
		}

		VarArr.Add(MakeShared<FJsonValueObject>(VObj));
	}

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetArrayField(TEXT("variables"), VarArr);
	OutResult->SetNumberField(TEXT("count"), VarArr.Num());
	return true;
}

// ==================== get_blueprint_functions ====================

bool UABlueprintCommands::ExecuteGetBlueprintFunctions(
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath)) { OutError = TEXT("'asset_path' is required"); return false; }

	UBlueprint* BP = LoadBlueprintFromPath(AssetPath, OutError);
	if (!BP) return false;

	TArray<TSharedPtr<FJsonValue>> FuncArr;
	for (UEdGraph* FuncGraph : BP->FunctionGraphs)
	{
		if (!FuncGraph) continue;

		auto FObj = MakeShared<FJsonObject>();
		FObj->SetStringField(TEXT("name"), FuncGraph->GetName());
		FObj->SetNumberField(TEXT("node_count"), FuncGraph->Nodes.Num());

		// 从 FunctionEntry 节点获取输入参数
		TArray<TSharedPtr<FJsonValue>> InputArr;
		TArray<TSharedPtr<FJsonValue>> OutputArr;
		bool bIsPure = false;

		for (UEdGraphNode* Node : FuncGraph->Nodes)
		{
			if (UK2Node_FunctionEntry* EntryNode = Cast<UK2Node_FunctionEntry>(Node))
			{
				// 检查是否纯函数
				if (EntryNode->GetExtraFlags() & FUNC_BlueprintPure)
				{
					bIsPure = true;
				}

				for (UEdGraphPin* Pin : EntryNode->Pins)
				{
					if (!Pin || Pin->bHidden) continue;
					if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
					{
						auto PObj = MakeShared<FJsonObject>();
						PObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
						PObj->SetStringField(TEXT("type"), PinTypeToString(Pin->PinType));
						InputArr.Add(MakeShared<FJsonValueObject>(PObj));
					}
				}
			}
			else if (UK2Node_FunctionResult* ResultNode = Cast<UK2Node_FunctionResult>(Node))
			{
				for (UEdGraphPin* Pin : ResultNode->Pins)
				{
					if (!Pin || Pin->bHidden) continue;
					if (Pin->Direction == EGPD_Input && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
					{
						auto PObj = MakeShared<FJsonObject>();
						PObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
						PObj->SetStringField(TEXT("type"), PinTypeToString(Pin->PinType));
						OutputArr.Add(MakeShared<FJsonValueObject>(PObj));
					}
				}
			}
		}

		FObj->SetArrayField(TEXT("inputs"), InputArr);
		FObj->SetArrayField(TEXT("outputs"), OutputArr);
		FObj->SetBoolField(TEXT("is_pure"), bIsPure);

		// 访问修饰符 — 手动遍历查找 FunctionEntry 节点
		FString Access = TEXT("Public");
		for (UEdGraphNode* GraphNode : FuncGraph->Nodes)
		{
			if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(GraphNode))
			{
				if (Entry->GetExtraFlags() & FUNC_Protected)
				{
					Access = TEXT("Protected");
				}
				else if (Entry->GetExtraFlags() & FUNC_Private)
				{
					Access = TEXT("Private");
				}
				break;
			}
		}
		FObj->SetStringField(TEXT("access"), Access);

		FuncArr.Add(MakeShared<FJsonValueObject>(FObj));
	}

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetArrayField(TEXT("functions"), FuncArr);
	OutResult->SetNumberField(TEXT("count"), FuncArr.Num());
	return true;
}

// ==================== add_variable ====================

bool UABlueprintCommands::ExecuteAddVariable(
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath)) { OutError = TEXT("'asset_path' is required"); return false; }

	FString VarName;
	if (!Params->TryGetStringField(TEXT("variable_name"), VarName)) { OutError = TEXT("'variable_name' is required"); return false; }

	FString VarType;
	if (!Params->TryGetStringField(TEXT("variable_type"), VarType)) { OutError = TEXT("'variable_type' is required"); return false; }

	UBlueprint* BP = LoadBlueprintFromPath(AssetPath, OutError);
	if (!BP) return false;

	FEdGraphPinType PinType;
	if (!StringToPinType(VarType, PinType))
	{
		OutError = FString::Printf(TEXT("Unknown variable type: %s. Supported: bool, int, float, double, string, text, name, byte, vector, rotator, transform, linearcolor, object"), *VarType);
		return false;
	}

	FScopedTransaction Transaction(FText::FromString(
		FString::Printf(TEXT("Agent: Add Variable %s"), *VarName)));

	FName VarFName(*VarName);

	// 检查变量是否已存在
	for (const FBPVariableDescription& Var : BP->NewVariables)
	{
		if (Var.VarName == VarFName)
		{
			OutError = FString::Printf(TEXT("Variable '%s' already exists"), *VarName);
			return false;
		}
	}

	bool bSuccess = FBlueprintEditorUtils::AddMemberVariable(BP, VarFName, PinType);
	if (!bSuccess)
	{
		OutError = FString::Printf(TEXT("Failed to add variable '%s'"), *VarName);
		return false;
	}

	// 设置 Instance Editable（is_exposed）
	bool bIsExposed = false;
	if (Params->TryGetBoolField(TEXT("is_exposed"), bIsExposed) && bIsExposed)
	{
		FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(BP, VarFName, false);
	}

	// 设置分类
	FString Category;
	if (Params->TryGetStringField(TEXT("category"), Category) && !Category.IsEmpty())
	{
		FBlueprintEditorUtils::SetBlueprintVariableCategory(BP, VarFName, nullptr, FText::FromString(Category));
	}

	// 设置默认值
	FString DefaultValue;
	if (Params->TryGetStringField(TEXT("default_value"), DefaultValue) && !DefaultValue.IsEmpty())
	{
		// 找到刚添加的变量并设置默认值
		for (FBPVariableDescription& Var : BP->NewVariables)
		{
			if (Var.VarName == VarFName)
			{
				Var.DefaultValue = DefaultValue;
				break;
			}
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetBoolField(TEXT("success"), true);
	OutResult->SetStringField(TEXT("variable_name"), VarName);
	OutResult->SetStringField(TEXT("variable_type"), VarType);
	return true;
}

// ==================== add_function ====================

bool UABlueprintCommands::ExecuteAddFunction(
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath)) { OutError = TEXT("'asset_path' is required"); return false; }

	FString FunctionName;
	if (!Params->TryGetStringField(TEXT("function_name"), FunctionName)) { OutError = TEXT("'function_name' is required"); return false; }

	UBlueprint* BP = LoadBlueprintFromPath(AssetPath, OutError);
	if (!BP) return false;

	FScopedTransaction Transaction(FText::FromString(
		FString::Printf(TEXT("Agent: Add Function %s"), *FunctionName)));

	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
		BP,
		FName(*FunctionName),
		UEdGraph::StaticClass(),
		UEdGraphSchema_K2::StaticClass()
	);

	if (!NewGraph)
	{
		OutError = FString::Printf(TEXT("Failed to create function graph: %s"), *FunctionName);
		return false;
	}

	FBlueprintEditorUtils::AddFunctionGraph<UClass>(BP, NewGraph, /*bIsUserCreated=*/true, nullptr);

	// 设置纯函数标记 — 手动遍历查找 FunctionEntry 节点
	bool bIsPure = false;
	if (Params->TryGetBoolField(TEXT("is_pure"), bIsPure) && bIsPure)
	{
		for (UEdGraphNode* GraphNode : NewGraph->Nodes)
		{
			if (UK2Node_FunctionEntry* EntryNode = Cast<UK2Node_FunctionEntry>(GraphNode))
			{
				EntryNode->AddExtraFlags(FUNC_BlueprintPure);
				break;
			}
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetBoolField(TEXT("success"), true);
	OutResult->SetStringField(TEXT("function_name"), FunctionName);
	OutResult->SetStringField(TEXT("graph_name"), NewGraph->GetName());
	return true;
}

// ==================== compile_blueprint ====================

bool UABlueprintCommands::ExecuteCompileBlueprint(
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath)) { OutError = TEXT("'asset_path' is required"); return false; }

	UBlueprint* BP = LoadBlueprintFromPath(AssetPath, OutError);
	if (!BP) return false;

	// 编译蓝图
	FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::SkipGarbageCollection);

	// 收集编译结果
	FString StatusStr;
	bool bSuccess = true;
	switch (BP->Status)
	{
	case BS_UpToDate:
		StatusStr = TEXT("UpToDate");
		break;
	case BS_Dirty:
		StatusStr = TEXT("Dirty");
		break;
	case BS_Error:
		StatusStr = TEXT("Error");
		bSuccess = false;
		break;
	default:
		StatusStr = TEXT("Unknown");
		break;
	}

	// 收集编译消息（错误和警告）
	TArray<TSharedPtr<FJsonValue>> ErrorArr;
	TArray<TSharedPtr<FJsonValue>> WarningArr;

	if (BP->Status == BS_Error || BP->Status == BS_UpToDate)
	{
		// 遍历所有图中的节点，检查是否有错误引脚
		TArray<UEdGraph*> AllGraphs;
		BP->GetAllGraphs(AllGraphs);

		for (UEdGraph* Graph : AllGraphs)
		{
			if (!Graph) continue;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node) continue;
				if (Node->bHasCompilerMessage)
				{
					auto MsgObj = MakeShared<FJsonObject>();
					MsgObj->SetStringField(TEXT("node"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
					MsgObj->SetStringField(TEXT("graph"), Graph->GetName());
					MsgObj->SetStringField(TEXT("message"), Node->ErrorMsg);

					if (Node->ErrorType == EMessageSeverity::Error)
					{
						ErrorArr.Add(MakeShared<FJsonValueObject>(MsgObj));
					}
					else
					{
						WarningArr.Add(MakeShared<FJsonValueObject>(MsgObj));
					}
				}
			}
		}
	}

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetBoolField(TEXT("success"), bSuccess);
	OutResult->SetStringField(TEXT("status"), StatusStr);
	OutResult->SetArrayField(TEXT("errors"), ErrorArr);
	OutResult->SetNumberField(TEXT("error_count"), ErrorArr.Num());
	OutResult->SetArrayField(TEXT("warnings"), WarningArr);
	OutResult->SetNumberField(TEXT("warning_count"), WarningArr.Num());

	UE_LOG(LogUABlueprint, Log, TEXT("compile_blueprint: %s — %s (%d errors, %d warnings)"),
		*BP->GetName(), *StatusStr, ErrorArr.Num(), WarningArr.Num());
	return true;
}
