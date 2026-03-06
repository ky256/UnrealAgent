// Copyright KuoYu. All Rights Reserved.

#include "Commands/UACommandRegistry.h"
#include "Commands/UACommandBase.h"
#include "Commands/UAProjectCommands.h"
#include "Commands/UAAssetCommands.h"
#include "Commands/UAWorldCommands.h"
#include "Commands/UAActorCommands.h"
#include "Commands/UAViewportCommands.h"
#include "Commands/UAPythonCommands.h"
#include "Commands/UAEditorCommands.h"
#include "UnrealAgent.h"

UACommandRegistry::UACommandRegistry()
{
	// Register all command groups
	RegisterCommand(MakeShared<UAProjectCommands>());
	RegisterCommand(MakeShared<UAAssetCommands>());
	RegisterCommand(MakeShared<UAWorldCommands>());
	RegisterCommand(MakeShared<UAActorCommands>());
	RegisterCommand(MakeShared<UAViewportCommands>());
	RegisterCommand(MakeShared<UAPythonCommands>());
	RegisterCommand(MakeShared<UAEditorCommands>());
}

void UACommandRegistry::RegisterCommand(TSharedPtr<UACommandBase> Command)
{
	if (!Command.IsValid())
	{
		return;
	}

	CommandGroups.Add(Command);

	TArray<FString> Methods = Command->GetSupportedMethods();
	for (const FString& Method : Methods)
	{
		if (MethodMap.Contains(Method))
		{
			UE_LOG(LogUnrealAgent, Warning, TEXT("Method '%s' is already registered, overwriting"), *Method);
		}
		MethodMap.Add(Method, Command);
		UE_LOG(LogUnrealAgent, Log, TEXT("Registered method: %s"), *Method);
	}
}

bool UACommandRegistry::Dispatch(
	const FString& MethodName,
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	TSharedPtr<UACommandBase>* CommandPtr = MethodMap.Find(MethodName);
	if (!CommandPtr || !CommandPtr->IsValid())
	{
		OutError = FString::Printf(TEXT("Unknown method: %s"), *MethodName);
		return false;
	}

	return (*CommandPtr)->Execute(MethodName, Params, OutResult, OutError);
}

TArray<TSharedPtr<FJsonObject>> UACommandRegistry::GetAllToolSchemas() const
{
	TArray<TSharedPtr<FJsonObject>> Schemas;

	for (const auto& CommandGroup : CommandGroups)
	{
		if (!CommandGroup.IsValid())
		{
			continue;
		}

		TArray<FString> Methods = CommandGroup->GetSupportedMethods();
		for (const FString& Method : Methods)
		{
			TSharedPtr<FJsonObject> Schema = CommandGroup->GetToolSchema(Method);
			if (Schema.IsValid())
			{
				Schemas.Add(Schema);
			}
		}
	}

	return Schemas;
}

TSharedPtr<FJsonObject> UACommandRegistry::HandleListTools()
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	TArray<TSharedPtr<FJsonValue>> ToolsArray;
	TArray<TSharedPtr<FJsonObject>> Schemas = GetAllToolSchemas();

	for (const auto& Schema : Schemas)
	{
		ToolsArray.Add(MakeShared<FJsonValueObject>(Schema));
	}

	Result->SetArrayField(TEXT("tools"), ToolsArray);
	Result->SetNumberField(TEXT("count"), ToolsArray.Num());

	return Result;
}
