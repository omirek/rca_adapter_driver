#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dshow.h>
#include <strmif.h>
#include "ds_capture_filter.h"
#include "ds_guids.h"

// Registry path for Video Capture Sources category
static const wchar_t kCategoryKey[] =
    L"CLSID\\{860BB310-5D01-11d0-BD3B-00A0C911CE86}\\Instance";
static const wchar_t kFilterName[] = L"RCA Capture (STK1160/EMP2861)";

// -----------------------------------------------------------------------
// DLL reference count (for DllCanUnloadNow)
// -----------------------------------------------------------------------
static volatile LONG g_dllRefCount = 0;

void DllAddRef()     { InterlockedIncrement(&g_dllRefCount); }
void DllRelease()    { InterlockedDecrement(&g_dllRefCount); }

// -----------------------------------------------------------------------
// DllMain
// -----------------------------------------------------------------------
BOOL WINAPI DllMain(HINSTANCE, DWORD fdwReason, LPVOID) {
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH: break;
        case DLL_PROCESS_DETACH: break;
    }
    return TRUE;
}

// -----------------------------------------------------------------------
// DllCanUnloadNow
// -----------------------------------------------------------------------
STDAPI DllCanUnloadNow() {
    return (g_dllRefCount == 0) ? S_OK : S_FALSE;
}

// -----------------------------------------------------------------------
// Class factory for RcaCaptureFilter
// -----------------------------------------------------------------------
class RcaClassFactory : public IClassFactory {
    volatile LONG m_ref { 1 };
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = this; AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        DllAddRef(); return InterlockedIncrement(&m_ref);
    }
    ULONG STDMETHODCALLTYPE Release() override {
        DllRelease();
        ULONG r = InterlockedDecrement(&m_ref);
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* pUnkOuter,
                                              REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;
        auto* filter = new RcaCaptureFilter();
        HRESULT hr = filter->QueryInterface(riid, ppv);
        filter->Release();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
        if (lock) DllAddRef(); else DllRelease();
        return S_OK;
    }
};

// -----------------------------------------------------------------------
// DllGetClassObject
// -----------------------------------------------------------------------
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    if (!ppv) return E_POINTER;
    if (rclsid != CLSID_RcaCaptureFilter) return CLASS_E_CLASSNOTAVAILABLE;
    auto* cf = new RcaClassFactory();
    HRESULT hr = cf->QueryInterface(riid, ppv);
    cf->Release();
    return hr;
}

// -----------------------------------------------------------------------
// Helper: write a registry string value
// -----------------------------------------------------------------------
static HRESULT RegSetSz(HKEY hRoot, const wchar_t* path,
                          const wchar_t* name, const wchar_t* value) {
    HKEY hKey = nullptr;
    LONG r = RegCreateKeyExW(hRoot, path, 0, nullptr, 0,
                              KEY_WRITE, nullptr, &hKey, nullptr);
    if (r != ERROR_SUCCESS) return HRESULT_FROM_WIN32(r);
    r = RegSetValueExW(hKey, name, 0, REG_SZ,
                        reinterpret_cast<const BYTE*>(value),
                        static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);
    return HRESULT_FROM_WIN32(r);
}

// -----------------------------------------------------------------------
// DllRegisterServer  – run once: regsvr32 RcaCapture.dll
// -----------------------------------------------------------------------
STDAPI DllRegisterServer() {
    wchar_t clsidStr[64];
    StringFromGUID2(CLSID_RcaCaptureFilter, clsidStr, 64);

    // 1. Register the COM class
    wchar_t clsPath[128];
    swprintf_s(clsPath, L"CLSID\\%s", clsidStr);
    RegSetSz(HKEY_CLASSES_ROOT, clsPath, nullptr, kFilterName);

    // 2. Register InprocServer32 pointing to this DLL
    wchar_t dllPath[MAX_PATH];
    GetModuleFileNameW(nullptr, dllPath, MAX_PATH);  // approximate; works for regsvr32

    wchar_t inprocPath[148];
    swprintf_s(inprocPath, L"CLSID\\%s\\InprocServer32", clsidStr);
    RegSetSz(HKEY_CLASSES_ROOT, inprocPath, nullptr,    dllPath);
    RegSetSz(HKEY_CLASSES_ROOT, inprocPath, L"ThreadingModel", L"Both");

    // 3. Register in Video Capture Sources category
    wchar_t catPath[256];
    swprintf_s(catPath, L"%s\\%s", kCategoryKey, clsidStr);
    RegSetSz(HKEY_CLASSES_ROOT, catPath, L"FriendlyName", kFilterName);
    RegSetSz(HKEY_CLASSES_ROOT, catPath, L"CLSID",        clsidStr);
    RegSetSz(HKEY_CLASSES_ROOT, catPath, L"FilterData",   L"");

    return S_OK;
}

// -----------------------------------------------------------------------
// DllUnregisterServer
// -----------------------------------------------------------------------
STDAPI DllUnregisterServer() {
    wchar_t clsidStr[64];
    StringFromGUID2(CLSID_RcaCaptureFilter, clsidStr, 64);

    wchar_t path[256];
    swprintf_s(path, L"CLSID\\%s\\InprocServer32", clsidStr);
    RegDeleteKeyW(HKEY_CLASSES_ROOT, path);

    swprintf_s(path, L"CLSID\\%s", clsidStr);
    RegDeleteKeyW(HKEY_CLASSES_ROOT, path);

    swprintf_s(path, L"%s\\%s", kCategoryKey, clsidStr);
    RegDeleteKeyW(HKEY_CLASSES_ROOT, path);

    return S_OK;
}
