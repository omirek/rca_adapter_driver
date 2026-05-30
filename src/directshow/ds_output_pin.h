#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dshow.h>
#include <atomic>
#include "video_frame.h"

class RcaCaptureFilter;

// -----------------------------------------------------------------------
// RcaOutputPin – push-mode video output pin.
//
// Implements: IPin, IQualityControl, IAMStreamConfig
//
// The delivery thread runs inside the pin and calls
// IMemInputPin::Receive() on the connected downstream pin.
// -----------------------------------------------------------------------
class RcaOutputPin : public IPin, public IAMStreamConfig, public IQualityControl {
public:
    explicit RcaOutputPin(RcaCaptureFilter* filter);
    ~RcaOutputPin();

    // --- IUnknown ---
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG   STDMETHODCALLTYPE AddRef()  override;
    ULONG   STDMETHODCALLTYPE Release() override;

    // --- IPin ---
    HRESULT STDMETHODCALLTYPE Connect(IPin* pReceivePin,
                                       const AM_MEDIA_TYPE* pmt) override;
    HRESULT STDMETHODCALLTYPE ReceiveConnection(IPin* pConnector,
                                                 const AM_MEDIA_TYPE* pmt) override;
    HRESULT STDMETHODCALLTYPE Disconnect() override;
    HRESULT STDMETHODCALLTYPE ConnectedTo(IPin** ppPin) override;
    HRESULT STDMETHODCALLTYPE ConnectionMediaType(AM_MEDIA_TYPE* pmt) override;
    HRESULT STDMETHODCALLTYPE QueryPinInfo(PIN_INFO* pInfo) override;
    HRESULT STDMETHODCALLTYPE QueryDirection(PIN_DIRECTION* pPinDir) override;
    HRESULT STDMETHODCALLTYPE QueryId(LPWSTR* Id) override;
    HRESULT STDMETHODCALLTYPE QueryAccept(const AM_MEDIA_TYPE* pmt) override;
    HRESULT STDMETHODCALLTYPE EnumMediaTypes(IEnumMediaTypes** ppEnum) override;
    HRESULT STDMETHODCALLTYPE QueryInternalConnections(IPin** apPin, ULONG* nPin) override;
    HRESULT STDMETHODCALLTYPE EndOfStream() override;
    HRESULT STDMETHODCALLTYPE BeginFlush() override;
    HRESULT STDMETHODCALLTYPE EndFlush() override;
    HRESULT STDMETHODCALLTYPE NewSegment(REFERENCE_TIME tStart,
                                          REFERENCE_TIME tStop,
                                          double dRate) override;

    // --- IAMStreamConfig ---
    HRESULT STDMETHODCALLTYPE SetFormat(AM_MEDIA_TYPE* pmt) override;
    HRESULT STDMETHODCALLTYPE GetFormat(AM_MEDIA_TYPE** ppmt) override;
    HRESULT STDMETHODCALLTYPE GetNumberOfCapabilities(int* piCount, int* piSize) override;
    HRESULT STDMETHODCALLTYPE GetStreamCaps(int iIndex, AM_MEDIA_TYPE** ppmt,
                                             BYTE* pSCC) override;

    // --- IQualityControl ---
    HRESULT STDMETHODCALLTYPE Notify(IBaseFilter* pSelf, Quality q) override;
    HRESULT STDMETHODCALLTYPE SetSink(IQualityControl* piqc) override;

    // Called by filter on state transitions
    HRESULT StartStreaming();
    HRESULT StopStreaming();

private:
    void    DeliverLoop();
    HRESULT BuildMediaType(AM_MEDIA_TYPE* pmt, const VideoFormat& fmt);
    void    FreeMediaType(AM_MEDIA_TYPE& mt);

    static DWORD WINAPI DeliverThreadProc(LPVOID param);

    std::atomic<ULONG> m_refCount  { 1 };
    RcaCaptureFilter*  m_filter    { nullptr };
    IPin*              m_connected { nullptr };
    IMemInputPin*      m_memInput  { nullptr };
    IMemAllocator*     m_allocator { nullptr };

    AM_MEDIA_TYPE      m_mediaType {};
    VideoFormat        m_videoFmt  {};

    HANDLE             m_deliverThread { nullptr };
    volatile bool      m_delivering    { false };
    volatile bool      m_flushing      { false };
    CRITICAL_SECTION   m_cs;
};

// -----------------------------------------------------------------------
// RcaEnumMediaTypes – enumerates the single supported media type
// -----------------------------------------------------------------------
class RcaEnumMediaTypes : public IEnumMediaTypes {
public:
    explicit RcaEnumMediaTypes(RcaOutputPin* pin, ULONG pos = 0);

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG   STDMETHODCALLTYPE AddRef()  override;
    ULONG   STDMETHODCALLTYPE Release() override;

    HRESULT STDMETHODCALLTYPE Next(ULONG cMediaTypes, AM_MEDIA_TYPE** ppMediaTypes,
                                    ULONG* pcFetched) override;
    HRESULT STDMETHODCALLTYPE Skip(ULONG cMediaTypes) override;
    HRESULT STDMETHODCALLTYPE Reset() override;
    HRESULT STDMETHODCALLTYPE Clone(IEnumMediaTypes** ppEnum) override;

private:
    std::atomic<ULONG> m_refCount { 1 };
    RcaOutputPin*      m_pin;
    ULONG              m_pos;
};
