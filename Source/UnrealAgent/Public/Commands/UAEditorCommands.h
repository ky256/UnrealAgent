// Copyright KuoYu. All Rights Reserved.

#pragma once

#include "Commands/UACommandBase.h"

/**
 * Editor-level commands: undo, redo.
 * Allows AI to revert or re-apply operations through the standard UE transaction system.
 *
 * Supported methods:
 *   - undo: Undo the last editor transaction
 *   - redo: Redo the last undone transaction
 */
class UAEditorCommands : public UACommandBase
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
	bool ExecuteUndo(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool ExecuteRedo(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
};
