using UnrealBuildTool;

public class UnrealAgent : ModuleRules
{
	public UnrealAgent(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
			"Sockets",           // FSocket, FTcpSocketBuilder
			"Networking",        // FTcpListener, FIPv4Endpoint
			"Json",              // FJsonObject, TJsonReader/Writer
			"JsonUtilities",     // FJsonObjectConverter
			"UnrealEd",          // GEditor, UEditorEngine
			"LevelEditor",       // FLevelEditorModule
			"AssetRegistry",     // IAssetRegistry
			"ToolMenus",         // UToolMenus
			"DeveloperSettings", // UDeveloperSettings
			"MaterialEditor",    // UMaterialEditingLibrary
 			"ContentBrowser",    // FContentBrowserModule (UAContextCommands)
			"Blutility",         // UEditorUtilityLibrary (UAContextCommands)
			"Kismet",            // FKismetEditorUtilities, FBlueprintEditorUtils (UABlueprintCommands)
			"BlueprintGraph",    // K2Node, EdGraphSchema_K2 (UABlueprintCommands)
			"KismetCompiler",    // 蓝图编译器 (UABlueprintCommands)
			"AssetTools",        // IAssetTools (UAAssetManageCommands)
			"EditorScriptingUtilities", // UEditorAssetLibrary (UAAssetManageCommands)
			"ImageWrapper",      // IImageWrapper, PNG 编码 (UAScreenshotCommands)
			"RenderCore",        // FlushRenderingCommands (UAScreenshotCommands)
			"RHI",               // FReadSurfaceDataFlags (UAScreenshotCommands)
		});

		// PythonScriptPlugin: header-only include (runtime optional via IPythonScriptPlugin::Get())
		PrivateIncludePathModuleNames.AddRange(new string[] {
			"PythonScriptPlugin",
		});
	}
}
