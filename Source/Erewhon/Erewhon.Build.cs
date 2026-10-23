// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Erewhon : ModuleRules
{
	public Erewhon(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Les sous-dossiers (Core/, World/, …) s'incluent depuis la racine du module.
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
			"NetCore", "DeveloperSettings",
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Slate", "SlateCore", "UMG", "Json", "ProceduralMeshComponent",
			"ImageCore", "RHI",   // signalements : capture d'écran en PNG, nom du GPU
		});
	}
}
