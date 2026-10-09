###############################################################################
# Phase 4 Step 4: Performance Benchmark
#
# Measures wall-clock elapsed time for serial vs pipeline mode across multiple
# pool_size values (Phase 4 doc §2.1).
#
# Each config runs 3 times; the script reports min/max/avg and speedup ratio.
# Results are written to both console and a CSV file.
#
# NOTE: stardis dual-stream output:
#   stdout → .ht data only | stderr → all log/message output (10,000+ lines)
# Uses cmd /c for clean OS-level stream separation.
###############################################################################

$ErrorActionPreference = "Stop"
$ProjectRoot = "D:\Works\Projects\Stardis-GPU"
$Stardis     = "$ProjectRoot\stardis-cus3d\build\bin\Release\stardis.exe"
$SceneDir    = "$ProjectRoot\Stardis-Starter-Pack\porous"
$OutDir      = "$ProjectRoot\optimization\wf_pipeline\validation\results\performance"
$CsvFile     = "$OutDir\benchmark_results.csv"
$LogTailN    = 30

if (-not (Test-Path $Stardis)) {
    Write-Error "stardis.exe not found at $Stardis — build first."
    exit 1
}
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }

# ── Helpers ────────────────────────────────────────────────────────────────────
function Invoke-Stardis {
    param([string]$RenderArgs, [string]$OutHt, [string]$OutLog)
    cmd /c "`"$Stardis`" $RenderArgs > `"$OutHt`" 2> `"$OutLog`""
    return $LASTEXITCODE
}

function Show-LogTail {
    param([string]$LogFile, [int]$Lines = $LogTailN)
    if (Test-Path $LogFile) {
        $tail = Get-Content $LogFile -Tail $Lines -ErrorAction SilentlyContinue
        if ($tail) {
            Write-Host "        --- stderr tail ($Lines lines) ---" -ForegroundColor DarkGray
            $tail | ForEach-Object { Write-Host "        $_" -ForegroundColor DarkGray }
        }
    }
}

function Get-PipelineSummary {
    param([string]$LogFile)
    if (Test-Path $LogFile) {
        $line = Get-Content $LogFile -Tail 50 -ErrorAction SilentlyContinue |
            Select-String "pipeline_summary:" | Select-Object -Last 1
        if ($line) { return $line.Line.Trim() }
    }
    return $null
}

# ── Configuration ────────────────────────────────────────────────────────────
# Default: 320×320 spp=32 (the project's standard benchmark scene)
# Override via env: $env:BENCH_IMG, $env:BENCH_SPP, $env:BENCH_RUNS
$BenchImg  = if ($env:BENCH_IMG)  { $env:BENCH_IMG }  else { "320x320" }
$BenchSpp  = if ($env:BENCH_SPP)  { $env:BENCH_SPP }  else { "32" }
$NumRuns   = if ($env:BENCH_RUNS) { [int]$env:BENCH_RUNS } else { 3 }

$PoolSizes = @(4096, 10240, 32768)
$PassThresholds = @{
    4096  = 1.5
    10240 = 1.3
    32768 = 1.2
}

# ── CSV header ───────────────────────────────────────────────────────────────
"pool_size,mode,run,elapsed_s" | Out-File -FilePath $CsvFile -Encoding utf8

Push-Location $SceneDir

$Results = @{}

foreach ($pool in $PoolSizes) {
    $Results[$pool] = @{ serial = @(); pipeline = @() }

    Write-Host "`n========== pool_size=$pool  img=$BenchImg  spp=$BenchSpp ==========" -ForegroundColor Cyan

    foreach ($mode in @("serial", "pipeline")) {
        $envVal = if ($mode -eq "serial") { "0" } else { "1" }
        $env:STARDIS_POOL_SIZE = "$pool"
        $env:STARDIS_PIPELINE  = $envVal

        Write-Host "  Mode: $mode" -ForegroundColor Yellow

        for ($r = 1; $r -le $NumRuns; $r++) {
            $outFile = "$OutDir\${mode}_pool${pool}_run${r}.ht"
            $logFile = "$OutDir\${mode}_pool${pool}_run${r}.log"

            Write-Host "    Run $r/$NumRuns ... " -NoNewline
            $renderArgStr = "-M porous.txt -t 4 -V 3 -R spp=${BenchSpp}:img=${BenchImg}:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0"
            $sw = [System.Diagnostics.Stopwatch]::StartNew()
            Invoke-Stardis -RenderArgs $renderArgStr -OutHt $outFile -OutLog $logFile | Out-Null
            $sw.Stop()
            $elapsed = $sw.Elapsed.TotalSeconds

            Write-Host ("{0:N1}s" -f $elapsed) -ForegroundColor White
            $Results[$pool][$mode] += $elapsed
            "${pool},${mode},${r},${elapsed}" | Out-File -FilePath $CsvFile -Append -Encoding utf8

            # Show pipeline summary for last run of pipeline mode
            if ($mode -eq "pipeline" -and $r -eq $NumRuns) {
                $summary = Get-PipelineSummary $logFile
                if ($summary) { Write-Host "      $summary" -ForegroundColor DarkCyan }
            }
        }
    }
}

Pop-Location

Remove-Item Env:\STARDIS_POOL_SIZE -ErrorAction SilentlyContinue
Remove-Item Env:\STARDIS_PIPELINE  -ErrorAction SilentlyContinue

# ── Summary ──────────────────────────────────────────────────────────────────
Write-Host "`n"
Write-Host ("=" * 72) -ForegroundColor Cyan
Write-Host "PERFORMANCE BENCHMARK SUMMARY" -ForegroundColor Cyan
Write-Host ("=" * 72) -ForegroundColor Cyan
Write-Host ("{0,-10} {1,12} {2,12} {3,10} {4,10}" -f "pool_size", "serial_avg", "pipeline_avg", "speedup", "pass?")
Write-Host ("{0,-10} {1,12} {2,12} {3,10} {4,10}" -f "---------", "----------", "------------", "-------", "-----")

$AllPassed = $true

foreach ($pool in $PoolSizes) {
    $sAvg = ($Results[$pool].serial  | Measure-Object -Average).Average
    $pAvg = ($Results[$pool].pipeline | Measure-Object -Average).Average
    $speedup = $sAvg / [Math]::Max($pAvg, 0.001)
    $threshold = $PassThresholds[$pool]
    $pass = $speedup -ge $threshold

    $passStr = if ($pass) { "YES" } else { "NO" }
    $color   = if ($pass) { "Green" } else { "Red" }

    Write-Host ("{0,-10} {1,10:N1}s {2,10:N1}s {3,8:N2}x {4,10}" -f $pool, $sAvg, $pAvg, $speedup, $passStr) -ForegroundColor $color

    if (-not $pass) { $AllPassed = $false }
}

Write-Host ""
Write-Host "Threshold: pool=4096 >= 1.5x, pool=10240 >= 1.3x, pool=32768 >= 1.2x"
Write-Host "Results saved to: $CsvFile"

if ($AllPassed) {
    Write-Host "`nALL PERFORMANCE BENCHMARKS PASSED" -ForegroundColor Green
} else {
    Write-Host "`nSOME BENCHMARKS BELOW THRESHOLD — see details above" -ForegroundColor Red
    exit 1
}
