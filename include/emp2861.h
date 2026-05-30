#pragma once
#include "usb_device.h"
#include "video_frame.h"
#include <cstdint>
#include <functional>

// -------------------------------------------------------------------------
// eMPIA EMP2860 / EMP2861 register definitions
// (integrated USB bridge + analog front-end, no external I2C decoder needed)
// -------------------------------------------------------------------------
namespace emp2861 {

// Bulk/isochronous video endpoint
constexpr uint8_t  VIDEO_ENDPOINT   = 0x82;
constexpr uint8_t  ALT_SETTING_IDLE = 0;
constexpr uint8_t  ALT_SETTING_ON   = 1;
constexpr uint32_t PACKETS_PER_URB  = 40;
constexpr uint32_t PACKET_SIZE      = 3072;

// Control request codes (vendor-specific)
constexpr uint8_t REQ_READ_REG  = 0x00;
constexpr uint8_t REQ_WRITE_REG = 0x01;

// Selected register addresses
constexpr uint16_t REG_RESET       = 0x0000;
constexpr uint16_t REG_VIDEO_STD   = 0x001C;  // 0=NTSC, 1=PAL
constexpr uint16_t REG_INPUT_SEL   = 0x001E;  // 0=CVBS, 1=S-Video
constexpr uint16_t REG_OUTPUT_FMT  = 0x0028;  // 0x05 = YUY2
constexpr uint16_t REG_STREAM_CTL  = 0x0030;  // bit0: start streaming

// Sync marker embedded in the isochronous payload
constexpr uint8_t FRAME_HEADER[4] = { 0x88, 0x88, 0x88, 0x88 };

} // namespace emp2861

using FrameCallback = std::function<void(const VideoFrame&)>;

class Emp2861Driver {
public:
    explicit Emp2861Driver(UsbDevice& dev);

    bool Initialize(VideoStandard std, InputChannel ch);

    bool StartCapture(const VideoFormat& fmt, FrameCallback cb);
    void StopCapture();

    bool IsCapturing() const { return m_capturing; }

private:
    bool ConfigureRegisters(VideoStandard std, InputChannel ch);
    void CaptureLoop();
    void ProcessPayload(const uint8_t* buf, uint32_t size,
                        VideoFrame& frameBuf, uint32_t& writePos);

    UsbDevice&    m_dev;
    VideoFormat   m_fmt;
    FrameCallback m_callback;
    bool          m_capturing { false };
    HANDLE        m_thread    { nullptr };
    uint32_t      m_seqNum    { 0 };

    static DWORD WINAPI ThreadProc(LPVOID param);
};
