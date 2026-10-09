###############################################################################
# Cascade OMP Validation Step 2: Performance Benchmark
#
# Measures wall-clock elapsed time with cascade OMP disabled vs enabled,
# across multiple thread counts.  Reports min/max/avg and speedup ratio.
#
# Controls:
#   STARDIS_CASCADE_OMP=0  -> serial cascade (baseline)
#   STARDIS_CASCADE_OMP=1  -> OMP-parallel cascade (default)
#   -t <N>                 -> thread count (scn->dev->nthreads)
#
# The cascade loop consumes ~65% of CPU time (85.7s of 132.8s on the
# standard 320x320 spp=32 benchmark).  Expected speedup: 17-22% overall
# with 4+ threads.
#
# NOTE: stardis dual-stream output:
#   stdout -> .ht data only | stderr -> all log/message output (10,000+ lines)
# Uses cmd /c for clean OS-level stream separation.
###############################################################################

$ErrorActionPreference = "Stop"
$ProjectRoot = "D:\Stardis-GPU"
$Stardis     = "$ProjectRoot\stardis-cus3d\build\bin\Release\stardis.exe"
$SceneDir    = "$ProjectRoot\Stardis-Starter-Pack\porous"
$OutDir      = "$ProjectRoot\optimization\omp\results\performance"
$CsvFile     = "$OutDir\benchmark_results.csv"
$LogTailN    = 30
$env:STARDIS_POOL_SIZE="2560"

if (-not (Test-Path $Stardis)) {
    Write-Error "stardis.exe not found at $Stardis -- build first."
    exit 1
}
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }

# -- Helpers ----------------------------------------------------------------
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

function Get-CascadeOmpInfo {
    param([string]$LogFile)
    if (Test-Path $LogFile) {
        $line = Get-Content $LogFile -ErrorAction SilentlyContinue |
            Select-String "Cascade OMP:" | Select-Object -First 1
        if ($line) { return $line.Line.Trim() }
    }
    return $null
}

# Extract elapsed time from log (look for "Rendering complete" or final timing)
function Get-RenderElapsed {
    param([string]$LogFile)
    if (Test-Path $LogFile) {
        # Match pattern like "elapsed: 123.456s" or "Elapsed ...  123.4 s"
        $lines = Get-Content $LogFile -ErrorAction SilentlyContinue
        foreach ($line in $lines) {
            if ($line -match 'elapsed[:\s]+(\d+\.?\d*)\s*s') {
                return [double]$Matches[1]
            }
        }
    }
    return $null
}

# -- Configuration -----------------------------------------------------------
# Default: 256x256 spp=8 (project standard benchmark)
# Override via env: $env:BENCH_IMG, $env:BENCH_SPP, $env:BENCH_RUNS
$BenchImg  = if ($env:BENCH_IMG)  { $env:BENCH_IMG }  else { "256x256" }
$BenchSpp  = if ($env:BENCH_SPP)  { [int]$env:BENCH_SPP }  else { 4 }
$NumRuns   = if ($env:BENCH_RUNS) { [int]$env:BENCH_RUNS } else { 1 }

# Thread counts to benchmark (including serial baseline)
$ThreadCounts = @(32)

# -- CSV header --------------------------------------------------------------
"mode,threads,run,wall_clock_s" | Out-File -FilePath $CsvFile -Encoding utf8

Push-Location $SceneDir

$Results = @{}

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "  Cascade OMP Performance Benchmark" -ForegroundColor Cyan
Write-Host "  Scene: porous  Resolution: $BenchImg  spp: $BenchSpp" -ForegroundColor Cyan
Write-Host "  Runs per config: $NumRuns" -ForegroundColor Cyan
Write-Host "  Thread counts: $($ThreadCounts -join ', ')" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

# ---- Baseline: OMP disabled (serial cascade) ----
Write-Host "`n========== Baseline: CASCADE_OMP=0 (serial) ==========" -ForegroundColor Yellow

$env:STARDIS_CASCADE_OMP = "0"
$baselineTimes = @()

$renderArgs = "-M porous.txt -t 4 -V 3 " +
    "-R spp=${BenchSpp}:img=${BenchImg}:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0"

for ($run = 1; $run -le $NumRuns; $run++) {
    $htFile  = "$OutDir\baseline_run${run}.ht"
    $logFile = "$OutDir\baseline_run${run}.log"

    Write-Host "  Run $run/$NumRuns..." -NoNewline
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $exitCode = Invoke-Stardis -RenderArgs $renderArgs -OutHt $htFile -OutLog $logFile
    $sw.Stop()
    $elapsed = $sw.Elapsed.TotalSeconds
    $baselineTimes += $elapsed
    "serial,1,$run,$($elapsed.ToString('F3'))" | Add-Content -Path $CsvFile -Encoding utf8
    Write-Host " $($elapsed.ToString('F1'))s (exit=$exitCode)"

    $info = Get-CascadeOmpInfo $logFile
    if ($info) { Write-Host "        $info" -ForegroundColor DarkCyan }
}

$baselineAvg = ($baselineTimes | Measure-Object -Average).Average
$baselineMin = ($baselineTimes | Measure-Object -Minimum).Minimum
$baselineMax = ($baselineTimes | Measure-Object -Maximum).Maximum
Write-Host "  Baseline: avg=$($baselineAvg.ToString('F1'))s  min=$($baselineMin.ToString('F1'))s  max=$($baselineMax.ToString('F1'))s" -ForegroundColor White

# ---- OMP enabled with varying thread counts ----
$env:STARDIS_CASCADE_OMP = "1"

foreach ($threads in $ThreadCounts) {
    Write-Host "`n========== CASCADE_OMP=1, threads=$threads ==========" -ForegroundColor Yellow

    $ompTimes = @()
    $ompRenderArgs = "-M porous.txt -t $threads -V 3 " +
        "-R spp=${BenchSpp}:img=${BenchImg}:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0"

    for ($run = 1; $run -le $NumRuns; $run++) {
        $htFile  = "$OutDir\omp_t${threads}_run${run}.ht"
        $logFile = "$OutDir\omp_t${threads}_run${run}.log"

        Write-Host "  Run $run/$NumRuns..." -NoNewline
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        $exitCode = Invoke-Stardis -RenderArgs $ompRenderArgs -OutHt $htFile -OutLog $logFile
        $sw.Stop()
        $elapsed = $sw.Elapsed.TotalSeconds
        $ompTimes += $elapsed
        "omp,$threads,$run,$($elapsed.ToString('F3'))" | Add-Content -Path $CsvFile -Encoding utf8
        Write-Host " $($elapsed.ToString('F1'))s (exit=$exitCode)"

        $info = Get-CascadeOmpInfo $logFile
        if ($info) { Write-Host "        $info" -ForegroundColor DarkCyan }
    }

    $ompAvg = ($ompTimes | Measure-Object -Average).Average
    $ompMin = ($ompTimes | Measure-Object -Minimum).Minimum
    $ompMax = ($ompTimes | Measure-Object -Maximum).Maximum
    $speedup = if ($ompAvg -gt 0) { $baselineAvg / $ompAvg } else { 0 }

    Write-Host "  OMP t=$threads`: avg=$($ompAvg.ToString('F1'))s  min=$($ompMin.ToString('F1'))s  max=$($ompMax.ToString('F1'))s" -ForegroundColor White
    $color = if ($speedup -ge 1.1) { "Green" } elseif ($speedup -ge 1.0) { "Yellow" } else { "Red" }
    Write-Host "  Speedup vs baseline: $($speedup.ToString('F2'))x" -ForegroundColor $color

    $Results[$threads] = @{
        Avg     = $ompAvg
        Min     = $ompMin
        Max     = $ompMax
        Speedup = $speedup
    }
}

Pop-Location

# Clean up env vars
Remove-Item Env:\STARDIS_CASCADE_OMP -ErrorAction SilentlyContinue

# -- Summary table -----------------------------------------------------------
Write-Host "`n==========================================================" -ForegroundColor Cyan
Write-Host "  SUMMARY" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host ("  {0,-12} {1,10} {2,10} {3,10} {4,10}" -f "Mode", "Avg(s)", "Min(s)", "Max(s)", "Speedup")
Write-Host ("  {0,-12} {1,10} {2,10} {3,10} {4,10}" -f "----", "------", "------", "------", "-------")
Write-Host ("  {0,-12} {1,10} {2,10} {3,10} {4,10}" -f "serial",
    $baselineAvg.ToString('F1'), $baselineMin.ToString('F1'),
    $baselineMax.ToString('F1'), "1.00x")

foreach ($threads in $ThreadCounts) {
    if ($Results.ContainsKey($threads)) {
        $r = $Results[$threads]
        Write-Host ("  {0,-12} {1,10} {2,10} {3,10} {4,10}" -f "omp-t$threads",
            $r.Avg.ToString('F1'), $r.Min.ToString('F1'),
            $r.Max.ToString('F1'), "$($r.Speedup.ToString('F2'))x")
    }
}

Write-Host "`nCSV written to: $CsvFile" -ForegroundColor DarkGray
Write-Host "==========================================================" -ForegroundColor Cyan
