cmake_minimum_required(VERSION 3.20)
project(RcaCaptureDriver VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows 10 1903+ / Windows 11 (required for WinUSB isoch API)
add_compile_definitions(
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
    NTDDI_VERSION=0x0A000007   # 1903
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

# -----------------------------------------------------------------------
# Common include path
# -----------------------------------------------------------------------
set(COMMON_INCLUDE ${CMAKE_SOURCE_DIR}/include)

# -----------------------------------------------------------------------
# RcaCapture.dll  –  DirectShow source filter
# -----------------------------------------------------------------------
set(DS_SOURCES
    src/usb_device.cpp
    src/stk1160.cpp
    src/emp2861.cpp
    src/capture_device.cpp
    src/directshow/ds_capture_filter.cpp
    src/directshow/ds_output_pin.cpp
    src/directshow/dllmain.cpp
)

add_library(RcaCapture SHARED ${DS_SOURCES})

target_include_directories(RcaCapture PRIVATE
    ${COMMON_INCLUDE}
    src/directshow
)

target_link_libraries(RcaCapture PRIVATE
    winusb
    setupapi
    strmiids    # DirectShow GUIDs (MEDIATYPE_Video etc.)
    uuid        # StringFromGUID2
    ole32
    oleaut32
)

# Export the four DLL entry points required by regsvr32
set_target_properties(RcaCapture PROPERTIES
    OUTPUT_NAME "RcaCapture"
    LINK_FLAGS  "/DEF:\"${CMAKE_SOURCE_DIR}/RcaCapture.def\""
)

# -----------------------------------------------------------------------
# Module-definition file (so regsvr32 finds the exports)
# -----------------------------------------------------------------------
file(WRITE ${CMAKE_SOURCE_DIR}/RcaCapture.def
"LIBRARY RcaCapture
EXPORTS
    DllGetClassObject   PRIVATE
    DllCanUnloadNow     PRIVATE
    DllRegisterServer   PRIVATE
    DllUnregisterServer PRIVATE
")

# -----------------------------------------------------------------------
# Installation helper (copies DLL + INF to a staging directory)
# -----------------------------------------------------------------------
install(TARGETS RcaCapture RUNTIME DESTINATION bin)
install(FILES driver/rca_capture.inf DESTINATION driver)
install(FILES tools/identify_device.ps1
              tools/install_driver.ps1
        DESTINATION tools)

# -----------------------------------------------------------------------
# Build notes printed at configure time
# -----------------------------------------------------------------------
message(STATUS "")
message(STATUS "=== RCA Capture Driver ===")
message(STATUS "Build:   cmake --build . --config Release")
message(STATUS "Install: cmake --install . --prefix <staging>")
message(STATUS "")
message(STATUS "On the target Windows 11 machine:")
message(STATUS "  1. bcdedit /set testsigning on   (if unsigned)")
message(STATUS "  2. .\\tools\\install_driver.ps1")
message(STATUS "  3. regsvr32 RcaCapture.dll")
message(STATUS "  4. Open OBS > Sources > Video Capture Device")
message(STATUS "     Select 'RCA Capture (STK1160/EMP2861)'")
message(STATUS "")
