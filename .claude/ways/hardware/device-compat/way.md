---
keywords: device|camera model|tiny 2|tiny 4k|meet 2|meet se|tiny 2 lite|tiny se|tail|compatib|lsusb|UVC|usb
---
# Device Compatibility

## Known Camera Support (from user testing)

| Camera | Status | Notes |
|--------|--------|-------|
| OBSBOT Tiny 2 | Full | Primary development target |
| OBSBOT Meet 2 | Good | Simpler tracking (on/off only, no AI modes) |
| OBSBOT Tiny 2 Lite | Partial | Detection works, preview may need SDK path fix (PR #24) |
| OBSBOT Tiny SE | Unknown | SDK supports it, `isTiny2Family()` includes it |
| OBSBOT Tiny 4K | Unknown | Reported in #13, needs testing |
| OBSBOT Meet SE | Partial | Basic compatibility confirmed in #9 |

## SDK-Listed Devices (not all tested)

The SDK (`sdk/include/dev/dev.hpp`, `ObsbotProductType` enum) knows about:
Tiny, Tiny 4K, Tiny 2, Tiny 2 Lite, Tiny SE, Meet, Meet 4K, Meet 2, Meet SE,
Tail Air, Tail 2, Tail 2S, Me, HDMI Box, NDI Box.

Most of these are untested with this application. SDK support != working GUI support.

## Camera Model Detection

- `isTiny2Family()` checks: Tiny2, Tiny2Lite, TinySE (CameraController.cpp)
- `m_tiny2Capabilities` flag gates advanced features (AI modes, HDR, etc.)
- Meet 2 gets simplified tracking UI (toggle only)
- Tiny 2 family gets full AI mode selector (Group, Human, Hand, Whiteboard, Desk, None)
- Detection happens in `CameraController` via SDK `Devices::get().getDevList()` callback
- Video device path: SDK Device has `videoDevPath()`, but GUI uses Qt `QMediaDevices::videoInputs()` for discovery

## Handling Compatibility Reports

When someone reports a new device:
1. Ask for: camera model, `lsusb` output, kernel version, what works/doesn't
2. Check if SDK lists the device in `sdk/include/dev/dev.hpp` (`ObsbotProductType` enum)
3. Determine if it's a detection issue (device not found) vs. feature issue (found but controls don't work)
4. Label issue appropriately: `bug` if previously working, `enhancement` if new device support

## Diagnostics

```bash
lsusb | grep -i obsbot          # Device detection
v4l2-ctl --list-devices          # Video device enumeration
ls -la /dev/video*               # Available video devices
dmesg | grep -i obsbot           # Kernel messages
```

## SDK

The OBSBOT SDK (`sdk/`) is a closed-source library distributed as precompiled binaries (.so/.dylib/.dll) plus C++ headers. No implementation source is provided — just a ~4000-line header of declarations and one sample app. Linux support is labeled "(beta)" by OBSBOT. Current project uses v1.0.2; latest available is v2.1.0.

What we can do: read the headers to understand the API surface, check `ObsbotProductType` for device support, use callbacks and methods as declared. What we can't do: fix SDK bugs, add device support, or understand why something doesn't work internally. We work around SDK limitations, not through them.
