#include "stk1160.h"
#include <windows.h>
#include <cstring>

using namespace stk1160;

// -----------------------------------------------------------------------
// SAA7113H initialization tables
// Format: { register, value }
// Based on public Philips SAA7113H datasheet and open-source Linux driver
// (drivers/media/usb/stk1160 – GPL reference for register values)
// -----------------------------------------------------------------------
static const uint8_t kSaa7113Init[][2] = {
    { 0x01, 0x08 },  // soft reset
    { 0x02, 0xC0 },  // CVBS on AIN1, automatic gain
    { 0x03, 0x33 },  // mix-mode: clamp auto, Y/C separation
    { 0x04, 0x00 },
    { 0x05, 0x00 },
    { 0x06, 0xEB },  // horizontal sync start
    { 0x07, 0xE0 },  // horizontal sync stop
    { 0x08, 0x88 },  // sync control
    { 0x09, 0x01 },  // luminance: aperture factor
    { 0x0A, 0x80 },  // hue = 0
    { 0x0B, 0x44 },  // chrominance trap BW
    { 0x0C, 0x40 },  // chrominance gain
    { 0x0D, 0x00 },
    { 0x0E, 0x01 },  // luminance output: Y + UV
    { 0x0F, 0x00 },
    { 0x10, 0x48 },  // output format: YUV 4:2:2 interleaved
    { 0x11, 0x1C },  // V-gate
    { 0x12, 0x00 },
    { 0x13, 0x00 },
    { 0x14, 0x00 },
    { 0x15, 0x00 },
    { 0x16, 0x00 },
    { 0x17, 0x00 },  // standard: NTSC-M (changed below for PAL)
};
static const size_t kSaa7113InitLen = sizeof(kSaa7113Init) / sizeof(kSaa7113Init[0]);

// -----------------------------------------------------------------------
// STK1160 constructor
// -----------------------------------------------------------------------
Stk1160Driver::Stk1160Driver(UsbDevice& dev) : m_dev(dev) {}

// -----------------------------------------------------------------------
// Initialize SAA7113H analog decoder via I2C
// -----------------------------------------------------------------------
bool Stk1160Driver::InitSaa7113(InputChannel ch, VideoStandard std) {
    for (size_t i = 0; i < kSaa7113InitLen; ++i) {
        if (!m_dev.I2CWrite(SAA7113_ADDR, kSaa7113Init[i][0],
                             kSaa7113Init[i][1]))
            return false;
    }

    // Input selection: AIN1 (CVBS) vs AIN3/AIN4 (S-Video Y/C)
    uint8_t inputVal = (ch == InputChannel::SVIDEO) ? 0x09 : 0xC0;
    if (!m_dev.I2CWrite(SAA7113_ADDR, SAA_INPUT_CFG, inputVal))
        return false;

    // Video standard (reg 0x08 bit4: 0=NTSC, 1=PAL)
    uint8_t stdVal = (std == VideoStandard::PAL) ? 0x98 : 0x88;
    if (!m_dev.I2CWrite(SAA7113_ADDR, 0x08, stdVal))
        return false;

    return true;
}

// -----------------------------------------------------------------------
// Initialize STK1160 digital control registers
// -----------------------------------------------------------------------
bool Stk1160Driver::InitStk1160Regs(VideoStandard std) {
    // Output pixel format: YUY2 (0x02)
    if (!m_dev.WriteReg(OUTFMT, 0x02)) return false;

    // Enable digital output
    if (!m_dev.WriteReg(DCTRL, 0x01)) return false;

    // NTSC timing: active lines 480, PAL: 576
    uint8_t timingByte = (std == VideoStandard::PAL) ? 0x01 : 0x00;
    if (!m_dev.WriteReg(0x0100, timingByte)) return false;

    return true;
}

// -----------------------------------------------------------------------
// Full initialization
// -----------------------------------------------------------------------
bool Stk1160Driver::Initialize(VideoStandard std, InputChannel ch) {
    if (!m_dev.IsOpen()) return false;

    // Idle alternate setting first
    m_dev.SetAlternateSetting(ALT_SETTING_IDLE);
    Sleep(50);

    if (!InitSaa7113(ch, std)) return false;
    if (!InitStk1160Regs(std)) return false;

    return true;
}

// -----------------------------------------------------------------------
// Capture thread entry
// -----------------------------------------------------------------------
DWORD WINAPI Stk1160Driver::ThreadProc(LPVOID param) {
    reinterpret_cast<Stk1160Driver*>(param)->CaptureLoop();
    return 0;
}

void Stk1160Driver::CaptureLoop() {
    const uint32_t batchSize = PACKETS_PER_URB * PACKET_SIZE_NTSC;
    std::vector<uint8_t> rxBuf(batchSize);

    VideoFrame frameBuf;
    frameBuf.format = m_fmt;
    frameBuf.data.resize(m_fmt.frameSize());
    bool oddField = true;
    uint32_t writePos = 0;

    while (m_capturing) {
        uint32_t bytes = m_dev.ReadIsochBatch(rxBuf.data(), batchSize, 200);
        if (bytes == 0) continue;
        ProcessPackets(rxBuf.data(), bytes, frameBuf, oddField);
    }
}

// -----------------------------------------------------------------------
// Parse USB isochronous packets and assemble interlaced YUY2 frames.
// The STK1160 embeds a 4-byte sync header in the stream:
//   Byte 0: 0x00
//   Byte 1: field flag (0x00=odd, 0x01=even)
//   Byte 2..3: 0x00
// -----------------------------------------------------------------------
void Stk1160Driver::ProcessPackets(const uint8_t* buf, uint32_t size,
                                    VideoFrame& frameBuf, bool& oddField) {
    const uint32_t frameSize = m_fmt.frameSize();
    static uint32_t writePos = 0;

    for (uint32_t i = 0; i < size; ) {
        // Look for 4-byte sync header: 00 xx 00 00
        if (i + 4 <= size &&
            buf[i] == 0x00 && buf[i+2] == 0x00 && buf[i+3] == 0x00) {

            bool isOdd = (buf[i+1] == FIELD_MARKER_ODD);

            if (!isOdd && oddField) {
                // Completed both fields → emit frame
                if (writePos >= frameSize / 2) {
                    frameBuf.timestampUs = static_cast<uint64_t>(
                        GetTickCount64()) * 1000;
                    frameBuf.sequenceNum = m_seqNum++;
                    if (m_callback) m_callback(frameBuf);
                }
                writePos = 0;
            }
            oddField = isOdd;
            i += 4;
            continue;
        }

        // Copy payload into the appropriate half of the frame buffer
        // Odd field → lines 0,2,4,...  Even field → lines 1,3,5,...
        if (writePos < frameSize) {
            uint32_t chunk = min(size - i, frameSize - writePos);
            memcpy(frameBuf.data.data() + writePos, buf + i, chunk);
            writePos += chunk;
            i += chunk;
        } else {
            ++i;
        }
    }
}

// -----------------------------------------------------------------------
// StartCapture / StopCapture
// -----------------------------------------------------------------------
bool Stk1160Driver::StartCapture(const VideoFormat& fmt, FrameCallback cb) {
    if (m_capturing) return true;

    m_fmt      = fmt;
    m_callback = std::move(cb);

    uint8_t altSetting = (fmt.standard == VideoStandard::PAL)
                         ? ALT_SETTING_PAL : ALT_SETTING_NTSC;

    if (!m_dev.SetAlternateSetting(altSetting)) return false;

    if (!m_dev.StartIsochCapture(VIDEO_ENDPOINT, PACKETS_PER_URB,
                                  PACKET_SIZE_NTSC))
        return false;

    m_capturing = true;
    m_thread = CreateThread(nullptr, 0, ThreadProc, this, 0, nullptr);
    return m_thread != nullptr;
}

void Stk1160Driver::StopCapture() {
    if (!m_capturing) return;
    m_capturing = false;

    if (m_thread) {
        WaitForSingleObject(m_thread, 2000);
        CloseHandle(m_thread);
        m_thread = nullptr;
    }

    m_dev.StopIsochCapture();
    m_dev.SetAlternateSetting(ALT_SETTING_IDLE);
}
