# STARDIS-GPU Build Unblocking Instructions

**Problem**: Phase 1 GPU code is complete but cannot build due to missing CUDA Visual Studio 2022 integration.

**Symptom**: CMake error: `No CUDA toolset found` when enabling CUDA language with VS2022 generator.

---

## Current System State

### ✅ Present
- CUDA Toolkit 11.2 installed: `C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v11.2`
- nvcc.exe in PATH: `C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v11.2\bin\nvcc.exe`
- Microsoft Visual Studio 2022 Community installed

### ❌ Missing
- CUDA VS build customizations: `MSBuild\Microsoft\VC\v170\BuildCustomizations\CUDA*.props`
- CUDA VS integration component: Enables CMake to use CUDA with VS generators

---

## Solution Options

### ⚠️ WORKAROUND: Use CMake with CUDA Direct Path

Since VS integration is missing, we can configure CMake to use CUDA directly by specifying the nvcc path and architecture. This bypasses the VS integration requirement.

**Steps:**
```cmd
cd D:\Works\Projects\Stardis-GPU
cmake -B build-gpu -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_CUDA_COMPILER="C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v11.2\bin\nvcc.exe" ^
  -DCMAKE_CUDA_ARCHITECTURES="89" ^
  -DCMAKE_BUILD_TYPE=Release
```

**Why this works**:
- CMake can use nvcc directly even without VS integration
- Architecture 89 targets RTX 4090 (native to your GPU)
- CMake generates a custom VS project that invokes nvcc through its custom build rules
- No reinstallation of CUDA needed

---

### Option 2: Install CUDA with VS Integration (NOT TESTED)

If the CMake approach above fails, reinstall CUDA with VS integration:
1. Download CUDA 11.2.2 or later (not 11.2):  
   https://developer.nvidia.com/cuda-downloads
2. Run installer with VS integration:
   ```cmd
   cuda_11.2.2_win10_network.exe --component=vs-integration
   ```

---

### Option 3: Use Ninja Build System (NOT TESTED)

If you cannot reinstall CUDA, install Ninja:
```cmd
winget install Ninja-build.Ninja
cmake -B build-gpu -G Ninja -DCMAKE_CUDA_COMPILER="C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v11.2/bin/nvcc.exe"
```

---

## Verification Steps

After configuration:

### 1. Verify CMake Configuration
```cmd
cd D:\Works\Projects\Stardis-GPU\build-gpu
cmake -L .
```

**Expected output:**
- ✅ `CMAKE_CUDA_COMPILER` is set to nvcc path
- ✅ `CMAKE_CUDA_ARCHITECTURES` shows `86` or `89`
- ✅ `cuBQL` subdirectory loaded successfully

### 2. Build Library
```cmd
cmake --build . --config Release
```

**Expected output:**
- ✅ No CUDA toolset errors
- ✅ `stardis_gpu_headers` interface library created
- ✅ `test_cubql_triangle` executable compiled

### 3. Run Validation Test
```cmd
ctest -C Release --test-dir . -V
```

**Expected output:**
```
=== CUDA Device 0: NVIDIA GeForce RTX 4090 ===
  Compute Capability: 8.9
  Double Precision:   YES (1:64 vs FP32)
=== Test: Single Triangle BVH ===
  BVH built successfully
  Hit test PASS: t=1.000000, u=0.250000, v=0.250000
  Miss test PASS
=== PASSED: Single Triangle BVH ===

Results: 2 passed, 0 failed
```

---

## Troubleshooting

### "CMake was unable to find a build program corresponding to Ninja"

If using Ninja, must specify build program explicitly:
```cmd
cmake -B build-gpu-ninja -G Ninja ^
  -DCMAKE_CUDA_COMPILER="C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA\v11.2/bin/nvcc.exe" ^
  -DCMAKE_BUILD_TYPE=Release
```

### Build succeeds but tests fail
- Check RTX 4090 is primary GPU (nvidia-smi)
- Ensure display drivers are up to date
- Run with verbose output: `cmake --build . --config Release -- VERBOSE`

---

## Alternative: Project Migration Strategy

If build configuration continues to fail due to CMake/VS integration issues, consider:
1. Create separate CMake configuration file specifically for CUDA
2. Use Visual Studio directly with CUDA projects (no CMake)
3. Switch to a more established CUDA build system (like Premake or bazel)

---

## Next Steps After Build Success

Once build passes, continue with Phase 2: Core Migration (from embree_migration_cuBQL.md):

1. Replace `s3d_backend.h` with cuBQL implementation
2. Maintain s3d public API compatibility
3. Implement batch ray processing for Monte Carlo loops
4. Validate GPU vs CPU results (1e-6 precision requirement)

---

**Document Version**: 1.1  
**Created**: 2026-01-22  
**Status**: Ready for build unblocking (with CMake direct path workaround)
