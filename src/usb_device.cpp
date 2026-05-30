#include "usb_device.h"
#include <setupapi.h>
#include <initguid.h>
#include <cassert>
#include <stdexcept>

#pragma comment(lib, "winusb.lib")
#pragma comment(lib, "setupapi.lib")

// -----------------------------------------------------------------------
// Construction / destruction
// -----------------------------------------------------------------------
UsbDevice::UsbDevice() = default;

UsbDevice::~UsbDevice() {
    Close();
}

// -----------------------------------------------------------------------
// Open by device interface GUID (set by the INF file)
// -----------------------------------------------------------------------
bool UsbDevice::Open() {
    HDEVINFO devInfo = SetupDiGetClassDevs(
        &RCA_DEVICE_INTERFACE_GUID, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    if (devInfo == INVALID_HANDLE_VALUE)
        return false;

    SP_DEVICE_INTERFACE_DATA ifData{};
    ifData.cbSize = sizeof(ifData);

    bool found = false;
    for (DWORD i = 0; !found; ++i) {
        if (!SetupDiEnumDeviceInterfaces(devInfo, nullptr,
                &RCA_DEVICE_INTERFACE_GUID, i, &ifData))
            break;

        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetail(devInfo, &ifData, nullptr, 0,
                                        &required, nullptr);

        std::vector<uint8_t> buf(required);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(
            buf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        if (SetupDiGetDeviceInterfaceDetail(devInfo, &ifData, detail,
                required, nullptr, nullptr)) {
            found = Open(detail->DevicePath);
        }
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return found;
}

bool UsbDevice::Open(const std::wstring& devicePath) {
    Close();

    m_devHandle = CreateFileW(devicePath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
        nullptr);

    if (m_devHandle == INVALID_HANDLE_VALUE)
        return false;

    if (!WinUsb_Initialize(m_devHandle, &m_winusbHandle)) {
        CloseHandle(m_devHandle);
        m_devHandle = INVALID_HANDLE_VALUE;
        return false;
    }

    // Set raw I/O for better throughput
    ULONG rawIo = 1;
    WinUsb_SetPipePolicy(m_winusbHandle, 0x00,
                         RAW_IO, sizeof(rawIo), &rawIo);
    return true;
}

void UsbDevice::Close() {
    StopIsochCapture();

    if (m_winusbHandle != reinterpret_cast<WINUSB_INTERFACE_HANDLE>(INVALID_HANDLE_VALUE)) {
        WinUsb_Free(m_winusbHandle);
        m_winusbHandle = reinterpret_cast<WINUSB_INTERFACE_HANDLE>(INVALID_HANDLE_VALUE);
    }
    if (m_devHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_devHandle);
        m_devHandle = INVALID_HANDLE_VALUE;
    }
}

// -----------------------------------------------------------------------
// Control transfers
// -----------------------------------------------------------------------
bool UsbDevice::ControlRead(uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
                             uint8_t* buf, uint16_t len) {
    WINUSB_SETUP_PACKET pkt{};
    pkt.RequestType = 0xC0;  // Device-to-host, Vendor, Device
    pkt.Request     = bRequest;
    pkt.Value       = wValue;
    pkt.Index       = wIndex;
    pkt.Length      = len;

    ULONG transferred = 0;
    return WinUsb_ControlTransfer(m_winusbHandle, pkt, buf, len,
                                  &transferred, nullptr) &&
           transferred == len;
}

bool UsbDevice::ControlWrite(uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
                              const uint8_t* buf, uint16_t len) {
    WINUSB_SETUP_PACKET pkt{};
    pkt.RequestType = 0x40;  // Host-to-device, Vendor, Device
    pkt.Request     = bRequest;
    pkt.Value       = wValue;
    pkt.Index       = wIndex;
    pkt.Length      = len;

    ULONG transferred = 0;
    return WinUsb_ControlTransfer(m_winusbHandle, pkt,
                                  const_cast<uint8_t*>(buf), len,
                                  &transferred, nullptr) &&
           transferred == len;
}

// -----------------------------------------------------------------------
// Register access (single byte, vendor request 0x00/0x01)
// -----------------------------------------------------------------------
bool UsbDevice::ReadReg(uint16_t reg, uint8_t& val) {
    return ControlRead(0x00, 0, reg, &val, 1);
}

bool UsbDevice::WriteReg(uint16_t reg, uint8_t val) {
    return ControlWrite(0x01, val, reg, nullptr, 0);
}

bool UsbDevice::MaskReg(uint16_t reg, uint8_t val, uint8_t mask) {
    uint8_t cur = 0;
    if (!ReadReg(reg, cur)) return false;
    cur = (cur & ~mask) | (val & mask);
    return WriteReg(reg, cur);
}

// -----------------------------------------------------------------------
// I2C tunnelled through STK1160 vendor control transfer
// -----------------------------------------------------------------------
bool UsbDevice::I2CWrite(uint8_t i2cAddr, uint8_t reg, uint8_t val) {
    // STK1160 tunnels I2C via control request 0x07:
    //   wValue  = data byte
    //   wIndex  = (i2cAddr << 8) | subReg
    uint16_t idx = (static_cast<uint16_t>(i2cAddr) << 8) | reg;
    return ControlWrite(0x07, val, idx, nullptr, 0);
}

bool UsbDevice::I2CRead(uint8_t i2cAddr, uint8_t reg, uint8_t& val) {
    uint16_t idx = (static_cast<uint16_t>(i2cAddr) << 8) | reg;
    return ControlRead(0x07, 0, idx, &val, 1);
}

// -----------------------------------------------------------------------
// Alternate setting
// -----------------------------------------------------------------------
bool UsbDevice::SetAlternateSetting(uint8_t altSetting) {
    return WinUsb_SetCurrentAlternateSetting(m_winusbHandle, altSetting) != FALSE;
}

// -----------------------------------------------------------------------
// Isochronous capture (WinUSB high-level isoch API, Windows 8.1+)
// -----------------------------------------------------------------------
bool UsbDevice::StartIsochCapture(uint8_t endpoint, uint32_t numPackets,
                                   uint32_t maxPacketSize) {
    if (m_isochRunning) return true;

    m_isochEndpoint = endpoint;
    m_numPackets    = numPackets;
    m_packetSize    = maxPacketSize;

    const uint32_t bufSize = numPackets * maxPacketSize;
    m_isochBuffer.resize(bufSize);

    if (!WinUsb_RegisterIsochBuffer(
            m_winusbHandle, endpoint,
            m_isochBuffer.data(), bufSize,
            &m_isochBufHandle)) {
        return false;
    }

    m_isochRunning = true;
    return true;
}

void UsbDevice::StopIsochCapture() {
    if (!m_isochRunning) return;
    m_isochRunning = false;

    if (m_isochBufHandle) {
        WinUsb_UnregisterIsochBuffer(m_isochBufHandle);
        m_isochBufHandle = nullptr;
    }
    m_isochBuffer.clear();
}

uint32_t UsbDevice::ReadIsochBatch(uint8_t* buf, uint32_t bufSize,
                                    uint32_t timeoutMs) {
    if (!m_isochRunning) return 0;

    OVERLAPPED ov{};
    ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

    BOOL ok = WinUsb_ReadIsochPipeAsap(
        m_isochBufHandle,
        0,
        m_numPackets * m_packetSize,
        FALSE,
        m_numPackets,
        nullptr,   // packet array – nullptr means fill automatically
        &ov);

    uint32_t transferred = 0;
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ov.hEvent, timeoutMs) == WAIT_OBJECT_0) {
            DWORD bytes = 0;
            WinUsb_GetOverlappedResult(m_winusbHandle, &ov, &bytes, FALSE);
            transferred = bytes;
        }
    }

    CloseHandle(ov.hEvent);

    const uint32_t copy = min(transferred, bufSize);
    if (copy > 0)
        memcpy(buf, m_isochBuffer.data(), copy);

    return copy;
}

// -----------------------------------------------------------------------
// Device enumeration by VID/PID (fallback when INF not yet installed)
// -----------------------------------------------------------------------
std::vector<UsbDeviceInfo> UsbDevice::Enumerate(
    const std::vector<std::pair<uint16_t,uint16_t>>& vidPids)
{
    std::vector<UsbDeviceInfo> results;

    HDEVINFO devInfo = SetupDiGetClassDevs(
        nullptr, L"USB", nullptr,
        DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (devInfo == INVALID_HANDLE_VALUE) return results;

    SP_DEVINFO_DATA devData{};
    devData.cbSize = sizeof(devData);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(devInfo, i, &devData); ++i) {
        wchar_t hwId[512] = {};
        if (!SetupDiGetDeviceRegistryPropertyW(devInfo, &devData,
                SPDRP_HARDWAREID, nullptr,
                reinterpret_cast<PBYTE>(hwId), sizeof(hwId), nullptr))
            continue;

        // hwId looks like: USB\VID_EB1A&PID_2861
        std::wstring id(hwId);
        for (auto& [vid, pid] : vidPids) {
            wchar_t pattern[32];
            swprintf_s(pattern, L"VID_%04X&PID_%04X", vid, pid);
            if (id.find(pattern) != std::wstring::npos) {
                UsbDeviceInfo info;
                info.vid = vid;
                info.pid = pid;
                // Get device path from interface
                info.devicePath = id;
                results.push_back(std::move(info));
                break;
            }
        }
    }
    SetupDiDestroyDeviceInfoList(devInfo);
    return results;
}
