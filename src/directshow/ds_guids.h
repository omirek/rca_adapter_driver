#pragma once
#include <guiddef.h>

// {7D7D4E2A-3F5B-4A8C-B9E1-6C2D5F8A0B3E}
// Filter CLSID – registered so OBS "Video Capture Device" lists it.
DEFINE_GUID(CLSID_RcaCaptureFilter,
    0x7d7d4e2a, 0x3f5b, 0x4a8c,
    0xb9, 0xe1, 0x6c, 0x2d, 0x5f, 0x8a, 0x0b, 0x3e);

// {2A8B5C1D-4E6F-7890-ABCD-EF1234567891}
// Output pin GUID (internal, not registered separately)
DEFINE_GUID(GUID_RcaOutputPin,
    0x2a8b5c1d, 0x4e6f, 0x7890,
    0xab, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78, 0x91);
