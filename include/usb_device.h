#pragma once
#include <windows.h>
#include <winusb.h>
#include <string>
#include <vector>
#include <cstdint>

// Device interface GUID declared in rca_capture.inf [Dev_AddReg]
static constexpr GUID RCA_DEVICE_INTERFACE_GUID = {
    0xD46A89B0, 0x5A5E, 0x4A8C,
    { 0xB9, 0xE1, 0x6C, 0x2D, 0x5F, 0x8A, 0x0B, 0x3E }
};

struct UsbDeviceInfo {
    uint16_t     vid;
    uint16_t     pid;
    std::wstring devicePath;
};

class UsbDevice {
public:
    UsbDevice();
    ~UsbDevice();

    UsbDevice(const UsbDevice&) = delete;
    UsbDevice& operator=(const UsbDevice&) = delete;

    // Open the first matching device by interface GUID (set in INF).
    bool Open();

    // Open a specific device path (from EnumerateDevices).
    bool Open(const std::wstring& devicePath);

    void Close();
    bool IsOpen() const { return m_winusbHandle != INVALID_HANDLE_VALUE; }

    // Vendor control transfers (USB request type 0x40 / 0xC0).
    bool ControlRead (uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
                      uint8_t* buf, uint16_t len);
    bool ControlWrite(uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
                      const uint8_t* buf, uint16_t len);

    // Convenience: single-byte register R/W (used by chip drivers).
    bool ReadReg (uint16_t reg, uint8_t& val);
    bool WriteReg(uint16_t reg, uint8_t  val);
    bool MaskReg (uint16_t reg, uint8_t  val, uint8_t mask);

    // I2C helpers (tunnelled through vendor control transfer).
    bool I2CWrite(uint8_t i2cAddr, uint8_t reg, uint8_t val);
    bool I2CRead (uint8_t i2cAddr, uint8_t reg, uint8_t& val);

    // Isochronous video stream.
    bool SetAlternateSetting(uint8_t altSetting);
    bool StartIsochCapture(uint8_t endpoint, uint32_t numPackets,
                           uint32_t maxPacketSize);
    void StopIsochCapture();

    // Read one batch of isochronous packets. Returns number of bytes written.
    // Blocks until data arrives or timeoutMs elapses.
    uint32_t ReadIsochBatch(uint8_t* buf, uint32_t bufSize,
                            uint32_t timeoutMs = 1000);

    static std::vector<UsbDeviceInfo> Enumerate(
        const std::vector<std::pair<uint16_t,uint16_t>>& vidPids);

private:
    HANDLE                  m_devHandle    { INVALID_HANDLE_VALUE };
    WINUSB_INTERFACE_HANDLE m_winusbHandle { reinterpret_cast<WINUSB_INTERFACE_HANDLE>(INVALID_HANDLE_VALUE) };

    // Isochronous state
    WINUSB_ISOCH_BUFFER_HANDLE m_isochBufHandle { nullptr };
    std::vector<uint8_t>       m_isochBuffer;
    uint8_t                    m_isochEndpoint  { 0 };
    uint32_t                   m_numPackets     { 0 };
    uint32_t                   m_packetSize     { 0 };
    bool                       m_isochRunning   { false };
};
