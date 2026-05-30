#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dshow.h>
#include "ds_capture_filter.h"
#include "ds_output_pin.h"
#include "ds_guids.h"

// -----------------------------------------------------------------------
// RcaCaptureFilter
// -----------------------------------------------------------------------
RcaCaptureFilter::RcaCaptureFilter() {
    m_outputPin = new RcaOutputPin(this);
    wcscpy_s(m_name, L"RCA Capture");
}

RcaCaptureFilter::~RcaCaptureFilter() {
    if (m_outputPin) { m_outputPin->Release(); m_outputPin = nullptr; }
    if (m_clock)     { m_clock->Release();     m_clock     = nullptr; }
}

// -----------------------------------------------------------------------
// IUnknown
// -----------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE RcaCaptureFilter::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IBaseFilter ||
        riid == IID_IMediaFilter || riid == IID_IPersist) {
        *ppv = static_cast<IBaseFilter*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE RcaCaptureFilter::AddRef() {
    return ++m_refCount;
}
ULONG STDMETHODCALLTYPE RcaCaptureFilter::Release() {
    ULONG r = --m_refCount;
    if (r == 0) delete this;
    return r;
}

// -----------------------------------------------------------------------
// IPersist
// -----------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE RcaCaptureFilter::GetClassID(CLSID* pClassID) {
    if (!pClassID) return E_POINTER;
    *pClassID = CLSID_RcaCaptureFilter;
    return S_OK;
}

// -----------------------------------------------------------------------
// IMediaFilter – state machine
// -----------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE RcaCaptureFilter::Stop() {
    if (m_state != State_Stopped) {
        m_outputPin->StopStreaming();
        m_device.StopCapture();
        m_device.Close();
    }
    m_state = State_Stopped;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE RcaCaptureFilter::Pause() {
    if (m_state == State_Stopped) {
        if (!m_device.Open()) return E_FAIL;
        if (!m_device.StartCapture()) { m_device.Close(); return E_FAIL; }
        m_outputPin->StartStreaming();
    }
    m_state = State_Paused;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE RcaCaptureFilter::Run(REFERENCE_TIME tStart) {
    m_tStart = tStart;
    if (m_state == State_Stopped) {
        HRESULT hr = Pause();
        if (FAILED(hr)) return hr;
    }
    m_state = State_Running;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE RcaCaptureFilter::GetState(DWORD /*dwMilliSecs*/,
                                                       FILTER_STATE* pState) {
    if (!pState) return E_POINTER;
    *pState = m_state;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE RcaCaptureFilter::SetSyncSource(IReferenceClock* pClock) {
    if (m_clock) m_clock->Release();
    m_clock = pClock;
    if (m_clock) m_clock->AddRef();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE RcaCaptureFilter::GetSyncSource(IReferenceClock** ppClock) {
    if (!ppClock) return E_POINTER;
    *ppClock = m_clock;
    if (m_clock) m_clock->AddRef();
    return S_OK;
}

// -----------------------------------------------------------------------
// IBaseFilter
// -----------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE RcaCaptureFilter::EnumPins(IEnumPins** ppEnum) {
    if (!ppEnum) return E_POINTER;
    *ppEnum = new RcaEnumPins(static_cast<IPin*>(m_outputPin));
    return S_OK;
}

HRESULT STDMETHODCALLTYPE RcaCaptureFilter::FindPin(LPCWSTR Id, IPin** ppPin) {
    if (!ppPin) return E_POINTER;
    if (wcscmp(Id, L"Output") == 0) {
        *ppPin = m_outputPin;
        m_outputPin->AddRef();
        return S_OK;
    }
    return VFW_E_NOT_FOUND;
}

HRESULT STDMETHODCALLTYPE RcaCaptureFilter::QueryFilterInfo(FILTER_INFO* pInfo) {
    if (!pInfo) return E_POINTER;
    wcscpy_s(pInfo->achName, m_name);
    pInfo->pGraph = m_graph;
    if (m_graph) m_graph->AddRef();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE RcaCaptureFilter::JoinFilterGraph(IFilterGraph* pGraph,
                                                              LPCWSTR pName) {
    m_graph = pGraph;   // back-reference, not AddRef'd (graph owns filter)
    if (pName) wcscpy_s(m_name, pName);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE RcaCaptureFilter::QueryVendorInfo(LPWSTR* pVendorInfo) {
    if (!pVendorInfo) return E_POINTER;
    *pVendorInfo = nullptr;
    return E_NOTIMPL;
}

// -----------------------------------------------------------------------
// RcaEnumPins
// -----------------------------------------------------------------------
RcaEnumPins::RcaEnumPins(IPin* pin, ULONG pos)
    : m_pin(pin), m_pos(pos) {
    if (m_pin) m_pin->AddRef();
}

HRESULT STDMETHODCALLTYPE RcaEnumPins::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IEnumPins) {
        *ppv = static_cast<IEnumPins*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}
ULONG STDMETHODCALLTYPE RcaEnumPins::AddRef()  { return ++m_refCount; }
ULONG STDMETHODCALLTYPE RcaEnumPins::Release() {
    ULONG r = --m_refCount;
    if (r == 0) {
        if (m_pin) m_pin->Release();
        delete this;
    }
    return r;
}

HRESULT STDMETHODCALLTYPE RcaEnumPins::Next(ULONG cPins, IPin** ppPins,
                                              ULONG* pcFetched) {
    if (!ppPins) return E_POINTER;
    ULONG fetched = 0;
    while (fetched < cPins && m_pos < 1) {
        ppPins[fetched++] = m_pin;
        m_pin->AddRef();
        ++m_pos;
    }
    if (pcFetched) *pcFetched = fetched;
    return (fetched == cPins) ? S_OK : S_FALSE;
}
HRESULT STDMETHODCALLTYPE RcaEnumPins::Skip(ULONG cPins) {
    m_pos += cPins;
    return (m_pos <= 1) ? S_OK : S_FALSE;
}
HRESULT STDMETHODCALLTYPE RcaEnumPins::Reset() { m_pos = 0; return S_OK; }
HRESULT STDMETHODCALLTYPE RcaEnumPins::Clone(IEnumPins** ppEnum) {
    if (!ppEnum) return E_POINTER;
    *ppEnum = new RcaEnumPins(m_pin, m_pos);
    return S_OK;
}
