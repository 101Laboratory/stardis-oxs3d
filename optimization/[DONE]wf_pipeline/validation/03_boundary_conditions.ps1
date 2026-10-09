###############################################################################
# Phase 4 Step 3: Boundary Condition Tests
#
# Tests edge cases from Phase 4 doc §1.3:
#   - Single pixel (1x1 spp=1)  → prologue → 1 round → epilogue
#   - Pool size = 1             → degenerate per-path, still correct
#   - Small scene               → bit-exact serial vs pipeline
#
# NOTE: stardis dual-stream output:
#   stdout → .ht data only | stderr → all log/message output (10,000+ lines)
# Uses cmd /c for clean OS-level stream separation.
###############################################################################

$ErrorActionPreference = "Stop"
$ProjectRoot = "D:\Works\Projects\Stardis-GPU"
$Stardis     = "$ProjectRoot\stardis-cus3d\build\bin\Release\stardis.exe"
$SceneDir    = "$ProjectRoot\Stardis-Starter-Pack\porous"
$OutDir      = "$ProjectRoot\optimization\wf_pipeline\validation\results\boundary"
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

$Tests = @(
    @{
        Tag  = "1x1_spp1_p4096"
        Desc = "Single pixel — prologue+1 round+epilogue"
        Img  = "1x1"; Spp = 1; Pool = 4096
    },
    @{
        Tag  = "4x4_spp1_p1"
        Desc = "Pool size=1 — degenerate per-path"
        Img  = "4x4"; Spp = 1; Pool = 1
    },
    @{
        Tag  = "8x8_spp2_p16"
        Desc = "Small scene, tiny pool — many drain steps"
        Img  = "8x8"; Spp = 2; Pool = 16
    },
    @{
        Tag  = "16x16_spp1_p8"
        Desc = "Pool < total tasks — exercises refill+drain"
        Img  = "16x16"; Spp = 1; Pool = 8
    }
)

$AllPassed = $true
Push-Location $SceneDir

foreach ($t in $Tests) {
    $tag  = $t.Tag
    $desc = $t.Desc
    $img  = $t.Img
    $spp  = $t.Spp
    $pool = $t.Pool

    $serialFile   = "$OutDir\serial_${tag}.ht"
    $pipelineFile = "$OutDir\pipeline_${tag}.ht"
    $serialLog    = "$OutDir\serial_${tag}.log"
    $pipelineLog  = "$OutDir\pipeline_${tag}.log"

    Write-Host "`n========== Boundary: $tag ==========" -ForegroundColor Cyan
    Write-Host "  $desc"
    Write-Host "  Resolution=$img  spp=$spp  pool_size=$pool"

    # Serial mode
    Write-Host "  [1/2] Serial..." -ForegroundColor Yellow
    $env:STARDIS_POOL_SIZE = "$pool"
    $env:STARDIS_PIPELINE  = "0"
    $renderArgStr = "-M porous.txt -t 4 -V 3 -R spp=${spp}:img=${img}:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0"
    $serialExit = Invoke-Stardis -RenderArgs $renderArgStr -OutHt $serialFile -OutLog $serialLog
    Write-Host "        exit=$serialExit"
    Show-LogTail $serialLog

    if ($serialExit -ne 0) {
        Write-Host "  FAIL — serial mode crashed (exit=$serialExit)" -ForegroundColor Red
        $AllPassed = $false
        continue
    }

    # Pipeline mode
    Write-Host "  [2/2] Pipeline..." -ForegroundColor Yellow
    $env:STARDIS_PIPELINE = "1"
    $pipelineExit = Invoke-Stardis -RenderArgs $renderArgStr -OutHt $pipelineFile -OutLog $pipelineLog
    Write-Host "        exit=$pipelineExit"
    Show-LogTail $pipelineLog

    if ($pipelineExit -ne 0) {
        Write-Host "  FAIL — pipeline mode crashed (exit=$pipelineExit)" -ForegroundColor Red
        $AllPassed = $false
        continue
    }

    # Binary comparison
    Write-Host "  Comparing..." -NoNewline
    $fcResult = fc.exe /B $serialFile $pipelineFile 2>&1
    if ($fcResult -match "no differences encountered") {
        Write-Host " PASS (bit-exact)" -ForegroundColor Green
    } else {
        Write-Host " FAIL" -ForegroundColor Red
        Write-Host "  fc output: $fcResult"
        $AllPassed = $false
    }

    # Show pipeline summary from log
    $summary = Get-PipelineSummary $pipelineLog
    if ($summary) {
        Write-Host "  $summary" -ForegroundColor DarkCyan
    }
}

Pop-Location

Remove-Item Env:\STARDIS_POOL_SIZE -ErrorAction SilentlyContinue
Remove-Item Env:\STARDIS_PIPELINE  -ErrorAction SilentlyContinue

Write-Host "`n========================================" -ForegroundColor Cyan
if ($AllPassed) {
    Write-Host "ALL BOUNDARY CONDITION TESTS PASSED" -ForegroundColor Green
} else {
    Write-Host "SOME TESTS FAILED — see details above" -ForegroundColor Red
    exit 1
}
