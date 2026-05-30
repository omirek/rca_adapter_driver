#pragma once
#include <cstdint>
#include <vector>

enum class VideoStandard { NTSC, PAL };
enum class InputChannel  { CVBS, SVIDEO };
enum class PixelFormat   { YUY2, UYVY, RGB24 };

struct VideoFormat {
    uint32_t      width;
    uint32_t      height;
    uint32_t      fps;
    PixelFormat   pixelFmt;
    VideoStandard standard;

    static VideoFormat NTSC_YUY2() {
        return { 720, 480, 30, PixelFormat::YUY2, VideoStandard::NTSC };
    }
    static VideoFormat PAL_YUY2() {
        return { 720, 576, 25, PixelFormat::YUY2, VideoStandard::PAL };
    }

    uint32_t stride()    const { return width * bytesPerPixel(); }
    uint32_t frameSize() const { return stride() * height; }

    uint32_t bytesPerPixel() const {
        switch (pixelFmt) {
            case PixelFormat::YUY2:
            case PixelFormat::UYVY:  return 2;
            case PixelFormat::RGB24: return 3;
        }
        return 2;
    }
};

struct VideoFrame {
    std::vector<uint8_t> data;
    VideoFormat          format;
    uint64_t             timestampUs;  // microseconds since capture start
    uint32_t             sequenceNum;
};
