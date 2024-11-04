
using System.IO;
using UnrealBuildTool;

public class VoxelNanoVDB : ModuleRules
{
	public VoxelNanoVDB(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		Type = ModuleType.External;
		
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "NanoVDB"));
		
		PublicDefinitions.Add("NANOVDB_USE_ZIP");
		PublicDefinitions.Add("NANOVDB_USE_TBB");
		
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"IntelTBB",
			"zlib",
		});
	}
}
