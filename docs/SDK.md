# OBSBOT SDK Analysis

The OBSBOT SDK (`sdk/`) is a closed-source library distributed as precompiled binaries and C++ headers. This document records what we know about it from inspecting the public headers and the unstripped binary.

## What's Distributed

- Three headers: `dev.hpp` (~4000 lines of declarations), `devs.hpp`, `comm.hpp`
- Compiled shared library: `libdev.so` (24MB, x86_64, ELF, debug symbols included, not stripped)
- One sample application: `sample_main.cpp`
- No license file, no copyright headers, no attribution notices

The library version reported in `comm.hpp` is 1.3.0. OBSBOT's download labeled "v2.1.0" contains byte-identical files (verified by SHA256).

## Internal Source Structure

Debug symbols in the unstripped binary reveal the original source tree:

```
libdev/
├── base64/          Base64 encode/decode
├── bsdiff/          Binary diff/patch (BSD 2-clause)
├── bzip2/           Compression (BSD-style license)
├── dev/
│   ├── dev.cpp, dev-info.cpp, dev-linux.cpp
│   ├── dev-mtp.cpp, dev-stream-linux.cpp, dev-upgrade.cpp
│   ├── dev-protocol-ai.cpp         # AI/tracking commands
│   ├── dev-protocol-camera.cpp     # Camera control commands
│   ├── dev-protocol-rm.cpp         # Remote management
│   ├── dev-protocol-sysmg.cpp      # System management
│   ├── dev-protocol-uvc-linux.cpp  # UVC extension unit protocol
│   ├── devs.cpp, devs-linux.cpp, devs-mdns.cpp
│   ├── av-capture-linux.cpp, bt-protocol-rm.cpp
│   └── uvc-protocol.cpp
├── ftp/             FTP client
├── kcp/             KCP reliable UDP transport
├── lzma/            Compression (public domain)
├── mdns/            mDNS/Zeroconf discovery
├── mtp/linux/       Full MTP/PTP/USB stack
├── package/         Firmware packaging and upgrade
├── protobuf/        Protobuf serialization (camera.pb.c, dev.pb.c)
└── util/            CRC, utilities
```

## What the SDK Actually Does

The library is a unified control interface for the entire OBSBOT product line — USB webcams, WiFi PTZ cameras, network converters. Most of its bulk is irrelevant for USB camera control:

| Component | Used for USB webcams? | Purpose |
|-----------|----------------------|---------|
| UVC XU protocol | **Yes** | Extension unit commands for AI tracking, camera settings |
| Device enumeration | **Yes** (but V4L2/udev does this too) | Find connected cameras |
| Protobuf serialization | **Yes** | Message encoding for XU commands |
| MTP stack | No | File transfer (backgrounds, firmware) |
| FTP client | No | Network camera file access |
| KCP/UDP | No | Reliable transport for WiFi cameras |
| mDNS | No | WiFi camera discovery |
| bsdiff/bzip2/lzma | No | Firmware delta updates |
| Bluetooth | No | BT device pairing |

For controlling a USB webcam, the SDK's job reduces to: write structured commands to UVC Extension Unit selectors and read status back. Everything else is overhead from the multi-product design.

## USB Work Modes

The binary references these camera USB modes, switchable via the SDK:

| Mode | Description |
|------|-------------|
| `UVC_UAC` | Normal webcam (video + audio) |
| `UVC_RNDIS` | Video + USB network interface |
| `RNDIS_ONLY` | Camera as pure network device |
| `MASS_STORAGE` | Camera as USB storage device |
| `MTP` | Media Transfer Protocol |
| `HOST` | Camera as USB host |
| `IDLE` | Idle state |

The RNDIS modes strongly suggest the camera runs an embedded Linux stack with USB gadget framework support.

## UVC Extension Unit (Direct Hardware Probe)

The OBSBOT Meet 2 exposes a vendor-specific UVC Extension Unit accessible without the SDK:

```
GUID: {9a1e7291-6843-4683-6d92-39bc7906ee49}
Unit ID: 2
Controls: 7 selectors, each 60 bytes, all GET+SET capable
```

Probed via standard `UVCIOC_CTRL_QUERY` ioctl:

| Selector | Idle State | Notes |
|----------|-----------|-------|
| 1 | zeros | Command channel |
| 2 | zeros | Command channel |
| 3 | zeros | Command channel |
| 4 | `01 00...` | State flag |
| 5 | zeros | Command channel |
| 6 | `24 00 00 02 02 00 01 01...` | Camera status (richest data) |
| 7 | `06 00...` | Product type or mode |

Selector 6 returns a packed struct with live camera state — AI mode, tracking status, feature enables, image settings. This is readable from any language that can do a V4L2 ioctl, no SDK required.

All standard camera controls (brightness, contrast, zoom, pan, tilt, exposure, white balance, focus) are exposed as normal V4L2 controls by the kernel `uvcvideo` driver and work without the SDK.

## Bundled Open Source Components

The binary contains code from several open source projects. No attribution is provided in the SDK distribution.

| Library | License | Attribution required? |
|---------|---------|----------------------|
| bsdiff | BSD 2-clause | Yes |
| bzip2 | BSD-style | Yes |
| LZMA SDK | Public domain | No |
| nanopb (protobuf) | zlib license | Yes |
| Boost | BSL-1.0 | Yes |

## Supported Devices (from SDK headers)

The `ObsbotProductType` enum in `dev.hpp`:

| Enum Value | Device |
|-----------|--------|
| `ObsbotProdTiny` | Tiny (original) |
| `ObsbotProdTiny4k` | Tiny 4K |
| `ObsbotProdTiny2` | Tiny 2 |
| `ObsbotProdTiny2Lite` | Tiny 2 Lite |
| `ObsbotProdTinySE` | Tiny SE |
| `ObsbotProdMeet` | Meet (original) |
| `ObsbotProdMeet4k` | Meet 4K |
| `ObsbotProdMeet2` | Meet 2 |
| `ObsbotProdMeetSE` | Meet SE |
| `ObsbotProdMe` | Me |
| `ObsbotProdTailAir` | Tail Air |
| `ObsbotProdTail2` | Tail 2 |
| `ObsbotProdTail2S` | Tail 2S |
| `ObsbotProdHDMIBox` | HDMI Box |
| `ObsbotProdNDIBox` | NDI Box |
