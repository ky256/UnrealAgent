// Copyright KuoYu. All Rights Reserved.

#include "Commands/UAPythonCommands.h"
#include "IPythonScriptPlugin.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

DEFINE_LOG_CATEGORY_STATIC(LogUAPython, Log, All);

TArray<FString> UAPythonCommands::GetSupportedMethods() const
{
	return {
		TEXT("execute_python"),
		TEXT("reset_python_context"),
	};
}

TSharedPtr<FJsonObject> UAPythonCommands::GetToolSchema(const FString& MethodName) const
{
	if (MethodName == TEXT("execute_python"))
	{
		TSharedPtr<FJsonObject> InputSchema = MakeShared<FJsonObject>();
		InputSchema->SetStringField(TEXT("type"), TEXT("object"));

		TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

		// code parameter (required)
		TSharedPtr<FJsonObject> CodeProp = MakeShared<FJsonObject>();
		CodeProp->SetStringField(TEXT("type"), TEXT("string"));
		CodeProp->SetStringField(TEXT("description"),
			TEXT("Python code to execute in the UE Editor context. "
				 "Use 'import unreal' to access the Unreal API. "
				 "Use print() for output. Context is shared across calls (stateful)."));
		Properties->SetObjectField(TEXT("code"), CodeProp);

		// timeout_seconds parameter (optional)
		TSharedPtr<FJsonObject> TimeoutProp = MakeShared<FJsonObject>();
		TimeoutProp->SetStringField(TEXT("type"), TEXT("number"));
		TimeoutProp->SetStringField(TEXT("description"),
			TEXT("Execution timeout in seconds (default 30, max 120). "
				 "Prevents infinite loops from freezing the editor."));
		Properties->SetObjectField(TEXT("timeout_seconds"), TimeoutProp);

		// transaction_name parameter (optional)
		TSharedPtr<FJsonObject> TransProp = MakeShared<FJsonObject>();
		TransProp->SetStringField(TEXT("type"), TEXT("string"));
		TransProp->SetStringField(TEXT("description"),
			TEXT("Human-readable name for the undo transaction. "
				 "Appears in Edit > Undo History. "
				 "Example: 'Set 12 lights brightness to 5000'. "
				 "Defaults to 'UnrealAgent Python' if not provided."));
		Properties->SetObjectField(TEXT("transaction_name"), TransProp);

		InputSchema->SetObjectField(TEXT("properties"), Properties);

		TArray<TSharedPtr<FJsonValue>> Required;
		Required.Add(MakeShared<FJsonValueString>(TEXT("code")));
		InputSchema->SetArrayField(TEXT("required"), Required);

		return MakeToolSchema(
			TEXT("execute_python"),
			TEXT("Execute Python code in the Unreal Editor context. "
				 "Has access to the full 'unreal' module API. "
				 "Context is stateful — variables and imports persist across calls. "
				 "Use print() to produce output. "
				 "Operations are wrapped in an undo transaction (Ctrl+Z to revert). "
				 "WARNING: Avoid calling open_editor_for_assets() or GAssetEditorSubsystem->OpenEditorForAsset() "
				 "as opening asset editors (especially Materials) triggers synchronous shader compilation "
				 "that blocks the editor's main thread and may cause temporary unresponsiveness."),
			InputSchema
		);
	}

	if (MethodName == TEXT("reset_python_context"))
	{
		return MakeToolSchema(
			TEXT("reset_python_context"),
			TEXT("Reset the shared Python execution context, clearing all variables and imports.")
		);
	}

	return nullptr;
}

bool UAPythonCommands::Execute(
	const FString& MethodName,
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	if (MethodName == TEXT("execute_python"))
	{
		return ExecutePython(Params, OutResult, OutError);
	}
	if (MethodName == TEXT("reset_python_context"))
	{
		return ExecuteResetContext(Params, OutResult, OutError);
	}

	OutError = FString::Printf(TEXT("Unknown method: %s"), *MethodName);
	return false;
}

bool UAPythonCommands::IsPythonAvailable(FString& OutError) const
{
	IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
	if (!PythonPlugin)
	{
		OutError = TEXT("PythonScriptPlugin is not loaded. Enable it in Edit > Plugins.");
		return false;
	}

	if (!PythonPlugin->IsPythonAvailable())
	{
		OutError = TEXT("Python is not available. Check PythonScriptPlugin configuration.");
		return false;
	}

	if (!PythonPlugin->IsPythonInitialized())
	{
		// Try to force-enable Python
		PythonPlugin->ForceEnablePythonAtRuntime();

		if (!PythonPlugin->IsPythonInitialized())
		{
			OutError = TEXT("Python is not initialized. PythonScriptPlugin may still be loading.");
			return false;
		}
	}

	return true;
}

FString UAPythonCommands::WrapCodeWithSafeguards(const FString& UserCode, float TimeoutSeconds, const FString& TransactionName) const
{
	// Escape the user code for embedding in a Python string literal
	FString EscapedCode = UserCode;
	EscapedCode.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	EscapedCode.ReplaceInline(TEXT("\"\"\""), TEXT("\\\"\\\"\\\""));

	// Escape the transaction name for Python string
	FString EscapedTransName = TransactionName;
	EscapedTransName.ReplaceInline(TEXT("'"), TEXT("\\'"));

	// Build the wrapper script:
	// 1. Start a timeout watchdog thread that will interrupt_main() after N seconds
	// 2. Wrap execution in a named Unreal undo transaction
	// 3. Execute user code via exec() in a persistent namespace
	// 4. Cancel the watchdog on completion
	return FString::Printf(TEXT(
		"import threading as _ua_threading\n"
		"import _thread as _ua_thread\n"
		"\n"
		"# --- Timeout watchdog ---\n"
		"_ua_timed_out = False\n"
		"def _ua_watchdog():\n"
		"    global _ua_timed_out\n"
		"    _ua_timed_out = True\n"
		"    _ua_thread.interrupt_main()\n"
		"_ua_timer = _ua_threading.Timer(%.1f, _ua_watchdog)\n"
		"_ua_timer.daemon = True\n"
		"_ua_timer.start()\n"
		"\n"
		"try:\n"
		"    # --- Undo transaction ---\n"
		"    import unreal as _ua_unreal\n"
		"    with _ua_unreal.ScopedEditorTransaction('%s'):\n"
		"        exec(\"\"\"%s\"\"\")\n"
		"except KeyboardInterrupt:\n"
		"    if _ua_timed_out:\n"
		"        print('[UnrealAgent] ERROR: Execution timed out after %.0f seconds')\n"
		"    else:\n"
		"        print('[UnrealAgent] Execution interrupted')\n"
		"except Exception as _ua_e:\n"
		"    import traceback as _ua_tb\n"
		"    _ua_tb.print_exc()\n"
		"finally:\n"
		"    _ua_timer.cancel()\n"
	), TimeoutSeconds, *EscapedTransName, *EscapedCode, TimeoutSeconds);
}

bool UAPythonCommands::ExecutePython(
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	// Validate Python availability
	if (!IsPythonAvailable(OutError))
	{
		return false;
	}

	// Extract code parameter
	FString Code;
	if (!Params->TryGetStringField(TEXT("code"), Code) || Code.IsEmpty())
	{
		OutError = TEXT("Invalid params: 'code' is required and must be non-empty");
		return false;
	}

	// Extract optional timeout (clamp to [1, 120] seconds)
	float TimeoutSeconds = DefaultTimeoutSeconds;
	if (Params->HasField(TEXT("timeout_seconds")))
	{
		TimeoutSeconds = static_cast<float>(Params->GetNumberField(TEXT("timeout_seconds")));
		TimeoutSeconds = FMath::Clamp(TimeoutSeconds, 1.0f, 120.0f);
	}

	UE_LOG(LogUAPython, Log, TEXT("Executing Python (%d chars, timeout=%.0fs)"), Code.Len(), TimeoutSeconds);

	// BUG-003/004 修复：检测可能阻塞 Game Thread 的 API 调用
	// 这些操作可能触发同步 shader 编译或模态对话框，阻塞编辑器主线程
	static const TArray<FString> BlockingPatterns = {
		TEXT("open_editor_for_asset"),
		TEXT("OpenEditorForAsset"),
		TEXT("open_editor_for_assets"),
		TEXT("OpenEditorForAssets"),
	};
	bool bHasBlockingCall = false;
	for (const FString& Pattern : BlockingPatterns)
	{
		if (Code.Contains(Pattern))
		{
			bHasBlockingCall = true;
			UE_LOG(LogUAPython, Warning, TEXT("Python code contains potentially blocking call: '%s'. This may cause editor unresponsiveness."), *Pattern);
			break;
		}
	}

	// Extract optional transaction name for Undo History
	FString TransactionName = TEXT("UnrealAgent Python");
	if (Params->HasField(TEXT("transaction_name")))
	{
		FString CustomName;
		if (Params->TryGetStringField(TEXT("transaction_name"), CustomName) && !CustomName.IsEmpty())
		{
			// Prefix with "AI: " for clarity in Undo History
			TransactionName = FString::Printf(TEXT("AI: %s"), *CustomName);
		}
	}

	IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();

	// Wrap user code with timeout watchdog and undo transaction
	const FString WrappedCode = WrapCodeWithSafeguards(Code, TimeoutSeconds, TransactionName);

	// Build the execution command
	FPythonCommandEx PythonCommand;
	PythonCommand.Command = WrappedCode;
	PythonCommand.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
	PythonCommand.FileExecutionScope = EPythonFileExecutionScope::Public;
	PythonCommand.Flags = EPythonCommandFlags::Unattended;

	// Execute
	const bool bSuccess = PythonPlugin->ExecPythonCommandEx(PythonCommand);

	// Collect log output (stdout/stderr)
	FString Output;
	FString Errors;

	for (const FPythonLogOutputEntry& Entry : PythonCommand.LogOutput)
	{
		if (Entry.Type == EPythonLogOutputType::Error)
		{
			if (!Errors.IsEmpty()) Errors += TEXT("\n");
			Errors += Entry.Output;
		}
		else
		{
			if (!Output.IsEmpty()) Output += TEXT("\n");
			Output += Entry.Output;
		}
	}

	// Check if timeout occurred (from the watchdog's print message)
	const bool bTimedOut = Output.Contains(TEXT("[UnrealAgent] ERROR: Execution timed out"));

	// Truncate if too long
	if (Output.Len() > MaxOutputLength)
	{
		Output = Output.Left(MaxOutputLength);
		Output += FString::Printf(TEXT("\n... [output truncated at %d chars]"), MaxOutputLength);
	}
	if (Errors.Len() > MaxOutputLength)
	{
		Errors = Errors.Left(MaxOutputLength);
		Errors += FString::Printf(TEXT("\n... [errors truncated at %d chars]"), MaxOutputLength);
	}

	// Build result
	OutResult = MakeShared<FJsonObject>();
	OutResult->SetBoolField(TEXT("success"), bSuccess && !bTimedOut);
	OutResult->SetStringField(TEXT("output"), Output);

	// Undo metadata — helps AI inform user about revertibility
	OutResult->SetBoolField(TEXT("undo_available"), bSuccess && !bTimedOut);
	OutResult->SetStringField(TEXT("transaction_name"), TransactionName);

	// BUG-003/004: 如果代码包含可能阻塞的调用，在返回结果中添加警告
	if (bHasBlockingCall)
	{
		OutResult->SetStringField(TEXT("warning"),
			TEXT("Code contains API calls (e.g. open_editor_for_assets) that may trigger synchronous shader compilation "
				 "and temporarily block the editor. If the editor becomes unresponsive, wait for shader compilation to finish."));
	}

	if (bTimedOut)
	{
		OutResult->SetStringField(TEXT("error"),
			FString::Printf(TEXT("Execution timed out after %.0f seconds. Possible infinite loop."), TimeoutSeconds));
	}

	if (!PythonCommand.CommandResult.IsEmpty())
	{
		OutResult->SetStringField(TEXT("result"), PythonCommand.CommandResult);
	}

	if (!Errors.IsEmpty())
	{
		if (OutResult->HasField(TEXT("error")))
		{
			// Append to existing error
			FString ExistingError = OutResult->GetStringField(TEXT("error"));
			OutResult->SetStringField(TEXT("error"), ExistingError + TEXT("\n") + Errors);
		}
		else
		{
			OutResult->SetStringField(TEXT("error"), Errors);
		}
	}

	if (bTimedOut)
	{
		UE_LOG(LogUAPython, Warning, TEXT("Python execution timed out after %.0fs"), TimeoutSeconds);
	}
	else if (bSuccess)
	{
		UE_LOG(LogUAPython, Log, TEXT("Python execution succeeded. Output: %d chars"), Output.Len());
	}
	else
	{
		UE_LOG(LogUAPython, Warning, TEXT("Python execution failed: %s"), *PythonCommand.CommandResult);
	}

	return true; // We always return true to send the result; success/failure is in the JSON
}

bool UAPythonCommands::ExecuteResetContext(
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	if (!IsPythonAvailable(OutError))
	{
		return false;
	}

	IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();

	// Reset by executing a script that clears the global namespace
	// We keep __builtins__ intact so Python still works
	FPythonCommandEx PythonCommand;
	PythonCommand.Command = TEXT(
		"_ua_keep = {'__builtins__', '__name__', '__doc__', '__package__', '__loader__', '__spec__', '_ua_keep'}\n"
		"_ua_to_del = [k for k in list(globals().keys()) if k not in _ua_keep]\n"
		"for _ua_k in _ua_to_del:\n"
		"    try:\n"
		"        del globals()[_ua_k]\n"
		"    except KeyError:\n"
		"        pass\n"
		"del _ua_keep, _ua_to_del\n"
		"print('Python context reset')\n"
	);
	PythonCommand.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
	PythonCommand.FileExecutionScope = EPythonFileExecutionScope::Public;
	PythonCommand.Flags = EPythonCommandFlags::Unattended;

	const bool bSuccess = PythonPlugin->ExecPythonCommandEx(PythonCommand);

	OutResult = MakeShared<FJsonObject>();
	OutResult->SetBoolField(TEXT("success"), bSuccess);
	OutResult->SetStringField(TEXT("message"),
		bSuccess ? TEXT("Python context reset successfully") : TEXT("Failed to reset Python context"));

	UE_LOG(LogUAPython, Log, TEXT("Python context reset: %s"), bSuccess ? TEXT("OK") : TEXT("FAILED"));

	return true;
}
