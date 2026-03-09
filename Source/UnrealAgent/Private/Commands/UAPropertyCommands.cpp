// Copyright KuoYu. All Rights Reserved.

#include "Commands/UAPropertyCommands.h"
#include "UnrealAgent.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "UObject/UnrealType.h"
#include "UObject/PropertyIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogUAProperty, Log, All);

// ====================================================================
// GetSupportedMethods
// ====================================================================

TArray<FString> UAPropertyCommands::GetSupportedMethods() const
{
	return {
		TEXT("get_property"),
		TEXT("set_property"),
		TEXT("list_properties"),
	};
}

// ====================================================================
// GetToolSchema
// ====================================================================

TSharedPtr<FJsonObject> UAPropertyCommands::GetToolSchema(const FString& MethodName) const
{
	if (MethodName == TEXT("get_property"))
	{
		TSharedPtr<FJsonObject> InputSchema = MakeShared<FJsonObject>();
		InputSchema->SetStringField(TEXT("type"), TEXT("object"));

		TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

		TSharedPtr<FJsonObject> ActorProp = MakeShared<FJsonObject>();
		ActorProp->SetStringField(TEXT("type"), TEXT("string"));
		ActorProp->SetStringField(TEXT("description"),
			TEXT("Actor 名称（label 或内部名）。"));
		Properties->SetObjectField(TEXT("actor_name"), ActorProp);

		TSharedPtr<FJsonObject> PathProp = MakeShared<FJsonObject>();
		PathProp->SetStringField(TEXT("type"), TEXT("string"));
		PathProp->SetStringField(TEXT("description"),
			TEXT("属性路径，格式: 'ComponentName.PropertyName' 或 'ComponentName.StructProp.Field'。"
				"如果只提供属性名（无点号），则直接在 Actor 上查找。"
				"示例: 'LightComponent.Intensity', 'StaticMeshComponent.StaticMesh', 'RootComponent.RelativeLocation.X'"));
		Properties->SetObjectField(TEXT("property_path"), PathProp);

		InputSchema->SetObjectField(TEXT("properties"), Properties);

		TArray<TSharedPtr<FJsonValue>> Required;
		Required.Add(MakeShared<FJsonValueString>(TEXT("actor_name")));
		Required.Add(MakeShared<FJsonValueString>(TEXT("property_path")));
		InputSchema->SetArrayField(TEXT("required"), Required);

		return MakeToolSchema(
			TEXT("get_property"),
			TEXT("读取 Actor/Component 的任意属性值。通过反射系统支持属性路径解析。"
				"路径示例: 'LightComponent.Intensity', 'StaticMeshComponent.StaticMesh'"),
			InputSchema
		);
	}

	if (MethodName == TEXT("set_property"))
	{
		TSharedPtr<FJsonObject> InputSchema = MakeShared<FJsonObject>();
		InputSchema->SetStringField(TEXT("type"), TEXT("object"));

		TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

		TSharedPtr<FJsonObject> ActorProp = MakeShared<FJsonObject>();
		ActorProp->SetStringField(TEXT("type"), TEXT("string"));
		ActorProp->SetStringField(TEXT("description"), TEXT("Actor 名称（label 或内部名）。"));
		Properties->SetObjectField(TEXT("actor_name"), ActorProp);

		TSharedPtr<FJsonObject> PathProp = MakeShared<FJsonObject>();
		PathProp->SetStringField(TEXT("type"), TEXT("string"));
		PathProp->SetStringField(TEXT("description"),
			TEXT("属性路径，与 get_property 格式相同。"));
		Properties->SetObjectField(TEXT("property_path"), PathProp);

		TSharedPtr<FJsonObject> ValueProp = MakeShared<FJsonObject>();
		ValueProp->SetStringField(TEXT("description"),
			TEXT("要设置的值。数值类型传数字，字符串传字符串，布尔传 true/false。"
				"结构体（如 FVector）传 JSON 对象: {\"x\":1,\"y\":2,\"z\":3}。"
				"对象引用传资产路径字符串。"));
		Properties->SetObjectField(TEXT("value"), ValueProp);

		InputSchema->SetObjectField(TEXT("properties"), Properties);

		TArray<TSharedPtr<FJsonValue>> Required;
		Required.Add(MakeShared<FJsonValueString>(TEXT("actor_name")));
		Required.Add(MakeShared<FJsonValueString>(TEXT("property_path")));
		Required.Add(MakeShared<FJsonValueString>(TEXT("value")));
		InputSchema->SetArrayField(TEXT("required"), Required);

		return MakeToolSchema(
			TEXT("set_property"),
			TEXT("设置 Actor/Component 的任意属性值。支持 Undo。"
				"操作后会触发 PostEditChangeProperty 通知，保证编辑器 UI 同步更新。"),
			InputSchema
		);
	}

	if (MethodName == TEXT("list_properties"))
	{
		TSharedPtr<FJsonObject> InputSchema = MakeShared<FJsonObject>();
		InputSchema->SetStringField(TEXT("type"), TEXT("object"));

		TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

		TSharedPtr<FJsonObject> ActorProp = MakeShared<FJsonObject>();
		ActorProp->SetStringField(TEXT("type"), TEXT("string"));
		ActorProp->SetStringField(TEXT("description"), TEXT("Actor 名称（label 或内部名）。"));
		Properties->SetObjectField(TEXT("actor_name"), ActorProp);

		TSharedPtr<FJsonObject> CompProp = MakeShared<FJsonObject>();
		CompProp->SetStringField(TEXT("type"), TEXT("string"));
		CompProp->SetStringField(TEXT("description"),
			TEXT("组件名称。留空列出 Actor 自身的属性。"));
		Properties->SetObjectField(TEXT("component_name"), CompProp);

		InputSchema->SetObjectField(TEXT("properties"), Properties);

		TArray<TSharedPtr<FJsonValue>> Required;
		Required.Add(MakeShared<FJsonValueString>(TEXT("actor_name")));
		InputSchema->SetArrayField(TEXT("required"), Required);

		return MakeToolSchema(
			TEXT("list_properties"),
			TEXT("列出 Actor 或组件的所有可编辑属性（名称、类型、当前值预览）。"
				"仅列出 EditAnywhere/VisibleAnywhere 标记的属性。"),
			InputSchema
		);
	}

	return nullptr;
}

// ====================================================================
// Execute
// ====================================================================

bool UAPropertyCommands::Execute(
	const FString& MethodName,
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	if (MethodName == TEXT("get_property"))    return ExecuteGetProperty(Params, OutResult, OutError);
	if (MethodName == TEXT("set_property"))    return ExecuteSetProperty(Params, OutResult, OutError);
	if (MethodName == TEXT("list_properties")) return ExecuteListProperties(Params, OutResult, OutError);

	OutError = FString::Printf(TEXT("Unknown method: %s"), *MethodName);
	return false;
}

// ====================================================================
// get_property
// ====================================================================

bool UAPropertyCommands::ExecuteGetProperty(
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString ActorName;
	if (!Params->TryGetStringField(TEXT("actor_name"), ActorName))
	{
		OutError = TEXT("Missing required parameter: actor_name");
		return false;
	}

	FString PropertyPath;
	if (!Params->TryGetStringField(TEXT("property_path"), PropertyPath))
	{
		OutError = TEXT("Missing required parameter: property_path");
		return false;
	}

	AActor* Actor = FindActorByName(ActorName);
	if (!Actor)
	{
		OutError = FString::Printf(TEXT("Actor not found: %s"), *ActorName);
		return false;
	}

	UObject* TargetObject = nullptr;
	FProperty* TargetProperty = nullptr;
	void* ValuePtr = nullptr;

	if (!ResolvePropertyPath(Actor, PropertyPath, TargetObject, TargetProperty, ValuePtr, OutError))
	{
		return false;
	}

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetField(TEXT("value"), PropertyToJsonValue(TargetProperty, ValuePtr));
	OutResult->SetStringField(TEXT("type"), TargetProperty->GetCPPType());
	OutResult->SetStringField(TEXT("property_name"), TargetProperty->GetName());

	return true;
}

// ====================================================================
// set_property
// ====================================================================

bool UAPropertyCommands::ExecuteSetProperty(
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString ActorName;
	if (!Params->TryGetStringField(TEXT("actor_name"), ActorName))
	{
		OutError = TEXT("Missing required parameter: actor_name");
		return false;
	}

	FString PropertyPath;
	if (!Params->TryGetStringField(TEXT("property_path"), PropertyPath))
	{
		OutError = TEXT("Missing required parameter: property_path");
		return false;
	}

	if (!Params->HasField(TEXT("value")))
	{
		OutError = TEXT("Missing required parameter: value");
		return false;
	}

	AActor* Actor = FindActorByName(ActorName);
	if (!Actor)
	{
		OutError = FString::Printf(TEXT("Actor not found: %s"), *ActorName);
		return false;
	}

	UObject* TargetObject = nullptr;
	FProperty* TargetProperty = nullptr;
	void* ValuePtr = nullptr;

	if (!ResolvePropertyPath(Actor, PropertyPath, TargetObject, TargetProperty, ValuePtr, OutError))
	{
		return false;
	}

	// 记录旧值
	TSharedPtr<FJsonValue> OldValue = PropertyToJsonValue(TargetProperty, ValuePtr);

	// 包裹事务以支持 Undo
	FScopedTransaction Transaction(FText::FromString(
		FString::Printf(TEXT("Agent: Set %s.%s"), *ActorName, *PropertyPath)));

	TargetObject->Modify();

	// 设置新值
	TSharedPtr<FJsonValue> NewJsonValue = Params->TryGetField(TEXT("value"));
	if (!JsonValueToProperty(TargetProperty, ValuePtr, NewJsonValue, OutError))
	{
		return false;
	}

	// 触发属性变更通知
	FPropertyChangedEvent ChangedEvent(TargetProperty);
	TargetObject->PostEditChangeProperty(ChangedEvent);

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetBoolField(TEXT("success"), true);
	OutResult->SetField(TEXT("old_value"), OldValue);
	OutResult->SetField(TEXT("new_value"), PropertyToJsonValue(TargetProperty, ValuePtr));
	OutResult->SetStringField(TEXT("property_name"), TargetProperty->GetName());

	UE_LOG(LogUAProperty, Log, TEXT("Set %s.%s"), *ActorName, *PropertyPath);
	return true;
}

// ====================================================================
// list_properties
// ====================================================================

bool UAPropertyCommands::ExecuteListProperties(
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString ActorName;
	if (!Params->TryGetStringField(TEXT("actor_name"), ActorName))
	{
		OutError = TEXT("Missing required parameter: actor_name");
		return false;
	}

	AActor* Actor = FindActorByName(ActorName);
	if (!Actor)
	{
		OutError = FString::Printf(TEXT("Actor not found: %s"), *ActorName);
		return false;
	}

	FString ComponentName;
	Params->TryGetStringField(TEXT("component_name"), ComponentName);

	UObject* TargetObj = Actor;
	if (!ComponentName.IsEmpty())
	{
		UActorComponent* Comp = FindComponentByName(Actor, ComponentName);
		if (!Comp)
		{
			OutError = FString::Printf(TEXT("Component not found: %s on actor %s"), *ComponentName, *ActorName);
			return false;
		}
		TargetObj = Comp;
	}

	TArray<TSharedPtr<FJsonValue>> PropertyList;

	for (TFieldIterator<FProperty> PropIt(TargetObj->GetClass()); PropIt; ++PropIt)
	{
		FProperty* Prop = *PropIt;

		// 只列出可编辑属性
		if (!Prop->HasAnyPropertyFlags(CPF_Edit))
		{
			continue;
		}

		TSharedPtr<FJsonObject> PropObj = MakeShared<FJsonObject>();
		PropObj->SetStringField(TEXT("name"), Prop->GetName());
		PropObj->SetStringField(TEXT("type"), Prop->GetCPPType());

		// 类别
		const FString& Category = Prop->GetMetaData(TEXT("Category"));
		PropObj->SetStringField(TEXT("category"), Category);

		// 是否可编辑
		bool bIsEditable = Prop->HasAnyPropertyFlags(CPF_Edit) &&
			!Prop->HasAnyPropertyFlags(CPF_EditConst);
		PropObj->SetBoolField(TEXT("is_editable"), bIsEditable);

		// 值预览（截断到100字符）
		void* ValueAddr = Prop->ContainerPtrToValuePtr<void>(TargetObj);
		FString ValueStr;
		Prop->ExportText_Direct(ValueStr, ValueAddr, nullptr, nullptr, PPF_None);
		if (ValueStr.Len() > 100)
		{
			ValueStr = ValueStr.Left(97) + TEXT("...");
		}
		PropObj->SetStringField(TEXT("value_preview"), ValueStr);

		PropertyList.Add(MakeShared<FJsonValueObject>(PropObj));
	}

	// 同时列出组件列表（如果目标是 Actor）
	TArray<TSharedPtr<FJsonValue>> ComponentList;
	if (TargetObj == Actor)
	{
		TArray<UActorComponent*> Components;
		Actor->GetComponents(Components);
		for (UActorComponent* Comp : Components)
		{
			if (!Comp) continue;
			TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
			CompObj->SetStringField(TEXT("name"), Comp->GetName());
			CompObj->SetStringField(TEXT("class"), Comp->GetClass()->GetName());
			ComponentList.Add(MakeShared<FJsonValueObject>(CompObj));
		}
	}

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetStringField(TEXT("target"), TargetObj->GetName());
	OutResult->SetStringField(TEXT("target_class"), TargetObj->GetClass()->GetName());
	OutResult->SetArrayField(TEXT("properties"), PropertyList);
	OutResult->SetNumberField(TEXT("property_count"), PropertyList.Num());

	if (ComponentList.Num() > 0)
	{
		OutResult->SetArrayField(TEXT("components"), ComponentList);
	}

	return true;
}

// ====================================================================
// 辅助方法
// ====================================================================

AActor* UAPropertyCommands::FindActorByName(const FString& ActorName) const
{
	if (!GEditor)
	{
		return nullptr;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetActorLabel() == ActorName || (*It)->GetName() == ActorName)
		{
			return *It;
		}
	}

	return nullptr;
}

UActorComponent* UAPropertyCommands::FindComponentByName(AActor* Actor, const FString& ComponentName) const
{
	if (!Actor)
	{
		return nullptr;
	}

	TArray<UActorComponent*> Components;
	Actor->GetComponents(Components);

	for (UActorComponent* Comp : Components)
	{
		if (Comp && (Comp->GetName() == ComponentName || Comp->GetReadableName() == ComponentName))
		{
			return Comp;
		}
	}

	// 尝试模糊匹配：去掉数字后缀
	for (UActorComponent* Comp : Components)
	{
		if (Comp && Comp->GetName().Contains(ComponentName))
		{
			return Comp;
		}
	}

	return nullptr;
}

bool UAPropertyCommands::ResolvePropertyPath(
	AActor* Actor,
	const FString& PropertyPath,
	UObject*& OutObject,
	FProperty*& OutProperty,
	void*& OutValuePtr,
	FString& OutError) const
{
	TArray<FString> Segments;
	PropertyPath.ParseIntoArray(Segments, TEXT("."));

	if (Segments.Num() == 0)
	{
		OutError = TEXT("Property path is empty");
		return false;
	}

	UObject* CurrentObj = Actor;
	void* CurrentContainer = Actor;

	// 如果第一段匹配组件名，则进入组件
	if (Segments.Num() >= 2)
	{
		UActorComponent* Comp = FindComponentByName(Actor, Segments[0]);
		if (Comp)
		{
			CurrentObj = Comp;
			CurrentContainer = Comp;
			Segments.RemoveAt(0);
		}
	}

	// 现在遍历剩余的属性路径
	FProperty* CurrentProp = nullptr;

	for (int32 i = 0; i < Segments.Num(); ++i)
	{
		const FString& Segment = Segments[i];

		CurrentProp = FindFProperty<FProperty>(CurrentObj->GetClass(), *Segment);
		if (!CurrentProp)
		{
			// 如果当前容器是结构体内部，尝试在结构体中查找
			OutError = FString::Printf(TEXT("Property '%s' not found on %s (%s)"),
				*Segment, *CurrentObj->GetName(), *CurrentObj->GetClass()->GetName());
			return false;
		}

		void* PropValuePtr = CurrentProp->ContainerPtrToValuePtr<void>(CurrentContainer);

		// 如果不是最后一段，且当前属性是结构体，则深入
		if (i < Segments.Num() - 1)
		{
			FStructProperty* StructProp = CastField<FStructProperty>(CurrentProp);
			if (StructProp)
			{
				// 进入结构体内部 — 注意这里 CurrentObj 不变（结构体不是 UObject）
				// 但我们需要更新 CurrentContainer 为结构体数据的地址
				CurrentContainer = PropValuePtr;

				// 对结构体的下一层查找，需要用结构体的 UScriptStruct
				// 覆盖下一次循环的查找逻辑
				// 改为直接查找结构体中的属性
				const FString& NextSegment = Segments[i + 1];
				FProperty* InnerProp = StructProp->Struct->FindPropertyByName(*NextSegment);
				if (!InnerProp)
				{
					OutError = FString::Printf(TEXT("Property '%s' not found in struct %s"),
						*NextSegment, *StructProp->Struct->GetName());
					return false;
				}

				// 跳到下一段
				CurrentProp = InnerProp;
				PropValuePtr = InnerProp->ContainerPtrToValuePtr<void>(CurrentContainer);
				++i; // 额外前进一步

				if (i < Segments.Num() - 1)
				{
					// 还有更深的层级
					FStructProperty* InnerStructProp = CastField<FStructProperty>(CurrentProp);
					if (InnerStructProp)
					{
						CurrentContainer = PropValuePtr;
						continue;
					}
					else
					{
						OutError = FString::Printf(TEXT("Property '%s' is not a struct, cannot go deeper"),
							*NextSegment);
						return false;
					}
				}
			}
			else
			{
				// 如果是对象引用属性，尝试进入引用的对象
				FObjectProperty* ObjProp = CastField<FObjectProperty>(CurrentProp);
				if (ObjProp)
				{
					UObject* ReferencedObj = ObjProp->GetObjectPropertyValue(PropValuePtr);
					if (ReferencedObj)
					{
						CurrentObj = ReferencedObj;
						CurrentContainer = ReferencedObj;
						continue;
					}
					else
					{
						OutError = FString::Printf(TEXT("Object property '%s' is null"), *Segment);
						return false;
					}
				}

				OutError = FString::Printf(TEXT("Property '%s' is neither struct nor object, cannot traverse"),
					*Segment);
				return false;
			}
		}

		// 最后一段 — 这就是目标属性
		OutObject = CurrentObj;
		OutProperty = CurrentProp;
		OutValuePtr = PropValuePtr;
	}

	if (!OutProperty)
	{
		OutError = TEXT("Failed to resolve property path");
		return false;
	}

	return true;
}

TSharedPtr<FJsonValue> UAPropertyCommands::PropertyToJsonValue(FProperty* Property, const void* ValuePtr) const
{
	if (!Property || !ValuePtr)
	{
		return MakeShared<FJsonValueNull>();
	}

	// 数值类型
	if (FNumericProperty* NumProp = CastField<FNumericProperty>(Property))
	{
		if (NumProp->IsFloatingPoint())
		{
			double Val = 0.0;
			NumProp->GetValue_InContainer(ValuePtr, &Val);
			// 使用 ExportText 更可靠
			FString ValStr;
			Property->ExportText_Direct(ValStr, ValuePtr, nullptr, nullptr, PPF_None);
			double ParsedVal = FCString::Atod(*ValStr);
			return MakeShared<FJsonValueNumber>(ParsedVal);
		}
		else if (NumProp->IsInteger())
		{
			FString ValStr;
			Property->ExportText_Direct(ValStr, ValuePtr, nullptr, nullptr, PPF_None);
			int64 ParsedVal = FCString::Atoi64(*ValStr);
			return MakeShared<FJsonValueNumber>(static_cast<double>(ParsedVal));
		}
	}

	// 布尔
	if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		bool bVal = BoolProp->GetPropertyValue(ValuePtr);
		return MakeShared<FJsonValueBoolean>(bVal);
	}

	// 字符串
	if (CastField<FStrProperty>(Property) || CastField<FNameProperty>(Property) || CastField<FTextProperty>(Property))
	{
		FString Val;
		Property->ExportText_Direct(Val, ValuePtr, nullptr, nullptr, PPF_None);
		return MakeShared<FJsonValueString>(Val);
	}

	// 枚举
	if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
	{
		FString Val;
		Property->ExportText_Direct(Val, ValuePtr, nullptr, nullptr, PPF_None);
		return MakeShared<FJsonValueString>(Val);
	}

	if (FByteProperty* ByteProp = CastField<FByteProperty>(Property))
	{
		if (ByteProp->Enum)
		{
			FString Val;
			Property->ExportText_Direct(Val, ValuePtr, nullptr, nullptr, PPF_None);
			return MakeShared<FJsonValueString>(Val);
		}
		else
		{
			uint8 Val = *static_cast<const uint8*>(ValuePtr);
			return MakeShared<FJsonValueNumber>(Val);
		}
	}

	// 结构体 — 尝试递归展开
	if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		TSharedPtr<FJsonObject> StructObj = MakeShared<FJsonObject>();

		for (TFieldIterator<FProperty> It(StructProp->Struct); It; ++It)
		{
			FProperty* InnerProp = *It;
			const void* InnerPtr = InnerProp->ContainerPtrToValuePtr<void>(ValuePtr);
			StructObj->SetField(InnerProp->GetName(), PropertyToJsonValue(InnerProp, InnerPtr));
		}

		return MakeShared<FJsonValueObject>(StructObj);
	}

	// 对象引用
	if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Property))
	{
		UObject* Obj = ObjProp->GetObjectPropertyValue(ValuePtr);
		if (Obj)
		{
			return MakeShared<FJsonValueString>(Obj->GetPathName());
		}
		return MakeShared<FJsonValueNull>();
	}

	// 数组
	if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
	{
		FScriptArrayHelper Helper(ArrayProp, ValuePtr);
		TArray<TSharedPtr<FJsonValue>> JsonArray;

		for (int32 i = 0; i < Helper.Num(); ++i)
		{
			JsonArray.Add(PropertyToJsonValue(ArrayProp->Inner, Helper.GetRawPtr(i)));
		}

		return MakeShared<FJsonValueArray>(JsonArray);
	}

	// 兜底：使用 ExportText
	FString FallbackStr;
	Property->ExportText_Direct(FallbackStr, ValuePtr, nullptr, nullptr, PPF_None);
	return MakeShared<FJsonValueString>(FallbackStr);
}

bool UAPropertyCommands::JsonValueToProperty(
	FProperty* Property, void* ValuePtr,
	const TSharedPtr<FJsonValue>& JsonValue,
	FString& OutError) const
{
	if (!Property || !ValuePtr || !JsonValue.IsValid())
	{
		OutError = TEXT("Invalid property, value pointer, or JSON value");
		return false;
	}

	// 数值类型
	if (FNumericProperty* NumProp = CastField<FNumericProperty>(Property))
	{
		double Val = 0.0;
		if (JsonValue->Type == EJson::Number)
		{
			Val = JsonValue->AsNumber();
		}
		else if (JsonValue->Type == EJson::String)
		{
			Val = FCString::Atod(*JsonValue->AsString());
		}
		else
		{
			OutError = FString::Printf(TEXT("Expected number for property %s, got %d"),
				*Property->GetName(), static_cast<int32>(JsonValue->Type));
			return false;
		}

		if (NumProp->IsFloatingPoint())
		{
			FString ImportStr = FString::SanitizeFloat(Val);
			Property->ImportText_Direct(*ImportStr, ValuePtr, nullptr, PPF_None);
		}
		else
		{
			FString ImportStr = FString::Printf(TEXT("%lld"), static_cast<int64>(Val));
			Property->ImportText_Direct(*ImportStr, ValuePtr, nullptr, PPF_None);
		}
		return true;
	}

	// 布尔
	if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		bool bVal = false;
		if (JsonValue->Type == EJson::Boolean)
		{
			bVal = JsonValue->AsBool();
		}
		else if (JsonValue->Type == EJson::Number)
		{
			bVal = JsonValue->AsNumber() != 0.0;
		}
		else if (JsonValue->Type == EJson::String)
		{
			bVal = JsonValue->AsString().ToBool();
		}
		BoolProp->SetPropertyValue(ValuePtr, bVal);
		return true;
	}

	// 字符串/名称/文本
	if (CastField<FStrProperty>(Property) || CastField<FNameProperty>(Property) || CastField<FTextProperty>(Property))
	{
		FString Val = JsonValue->AsString();
		Property->ImportText_Direct(*Val, ValuePtr, nullptr, PPF_None);
		return true;
	}

	// 枚举
	if (CastField<FEnumProperty>(Property) || (CastField<FByteProperty>(Property) && CastField<FByteProperty>(Property)->Enum))
	{
		FString Val = JsonValue->AsString();
		Property->ImportText_Direct(*Val, ValuePtr, nullptr, PPF_None);
		return true;
	}

	// 结构体
	if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		if (JsonValue->Type == EJson::Object)
		{
			TSharedPtr<FJsonObject> JsonObj = JsonValue->AsObject();

			for (TFieldIterator<FProperty> It(StructProp->Struct); It; ++It)
			{
				FProperty* InnerProp = *It;
				TSharedPtr<FJsonValue> InnerVal = JsonObj->TryGetField(InnerProp->GetName());
				if (InnerVal.IsValid())
				{
					void* InnerPtr = InnerProp->ContainerPtrToValuePtr<void>(ValuePtr);
					if (!JsonValueToProperty(InnerProp, InnerPtr, InnerVal, OutError))
					{
						return false;
					}
				}
			}
			return true;
		}
		else if (JsonValue->Type == EJson::String)
		{
			// 尝试用 ImportText 解析字符串表示
			FString Val = JsonValue->AsString();
			Property->ImportText_Direct(*Val, ValuePtr, nullptr, PPF_None);
			return true;
		}
	}

	// 对象引用
	if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Property))
	{
		FString AssetPath = JsonValue->AsString();
		if (AssetPath.IsEmpty() || AssetPath == TEXT("null"))
		{
			ObjProp->SetObjectPropertyValue(ValuePtr, nullptr);
			return true;
		}

		UObject* Obj = StaticLoadObject(ObjProp->PropertyClass, nullptr, *AssetPath);
		if (!Obj)
		{
			OutError = FString::Printf(TEXT("Failed to load object: %s"), *AssetPath);
			return false;
		}
		ObjProp->SetObjectPropertyValue(ValuePtr, Obj);
		return true;
	}

	// 兜底：ImportText
	FString TextVal = JsonValue->AsString();
	if (!TextVal.IsEmpty())
	{
		Property->ImportText_Direct(*TextVal, ValuePtr, nullptr, PPF_None);
		return true;
	}

	OutError = FString::Printf(TEXT("Cannot convert JSON value to property type: %s"), *Property->GetCPPType());
	return false;
}
