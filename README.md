# RCA Capture Driver

Open-source Windows 11 driver for cheap USB video capture devices
(EasyCap clones: STK1160, EMP2860, EMP2861) — written to replace
bundled malicious software.

Exposes the device as a **DirectShow source filter** so OBS Studio
can use it directly via *Video Capture Device* without any third-party
software.

---

## Supported devices

| VID   | PID    | Chipset          | Notes                     |
|-------|--------|------------------|---------------------------|
| 05E1  | 0408   | Syntek STK1160   | + SAA7113H video decoder  |
| EB1A  | 2861   | eMPIA EMP2861    | Integrated front-end      |
| EB1A  | 2860   | eMPIA EMP2860    | Integrated front-end      |
| EB1A  | E8D3   | eMPIA (generic)  |                           |
| 1C88  | 0007   | Somagic SM-USB   |                           |

If your device has a different VID/PID add a line in `driver/rca_capture.inf`
under `[Standard.NTamd64]`.

---

## Architecture

```
USB device
   │ isochronous transfers (WinUSB)
   ▼
UsbDevice  (src/usb_device.cpp)
   │ raw packets
   ▼
Stk1160Driver or Emp2861Driver  (src/stk1160.cpp / emp2861.cpp)
   │ decoded VideoFrame (YUY2)
   ▼
CaptureDevice  (src/capture_device.cpp)
   │ FrameRingBuffer (8 frames)
   ▼
RcaOutputPin  (src/directshow/ds_output_pin.cpp)  ← deliver thread
   │ IMediaSample (YUY2, 720×480@30 or 720×576@25)
   ▼
OBS "Video Capture Device" source (DirectShow)
```

---

## Building

### Requirements
- Visual Studio 2022 (MSVC v143) with *Desktop development with C++*
- Windows SDK 10.0.22621+ (ships with VS2022)
- CMake 3.20+

### Steps

```bat
git clone https://github.com/angelusgi/rca_adapter_driver
cd rca_adapter_driver

cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Output: `build\Release\RcaCapture.dll`

---

## Installation on Windows 11

### 1. Find your device's VID/PID (optional but recommended)

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\identify_device.ps1
```

### 2. Enable test-signing (if the DLL is not EV-signed)

```bat
bcdedit /set testsigning on
shutdown /r /t 0
```

> Skip this step if you have an EV code-signing certificate and sign
> `RcaCapture.dll` and `rca_capture.cat` before installation.

### 3. Install

```powershell
# Run as Administrator
powershell -ExecutionPolicy Bypass -File .\tools\install_driver.ps1 -TestSign
```

The script:
1. Removes the existing (malicious) driver via `pnputil`
2. Installs `rca_capture.inf` via `pnputil /add-driver`
3. Copies `RcaCapture.dll` to `System32` and registers it with `regsvr32`

### 4. Use with OBS

1. Open OBS Studio
2. *Sources* → **+** → *Video Capture Device*
3. Device: **RCA Capture (STK1160/EMP2861)**
4. Configure resolution (720×480 NTSC or 720×576 PAL) and frame rate
5. Connect your VHS/camera via the RCA or S-Video cables

---

## Uninstall

```powershell
regsvr32 /u C:\Windows\System32\RcaCapture.dll
pnputil /remove-driver rca_capture.inf
del C:\Windows\System32\RcaCapture.dll
bcdedit /set testsigning off   # if you enabled it
```

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| Device not listed in OBS | Run `regsvr32 RcaCapture.dll` again as Admin |
| Black screen / no signal | Run `identify_device.ps1` to confirm VID/PID; check cable connection |
| "This app can't run" watermark | Test-signing is active; expected during development |
| Blue screen | File a bug — this is user-mode only code, should not BSOD |

---

## License

MIT — see [LICENSE](LICENSE).  
STK1160 register values derived from the Linux kernel `stk1160` driver
(GPL-2.0), used only as a reference for chip programming; no GPL code
is copied into this repository.
