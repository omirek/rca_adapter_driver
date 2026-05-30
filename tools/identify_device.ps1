# identify_device.ps1
# Run this script BEFORE installing the driver to discover the
# VID/PID and current driver of your USB video capture device.
#
# Usage (as Administrator not required):
#   powershell -ExecutionPolicy Bypass -File .\tools\identify_device.ps1

$targetKeywords = @(
    "easycap", "grabber", "capture", "stk1160", "emp286",
    "usb video", "usb2.0", "vhs", "cvbs", "composite"
)

Write-Host "`n=== USB Video Capture Device Finder ===" -ForegroundColor Cyan
Write-Host "Searching for connected USB devices...`n"

$devices = Get-PnpDevice -PresentOnly | Where-Object {
    $_.Class -in @("Image","Camera","Media","USB") -or
    $_.InstanceId -match "USB\\VID_"
}

$found = @()

foreach ($dev in $devices) {
    $lower = ($dev.FriendlyName + " " + $dev.InstanceId).ToLower()
    $isTarget = $false

    # Check for known VID/PIDs
    $knownVidPids = @(
        "VID_05E1&PID_0408",   # STK1160
        "VID_EB1A&PID_2861",   # EMP2861
        "VID_EB1A&PID_2860",   # EMP2860
        "VID_EB1A&PID_E8D3",   # eMPIA generic
        "VID_1C88&PID_0007",   # Somagic
        "VID_2040&PID_7200"    # Auvitek
    )
    foreach ($vp in $knownVidPids) {
        if ($dev.InstanceId -like "*$vp*") { $isTarget = $true; break }
    }

    # Also match by keyword
    foreach ($kw in $targetKeywords) {
        if ($lower -like "*$kw*") { $isTarget = $true; break }
    }

    if ($isTarget) { $found += $dev }
}

if ($found.Count -eq 0) {
    Write-Host "No USB video capture device found." -ForegroundColor Yellow
    Write-Host "Make sure the device is plugged in and try again."
    Write-Host "`nAll USB devices currently seen:"
    Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -match "^USB\\" } |
        Select-Object FriendlyName, InstanceId, Status | Format-Table -AutoSize
    exit 1
}

Write-Host "Found $($found.Count) candidate device(s):`n" -ForegroundColor Green

foreach ($dev in $found) {
    # Extract VID and PID
    if ($dev.InstanceId -match "VID_([0-9A-Fa-f]{4})&PID_([0-9A-Fa-f]{4})") {
        $vid = $Matches[1].ToUpper()
        $pid = $Matches[2].ToUpper()
    } else {
        $vid = "????"; $pid = "????"
    }

    # Determine likely chipset
    $chip = switch ("$vid $pid") {
        "05E1 0408" { "Syntek STK1160" }
        "EB1A 2861" { "eMPIA EMP2861" }
        "EB1A 2860" { "eMPIA EMP2860" }
        "EB1A E8D3" { "eMPIA (generic)" }
        "1C88 0007" { "Somagic SM-USB 007" }
        default     { "Unknown (check rca_capture.inf)" }
    }

    # Get current driver
    $driverInfo = Get-PnpDeviceProperty -InputObject $dev `
        -KeyName "DEVPKEY_Device_DriverInfPath" -ErrorAction SilentlyContinue
    $currentDriver = if ($driverInfo) { $driverInfo.Data } else { "No driver / unknown" }

    Write-Host "  Name   : $($dev.FriendlyName)" -ForegroundColor White
    Write-Host "  VID    : 0x$vid"
    Write-Host "  PID    : 0x$pid"
    Write-Host "  Chipset: $chip"
    Write-Host "  Driver : $currentDriver"
    Write-Host "  Status : $($dev.Status)"
    Write-Host "  InstanceId: $($dev.InstanceId)"
    Write-Host ""
}

Write-Host "Next steps:" -ForegroundColor Cyan
Write-Host "  1. Check the INF covers your VID/PID  (driver\rca_capture.inf)"
Write-Host "  2. Run: .\tools\install_driver.ps1  (as Administrator)"
