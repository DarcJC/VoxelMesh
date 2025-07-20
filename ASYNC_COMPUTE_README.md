# VoxelMesh Async Compute Implementation

This document describes the async compute refactor for the VoxelMesh plugin's GPU Marching Cubes implementation.

## Overview

The VoxelMesh plugin has been refactored to use AsyncCompute queues instead of synchronous GPU compute operations. This improves performance by avoiding render thread blocking during voxel mesh generation.

## Key Changes

### 1. Async Compute Pipeline
- **Before**: Synchronous GPU compute on main graphics queue
- **After**: Asynchronous GPU compute on dedicated async compute queue

### 2. Three-Stage Pipeline (Async)
1. **CalcCubeIndexCS** - Calculate cube indices for each voxel cube (async)
2. **CalcVertexAndIndexPrefixSumCS** - Calculate prefix sums and prepare resources (async)  
3. **MarchingCubeMeshGenerationCS** - Generate final mesh vertices and indices (async)

### 3. New Features

#### Console Variables
- `voxel.UseAsyncCompute` (0/1) - Enable/disable async compute (default: 1)
- `voxel.MeshGenerationComputeDebug` (0/1) - Enable RenderDoc capture for debugging

#### Blueprint Functions
- `RebuildMesh()` - Standard rebuild using current settings
- `RebuildMeshAsync()` - Force async compute rebuild (for testing)
- `RebuildMeshSync()` - Force synchronous rebuild (for testing)

#### Automatic Fallback
- Automatically falls back to synchronous compute if:
  - `GRHISupportsAsyncCompute` is false
  - Feature level is below SM5
  - User disables async compute via console variable

## Performance Benefits

### Render Thread Non-Blocking
- Async compute operations no longer block the main render thread
- Improves overall frame rates during voxel mesh generation
- Better responsiveness in applications with real-time voxel updates

### GPU Utilization
- Async compute queue can run in parallel with graphics operations
- Better GPU utilization on modern hardware
- Reduced GPU idle time

## Memory Modes (Preserved)

Both memory optimization modes are preserved in the async implementation:

### Performance Optimized Mode (Default)
- Allocates maximum buffer sizes upfront
- No GPU-CPU synchronization during generation
- Fastest execution, uses more memory

### Memory Optimized Mode  
- Reads counter buffer to determine exact buffer sizes needed
- Includes one GPU-CPU sync point for counter readback
- Slower execution, uses less memory

## Usage

### Basic Usage
```cpp
// Standard usage - will use async compute if supported
VoxelChunkView->RebuildMesh();
```

### Force Async (Testing)
```cpp
// Force async compute for testing/benchmarking
VoxelChunkView->RebuildMeshAsync();
```

### Force Sync (Compatibility)
```cpp
// Force synchronous compute for compatibility testing
VoxelChunkView->RebuildMeshSync();
```

### Console Commands
```
// Disable async compute
voxel.UseAsyncCompute 0

// Enable async compute (default)
voxel.UseAsyncCompute 1

// Enable debug capture
voxel.MeshGenerationComputeDebug 1
```

## Technical Implementation

### Thread Safety
- Uses atomic variables for state management
- Prevents concurrent generation attempts
- Thread-safe async operation queuing

### GPU Synchronization
- Uses `FRHIGPUFence` for async compute completion detection
- Proper resource transitions between compute and graphics queues
- Maintains deterministic execution order

### Error Handling
- Graceful fallback for unsupported platforms
- Comprehensive error logging
- Validation of GPU resources before operations

## Performance Monitoring

The async implementation includes performance timing:

```
LogVoxelMesh: Async voxel mesh generation completed in 15.32 ms
```

## Compatibility

### Supported Platforms
- Any platform with `GRHISupportsAsyncCompute = true`
- Requires feature level SM5 or higher
- Tested on modern DirectX 11/12 and Vulkan implementations

### Fallback Behavior
- Automatically uses synchronous compute on unsupported platforms
- No API changes for existing code
- Seamless backward compatibility

## Migration Guide

### Existing Code
No changes required for existing code. The public API remains the same:

```cpp
// This continues to work exactly as before
VoxelChunkView->RebuildMesh();
```

### Performance Testing
Use the new debug functions to compare performance:

```cpp
// Test async performance
auto StartTime = FPlatformTime::Seconds();
VoxelChunkView->RebuildMeshAsync();
// Check completion with VoxelChunkView->GetRHIProxy()->IsReady()

// Test sync performance  
auto StartTime2 = FPlatformTime::Seconds();
VoxelChunkView->RebuildMeshSync();
// Compare timing
```

## Future Improvements

Potential areas for further optimization:
1. GPU memory pooling for intermediate buffers
2. Multi-frame async generation for large datasets
3. Progressive mesh generation with priority systems
4. Integration with Unreal's render dependency graph system