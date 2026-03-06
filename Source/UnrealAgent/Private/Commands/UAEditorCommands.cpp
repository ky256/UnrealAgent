// Copyright KuoYu. All Rights Reserved.

#include "Commands/UAEditorCommands.h"
#include "UnrealAgent.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Misc/ITransaction.h"

DEFINE_LOG_CATEGORY_STATIC(LogUAEditor, Log, All);

TArray<FString> UAEditorCommands::GetSupportedMethods() const
{
	return {
		TEXT("undo"),
		TEXT("redo"),
	};
}

TSharedPtr<FJsonObject> UAEditorCommands::GetToolSchema(const FString& MethodName) const
{
	if (MethodName == TEXT("undo"))
	{
		TSharedPtr<FJsonObject> InputSchema = MakeShared<FJsonObject>();
		InputSchema->SetStringField(TEXT("type"), TEXT("object"));

		TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

		TSharedPtr<FJsonObject> StepsProp = MakeShared<FJsonObject>();
		StepsProp->SetStringField(TEXT("type"), TEXT("integer"));
		StepsProp->SetStringField(TEXT("description"),
			TEXT("Number of steps to undo (default 1, max 20)."));
		Properties->SetObjectField(TEXT("steps"), StepsProp);

		InputSchema->SetObjectField(TEXT("properties"), Properties);

		return MakeToolSchema(
			TEXT("undo"),
			TEXT("Undo the last editor operation(s). "
				 "Works on all operations including those performed by execute_python. "
				 "Each execute_python call is a single undo step."),
			InputSchema
		);
	}

	if (MethodName == TEXT("redo"))
	{
		TSharedPtr<FJsonObject> InputSchema = MakeShared<FJsonObject>();
		InputSchema->SetStringField(TEXT("type"), TEXT("object"));

		TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

		TSharedPtr<FJsonObject> StepsProp = MakeShared<FJsonObject>();
		StepsProp->SetStringField(TEXT("type"), TEXT("integer"));
		StepsProp->SetStringField(TEXT("description"),
			TEXT("Number of steps to redo (default 1, max 20)."));
		Properties->SetObjectField(TEXT("steps"), StepsProp);

		InputSchema->SetObjectField(TEXT("properties"), Properties);

		return MakeToolSchema(
			TEXT("redo"),
			TEXT("Redo the last undone operation(s)."),
			InputSchema
		);
	}

	return nullptr;
}

bool UAEditorCommands::Execute(
	const FString& MethodName,
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	if (MethodName == TEXT("undo")) return ExecuteUndo(Params, OutResult, OutError);
	if (MethodName == TEXT("redo")) return ExecuteRedo(Params, OutResult, OutError);

	OutError = FString::Printf(TEXT("Unknown method: %s"), *MethodName);
	return false;
}

bool UAEditorCommands::ExecuteUndo(
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	if (!GEditor || !GEditor->Trans)
	{
		OutError = TEXT("Editor transaction system is not available");
		return false;
	}

	int32 Steps = 1;
	if (Params->HasField(TEXT("steps")))
	{
		Steps = static_cast<int32>(Params->GetNumberField(TEXT("steps")));
		Steps = FMath::Clamp(Steps, 1, 20);
	}

	int32 UndoneCount = 0;
	TArray<FString> UndoneDescriptions;

	for (int32 i = 0; i < Steps; ++i)
	{
		if (!GEditor->Trans->CanUndo())
		{
			break;
		}

		// Get the description of what we're about to undo
		FTransactionContext Context = GEditor->Trans->GetUndoContext(false);
		if (!Context.Title.IsEmpty())
		{
			UndoneDescriptions.Add(Context.Title.ToString());
		}

		if (GEditor->UndoTransaction())
		{
			UndoneCount++;
		}
		else
		{
			break;
		}
	}

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetBoolField(TEXT("success"), UndoneCount > 0);
	OutResult->SetNumberField(TEXT("steps_undone"), UndoneCount);

	// Return descriptions of what was undone
	TArray<TSharedPtr<FJsonValue>> DescArray;
	for (const FString& Desc : UndoneDescriptions)
	{
		DescArray.Add(MakeShared<FJsonValueString>(Desc));
	}
	OutResult->SetArrayField(TEXT("undone_operations"), DescArray);

	OutResult->SetBoolField(TEXT("can_undo_more"), GEditor->Trans->CanUndo());
	OutResult->SetBoolField(TEXT("can_redo"), GEditor->Trans->CanRedo());

	if (UndoneCount > 0)
	{
		UE_LOG(LogUAEditor, Log, TEXT("Undid %d step(s)"), UndoneCount);
	}
	else
	{
		UE_LOG(LogUAEditor, Log, TEXT("Nothing to undo"));
	}

	return true;
}

bool UAEditorCommands::ExecuteRedo(
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	if (!GEditor || !GEditor->Trans)
	{
		OutError = TEXT("Editor transaction system is not available");
		return false;
	}

	int32 Steps = 1;
	if (Params->HasField(TEXT("steps")))
	{
		Steps = static_cast<int32>(Params->GetNumberField(TEXT("steps")));
		Steps = FMath::Clamp(Steps, 1, 20);
	}

	int32 RedoneCount = 0;
	TArray<FString> RedoneDescriptions;

	for (int32 i = 0; i < Steps; ++i)
	{
		if (!GEditor->Trans->CanRedo())
		{
			break;
		}

		FTransactionContext Context = GEditor->Trans->GetUndoContext(true);
		if (!Context.Title.IsEmpty())
		{
			RedoneDescriptions.Add(Context.Title.ToString());
		}

		if (GEditor->RedoTransaction())
		{
			RedoneCount++;
		}
		else
		{
			break;
		}
	}

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetBoolField(TEXT("success"), RedoneCount > 0);
	OutResult->SetNumberField(TEXT("steps_redone"), RedoneCount);

	TArray<TSharedPtr<FJsonValue>> DescArray;
	for (const FString& Desc : RedoneDescriptions)
	{
		DescArray.Add(MakeShared<FJsonValueString>(Desc));
	}
	OutResult->SetArrayField(TEXT("redone_operations"), DescArray);

	OutResult->SetBoolField(TEXT("can_undo"), GEditor->Trans->CanUndo());
	OutResult->SetBoolField(TEXT("can_redo_more"), GEditor->Trans->CanRedo());

	if (RedoneCount > 0)
	{
		UE_LOG(LogUAEditor, Log, TEXT("Redid %d step(s)"), RedoneCount);
	}
	else
	{
		UE_LOG(LogUAEditor, Log, TEXT("Nothing to redo"));
	}

	return true;
}
