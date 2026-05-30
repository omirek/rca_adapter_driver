# install_driver.ps1
# Installs the WinUSB driver (rca_capture.inf) and registers RcaCapture.dll
# Must run as Administrator.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File .\tools\install_driver.ps1
#
# Optional parameters:
#   -DllPath   path to RcaCapture.dll  (default: .\build\Release\RcaCapture.dll)
#   -InfPath   path to rca_capture.inf (default: .\driver\rca_capture.inf)
#   -TestSign  enable test-signing mode (needed if DLL is not signed)

param(
    [string]$DllPath  = "$PSScriptRoot\..\build\Release\RcaCapture.dll",
    [string]$InfPath  = "$PSScriptRoot\..\driver\rca_capture.inf",
    [switch]$TestSign
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ── Admin check ─────────────────────────────────────────────────────────
$isAdmin = ([Security.Principal.WindowsPrincipal]
            [Security.Principal.WindowsIdentity]::GetCurrent()
           ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    Write-Error "This script must be run as Administrator. Restart PowerShell elevated."
    exit 1
}

# ── Resolve paths ────────────────────────────────────────────────────────
$InfPath = Resolve-Path $InfPath -ErrorAction Stop

if (-not (Test-Path $DllPath)) {
    Write-Host "DLL not found at: $DllPath" -ForegroundColor Yellow
    Write-Host "Build the project first:"
    Write-Host "  cmake -B build -G `"Visual Studio 17 2022`" -A x64"
    Write-Host "  cmake --build build --config Release"
    exit 1
}

Write-Host "`n=== RCA Capture Driver Installer ===" -ForegroundColor Cyan

# ── Optional: enable test-signing ────────────────────────────────────────
if ($TestSign) {
    Write-Host "[1/4] Enabling test-signing mode (requires reboot)..." -ForegroundColor Yellow
    & bcdedit /set testsigning on | Out-Null
    Write-Host "      Test-signing enabled. Reboot AFTER this script if prompted."
}

# ── Remove existing malicious / conflicting driver ────────────────────────
Write-Host "[2/4] Checking for existing drivers on the device..." -ForegroundColor White

$knownVidPids = @("VID_05E1&PID_0408","VID_EB1A&PID_2861",
                  "VID_EB1A&PID_2860","VID_EB1A&PID_E8D3","VID_1C88&PID_0007")

foreach ($vp in $knownVidPids) {
    $devs = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
            Where-Object { $_.InstanceId -like "*$vp*" }
    foreach ($d in $devs) {
        Write-Host "  Found: $($d.FriendlyName)  [$($d.InstanceId)]"
        try {
            # Attempt to uninstall the current (malicious) driver
            $null = & pnputil /remove-device $d.InstanceId 2>&1
            Write-Host "  Uninstalled existing driver." -ForegroundColor Green
        } catch {
            Write-Host "  Could not uninstall existing driver (may be OK)." -ForegroundColor Yellow
        }
    }
}

# ── Install WinUSB INF ────────────────────────────────────────────────────
Write-Host "[3/4] Installing WinUSB INF: $InfPath" -ForegroundColor White
$result = & pnputil /add-driver "$InfPath" /install 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host $result -ForegroundColor Red
    Write-Error "pnputil failed. Make sure the INF is valid and you are running as Admin."
}
Write-Host "  INF installed successfully." -ForegroundColor Green

# ── Register DirectShow DLL ───────────────────────────────────────────────
Write-Host "[4/4] Registering DirectShow filter: $DllPath" -ForegroundColor White

# Copy DLL to System32 so regsvr32 can find it reliably
$dest = "$env:SystemRoot\System32\RcaCapture.dll"
Copy-Item $DllPath $dest -Force
Write-Host "  Copied to: $dest"

$regResult = & regsvr32 /s "$dest" 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host $regResult -ForegroundColor Red
    Write-Error "regsvr32 failed."
}
Write-Host "  DLL registered successfully." -ForegroundColor Green

# ── Summary ──────────────────────────────────────────────────────────────
Write-Host "`n=== Installation complete ===" -ForegroundColor Green
Write-Host ""
Write-Host "Next steps:"
Write-Host "  1. Plug in your USB video capture device (if not already plugged in)."
Write-Host "  2. Open OBS Studio."
Write-Host "  3. Add a source > Video Capture Device."
Write-Host "  4. Select 'RCA Capture (STK1160/EMP2861)'."
Write-Host "  5. Choose the input (CVBS = Composite, or S-Video)."
Write-Host ""
Write-Host "To uninstall:"
Write-Host "  regsvr32 /u C:\Windows\System32\RcaCapture.dll"
Write-Host "  pnputil /remove-driver rca_capture.inf"
Write-Host "  del C:\Windows\System32\RcaCapture.dll"
