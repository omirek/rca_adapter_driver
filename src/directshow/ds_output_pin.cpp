#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dshow.h>
#include <dvdmedia.h>
#include <uuids.h>
#include "ds_output_pin.h"
#include "ds_capture_filter.h"

// -----------------------------------------------------------------------
// RcaOutputPin
// -----------------------------------------------------------------------
RcaOutputPin::RcaOutputPin(RcaCaptureFilter* filter)
    : m_filter(filter) {
    InitializeCriticalSection(&m_cs);
    m_videoFmt = VideoFormat::NTSC_YUY2();
    ZeroMemory(&m_mediaType, sizeof(m_mediaType));
}

RcaOutputPin::~RcaOutputPin() {
    StopStreaming();
    FreeMediaType(m_mediaType);
    DeleteCriticalSection(&m_cs);
    if (m_allocator)  { m_allocator->Release();  m_allocator  = nullptr; }
    if (m_memInput)   { m_memInput->Release();   m_memInput   = nullptr; }
    if (m_connected)  { m_connected->Release();  m_connected  = nullptr; }
}

// -----------------------------------------------------------------------
// IUnknown
// -----------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE RcaOutputPin::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IPin) {
        *ppv = static_cast<IPin*>(this); AddRef(); return S_OK;
    }
    if (riid == IID_IAMStreamConfig) {
        *ppv = static_cast<IAMStreamConfig*>(this); AddRef(); return S_OK;
    }
    if (riid == IID_IQualityControl) {
        *ppv = static_cast<IQualityControl*>(this); AddRef(); return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}
ULONG STDMETHODCALLTYPE RcaOutputPin::AddRef()  { return ++m_refCount; }
ULONG STDMETHODCALLTYPE RcaOutputPin::Release() {
    ULONG r = --m_refCount;
    if (r == 0) delete this;
    return r;
}

// -----------------------------------------------------------------------
// Media type helper
// -----------------------------------------------------------------------
HRESULT RcaOutputPin::BuildMediaType(AM_MEDIA_TYPE* pmt, const VideoFormat& fmt) {
    ZeroMemory(pmt, sizeof(AM_MEDIA_TYPE));

    auto* vi = reinterpret_cast<VIDEOINFOHEADER*>(
        CoTaskMemAlloc(sizeof(VIDEOINFOHEADER)));
    if (!vi) return E_OUTOFMEMORY;
    ZeroMemory(vi, sizeof(VIDEOINFOHEADER));

    vi->AvgTimePerFrame = 10000000LL / fmt.fps;  // 100ns units
    vi->bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    vi->bmiHeader.biWidth       = static_cast<LONG>(fmt.width);
    vi->bmiHeader.biHeight      = static_cast<LONG>(fmt.height);
    vi->bmiHeader.biPlanes      = 1;
    vi->bmiHeader.biBitCount    = 16;  // YUY2
    vi->bmiHeader.biCompression = MAKEFOURCC('Y','U','Y','2');
    vi->bmiHeader.biSizeImage   = fmt.frameSize();

    pmt->majortype            = MEDIATYPE_Video;
    pmt->subtype              = MEDIASUBTYPE_YUY2;
    pmt->formattype           = FORMAT_VideoInfo;
    pmt->bFixedSizeSamples    = TRUE;
    pmt->bTemporalCompression = FALSE;
    pmt->lSampleSize          = fmt.frameSize();
    pmt->cbFormat             = sizeof(VIDEOINFOHEADER);
    pmt->pbFormat             = reinterpret_cast<BYTE*>(vi);

    return S_OK;
}

void RcaOutputPin::FreeMediaType(AM_MEDIA_TYPE& mt) {
    if (mt.cbFormat && mt.pbFormat) {
        CoTaskMemFree(mt.pbFormat);
        mt.pbFormat = nullptr;
        mt.cbFormat = 0;
    }
    if (mt.pUnk) { mt.pUnk->Release(); mt.pUnk = nullptr; }
}

// -----------------------------------------------------------------------
// IPin – Connect / Disconnect
// -----------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE RcaOutputPin::Connect(IPin* pReceivePin,
                                                  const AM_MEDIA_TYPE* pmt) {
    if (!pReceivePin) return E_POINTER;
    if (m_connected)  return VFW_E_ALREADY_CONNECTED;

    // Build our preferred media type
    AM_MEDIA_TYPE proposed{};
    HRESULT hr = BuildMediaType(&proposed, m_videoFmt);
    if (FAILED(hr)) return hr;

    // If caller specifies a type, verify compatibility
    if (pmt && pmt->majortype != GUID_NULL) {
        if (pmt->majortype != MEDIATYPE_Video ||
            pmt->subtype   != MEDIASUBTYPE_YUY2) {
            FreeMediaType(proposed);
            return VFW_E_TYPE_NOT_ACCEPTED;
        }
    }

    hr = pReceivePin->ReceiveConnection(static_cast<IPin*>(this), &proposed);
    FreeMediaType(proposed);
    if (FAILED(hr)) return hr;

    hr = pReceivePin->QueryInterface(IID_IMemInputPin,
                                     reinterpret_cast<void**>(&m_memInput));
    if (FAILED(hr)) { pReceivePin->Disconnect(); return hr; }

    // Negotiate allocator
    ALLOCATOR_PROPERTIES props{};
    props.cBuffers  = 4;
    props.cbBuffer  = static_cast<LONG>(m_videoFmt.frameSize());
    props.cbAlign   = 1;
    props.cbPrefix  = 0;

    hr = m_memInput->GetAllocator(&m_allocator);
    if (FAILED(hr)) {
        hr = CoCreateInstance(CLSID_MemoryAllocator, nullptr, CLSCTX_INPROC,
                              IID_IMemAllocator,
                              reinterpret_cast<void**>(&m_allocator));
        if (FAILED(hr)) { m_memInput->Release(); m_memInput = nullptr; return hr; }
    }

    ALLOCATOR_PROPERTIES actual{};
    m_allocator->SetProperties(&props, &actual);
    m_allocator->Commit();
    m_memInput->NotifyAllocator(m_allocator, FALSE);

    m_connected = pReceivePin;
    m_connected->AddRef();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE RcaOutputPin::ReceiveConnection(IPin*, const AM_MEDIA_TYPE*) {
    return E_UNEXPECTED;  // output pin does not accept incoming connections
}

HRESULT STDMETHODCALLTYPE RcaOutputPin::Disconnect() {
    StopStreaming();
    if (m_allocator)  { m_allocator->Decommit(); m_allocator->Release(); m_allocator = nullptr; }
    if (m_memInput)   { m_memInput->Release();   m_memInput  = nullptr; }
    if (m_connected)  { m_connected->Release();  m_connected = nullptr; }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE RcaOutputPin::ConnectedTo(IPin** ppPin) {
    if (!ppPin) return E_POINTER;
    *ppPin = m_connected;
    if (m_connected) m_connected->AddRef();
    return m_connected ? S_OK : VFW_E_NOT_CONNECTED;
}

HRESULT STDMETHODCALLTYPE RcaOutputPin::ConnectionMediaType(AM_MEDIA_TYPE* pmt) {
    if (!pmt) return E_POINTER;
    if (!m_connected) return VFW_E_NOT_CONNECTED;
    return BuildMediaType(pmt, m_videoFmt);
}

HRESULT STDMETHODCALLTYPE RcaOutputPin::QueryPinInfo(PIN_INFO* pInfo) {
    if (!pInfo) return E_POINTER;
    pInfo->pFilter = m_filter;
    if (m_filter) m_filter->AddRef();
    pInfo->dir = PINDIR_OUTPUT;
    wcscpy_s(pInfo->achName, L"Output");
    return S_OK;
}
HRESULT STDMETHODCALLTYPE RcaOutputPin::QueryDirection(PIN_DIRECTION* pDir) {
    if (!pDir) return E_POINTER;
    *pDir = PINDIR_OUTPUT;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE RcaOutputPin::QueryId(LPWSTR* Id) {
    if (!Id) return E_POINTER;
    *Id = static_cast<LPWSTR>(CoTaskMemAlloc(14));
    if (!*Id) return E_OUTOFMEMORY;
    wcscpy(reinterpret_cast<wchar_t*>(*Id), L"Output");
    return S_OK;
}
HRESULT STDMETHODCALLTYPE RcaOutputPin::QueryAccept(const AM_MEDIA_TYPE* pmt) {
    if (!pmt) return E_POINTER;
    return (pmt->majortype == MEDIATYPE_Video &&
            pmt->subtype   == MEDIASUBTYPE_YUY2) ? S_OK : S_FALSE;
}
HRESULT STDMETHODCALLTYPE RcaOutputPin::EnumMediaTypes(IEnumMediaTypes** ppEnum) {
    if (!ppEnum) return E_POINTER;
    *ppEnum = new RcaEnumMediaTypes(this);
    return S_OK;
}
HRESULT STDMETHODCALLTYPE RcaOutputPin::QueryInternalConnections(IPin**, ULONG* nPin) {
    if (nPin) *nPin = 0;
    return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE RcaOutputPin::EndOfStream()  { return S_OK; }
HRESULT STDMETHODCALLTYPE RcaOutputPin::BeginFlush() {
    m_flushing = true;  return S_OK;
}
HRESULT STDMETHODCALLTYPE RcaOutputPin::EndFlush() {
    m_flushing = false; return S_OK;
}
HRESULT STDMETHODCALLTYPE RcaOutputPin::NewSegment(REFERENCE_TIME, REFERENCE_TIME, double) {
    return S_OK;
}

// -----------------------------------------------------------------------
// IAMStreamConfig
// -----------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE RcaOutputPin::SetFormat(AM_MEDIA_TYPE* pmt) {
    if (!pmt) return E_POINTER;
    if (pmt->majortype != MEDIATYPE_Video || pmt->subtype != MEDIASUBTYPE_YUY2)
        return VFW_E_INVALIDMEDIATYPE;
    if (pmt->formattype == FORMAT_VideoInfo && pmt->pbFormat) {
        auto* vi = reinterpret_cast<VIDEOINFOHEADER*>(pmt->pbFormat);
        m_videoFmt.width  = static_cast<uint32_t>(vi->bmiHeader.biWidth);
        m_videoFmt.height = static_cast<uint32_t>(abs(vi->bmiHeader.biHeight));
        m_videoFmt.fps    = (vi->AvgTimePerFrame > 0)
                            ? static_cast<uint32_t>(10000000LL / vi->AvgTimePerFrame)
                            : 30;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE RcaOutputPin::GetFormat(AM_MEDIA_TYPE** ppmt) {
    if (!ppmt) return E_POINTER;
    *ppmt = static_cast<AM_MEDIA_TYPE*>(CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE)));
    if (!*ppmt) return E_OUTOFMEMORY;
    return BuildMediaType(*ppmt, m_videoFmt);
}

HRESULT STDMETHODCALLTYPE RcaOutputPin::GetNumberOfCapabilities(int* piCount, int* piSize) {
    if (!piCount || !piSize) return E_POINTER;
    *piCount = 2;  // NTSC + PAL
    *piSize  = sizeof(VIDEO_STREAM_CONFIG_CAPS);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE RcaOutputPin::GetStreamCaps(int iIndex,
                                                        AM_MEDIA_TYPE** ppmt,
                                                        BYTE* pSCC) {
    if (!ppmt || !pSCC) return E_POINTER;
    if (iIndex < 0 || iIndex > 1) return S_FALSE;

    VideoFormat fmt = (iIndex == 0) ? VideoFormat::NTSC_YUY2() : VideoFormat::PAL_YUY2();
    *ppmt = static_cast<AM_MEDIA_TYPE*>(CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE)));
    if (!*ppmt) return E_OUTOFMEMORY;
    HRESULT hr = BuildMediaType(*ppmt, fmt);
    if (FAILED(hr)) { CoTaskMemFree(*ppmt); return hr; }

    auto* caps = reinterpret_cast<VIDEO_STREAM_CONFIG_CAPS*>(pSCC);
    ZeroMemory(caps, sizeof(VIDEO_STREAM_CONFIG_CAPS));
    caps->guid              = FORMAT_VideoInfo;
    caps->VideoStandard     = (iIndex == 0) ? AnalogVideo_NTSC_M : AnalogVideo_PAL_B;
    caps->InputSize         = { static_cast<LONG>(fmt.width), static_cast<LONG>(fmt.height) };
    caps->MinCroppingSize   = caps->MaxCroppingSize = caps->InputSize;
    caps->CropGranularityX  = caps->CropGranularityY  = 1;
    caps->MinOutputSize     = caps->MaxOutputSize     = caps->InputSize;
    caps->OutputGranularityX = caps->OutputGranularityY = 1;
    caps->MinFrameInterval  = caps->MaxFrameInterval =
        caps->MinFrameInterval = 10000000LL / fmt.fps;
    caps->MinBitsPerSecond  = caps->MaxBitsPerSecond =
        fmt.frameSize() * fmt.fps * 8;

    return S_OK;
}

// -----------------------------------------------------------------------
// IQualityControl
// -----------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE RcaOutputPin::Notify(IBaseFilter*, Quality) { return S_OK; }
HRESULT STDMETHODCALLTYPE RcaOutputPin::SetSink(IQualityControl*)     { return S_OK; }

// -----------------------------------------------------------------------
// Delivery thread
// -----------------------------------------------------------------------
DWORD WINAPI RcaOutputPin::DeliverThreadProc(LPVOID param) {
    reinterpret_cast<RcaOutputPin*>(param)->DeliverLoop();
    return 0;
}

void RcaOutputPin::DeliverLoop() {
    while (m_delivering) {
        if (m_flushing || !m_connected || !m_memInput || !m_allocator) {
            Sleep(5);
            continue;
        }

        VideoFrame frame;
        if (!m_filter->GetDevice().WaitForFrame(frame, 100))
            continue;

        IMediaSample* sample = nullptr;
        HRESULT hr = m_allocator->GetBuffer(&sample, nullptr, nullptr, 0);
        if (FAILED(hr)) continue;

        BYTE* buf = nullptr;
        sample->GetPointer(&buf);

        const uint32_t frameSize = frame.format.frameSize();
        const LONG     sampleLen = static_cast<LONG>(
            min(frameSize, static_cast<uint32_t>(sample->GetSize())));

        memcpy(buf, frame.data.data(), sampleLen);
        sample->SetActualDataLength(sampleLen);
        sample->SetSyncPoint(TRUE);
        sample->SetDiscontinuity(FALSE);

        // Set timestamps
        if (IReferenceClock* clock = m_filter->GetClock()) {
            REFERENCE_TIME now = 0;
            clock->GetTime(&now);
            REFERENCE_TIME duration = 10000000LL / frame.format.fps;
            REFERENCE_TIME stop = now + duration;
            sample->SetTime(&now, &stop);
        }

        m_memInput->Receive(sample);
        sample->Release();
    }
}

HRESULT RcaOutputPin::StartStreaming() {
    if (m_delivering) return S_OK;
    m_delivering = true;
    m_flushing   = false;
    m_deliverThread = CreateThread(nullptr, 0, DeliverThreadProc, this, 0, nullptr);
    return m_deliverThread ? S_OK : E_FAIL;
}

HRESULT RcaOutputPin::StopStreaming() {
    if (!m_delivering) return S_OK;
    m_delivering = false;
    if (m_deliverThread) {
        WaitForSingleObject(m_deliverThread, 2000);
        CloseHandle(m_deliverThread);
        m_deliverThread = nullptr;
    }
    return S_OK;
}

// -----------------------------------------------------------------------
// RcaEnumMediaTypes
// -----------------------------------------------------------------------
RcaEnumMediaTypes::RcaEnumMediaTypes(RcaOutputPin* pin, ULONG pos)
    : m_pin(pin), m_pos(pos) {
    if (m_pin) m_pin->AddRef();
}

HRESULT STDMETHODCALLTYPE RcaEnumMediaTypes::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IEnumMediaTypes) {
        *ppv = static_cast<IEnumMediaTypes*>(this); AddRef(); return S_OK;
    }
    *ppv = nullptr; return E_NOINTERFACE;
}
ULONG STDMETHODCALLTYPE RcaEnumMediaTypes::AddRef()  { return ++m_refCount; }
ULONG STDMETHODCALLTYPE RcaEnumMediaTypes::Release() {
    ULONG r = --m_refCount;
    if (r == 0) { if (m_pin) m_pin->Release(); delete this; }
    return r;
}

HRESULT STDMETHODCALLTYPE RcaEnumMediaTypes::Next(ULONG cTypes,
                                                    AM_MEDIA_TYPE** ppTypes,
                                                    ULONG* pcFetched) {
    if (!ppTypes) return E_POINTER;
    ULONG fetched = 0;

    // We advertise 2 types: NTSC and PAL
    VideoFormat fmts[2] = { VideoFormat::NTSC_YUY2(), VideoFormat::PAL_YUY2() };

    while (fetched < cTypes && m_pos < 2) {
        ppTypes[fetched] = static_cast<AM_MEDIA_TYPE*>(
            CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE)));
        if (!ppTypes[fetched]) break;

        // Use IAMStreamConfig::GetStreamCaps logic inline via the pin
        int dummy = 0, dummy2 = 0;
        m_pin->GetNumberOfCapabilities(&dummy, &dummy2);

        AM_MEDIA_TYPE* pmt = nullptr;
        std::vector<BYTE> scc(dummy2);
        if (SUCCEEDED(m_pin->GetStreamCaps(static_cast<int>(m_pos),
                                            &pmt, scc.data())) && pmt) {
            *ppTypes[fetched] = *pmt;
            CoTaskMemFree(pmt);  // shallow-free the outer struct only
            ++fetched;
            ++m_pos;
        } else {
            CoTaskMemFree(ppTypes[fetched]);
            ppTypes[fetched] = nullptr;
            break;
        }
    }

    if (pcFetched) *pcFetched = fetched;
    return (fetched == cTypes) ? S_OK : S_FALSE;
}

HRESULT STDMETHODCALLTYPE RcaEnumMediaTypes::Skip(ULONG n) {
    m_pos += n; return (m_pos <= 2) ? S_OK : S_FALSE;
}
HRESULT STDMETHODCALLTYPE RcaEnumMediaTypes::Reset() { m_pos = 0; return S_OK; }
HRESULT STDMETHODCALLTYPE RcaEnumMediaTypes::Clone(IEnumMediaTypes** ppEnum) {
    if (!ppEnum) return E_POINTER;
    *ppEnum = new RcaEnumMediaTypes(m_pin, m_pos);
    return S_OK;
}
