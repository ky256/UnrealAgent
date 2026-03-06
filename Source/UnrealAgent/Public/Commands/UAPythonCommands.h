// Copyright KuoYu. All Rights Reserved.

#pragma once

#include "Commands/UACommandBase.h"

/**
 * Python execution command for the universal execution layer.
 * Allows AI agents to execute arbitrary Python code in the UE Editor context.
 *
 * Supported methods:
 *   - execute_python: Execute Python code and return output
 *   - reset_python_context: Reset the shared Python execution context
 */
class UAPythonCommands : public UACommandBase
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
	bool ExecutePython(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool ExecuteResetContext(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& OutResult, FString& OutError);

	/** Check if the Python scripting plugin is available */
	bool IsPythonAvailable(FString& OutError) const;

	/** Wrap user code with timeout watchdog and undo transaction */
	FString WrapCodeWithSafeguards(const FString& UserCode, float TimeoutSeconds, const FString& TransactionName) const;

	/** Maximum output length in characters (default 64KB) */
	static constexpr int32 MaxOutputLength = 65536;

	/** Default timeout for Python execution in seconds */
	static constexpr float DefaultTimeoutSeconds = 30.0f;
};
