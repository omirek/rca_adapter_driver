#include "capture_device.h"
#include "usb_device.h"
#include "stk1160.h"
#include "emp2861.h"
#include <variant>

// Supported VID/PID list
static const std::vector<std::pair<uint16_t,uint16_t>> kSupportedDevices = {
    { 0x05E1, 0x0408 },  // STK1160
    { 0xEB1A, 0x2861 },  // EMP2861
    { 0xEB1A, 0x2860 },  // EMP2860
    { 0xEB1A, 0xE8D3 },  // eMPIA generic
    { 0x1C88, 0x0007 },  // Somagic
};

static ChipType ChipFromVidPid(uint16_t vid, uint16_t pid) {
    if (vid == 0x05E1 && pid == 0x0408) return ChipType::STK1160;
    if (vid == 0xEB1A && pid == 0x2861) return ChipType::EMP2861;
    if (vid == 0xEB1A && pid == 0x2860) return ChipType::EMP2860;
    return ChipType::Unknown;
}

// -----------------------------------------------------------------------
// Pimpl – holds the chipset driver (variant avoids dynamic allocation)
// -----------------------------------------------------------------------
struct CaptureDevice::Impl {
    UsbDevice                                 usb;
    std::variant<std::monostate,
                 Stk1160Driver,
                 Emp2861Driver>               chip;
};

// -----------------------------------------------------------------------
CaptureDevice::CaptureDevice()
    : m_impl(std::make_unique<Impl>())
    , m_ringBuf(8)
{
    m_newFrameEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

CaptureDevice::~CaptureDevice() {
    Close();
    if (m_newFrameEvent) {
        CloseHandle(m_newFrameEvent);
        m_newFrameEvent = nullptr;
    }
}

// -----------------------------------------------------------------------
std::vector<DeviceDescriptor> CaptureDevice::EnumerateAll() {
    auto infos = UsbDevice::Enumerate(kSupportedDevices);
    std::vector<DeviceDescriptor> result;
    for (auto& info : infos) {
        DeviceDescriptor d;
        d.path  = info.devicePath;
        d.vid   = info.vid;
        d.pid   = info.pid;
        d.chip  = ChipFromVidPid(info.vid, info.pid);
        switch (d.chip) {
            case ChipType::STK1160: d.friendlyName = L"USB Video Capture (STK1160)"; break;
            case ChipType::EMP2861: d.friendlyName = L"USB Video Capture (EMP2861)"; break;
            case ChipType::EMP2860: d.friendlyName = L"USB Video Capture (EMP2860)"; break;
            default:                d.friendlyName = L"USB Video Capture (Unknown)";  break;
        }
        result.push_back(std::move(d));
    }
    return result;
}

// -----------------------------------------------------------------------
bool CaptureDevice::Open(const std::wstring& devicePath) {
    Close();

    bool opened = devicePath.empty()
                  ? m_impl->usb.Open()
                  : m_impl->usb.Open(devicePath);

    if (!opened) return false;

    // Read VID/PID from USB descriptor to identify chip
    // (We can't easily query VID/PID after opening via interface GUID,
    //  so default to EMP2861 if unknown – most common for this device)
    m_chip = ChipType::EMP2861;

    // Construct the right chip driver
    if (m_chip == ChipType::STK1160) {
        m_impl->chip.emplace<Stk1160Driver>(m_impl->usb);
    } else {
        m_impl->chip.emplace<Emp2861Driver>(m_impl->usb);
    }

    m_open = true;
    return true;
}

void CaptureDevice::Close() {
    StopCapture();
    m_impl->chip.emplace<std::monostate>();
    m_impl->usb.Close();
    m_open = false;
}

// -----------------------------------------------------------------------
VideoFormat CaptureDevice::GetDefaultFormat() const {
    return VideoFormat::NTSC_YUY2();
}

// -----------------------------------------------------------------------
void CaptureDevice::OnFrame(const VideoFrame& frame) {
    VideoFrame copy = frame;
    m_ringBuf.push(std::move(copy));
    SetEvent(m_newFrameEvent);
}

bool CaptureDevice::StartCapture(VideoStandard std, InputChannel ch) {
    if (!m_open) return false;

    m_fmt = (std == VideoStandard::PAL)
            ? VideoFormat::PAL_YUY2()
            : VideoFormat::NTSC_YUY2();

    FrameCallback cb = [this](const VideoFrame& f) { OnFrame(f); };

    bool ok = false;
    if (auto* stk = std::get_if<Stk1160Driver>(&m_impl->chip)) {
        ok = stk->Initialize(std, ch) && stk->StartCapture(m_fmt, cb);
    } else if (auto* emp = std::get_if<Emp2861Driver>(&m_impl->chip)) {
        ok = emp->Initialize(std, ch) && emp->StartCapture(m_fmt, cb);
    }
    return ok;
}

void CaptureDevice::StopCapture() {
    if (auto* stk = std::get_if<Stk1160Driver>(&m_impl->chip))
        stk->StopCapture();
    else if (auto* emp = std::get_if<Emp2861Driver>(&m_impl->chip))
        emp->StopCapture();
}

bool CaptureDevice::WaitForFrame(VideoFrame& out, uint32_t timeoutMs) {
    // Fast path: frame already in buffer
    auto frame = m_ringBuf.pop();
    if (frame) { out = std::move(*frame); return true; }

    // Wait for producer signal
    if (WaitForSingleObject(m_newFrameEvent, timeoutMs) != WAIT_OBJECT_0)
        return false;

    frame = m_ringBuf.pop();
    if (frame) { out = std::move(*frame); return true; }
    return false;
}
