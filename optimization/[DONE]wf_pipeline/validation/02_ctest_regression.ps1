###############################################################################
# Phase 4 Step 2: CTest Regression
#
# Runs full ctest suite in both serial and pipeline modes.
# Both must pass with zero failures.
#
# NOTE: ctest invokes test exes whose stderr output can be very large.
# We capture to log files and show only the ctest summary + tail.
###############################################################################

$ErrorActionPreference = "Stop"
$ProjectRoot = "D:\Works\Projects\Stardis-GPU"
$BuildDir    = "$ProjectRoot\stardis-cus3d\build"
$OutDir      = "$ProjectRoot\optimization\wf_pipeline\validation\results\ctest"
$LogTailN    = 40  # Show last N lines from ctest log

if (-not (Test-Path "$BuildDir\CTestTestfile.cmake")) {
    Write-Error "CTest not configured in $BuildDir — run cmake first."
    exit 1
}
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }

Push-Location $BuildDir
$AllPassed = $true

foreach ($mode in @("serial", "pipeline")) {
    $envVal  = if ($mode -eq "serial") { "0" } else { "1" }
    $logFile = "$OutDir\ctest_${mode}.log"

    Write-Host "`n========== CTest: $($mode.ToUpper()) mode (STARDIS_PIPELINE=$envVal) ==========" -ForegroundColor Cyan
    $env:STARDIS_PIPELINE = $envVal

    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    # Use cmd /c to avoid PowerShell treating stderr as error records
    cmd /c "ctest -C Release --output-on-failure > `"$logFile`" 2>&1"
    $ctestExit = $LASTEXITCODE
    $sw.Stop()

    Write-Host "  Finished in $($sw.Elapsed.TotalSeconds.ToString('F1'))s (exit=$ctestExit)"

    # Show summary: last N lines (contains pass/fail counts and timing)
    if (Test-Path $logFile) {
        $tail = Get-Content $logFile -Tail $LogTailN -ErrorAction SilentlyContinue
        Write-Host "  --- ctest output (last $LogTailN lines) ---" -ForegroundColor DarkGray
        $tail | ForEach-Object { Write-Host "  $_" -ForegroundColor DarkGray }
        Write-Host "  Full log: $logFile" -ForegroundColor DarkGray
    }

    if ($ctestExit -ne 0) {
        Write-Host "  $($mode.ToUpper()) MODE: SOME TESTS FAILED" -ForegroundColor Red
        $AllPassed = $false
    } else {
        Write-Host "  $($mode.ToUpper()) MODE: ALL TESTS PASSED" -ForegroundColor Green
    }
}

Pop-Location

# Clean up env vars
Remove-Item Env:\STARDIS_PIPELINE -ErrorAction SilentlyContinue

Write-Host "`n========================================" -ForegroundColor Cyan
if ($AllPassed) {
    Write-Host "ALL CTEST REGRESSION TESTS PASSED (both modes)" -ForegroundColor Green
} else {
    Write-Host "SOME TESTS FAILED — see details above" -ForegroundColor Red
    exit 1
}
