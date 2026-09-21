<#
.SYNOPSIS
    Build, install, launch on the headset, and show how far the boot got.

.DESCRIPTION
    The boot log is the only way to see what this port is doing, and the Quest's
    own services flood logcat fast enough to rotate our lines out of the buffer
    before they can be read after the fact. So this starts the capture first,
    launches into it, and filters afterwards.

    It also broadcasts prox_close, because the headset suspends immersive apps
    when nobody is wearing it - without that the app is paused a few frames in
    and never reaches the interesting part.

.EXAMPLE
    .\tools\gevr_boot_test.ps1
    .\tools\gevr_boot_test.ps1 -Seconds 30 -Full
#>
param(
    [int]$Seconds = 18,
    [switch]$SkipBuild,
    [switch]$Full          # show the whole capture rather than the summary lines
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$apk = Join-Path $repo "android\app\build\outputs\apk\debug\app-debug.apk"
$log = Join-Path $env:TEMP "gevr_boot.log"
$pkg = "com.gevr.port"

if (-not (adb devices | Select-String -Pattern "device$")) {
    Write-Host "No device. Wake the headset, check the cable, and allow USB debugging." -ForegroundColor Yellow
    exit 1
}

if (-not $SkipBuild) {
    Push-Location (Join-Path $repo "android")
    $result = & .\gradlew.bat assembleDebug --console=plain 2>&1
    Pop-Location
    $ok = $result | Select-String -Pattern "BUILD SUCCESSFUL"
    if (-not $ok) {
        $result | Select-String -Pattern "error:|BUILD FAILED" | Select-Object -Last 15
        exit 1
    }
    Write-Host "build ok" -ForegroundColor Green
}

adb install -r $apk | Out-Null
adb shell am force-stop $pkg
adb logcat -c

# Capture before launching, or the first lines are gone by the time we look.
$cap = Start-Process -FilePath "adb" `
    -ArgumentList "logcat","-v","time","GoldenEye:V","GoldenEye-VR:V","GoldenEye-GFX:V","GEVR:V","DEBUG:V","*:S" `
    -RedirectStandardOutput $log -NoNewWindow -PassThru
Start-Sleep -Seconds 2

adb shell am broadcast -a com.oculus.vrpowermanager.prox_close | Out-Null
adb shell am start -n "$pkg/.MainActivity" | Out-Null
Write-Host "launched; capturing for $Seconds seconds..."
Start-Sleep -Seconds $Seconds

Stop-Process -Id $cap.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1

if ($Full) {
    Get-Content $log
    exit 0
}

Write-Host "`n--- boot progress ---" -ForegroundColor Cyan
Get-Content $log | Select-String -Pattern "starting|complete|rom:|bound|heap"

$crash = Get-Content $log | Select-String -Pattern "signal "
if ($crash) {
    Write-Host "`n--- crash ---" -ForegroundColor Red
    Get-Content $log | Select-String -Pattern "signal |fault addr"
    Get-Content $log | Select-String -Pattern "#0[0-4] pc" |
        ForEach-Object { ($_ -split "\s+" | Select-Object -Last 3) -join " " }
} else {
    $alive = (adb shell "pidof $pkg").Trim()
    if ($alive) {
        Write-Host "`nStill running (pid $alive) - no crash in this window." -ForegroundColor Green
    } else {
        Write-Host "`nProcess exited without a fatal signal." -ForegroundColor Yellow
    }
}
Write-Host "`nfull log: $log"
