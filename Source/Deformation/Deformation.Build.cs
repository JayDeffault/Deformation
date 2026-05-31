using UnrealBuildTool;

public class Deformation : ModuleRules
{
    public Deformation(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "ProceduralMeshComponent"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "PhysicsCore"
        });
    }
}
