#pragma once
#include "usb_device.h"
#include "video_frame.h"
#include <cstdint>
#include <functional>

// -------------------------------------------------------------------------
// STK1160 register map (from public Syntek documentation and Linux kernel)
// -------------------------------------------------------------------------
namespace stk1160 {

constexpr uint16_t DCTRL      = 0x0001;  // digital control
constexpr uint16_t DIOCTRL    = 0x0002;  // digital I/O control
constexpr uint16_t OUTFMT     = 0x0003;  // output pixel format
constexpr uint16_t CHNL       = 0x000F;  // input channel select
constexpr uint16_t TSKIP      = 0x0011;  // timing skip
constexpr uint16_t ISOC_CTL   = 0x0100;  // isochronous control

// SAA7113H I2C address (on-board analog video decoder)
constexpr uint8_t SAA7113_ADDR = 0x4A;

// SAA7113H registers
constexpr uint8_t SAA_RESET       = 0x01;
constexpr uint8_t SAA_INPUT_CFG   = 0x02;
constexpr uint8_t SAA_MIX_MODE    = 0x03;
constexpr uint8_t SAA_LUM_CTRL1   = 0x09;
constexpr uint8_t SAA_HUE         = 0x0A;
constexpr uint8_t SAA_CHROMA_CTRL = 0x0B;
constexpr uint8_t SAA_CHROMA_GAIN = 0x0C;
constexpr uint8_t SAA_LUM_CTRL2   = 0x0E;
constexpr uint8_t SAA_ANALOG_CTL  = 0x0F;
constexpr uint8_t SAA_DIGITIZER   = 0x10;
constexpr uint8_t SAA_STATUS      = 0x1F;

// USB isochronous endpoint for video data
constexpr uint8_t  VIDEO_ENDPOINT   = 0x82;  // EP2 IN
constexpr uint8_t  ALT_SETTING_IDLE = 0;
constexpr uint8_t  ALT_SETTING_NTSC = 5;     // 3072 bytes/packet @ 30fps
constexpr uint8_t  ALT_SETTING_PAL  = 6;
constexpr uint32_t PACKETS_PER_URB  = 64;
constexpr uint32_t PACKET_SIZE_NTSC = 3072;
constexpr uint32_t PACKET_SIZE_PAL  = 3072;

// Pixel interleaving sync markers in the USB stream
constexpr uint8_t FIELD_MARKER_ODD  = 0x00;
constexpr uint8_t FIELD_MARKER_EVEN = 0x01;

} // namespace stk1160

// -------------------------------------------------------------------------
// Callback invoked by Stk1160Driver when a complete frame is available.
// -------------------------------------------------------------------------
using FrameCallback = std::function<void(const VideoFrame&)>;

// -------------------------------------------------------------------------
// STK1160 chip driver
// -------------------------------------------------------------------------
class Stk1160Driver {
public:
    explicit Stk1160Driver(UsbDevice& dev);

    // One-time chip init. Call after UsbDevice::Open().
    bool Initialize(VideoStandard std, InputChannel ch);

    // Start/stop streaming. FrameCallback is called from the worker thread.
    bool StartCapture(const VideoFormat& fmt, FrameCallback cb);
    void StopCapture();

    bool IsCapturing() const { return m_capturing; }

private:
    bool InitSaa7113(InputChannel ch, VideoStandard std);
    bool InitStk1160Regs(VideoStandard std);
    void CaptureLoop();
    void ProcessPackets(const uint8_t* buf, uint32_t size,
                        VideoFrame& frameBuf, bool& oddField);

    UsbDevice&    m_dev;
    VideoFormat   m_fmt;
    FrameCallback m_callback;
    bool          m_capturing { false };
    HANDLE        m_thread    { nullptr };
    uint32_t      m_seqNum   { 0 };

    static DWORD WINAPI ThreadProc(LPVOID param);
};
