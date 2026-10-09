#!/usr/bin/env pwsh
# run_csv_tests.ps1 - 设置 SDIS_CSV_DIR 环境变量并依次运行 wavefront 测试
# 用法: .\run_csv_tests.ps1 [-Only <test_name>]

param(
    [string]$Only = ""          # 只运行指定测试，如 "A3" 或 "i1"
)

# ── 配置 ──
$BinDir    = "D:\Stardis-GPU\stardis-oxs3d\build\bin\Release"
$CsvOutDir = "D:\Stardis-GPU\csv_output\gpu"
$env:STARDIS_POOL_SIZE="16384"

$Tests = @(
    "test_sdis_wf_a7_solve_probe3",
    "test_sdis_wf_b3_boundary_flux",
    "test_sdis_wf_f2_diffuse_radiance",
    "test_sdis_wf_b5_probe_list",
    "test_sdis_wf_i1_volumic_power2",
    "test_sdis_wf_c3_picard_multi",
    "test_sdis_wf_d2_convection_nonuniform",
    "test_sdis_wf_e5_unsteady_atm",
    "test_sdis_wf_f1_external_flux",
    "test_sdis_wf_a3_contact_resistance"
)

# ── 准备 ──
if (-not (Test-Path $CsvOutDir)) {
    New-Item -ItemType Directory -Path $CsvOutDir -Force | Out-Null
}
$env:SDIS_CSV_DIR = $CsvOutDir

Write-Host "SDIS_CSV_DIR = $CsvOutDir" -ForegroundColor Cyan
Write-Host ""

foreach ($testName in $Tests) {
    if ($Only -ne "" -and $testName -notlike "*$Only*") { continue }

    $exePath = Join-Path $BinDir "$testName.exe"
    if (-not (Test-Path $exePath)) {
        Write-Host "[SKIP] $testName.exe not found" -ForegroundColor DarkGray
        continue
    }

    Write-Host ">>> $testName" -ForegroundColor Yellow
    & $exePath
    Write-Host ""
}

# ── 列出生成的 CSV 文件 ──
Write-Host "CSV files in $CsvOutDir :" -ForegroundColor Cyan
Get-ChildItem $CsvOutDir -Filter "*.csv" -ErrorAction SilentlyContinue | Format-Table Name, Length, LastWriteTime -AutoSize
