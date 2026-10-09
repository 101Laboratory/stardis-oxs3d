###############################################################################
# Phase 4 Step 1: Bit-exact Validation
#
# Compares serial (STARDIS_PIPELINE=0) vs pipeline (STARDIS_PIPELINE=1) output
# across multiple resolution/spp/pool_size configurations.
# Expected: FC reports "no differences encountered" for every pair.
#
# NOTE: stardis dual-stream output:
#   stdout → .ht data only (binary/text temperature field)
#   stderr → all log/message output (potentially 10,000+ lines)
# We use cmd /c for clean stream separation, avoiding PowerShell's
# stderr-as-ErrorRecord wrapping which would corrupt .ht data or abort.
###############################################################################

$ErrorActionPreference = "Stop"
$ProjectRoot = "D:\Works\Projects\Stardis-GPU"
$Stardis     = "$ProjectRoot\stardis-cus3d\build\bin\Release\stardis.exe"
$SceneDir    = "$ProjectRoot\Stardis-Starter-Pack\porous"
$OutDir      = "$ProjectRoot\optimization\wf_pipeline\validation\results\bitexact"
$LogTailN    = 30  # Show last N lines of stderr for diagnostics

if (-not (Test-Path $Stardis)) {
    Write-Error "stardis.exe not found at $Stardis — build first."
    exit 1
}
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }

# ── Helper: run stardis with clean stdout/stderr separation via cmd /c ────
function Invoke-Stardis {
    param(
        [string]$RenderArgs,
        [string]$OutHt,
        [string]$OutLog
    )
    # cmd /c handles > and 2> at the OS level — no ErrorRecord wrapping
    cmd /c "`"$Stardis`" $RenderArgs > `"$OutHt`" 2> `"$OutLog`""
    return $LASTEXITCODE
}

# ── Helper: show tail of stderr log (the useful part) ─────────────────────
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

# ── Helper: extract pipeline_summary from log ─────────────────────────────
function Get-PipelineSummary {
    param([string]$LogFile)
    if (Test-Path $LogFile) {
        $line = Get-Content $LogFile -Tail 50 -ErrorAction SilentlyContinue |
            Select-String "pipeline_summary:" | Select-Object -Last 1
        if ($line) { return $line.Line.Trim() }
    }
    return $null
}

# Validation matrix from Phase 4 doc §1.1
$Tests = @(
    @{ Tag="64x64_spp1_p4096";    Img="64x64";    Spp=1;  Pool=4096  },
    @{ Tag="256x256_spp4_p4096";   Img="256x256";  Spp=4;  Pool=4096  },
    @{ Tag="320x320_spp32_p4096";  Img="320x320";  Spp=32; Pool=4096  },
    @{ Tag="320x320_spp32_p10240"; Img="320x320";  Spp=32; Pool=10240 },
    @{ Tag="320x320_spp32_p32768"; Img="320x320";  Spp=32; Pool=32768 }
)

$AllPassed = $true
Push-Location $SceneDir

foreach ($t in $Tests) {
    $tag  = $t.Tag
    $img  = $t.Img
    $spp  = $t.Spp
    $pool = $t.Pool

    $serialFile   = "$OutDir\serial_${tag}.ht"
    $pipelineFile = "$OutDir\pipeline_${tag}.ht"
    $serialLog    = "$OutDir\serial_${tag}.log"
    $pipelineLog  = "$OutDir\pipeline_${tag}.log"

    $renderArgs = "-M porous.txt -t 4 -V 3 " +
        "-R spp=${spp}:img=${img}:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0"

    Write-Host "`n========== Test: $tag ==========" -ForegroundColor Cyan
    Write-Host "  Resolution=$img  spp=$spp  pool_size=$pool"

    # Serial mode
    Write-Host "  [1/2] Running serial mode..." -ForegroundColor Yellow
    $env:STARDIS_POOL_SIZE = "$pool"
    $env:STARDIS_PIPELINE  = "0"
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $serialExit = Invoke-Stardis -RenderArgs $renderArgs -OutHt $serialFile -OutLog $serialLog
    $sw.Stop()
    Write-Host "        Serial done in $($sw.Elapsed.TotalSeconds.ToString('F1'))s (exit=$serialExit)"
    Show-LogTail $serialLog

    # Pipeline mode
    Write-Host "  [2/2] Running pipeline mode..." -ForegroundColor Yellow
    $env:STARDIS_PIPELINE = "1"
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $pipelineExit = Invoke-Stardis -RenderArgs $renderArgs -OutHt $pipelineFile -OutLog $pipelineLog
    $sw.Stop()
    Write-Host "        Pipeline done in $($sw.Elapsed.TotalSeconds.ToString('F1'))s (exit=$pipelineExit)"
    Show-LogTail $pipelineLog

    # Show pipeline diagnostics
    $summary = Get-PipelineSummary $pipelineLog
    if ($summary) { Write-Host "        $summary" -ForegroundColor DarkCyan }

    # Binary comparison (compares only stdout .ht data — clean, no log pollution)
    Write-Host "  Comparing..." -NoNewline
    $fcResult = fc.exe /B $serialFile $pipelineFile 2>&1
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
Remove-Item Env:\STARDIS_POOL_SIZE -ErrorAction SilentlyContinue
Remove-Item Env:\STARDIS_PIPELINE  -ErrorAction SilentlyContinue

Write-Host "`n========================================" -ForegroundColor Cyan
if ($AllPassed) {
    Write-Host "ALL BIT-EXACT TESTS PASSED" -ForegroundColor Green
} else {
    Write-Host "SOME TESTS FAILED — see details above" -ForegroundColor Red
    exit 1
}
