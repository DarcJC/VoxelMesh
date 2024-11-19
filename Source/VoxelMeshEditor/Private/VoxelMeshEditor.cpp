#include "VoxelMeshEditor.h"

#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "VoxelChunkViewEditor.h"

#define LOCTEXT_NAMESPACE "FVoxelMeshEditorModule"

void FVoxelMeshEditorModule::StartupModule()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	AssetTools.RegisterAssetTypeActions(MakeShared<FVoxelChunkAssetTypeActions>());
}

void FVoxelMeshEditorModule::ShutdownModule()
{
    
}

#undef LOCTEXT_NAMESPACE
    
IMPLEMENT_MODULE(FVoxelMeshEditorModule, VoxelMeshEditor)