#include "emp2861.h"
#include <windows.h>
#include <cstring>

using namespace emp2861;

Emp2861Driver::Emp2861Driver(UsbDevice& dev) : m_dev(dev) {}

// -----------------------------------------------------------------------
// Register configuration for EMP2860/EMP2861
// The chip has an integrated analog front-end, so no external I2C decoder.
// -----------------------------------------------------------------------
bool Emp2861Driver::ConfigureRegisters(VideoStandard std, InputChannel ch) {
    // Soft reset
    if (!m_dev.WriteReg(REG_RESET, 0x01)) return false;
    Sleep(50);
    if (!m_dev.WriteReg(REG_RESET, 0x00)) return false;
    Sleep(10);

    // Video standard
    uint8_t stdVal = (std == VideoStandard::PAL) ? 0x01 : 0x00;
    if (!m_dev.WriteReg(REG_VIDEO_STD, stdVal)) return false;

    // Input channel
    uint8_t chVal = (ch == InputChannel::SVIDEO) ? 0x01 : 0x00;
    if (!m_dev.WriteReg(REG_INPUT_SEL, chVal)) return false;

    // Output format: YUY2
    if (!m_dev.WriteReg(REG_OUTPUT_FMT, 0x05)) return false;

    return true;
}

bool Emp2861Driver::Initialize(VideoStandard std, InputChannel ch) {
    if (!m_dev.IsOpen()) return false;
    m_dev.SetAlternateSetting(ALT_SETTING_IDLE);
    Sleep(50);
    return ConfigureRegisters(std, ch);
}

// -----------------------------------------------------------------------
// Capture thread
// -----------------------------------------------------------------------
DWORD WINAPI Emp2861Driver::ThreadProc(LPVOID param) {
    reinterpret_cast<Emp2861Driver*>(param)->CaptureLoop();
    return 0;
}

void Emp2861Driver::CaptureLoop() {
    const uint32_t batchSize = PACKETS_PER_URB * PACKET_SIZE;
    std::vector<uint8_t> rxBuf(batchSize);

    VideoFrame frameBuf;
    frameBuf.format = m_fmt;
    frameBuf.data.resize(m_fmt.frameSize());
    uint32_t writePos = 0;

    while (m_capturing) {
        uint32_t bytes = m_dev.ReadIsochBatch(rxBuf.data(), batchSize, 200);
        if (bytes == 0) continue;
        ProcessPayload(rxBuf.data(), bytes, frameBuf, writePos);
    }
}

// -----------------------------------------------------------------------
// EMP2861 embeds 0x88888888 as frame-start marker.
// -----------------------------------------------------------------------
void Emp2861Driver::ProcessPayload(const uint8_t* buf, uint32_t size,
                                    VideoFrame& frameBuf, uint32_t& writePos) {
    const uint32_t frameSize = m_fmt.frameSize();

    for (uint32_t i = 0; i < size; ) {
        // Detect frame header 88 88 88 88
        if (i + 4 <= size &&
            buf[i]   == FRAME_HEADER[0] &&
            buf[i+1] == FRAME_HEADER[1] &&
            buf[i+2] == FRAME_HEADER[2] &&
            buf[i+3] == FRAME_HEADER[3]) {

            if (writePos > frameSize / 2) {
                frameBuf.timestampUs = static_cast<uint64_t>(
                    GetTickCount64()) * 1000;
                frameBuf.sequenceNum = m_seqNum++;
                if (m_callback) m_callback(frameBuf);
            }
            writePos = 0;
            i += 4;
            continue;
        }

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

bool Emp2861Driver::StartCapture(const VideoFormat& fmt, FrameCallback cb) {
    if (m_capturing) return true;

    m_fmt      = fmt;
    m_callback = std::move(cb);

    if (!m_dev.WriteReg(REG_STREAM_CTL, 0x01)) return false;
    if (!m_dev.SetAlternateSetting(ALT_SETTING_ON)) return false;
    if (!m_dev.StartIsochCapture(VIDEO_ENDPOINT, PACKETS_PER_URB, PACKET_SIZE))
        return false;

    m_capturing = true;
    m_thread = CreateThread(nullptr, 0, ThreadProc, this, 0, nullptr);
    return m_thread != nullptr;
}

void Emp2861Driver::StopCapture() {
    if (!m_capturing) return;
    m_capturing = false;

    if (m_thread) {
        WaitForSingleObject(m_thread, 2000);
        CloseHandle(m_thread);
        m_thread = nullptr;
    }

    m_dev.StopIsochCapture();
    m_dev.WriteReg(emp2861::REG_STREAM_CTL, 0x00);
    m_dev.SetAlternateSetting(ALT_SETTING_IDLE);
}
