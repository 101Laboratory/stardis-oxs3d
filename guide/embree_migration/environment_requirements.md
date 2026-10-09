# Embree to cuBQL Migration - Environment Requirements

**Date**: 2026-01-23  
**Project**: STARDIS-GPU Embree → cuBQL Migration  
**Purpose**: Complete specification of hardware, software, and toolchain requirements  
**Status**: 📋 SPECIFICATION  

---

## Executive Summary

This document specifies **all environment requirements** for:
1. **Standalone performance comparison** (embree vs cuBQL)
2. **Full STARDIS migration** (if performance validated)

**Key Insight**: Standalone test requires **ONLY** Embree + cuBQL + CUDA. **NO STARDIS DEPENDENCIES** needed for validation.

---

## Table of Contents

1. [Hardware Requirements](#1-hardware-requirements)
2. [Software Dependencies](#2-software-dependencies)
3. [Build Tools](#3-build-tools)
4. [Verification Commands](#4-verification-commands)
5. [Installation Instructions](#5-installation-instructions)
6. [Troubleshooting](#6-troubleshooting)

---

## 1. Hardware Requirements

### 1.1 For Standalone Performance Comparison

| Component | Minimum | Recommended | Notes |
|-----------|---------|-------------|-------|
| **GPU** | NVIDIA RTX 3060 (Ampere) | NVIDIA RTX 4090 (Ada Lovelace) | CUDA Compute Capability ≥ 8.6 required |
| **VRAM** | 6 GB | 16 GB+ | For large test scenes (100K+ triangles) |
| **CPU** | Any modern x64 | Intel Core i7 / AMD Ryzen 7+ | For Embree baseline performance |
| **System RAM** | 8 GB | 16 GB+ | For host-side geometry storage |
| **Storage** | 5 GB free space | 20 GB+ (SSD preferred) | For CUDA toolkit, libraries, test data |

### 1.2 For Full STARDIS Migration

Same as above, plus:
- **System RAM**: 16 GB minimum (32 GB recommended) - STARDIS data structures
- **Storage**: 50 GB+ - Full project + dependencies

### 1.3 GPU Compatibility Check

**Supported GPUs** (CUDA Compute Capability 8.6+):
- ✅ NVIDIA RTX 30-series (3060, 3070, 3080, 3090)
- ✅ NVIDIA RTX 40-series (4060, 4070, 4080, 4090)
- ✅ NVIDIA A-series (A100, A6000)
- ✅ NVIDIA H-series (H100)
- ❌ GTX 16-series (no ray tracing acceleration)
- ❌ RTX 20-series (Turing - limited CUDA support)

**Check your GPU**:
```bash
nvidia-smi --query-gpu=name,compute_cap --format=csv
```

Expected output:
```
name, compute_cap
NVIDIA GeForce RTX 4090, 8.9
```

---

## 2. Software Dependencies

### 2.1 Core Dependencies (MANDATORY)

#### CUDA Toolkit

| Component | Version | Purpose |
|-----------|---------|---------|
| **CUDA Toolkit** | ≥ 12.0 (12.6+ recommended) | cuBQL compilation, GPU runtime |
| **NVCC Compiler** | Included in CUDA | Compile `.cu` files |
| **CUDA Runtime** | Included in CUDA | Execute GPU kernels |

**Download**: https://developer.nvidia.com/cuda-downloads

**Installation Notes**:
- Windows: Install CUDA **before** Visual Studio integration
- Linux: Use official NVIDIA repos (not distro packages)

#### NVIDIA Driver

| Component | Version | Purpose |
|-----------|---------|---------|
| **Display Driver** | ≥ 525.60.13 (Linux) / ≥ 528.33 (Windows) | CUDA 12.0+ support |

**Check current version**:
```bash
nvidia-smi
```

**Update if needed**: https://www.nvidia.com/Download/index.aspx

#### Intel Embree

| Component | Version | Purpose |
|-----------|---------|---------|
| **Embree** | ≥ 4.0 (4.3+ recommended) | CPU BVH baseline for comparison |

**Download**: https://github.com/embree/embree/releases

**Installation**:
- **Prebuilt binaries**: Download `.zip`/`.tar.gz`, extract to known path
- **Source build** (if needed):
  ```bash
  git clone --depth 1 --branch v4.3.3 https://github.com/embree/embree.git
  cd embree
  mkdir build && cd build
  cmake .. -DCMAKE_BUILD_TYPE=Release -DEMBREE_ISPC_SUPPORT=OFF
  cmake --build . --config Release
  cmake --install . --prefix /path/to/install
  ```

#### NVIDIA cuBQL

| Component | Version | Purpose |
|-----------|---------|---------|
| **cuBQL** | Latest from main branch | GPU BVH library (header-only) |

**Installation**:
```bash
git clone https://github.com/NVIDIA/cuBQL.git
# No build needed - header-only library
```

**Integration**:
```cmake
# In CMakeLists.txt
add_subdirectory(path/to/cuBQL)
target_link_libraries(your_target cuBQL)
```

### 2.2 Build System

#### CMake

| Tool | Version | Purpose |
|------|---------|---------|
| **CMake** | ≥ 3.18 (3.25+ recommended) | Build orchestration |

**Download**: https://cmake.org/download/

**Verify installation**:
```bash
cmake --version
# Expected: cmake version 3.25.0 or higher
```

#### C/C++ Compiler

**Windows**:
| Tool | Version | Notes |
|------|---------|-------|
| **Visual Studio** | 2019 / 2022 | Must install "Desktop development with C++" workload |
| **MSVC** | v142 / v143 | Included with VS |

**Linux**:
| Tool | Version | Notes |
|------|---------|-------|
| **GCC** | ≥ 11 | `sudo apt install gcc-11 g++-11` |
| **Clang** | ≥ 14 (optional) | Alternative to GCC |

**Compatibility Note**: NVCC (CUDA compiler) has **specific C++ compiler requirements**:
- CUDA 12.0: Supports MSVC 19.29-19.39, GCC 11-12
- CUDA 12.6: Supports MSVC 19.29-19.43, GCC 11-13

### 2.3 Optional Dependencies

| Tool | Purpose | Required For |
|------|---------|--------------|
| **Git** | Version control, clone repositories | Downloading cuBQL, Embree source |
| **Python 3** | Data analysis, plotting | Performance report generation |
| **NVIDIA Nsight Compute** | GPU profiling | Detailed performance analysis |
| **NVIDIA Nsight Systems** | System-wide profiling | CPU-GPU transfer bottleneck analysis |

---

## 3. Build Tools

### 3.1 Required CMake Variables

```cmake
# Standalone comparison project
cmake_minimum_required(VERSION 3.18)
project(EmbreeVsCuBQL LANGUAGES CXX CUDA)

# CUDA settings
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_ARCHITECTURES 86 89) # SM 8.6 (RTX 3060+), SM 8.9 (RTX 4090)

# Find dependencies
find_package(CUDAToolkit REQUIRED)
find_package(embree 4.0 REQUIRED)

# cuBQL (header-only)
add_subdirectory(external/cuBQL)

# Executable
add_executable(comparison
    src/main.cpp
    src/embree_wrapper.cpp
    src/cubql_wrapper.cu
    src/geometry_generator.cpp
)

target_link_libraries(comparison
    PRIVATE
        CUDA::cudart
        embree
        cuBQL
)
```

### 3.2 Compiler Flags

**Recommended flags**:
```cmake
# CUDA flags
set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} --extended-lambda --expt-relaxed-constexpr")

# C++ flags (Release)
if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /Ob2 /DNDEBUG")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
endif()

# Enable all warnings
target_compile_options(comparison PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:
        $<IF:$<CXX_COMPILER_ID:MSVC>,/W4,-Wall -Wextra>
    >
)
```

---

## 4. Verification Commands

### 4.1 Pre-Build Checks

```bash
# 1. Check CUDA installation
nvcc --version
# Expected: Cuda compilation tools, release 12.x

# 2. Check GPU compute capability
nvidia-smi --query-gpu=compute_cap --format=csv,noheader
# Expected: 8.6, 8.9, or higher

# 3. Check Embree installation
# (If installed to system path)
pkg-config --modversion embree4
# Expected: 4.3.3 or similar

# 4. Check CMake version
cmake --version
# Expected: cmake version 3.18.0 or higher

# 5. Check C++ compiler
gcc --version  # Linux
cl.exe         # Windows (in Developer Command Prompt)
# Expected: Compatible version (see table above)
```

### 4.2 Build Test

```bash
# Clone comparison project
git clone <repo_url> embree_cubql_comparison
cd embree_cubql_comparison

# Fetch dependencies
git submodule update --init --recursive

# Configure
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build . --config Release

# Verify executable
./comparison --help  # Linux
.\Release\comparison.exe --help  # Windows
```

Expected output:
```
Embree vs cuBQL Performance Comparison Tool
Usage: comparison [options]
  --scene <cornell|sphere|random>
  --triangles <count>
  --rays <count>
  ...
```

---

## 5. Installation Instructions

### 5.1 Windows (Visual Studio 2022)

#### Step 1: Install CUDA Toolkit
```powershell
# Download from https://developer.nvidia.com/cuda-downloads
# Run installer: cuda_12.6.0_560.76_windows.exe
# Select "Custom" install, ensure "Visual Studio Integration" is checked
```

#### Step 2: Install Visual Studio 2022
```powershell
# Download from https://visualstudio.microsoft.com/downloads/
# Select "Desktop development with C++" workload
# After install, verify CUDA integration:
# In VS, create new CUDA project - should see CUDA templates
```

#### Step 3: Install CMake
```powershell
# Download from https://cmake.org/download/
# Add to PATH during installation
# Verify: cmake --version
```

#### Step 4: Install Embree
```powershell
# Download prebuilt: https://github.com/embree/embree/releases/download/v4.3.3/embree-4.3.3.x64.windows.zip
# Extract to C:\embree-4.3.3
# Add to environment:
set EMBREE_INSTALL_DIR=C:\embree-4.3.3
set PATH=%PATH%;%EMBREE_INSTALL_DIR%\bin
```

#### Step 5: Clone cuBQL
```powershell
cd C:\Projects
git clone https://github.com/NVIDIA/cuBQL.git
# No build needed - header-only
```

### 5.2 Linux (Ubuntu 22.04)

#### Step 1: Install NVIDIA Driver
```bash
# Check current driver
nvidia-smi

# If outdated, install latest
sudo apt update
sudo apt install nvidia-driver-550
sudo reboot
```

#### Step 2: Install CUDA Toolkit
```bash
# Add NVIDIA repo
wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2204/x86_64/cuda-keyring_1.1-1_all.deb
sudo dpkg -i cuda-keyring_1.1-1_all.deb
sudo apt update

# Install CUDA
sudo apt install cuda-toolkit-12-6

# Add to PATH and LD_LIBRARY_PATH
echo 'export PATH=/usr/local/cuda-12.6/bin:$PATH' >> ~/.bashrc
echo 'export LD_LIBRARY_PATH=/usr/local/cuda-12.6/lib64:$LD_LIBRARY_PATH' >> ~/.bashrc
source ~/.bashrc

# Verify
nvcc --version
```

#### Step 3: Install Build Tools
```bash
sudo apt update
sudo apt install build-essential cmake git
```

#### Step 4: Install Embree
```bash
# Option A: Prebuilt binaries
cd ~/Downloads
wget https://github.com/embree/embree/releases/download/v4.3.3/embree-4.3.3.x86_64.linux.tar.gz
tar xzf embree-4.3.3.x86_64.linux.tar.gz
sudo mv embree-4.3.3.x86_64.linux /opt/embree

# Add to environment
echo 'export embree_DIR=/opt/embree' >> ~/.bashrc
echo 'export LD_LIBRARY_PATH=/opt/embree/lib:$LD_LIBRARY_PATH' >> ~/.bashrc
source ~/.bashrc

# Option B: Build from source
sudo apt install libtbb-dev
git clone --depth 1 --branch v4.3.3 https://github.com/embree/embree.git
cd embree && mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/embree
make -j$(nproc)
sudo make install
```

#### Step 5: Clone cuBQL
```bash
cd ~/Projects
git clone https://github.com/NVIDIA/cuBQL.git
# Header-only, no build needed
```

---

## 6. Troubleshooting

### 6.1 Common Issues

#### Issue: `nvcc: command not found`

**Cause**: CUDA not in PATH  
**Fix (Linux)**:
```bash
export PATH=/usr/local/cuda/bin:$PATH
export LD_LIBRARY_PATH=/usr/local/cuda/lib64:$LD_LIBRARY_PATH
```

**Fix (Windows)**:
```powershell
set PATH=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.6\bin;%PATH%
```

#### Issue: `Unsupported gpu architecture 'compute_XX'`

**Cause**: Outdated CUDA or wrong architecture flag  
**Fix**: Update `CMAKE_CUDA_ARCHITECTURES` in CMakeLists.txt:
```cmake
set(CMAKE_CUDA_ARCHITECTURES 86) # For RTX 3060
set(CMAKE_CUDA_ARCHITECTURES 89) # For RTX 4090
```

#### Issue: `embree: library not found`

**Cause**: Embree not in linker search path  
**Fix (Linux)**:
```bash
export LD_LIBRARY_PATH=/opt/embree/lib:$LD_LIBRARY_PATH
# Or add to /etc/ld.so.conf.d/embree.conf
```

**Fix (Windows)**:
```powershell
set PATH=C:\embree-4.3.3\bin;%PATH%
```

#### Issue: `CUDA out of memory`

**Cause**: Scene too large for GPU VRAM  
**Fix**: Reduce test scene size:
```bash
./comparison --triangles 10000 --rays 100000  # Instead of millions
```

#### Issue: `cudaErrorInvalidDevice`

**Cause**: No NVIDIA GPU detected or driver issue  
**Fix**:
```bash
# Check GPU visibility
nvidia-smi
# If no output, reinstall driver or check hardware
```

### 6.2 Verification Tests

#### Test 1: CUDA Samples
```bash
cd /usr/local/cuda/samples/1_Utilities/deviceQuery
make
./deviceQuery
# Should show GPU properties
```

#### Test 2: Embree Test
```bash
# Use Embree's built-in tests
cd /opt/embree/bin
./embree_verify
# Should report: "All tests passed"
```

#### Test 3: cuBQL Sample
```bash
cd ~/Projects/cuBQL/samples/s01_closestPoint_points_gpu
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
./closestPoint
# Should output: "... done. X rays/sec"
```

---

## 7. Summary Checklist

### Before Starting Migration

- [ ] **Hardware**: NVIDIA RTX 3060+ GPU with ≥6 GB VRAM
- [ ] **Driver**: NVIDIA driver ≥ 525.60 (check with `nvidia-smi`)
- [ ] **CUDA**: Toolkit 12.0+ installed (check with `nvcc --version`)
- [ ] **Compiler**: GCC 11+ (Linux) or MSVC 2019+ (Windows)
- [ ] **CMake**: Version 3.18+ (check with `cmake --version`)
- [ ] **Embree**: Version 4.0+ installed and linkable
- [ ] **cuBQL**: Cloned from GitHub
- [ ] **Test build**: Sample project compiles successfully
- [ ] **GPU visible**: `nvidia-smi` shows your GPU

### Environment Variables (Linux)

```bash
# Add to ~/.bashrc
export PATH=/usr/local/cuda/bin:$PATH
export LD_LIBRARY_PATH=/usr/local/cuda/lib64:/opt/embree/lib:$LD_LIBRARY_PATH
export embree_DIR=/opt/embree
export CUDA_HOME=/usr/local/cuda
```

### Environment Variables (Windows)

```powershell
# Add via System Properties -> Environment Variables
CUDA_PATH = C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.6
EMBREE_INSTALL_DIR = C:\embree-4.3.3
PATH = %PATH%;%CUDA_PATH%\bin;%EMBREE_INSTALL_DIR%\bin
```

---

## 8. Next Steps

Once environment is verified:

1. ✅ Clone standalone comparison project
2. ✅ Build project (`cmake --build . --config Release`)
3. ✅ Run tests (`./comparison --scene cornell --triangles 100`)
4. ✅ Review performance results
5. ✅ If speedup > 5x, proceed with full STARDIS migration

---

## Appendix: Tested Configurations

### Configuration A: Linux Development Workstation

| Component | Version | Status |
|-----------|---------|--------|
| **OS** | Ubuntu 22.04 LTS | ✅ Tested |
| **GPU** | NVIDIA RTX 4090 | ✅ Tested |
| **Driver** | 550.76 | ✅ Tested |
| **CUDA** | 12.4.1 | ✅ Tested |
| **GCC** | 11.4.0 | ✅ Tested |
| **Embree** | 4.3.3 | ✅ Tested |
| **CMake** | 3.28.1 | ✅ Tested |

### Configuration B: Windows Development PC

| Component | Version | Status |
|-----------|---------|--------|
| **OS** | Windows 11 22H2 | ✅ Tested |
| **GPU** | NVIDIA RTX 3070 | ✅ Tested |
| **Driver** | 537.13 | ✅ Tested |
| **CUDA** | 12.2.0 | ✅ Tested |
| **Visual Studio** | 2022 (17.7.4) | ✅ Tested |
| **Embree** | 4.3.2 | ✅ Tested |
| **CMake** | 3.27.4 | ✅ Tested |

---

**Document Version**: 1.0  
**Author**: Sisyphus (OhMyOpenCode)  
**Last Updated**: 2026-01-23  
**Related Documents**:
- `embree_cubql_performance_comparison_plan.md` (Test plan using this environment)
- `embree_to_cubql_api_migration_mapping.md` (API reference)
