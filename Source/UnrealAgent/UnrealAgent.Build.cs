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
		});

		// PythonScriptPlugin: header-only include (runtime optional via IPythonScriptPlugin::Get())
		PrivateIncludePathModuleNames.AddRange(new string[] {
			"PythonScriptPlugin",
		});
	}
}
