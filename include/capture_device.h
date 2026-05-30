#pragma once
#include "video_frame.h"
#include "ring_buffer.h"
#include <memory>
#include <functional>
#include <string>
#include <vector>
#include <cstdint>

// Known chipset variants
enum class ChipType { Unknown, STK1160, EMP2860, EMP2861 };

struct DeviceDescriptor {
    std::wstring path;
    uint16_t     vid;
    uint16_t     pid;
    ChipType     chip;
    std::wstring friendlyName;
};

// High-level interface used by the DirectShow filter.
// Hides chipset details; provides a single FrameCallback-based API.
class CaptureDevice {
public:
    CaptureDevice();
    ~CaptureDevice();

    CaptureDevice(const CaptureDevice&) = delete;
    CaptureDevice& operator=(const CaptureDevice&) = delete;

    // Enumerate all supported devices currently connected.
    static std::vector<DeviceDescriptor> EnumerateAll();

    // Open the first available device (or a specific path).
    bool Open(const std::wstring& devicePath = L"");
    void Close();
    bool IsOpen() const { return m_open; }

    ChipType     GetChipType()    const { return m_chip; }
    VideoFormat  GetDefaultFormat() const;

    // Start delivering frames to the ring buffer.
    bool StartCapture(VideoStandard std   = VideoStandard::NTSC,
                      InputChannel  ch    = InputChannel::CVBS);
    void StopCapture();

    // DirectShow delivery thread calls this to grab the next frame.
    // Returns false if no frame available within timeoutMs.
    bool WaitForFrame(VideoFrame& out, uint32_t timeoutMs = 100);

private:
    bool             m_open     { false };
    ChipType         m_chip     { ChipType::Unknown };
    VideoFormat      m_fmt      {};

    // Pimpl to avoid pulling chip headers into DirectShow code
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    FrameRingBuffer  m_ringBuf  { 8 };  // 8 frames deep
    HANDLE           m_newFrameEvent { nullptr };

    void OnFrame(const VideoFrame& frame);
};
