// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class VoxelMesh : ModuleRules
{

	private string ShaderDir => Path.GetFullPath( Path.Combine(ModuleDirectory, "..", "..", "Shaders") );
	private string ThirdPartyDir => Path.GetFullPath( Path.Combine(ModuleDirectory, "..", "..", "ThirdParties") );

	public VoxelMesh(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
				ShaderDir,
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"RHICore",
				"RHI",
				"Renderer",
				"RenderCore",
				"Projects",
				// ... add private dependencies that you statically link with here ...	
				"VoxelNanoVDB",
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);

		
		if (Target.bBuildWithEditorOnlyData && Target.bBuildEditor)
		{

            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
	                "UnrealEd",
	                "AssetTools",
				}
				);
		}
	}
}
