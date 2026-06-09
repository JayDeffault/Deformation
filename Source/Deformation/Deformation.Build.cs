// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Deformation : ModuleRules
{
	public Deformation(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"PhysicsCore"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Chaos"
			});

		SetupModulePhysicsSupport(Target);
	}
}
