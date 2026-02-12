# Contributing

## Building

```bash
./build.sh build --confirm
```

Binary ends up in `./bin/obsbot-gui`. Run it directly to test.

For a clean rebuild:
```bash
./build.sh clean --confirm
./build.sh build --confirm
```

## Dependencies

**Build**: cmake, make, gcc/g++, qt6-base, qt6-multimedia, pkg-config

On Arch: `pacman -S cmake qt6-base qt6-multimedia`

## Making Changes

1. Fork and branch from `main`
2. Make your changes in `src/`
3. Build and test with your camera
4. Submit a PR

### Code Style

- Follow what's already there — this isn't the project for style debates
- Use `blockSignals()` when setting widget values programmatically (avoids feedback loops)
- Don't add custom widget styling — respect the user's Qt/KDE theme

### What to Include in Your PR

- What you changed and why
- Which camera model you tested with
- If it's a UI change, a screenshot helps

## Project Structure

```
src/
├── gui/           # Qt GUI — most changes happen here
│   ├── MainWindow.cpp/h
│   ├── TrackingControlWidget.cpp/h    # Auto-framing + PTZ
│   ├── PTZControlWidget.cpp/h         # Preset management
│   ├── CameraSettingsWidget.cpp/h     # Image settings
│   └── CameraPreviewWidget.cpp/h      # Live preview
├── cli/           # CLI tool
├── common/        # Shared code
└── camera/        # Camera controller
```

## Camera-Specific Notes

- `CameraController::isTiny2Family()` determines which advanced features are shown
- Meet 2 gets simpler tracking UI (on/off only)
- Tiny 2 family gets full AI mode selector
- If your change only applies to certain camera models, say so in the PR

## SDK

The OBSBOT SDK is a closed-source library — we get headers and a compiled `.so`, no source. If something doesn't work at the SDK level, we work around it. Check `sdk/include/dev/dev.hpp` for the API surface.
