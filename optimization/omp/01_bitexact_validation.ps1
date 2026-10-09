###############################################################################
# Cascade OMP Validation Step 1: Bit-exact Correctness
#
# Compares serial (STARDIS_CASCADE_OMP=0) vs OMP (STARDIS_CASCADE_OMP=1)
# output across multiple resolution/spp configurations.
# Expected: FC reports "no differences encountered" for every pair.
#
# The OMP parallelization only affects the cascade loop scheduling;
# the per-path logic and RNG state are identical, so output must be
# bit-exact regardless of thread count.
#
# NOTE: stardis dual-stream output:
#   stdout -> .ht data only (binary/text temperature field)
#   stderr -> all log/message output (potentially 10,000+ lines)
# We use cmd /c for clean stream separation, avoiding PowerShell's
# stderr-as-ErrorRecord wrapping which would corrupt .ht data or abort.
###############################################################################

$ErrorActionPreference = "Stop"
$ProjectRoot = "D:\Works\Projects\Stardis-GPU"
$Stardis     = "$ProjectRoot\stardis-cus3d\build\bin\Release\stardis.exe"
$SceneDir    = "$ProjectRoot\Stardis-Starter-Pack\porous"
$OutDir      = "$ProjectRoot\optimization\omp\results\bitexact"
$LogTailN    = 30  # Show last N lines of stderr for diagnostics

if (-not (Test-Path $Stardis)) {
    Write-Error "stardis.exe not found at $Stardis -- build first."
    exit 1
}
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }

# -- Helper: run stardis with clean stdout/stderr separation via cmd /c ----
function Invoke-Stardis {
    param(
        [string]$RenderArgs,
        [string]$OutHt,
        [string]$OutLog
    )
    cmd /c "`"$Stardis`" $RenderArgs > `"$OutHt`" 2> `"$OutLog`""
    return $LASTEXITCODE
}

# -- Helper: show tail of stderr log (the useful part) ---------------------
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

# -- Helper: extract cascade OMP info from log -----------------------------
function Get-CascadeOmpInfo {
    param([string]$LogFile)
    if (Test-Path $LogFile) {
        $line = Get-Content $LogFile -ErrorAction SilentlyContinue |
            Select-String "Cascade OMP:" | Select-Object -First 1
        if ($line) { return $line.Line.Trim() }
    }
    return $null
}

# Thread count: default = 4 (matches standard -t 4 in project convention)
# Override via env: $env:CASCADE_THREADS
$Threads = if ($env:CASCADE_THREADS) { [int]$env:CASCADE_THREADS } else { 4 }

# Validation matrix: multiple configs to stress-test determinism
$Tests = @(
    @{ Tag="64x64_spp1";     Img="64x64";    Spp=1  },
    @{ Tag="64x64_spp4";     Img="64x64";    Spp=4  },
    @{ Tag="128x128_spp4";   Img="128x128";  Spp=4  },
    @{ Tag="320x320_spp32";  Img="320x320";  Spp=32 }
)

$AllPassed = $true
Push-Location $SceneDir

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "  Cascade OMP Bit-exact Validation" -ForegroundColor Cyan
Write-Host "  Threads: $Threads" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

foreach ($t in $Tests) {
    $tag  = $t.Tag
    $img  = $t.Img
    $spp  = $t.Spp

    $serialFile = "$OutDir\serial_${tag}.ht"
    $ompFile    = "$OutDir\omp_${tag}.ht"
    $serialLog  = "$OutDir\serial_${tag}.log"
    $ompLog     = "$OutDir\omp_${tag}.log"

    $renderArgs = "-M porous.txt -t $Threads -V 3 " +
        "-R spp=${spp}:img=${img}:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0"

    Write-Host "`n========== Test: $tag ==========" -ForegroundColor Cyan
    Write-Host "  Resolution=$img  spp=$spp  threads=$Threads"

    # Serial mode (OMP disabled)
    Write-Host "  [1/2] Running serial mode (STARDIS_CASCADE_OMP=0)..." -ForegroundColor Yellow
    $env:STARDIS_CASCADE_OMP = "0"
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $serialExit = Invoke-Stardis -RenderArgs $renderArgs -OutHt $serialFile -OutLog $serialLog
    $sw.Stop()
    Write-Host "        Serial done in $($sw.Elapsed.TotalSeconds.ToString('F1'))s (exit=$serialExit)"
    $info = Get-CascadeOmpInfo $serialLog
    if ($info) { Write-Host "        $info" -ForegroundColor DarkCyan }
    Show-LogTail $serialLog

    # OMP mode (OMP enabled)
    Write-Host "  [2/2] Running OMP mode (STARDIS_CASCADE_OMP=1)..." -ForegroundColor Yellow
    $env:STARDIS_CASCADE_OMP = "1"
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $ompExit = Invoke-Stardis -RenderArgs $renderArgs -OutHt $ompFile -OutLog $ompLog
    $sw.Stop()
    Write-Host "        OMP done in $($sw.Elapsed.TotalSeconds.ToString('F1'))s (exit=$ompExit)"
    $info = Get-CascadeOmpInfo $ompLog
    if ($info) { Write-Host "        $info" -ForegroundColor DarkCyan }
    Show-LogTail $ompLog

    # Binary comparison
    Write-Host "  Comparing..." -NoNewline
    $fcResult = fc.exe /B $serialFile $ompFile 2>&1
    if ($fcResult -match "no differences encountered") {
        Write-Host " PASS (bit-exact)" -ForegroundColor Green
    } else {
        Write-Host " FAIL" -ForegroundColor Red
        Write-Host "  fc output: $fcResult"
        $AllPassed = $false
    }
}

Pop-Location

# Clean up env vars
Remove-Item Env:\STARDIS_CASCADE_OMP -ErrorAction SilentlyContinue

Write-Host "`n==========================================================" -ForegroundColor Cyan
if ($AllPassed) {
    Write-Host "ALL BIT-EXACT TESTS PASSED" -ForegroundColor Green
    Write-Host "Cascade OMP produces identical output to serial." -ForegroundColor Green
} else {
    Write-Host "SOME TESTS FAILED -- see details above" -ForegroundColor Red
    Write-Host "OMP may have introduced non-determinism." -ForegroundColor Red
    exit 1
}
