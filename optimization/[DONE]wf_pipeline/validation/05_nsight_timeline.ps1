###############################################################################
# Nsight Systems Timeline Profiling - Serial Mode Only
#
# Captures GPU timeline for serial execution mode using nsys.
#
# Prerequisites:
#   - NVIDIA Nsight Systems CLI (nsys) in PATH
#   - CUDA-capable GPU
#
# Produces .nsys-rep file that can be opened in Nsight Systems GUI to
# analyze CPU/GPU execution pattern and kernel behavior.
#
# NOTE: stardis dual-stream output:
#   stdout → .ht data only | stderr → all log/message output (10,000+ lines)
# Uses cmd /c for clean OS-level stream separation.
###############################################################################

$ErrorActionPreference = "Stop"
$ProjectRoot = "D:\Works\Projects\Stardis-GPU"
$Stardis     = "$ProjectRoot\stardis-cus3d\build\bin\Release\stardis.exe"
$SceneDir    = "$ProjectRoot\Stardis-Starter-Pack\porous"
$OutDir      = "$ProjectRoot\optimization\wf_pipeline\validation\results\nsight"
$LogTailN    = 30

if (-not (Test-Path $Stardis)) {
    Write-Error "stardis.exe not found at $Stardis — build first."
    exit 1
}

# Check nsys availability
$nsys = Get-Command nsys -ErrorAction SilentlyContinue
if (-not $nsys) {
    Write-Warning "nsys (Nsight Systems CLI) not found in PATH."
    Write-Warning "Typical location: C:\Program Files\NVIDIA Corporation\Nsight Systems *\target-windows-x64\nsys.exe"
    Write-Warning "Add it to PATH or set `$env:NSYS_PATH before running this script."

    if ($env:NSYS_PATH -and (Test-Path $env:NSYS_PATH)) {
        $nsysCmd = $env:NSYS_PATH
        Write-Host "Using NSYS_PATH=$nsysCmd" -ForegroundColor Yellow
    } else {
        Write-Error "Cannot proceed without nsys. Exiting."
        exit 1
    }
} else {
    $nsysCmd = $nsys.Source
}

if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }

# ── Smaller scene for profiling (256×256 spp=8 — fast but representative) ──
$BenchImg = "256x256"
$BenchSpp = "8"
$PoolSize = "4096"

$RenderArgStr = "-M porous.txt -t 4 -V 3 -R spp=${BenchSpp}:img=${BenchImg}:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0"

# ── Helper: show tail of stderr log ──────────────────────────────────────────
function Show-LogTail {
    param([string]$LogFile, [int]$Lines = $LogTailN)
    if (Test-Path $LogFile) {
        $tail = Get-Content $LogFile -Tail $Lines -ErrorAction SilentlyContinue
        if ($tail) {
            Write-Host "`n    --- stderr tail ($Lines lines) ---" -ForegroundColor DarkGray
            $tail | ForEach-Object { Write-Host "    $_" -ForegroundColor DarkGray }
        }
    }
}

function Get-TimingSummary {
    param([string]$LogFile)
    if (Test-Path $LogFile) {
        # Extract key timing info from log
        $content = Get-Content $LogFile -ErrorAction SilentlyContinue
        $timingLine = $content | Select-String "timing:" | Select-Object -Last 1
        $totalTime = $content | Select-String "total time:" | Select-Object -Last 1
        
        $result = @()
        if ($timingLine) { $result += $timingLine.Line.Trim() }
        if ($totalTime) { $result += $totalTime.Line.Trim() }
        return $result
    }
    return $null
}

Push-Location $SceneDir
$env:STARDIS_POOL_SIZE = $PoolSize
$env:STARDIS_PIPELINE = "0"  # Serial mode only

# ── Profile: Serial mode ─────────────────────────────────────────────────
Write-Host "`n========== Nsight Systems Profiling (Serial Mode) ==========" -ForegroundColor Cyan
Write-Host "Config: ${BenchImg}, spp=${BenchSpp}, pool=${PoolSize}`n" -ForegroundColor Yellow

$profileReport  = "$OutDir\serial_timeline"
$profileHt      = "$OutDir\serial_stdout.ht"
$profileLog     = "$OutDir\serial_stderr.log"

# nsys wraps stardis; use cmd /c for clean stream separation from the inner exe
# Note: Boolean flags like --cuda-memory-usage and -f don't take =true
$nsysArgs = "profile --trace=cuda,nvtx --cuda-memory-usage true -o `"$profileReport`" -f true -- `"$Stardis`" $RenderArgStr"
Write-Host "Running: nsys $nsysArgs > stdout.ht 2> stderr.log" -ForegroundColor DarkGray

cmd /c "`"$nsysCmd`" $nsysArgs > `"$profileHt`" 2> `"$profileLog`""

if ($LASTEXITCODE -ne 0) {
    Write-Warning "Profiling exited with code $LASTEXITCODE"
    Show-LogTail $profileLog
} else {
    Write-Host "`nProfiling completed successfully." -ForegroundColor Green
    
    # Show timing summary if available
    $timings = Get-TimingSummary $profileLog
    if ($timings) {
        Write-Host "`n    --- Performance Summary ---" -ForegroundColor Cyan
        $timings | ForEach-Object { Write-Host "    $_" -ForegroundColor White }
    }
    
    Show-LogTail $profileLog
}

Pop-Location

Remove-Item Env:\STARDIS_POOL_SIZE -ErrorAction SilentlyContinue
Remove-Item Env:\STARDIS_PIPELINE  -ErrorAction SilentlyContinue

# ── Summary ────────────────────────────────────────────────────────────────
Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "Nsight Systems profiling complete." -ForegroundColor Green
Write-Host ""
Write-Host "Output files:"
Write-Host "  Timeline report: ${profileReport}.nsys-rep"
Write-Host "  Render data:     ${profileHt}"
Write-Host "  Full log:        ${profileLog}"
Write-Host ""
Write-Host "To view timeline:" -ForegroundColor Yellow
Write-Host "  nsys-ui ${profileReport}.nsys-rep"
Write-Host ""
Write-Host "Key analysis points:"
Write-Host "  - Measure GPU kernel idle gaps between consecutive trace calls"
Write-Host "  - Check H2D/D2H memory transfer overhead"
Write-Host "  - Identify CPU processing time between GPU kernel launches"
Write-Host "  - Verify CUDA stream usage and synchronization points"
