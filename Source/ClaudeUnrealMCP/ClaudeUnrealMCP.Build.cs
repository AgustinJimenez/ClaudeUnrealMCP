using UnrealBuildTool;

public class ClaudeUnrealMCP : ModuleRules
{
	public ClaudeUnrealMCP(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Sockets",
			"Networking"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"BlueprintEditorLibrary",
			"BlueprintGraph",
			"Kismet",
			"AssetRegistry",
			"Json",
			"JsonUtilities",
			"EnhancedInput",
			"InputCore",
			"UMGEditor",  // For UWidgetBlueprint
			"Chooser",    // For UChooserTable migration
			"StructUtils",  // For FInstancedStruct
			"EditorScriptingUtilities",  // For UEditorAssetLibrary
			"AssetTools",  // For material/asset creation factories
			"UMG",  // For UWidget, UWidgetTree, widget components
			"AIModule",  // For BehaviorTree, BlackboardData
			"BehaviorTreeEditor",  // For BT/BB factories
			"LevelEditor",  // For viewport camera control
			"Slate",  // For SLevelViewport
			"SlateCore",
			"MaterialEditor"  // For UMaterialEditingLibrary
		});
	}
}
