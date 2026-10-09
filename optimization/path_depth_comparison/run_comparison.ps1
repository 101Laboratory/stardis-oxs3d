# =============================================================================
# Path Depth Comparison — Quick-start Run Script
# =============================================================================
# Usage:  powershell -ExecutionPolicy Bypass -File run_comparison.ps1
#
# Prerequisites:
#   - CPU solver built:   stardis-cpu/build/bin/Release/stardis.exe
#   - GPU solver built:   stardis-cus3d/build/bin/Release/stardis.exe
#   - Instrumentation patches applied to both versions
#   - Python 3 with pandas/numpy (for analysis)
#
# Adjust SPP and resolution for desired accuracy vs speed tradeoff:
#   Quick test:  SPP=1  IMG=16x16   (~seconds)
#   Fast run:    SPP=8  IMG=64x64   (~minutes)
#   Full run:    SPP=32 IMG=320x320 (~hours)
# =============================================================================

param(
    [string]$Mode = "fast",   # "quick", "fast", or "full"
    [int]$Threads = 1         # -t parameter; use 1 for deterministic comparison
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$OutputDir = $PSScriptRoot
$PorousDir = Join-Path $ProjectRoot "Stardis-Starter-Pack\porous"

$CpuExe = Join-Path $ProjectRoot "stardis-cpu\build\bin\Release\stardis.exe"
$GpuExe = Join-Path $ProjectRoot "stardis-cus3d\build\bin\Release\stardis.exe"

# Select parameters based on mode
switch ($Mode) {
    "quick" { $SPP = 1;  $IMG = "16x16"  }
    "fast"  { $SPP = 8;  $IMG = "64x64"  }
    "full"  { $SPP = 32; $IMG = "320x320" }
    default { Write-Error "Unknown mode: $Mode. Use quick/fast/full."; exit 1 }
}

$RenderArgs = "spp=${SPP}:img=${IMG}:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0"

Write-Host "===============================================" -ForegroundColor Cyan
Write-Host " Path Depth Comparison - Mode: $Mode" -ForegroundColor Cyan
Write-Host " SPP=$SPP  IMG=$IMG  Threads=$Threads" -ForegroundColor Cyan
Write-Host "===============================================" -ForegroundColor Cyan

# Check executables
if (!(Test-Path $CpuExe)) { Write-Error "CPU executable not found: $CpuExe"; exit 1 }
if (!(Test-Path $GpuExe)) { Write-Error "GPU executable not found: $GpuExe"; exit 1 }

Push-Location $PorousDir

# ========================= CPU Run =========================
Write-Host "`n>>> Running CPU solver..." -ForegroundColor Yellow
$env:STARDIS_PATH_DEPTH_CSV = Join-Path $OutputDir "cpu_path_depth.csv"
$env:STARDIS_PIXEL_TRACE_CPU = Join-Path $OutputDir "cpu_pixel_trace.csv"

$cpuStart = Get-Date
& $CpuExe -M porous.txt -t $Threads -V 3 -R $RenderArgs > (Join-Path $OutputDir "cpu_IR.ht")
$cpuElapsed = (Get-Date) - $cpuStart
Write-Host "CPU completed in $($cpuElapsed.ToString('hh\:mm\:ss'))" -ForegroundColor Green

# Clean up env vars
Remove-Item Env:STARDIS_PATH_DEPTH_CSV -ErrorAction SilentlyContinue
Remove-Item Env:STARDIS_PIXEL_TRACE_CPU -ErrorAction SilentlyContinue

# ========================= GPU Run =========================
Write-Host "`n>>> Running GPU wavefront solver..." -ForegroundColor Yellow
$env:STARDIS_PIXEL_TRACE = Join-Path $OutputDir "gpu_pixel_trace.csv"
$env:STARDIS_POOL_SIZE = "4096"

$gpuStart = Get-Date
& $GpuExe -M porous.txt -t $Threads -V 3 -R $RenderArgs > (Join-Path $OutputDir "gpu_IR.ht")
$gpuElapsed = (Get-Date) - $gpuStart
Write-Host "GPU completed in $($gpuElapsed.ToString('hh\:mm\:ss'))" -ForegroundColor Green

Remove-Item Env:STARDIS_PIXEL_TRACE -ErrorAction SilentlyContinue
Remove-Item Env:STARDIS_POOL_SIZE -ErrorAction SilentlyContinue

Pop-Location

# ========================= Analysis =========================
Write-Host "`n>>> Running analysis..." -ForegroundColor Yellow

$CpuCsv = Join-Path $OutputDir "cpu_path_depth.csv"
$GpuCsv = Join-Path $OutputDir "gpu_pixel_trace.csv"
$Report = Join-Path $OutputDir "comparison_report.md"

if ((Test-Path $CpuCsv) -and (Test-Path $GpuCsv)) {
    python (Join-Path $OutputDir "analyze_path_depth.py") `
        --cpu $CpuCsv --gpu $GpuCsv --out $Report
    Write-Host "`nReport generated: $Report" -ForegroundColor Green
} else {
    Write-Warning "CSV files not found. Ensure instrumentation is active."
    if (!(Test-Path $CpuCsv)) { Write-Warning "  Missing: $CpuCsv" }
    if (!(Test-Path $GpuCsv)) { Write-Warning "  Missing: $GpuCsv" }
}

Write-Host "`n===============================================" -ForegroundColor Cyan
Write-Host " Comparison complete" -ForegroundColor Cyan
Write-Host " CPU: $($cpuElapsed.ToString('hh\:mm\:ss'))" -ForegroundColor Cyan
Write-Host " GPU: $($gpuElapsed.ToString('hh\:mm\:ss'))" -ForegroundColor Cyan
Write-Host "===============================================" -ForegroundColor Cyan
