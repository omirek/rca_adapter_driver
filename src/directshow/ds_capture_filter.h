#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dshow.h>
#include <atomic>
#include <memory>
#include "capture_device.h"
#include "ds_guids.h"

class RcaOutputPin;

// -----------------------------------------------------------------------
// RcaCaptureFilter – DirectShow source filter.
//
// Implements: IBaseFilter, IMediaFilter, IPersist, IUnknown
//
// OBS enumerates this via the "Video Capture Sources" category
// (CLSID_VideoInputDeviceCategory).  The output pin delivers YUY2 frames
// at 720x480@30fps (NTSC) or 720x576@25fps (PAL).
// -----------------------------------------------------------------------
class RcaCaptureFilter : public IBaseFilter {
public:
    RcaCaptureFilter();
    ~RcaCaptureFilter();

    // --- IUnknown ---
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG   STDMETHODCALLTYPE AddRef()  override;
    ULONG   STDMETHODCALLTYPE Release() override;

    // --- IPersist ---
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* pClassID) override;

    // --- IMediaFilter ---
    HRESULT STDMETHODCALLTYPE Stop()  override;
    HRESULT STDMETHODCALLTYPE Pause() override;
    HRESULT STDMETHODCALLTYPE Run(REFERENCE_TIME tStart) override;
    HRESULT STDMETHODCALLTYPE GetState(DWORD dwMilliSecsTimeout,
                                        FILTER_STATE* State) override;
    HRESULT STDMETHODCALLTYPE SetSyncSource(IReferenceClock* pClock) override;
    HRESULT STDMETHODCALLTYPE GetSyncSource(IReferenceClock** ppClock) override;

    // --- IBaseFilter ---
    HRESULT STDMETHODCALLTYPE EnumPins(IEnumPins** ppEnum) override;
    HRESULT STDMETHODCALLTYPE FindPin(LPCWSTR Id, IPin** ppPin) override;
    HRESULT STDMETHODCALLTYPE QueryFilterInfo(FILTER_INFO* pInfo) override;
    HRESULT STDMETHODCALLTYPE JoinFilterGraph(IFilterGraph* pGraph,
                                               LPCWSTR pName) override;
    HRESULT STDMETHODCALLTYPE QueryVendorInfo(LPWSTR* pVendorInfo) override;

    // Accessors used by the output pin
    IFilterGraph*     GetGraph()    const { return m_graph; }
    IReferenceClock*  GetClock()    const { return m_clock; }
    FILTER_STATE      GetState()    const { return m_state; }
    CaptureDevice&    GetDevice()         { return m_device; }

private:
    std::atomic<ULONG>  m_refCount { 1 };
    FILTER_STATE        m_state    { State_Stopped };
    IFilterGraph*       m_graph    { nullptr };
    IReferenceClock*    m_clock    { nullptr };
    wchar_t             m_name[128]{};
    REFERENCE_TIME      m_tStart   { 0 };

    CaptureDevice       m_device;
    RcaOutputPin*       m_outputPin{ nullptr };
};

// -----------------------------------------------------------------------
// RcaEnumPins – minimal single-pin enumerator required by DirectShow
// -----------------------------------------------------------------------
class RcaEnumPins : public IEnumPins {
public:
    RcaEnumPins(IPin* pin, ULONG pos = 0);

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG   STDMETHODCALLTYPE AddRef()  override;
    ULONG   STDMETHODCALLTYPE Release() override;

    HRESULT STDMETHODCALLTYPE Next(ULONG cPins, IPin** ppPins, ULONG* pcFetched) override;
    HRESULT STDMETHODCALLTYPE Skip(ULONG cPins) override;
    HRESULT STDMETHODCALLTYPE Reset() override;
    HRESULT STDMETHODCALLTYPE Clone(IEnumPins** ppEnum) override;

private:
    std::atomic<ULONG> m_refCount { 1 };
    IPin*              m_pin;
    ULONG              m_pos;
};
