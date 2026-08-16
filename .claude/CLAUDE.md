# OBSBOT Control - Project Context for Claude

This document provides context and workflows for working on the OBSBOT Control project.

## Project Overview

OBSBOT Control is a Qt6-based native Linux application for controlling OBSBOT cameras (Meet 2, Tiny 2, Tiny 2 Lite, Tiny 4K). It provides PTZ control, auto-framing, image settings, presets, live preview, and system tray integration.

**Repository**: https://github.com/aaronsb/obsbot-camera-control
**AUR Package**: https://aur.archlinux.org/packages/obsbot-camera-control
**Maintainer**: Aaron Bockelie <aaronsb@gmail.com>

## Project Structure

```
obsbot-camera-control/
├── src/
│   ├── gui/              # Qt6 GUI components
│   │   ├── MainWindow.cpp/h
│   │   ├── TrackingControlWidget.cpp/h    # Auto-framing + Manual PTZ
│   │   ├── PTZControlWidget.cpp/h         # Preset management
│   │   ├── CameraSettingsWidget.cpp/h     # Image quality settings
│   │   └── CameraPreviewWidget.cpp/h      # Live preview
│   ├── cli/              # CLI tool
│   ├── common/           # Shared code
│   └── camera/           # Camera controller
├── PKGBUILD              # Arch Linux package definition
├── .SRCINFO              # AUR package metadata (auto-generated)
├── build.sh              # Build automation script
└── local-install.sh      # Developer installation script
```

## Key Architecture Decisions

### UI Organization (Current)
- **Tracking Tab**: Auto-framing controls + Manual PTZ controls (mutually exclusive)
- **Presets Tab**: Camera position/zoom presets + Image quality presets
- **Image Tab**: All image quality settings (HDR, FOV, brightness, contrast, etc.)

**Important**: PTZ controls are locked out (disabled) when auto-framing is enabled. This is enforced by `TrackingControlWidget::updatePTZControlsState()`.

### Preset System
Presets are "quick settings" that restore complete camera state:

**PTZ Presets** (3 slots):
- Pan/Tilt position
- Zoom level

**Image Quality Presets** (3 slots):
- HDR, FOV, Face AE/Focus
- Brightness, Contrast, Saturation (auto + manual values)
- White Balance

### Camera Model Detection
The code detects camera models and shows/hides controls accordingly:
- `m_tiny2Capabilities` flag determines if Tiny 2 family advanced features are shown
- Meet 2 has simpler tracking (on/off)
- Tiny 2 family has AI modes (Group, Human, Hand, Whiteboard, Desk, None)

## Development Workflows

### Building Locally

```bash
# Standard build
./build.sh build --confirm

# Build and install to ~/.local/bin
./build.sh install --confirm

# Clean rebuild
./build.sh clean --confirm
./build.sh build --confirm
```

### Making Changes

1. **Code changes** in `src/`
2. **Build and test**: `./build.sh build --confirm`
3. **Test binary**: `./bin/obsbot-gui` or `~/.local/bin/obsbot-gui`
4. **Commit changes**: Standard git workflow

### UI Changes Guidelines

- **Theme Respect**: Don't add custom button/combo/input styling - respect Qt/KDE themes
- **Layout**: Use compact layouts, minimal padding
- **Mutual Exclusion**: Keep PTZ lockout when auto-framing enabled
- **Signal Blocking**: Use `blockSignals()` when setting values programmatically to avoid feedback loops

## Release & Publishing Workflow

### Version Numbering
- Follow semantic versioning: MAJOR.MINOR.PATCH
- Do NOT edit `pkgver`, `pkgrel` or `sha256sums`. aaronsb/arch-repo overwrites
  all three: `pkgver` from the newest published release, `pkgrel` with its own
  count of how many times it has packaged that release. They are placeholders.

### Publishing a New Version

arch-repo publishes this package. Tag and cut a GitHub release; it does the rest.

```bash
# 1. Check the recipe still builds and lints before you tag
make package        # clean chroot + namcap, fails on a namcap error

# 2. Tag and release
git tag -a vX.Y.Z -m "vX.Y.Z" && git push origin vX.Y.Z
gh release create vX.Y.Z --generate-notes
```

arch-repo reads ./PKGBUILD from the default branch, takes the version from the
release, builds in a clean container, lints, signs, and pushes to the AUR and
the [aaronsb] pacman repository.

A packaging fix needs no release at all: change the recipe on the default
branch and arch-repo ships it as a pkgrel bump.

**Option 2: Manual process**

```bash
# 1. Update PKGBUILD version
vim PKGBUILD

# 2. Regenerate .SRCINFO
makepkg --printsrcinfo > .SRCINFO

# 3. Commit to main repo
git add PKGBUILD .SRCINFO
git commit -m "Bump version to X.Y.Z"
git tag -a vX.Y.Z -m "Release version X.Y.Z"
git push origin main
git push origin vX.Y.Z

# 4. Update AUR
cd /tmp
git clone ssh://aur@aur.archlinux.org/obsbot-camera-control.git
cd obsbot-camera-control
cp ~/Projects/hardware/obsbot/{PKGBUILD,.SRCINFO} .
git add PKGBUILD .SRCINFO
git commit -m "Update to version X.Y.Z"
git push origin master  # NOTE: AUR uses 'master', not 'main'
```

### Pre-Release Checklist

Before publishing a new version:

- [ ] All changes committed and pushed to main repository
- [ ] Version number updated in PKGBUILD
- [ ] Build tested locally: `./build.sh build --confirm`
- [ ] Binary tested: `./bin/obsbot-gui`
- [ ] Git tag created matching PKGBUILD version
- [ ] Tag pushed to GitHub (required for AUR PKGBUILD source)
- [ ] .SRCINFO regenerated from PKGBUILD
- [ ] Commit message describes changes

### Git Tag Requirements

**Critical**: The git tag version MUST match the `pkgver` in PKGBUILD because the PKGBUILD sources from:

```bash
source=("git+https://github.com/aaronsb/obsbot-camera-control.git#tag=v${pkgver}")
```

If tag doesn't exist or isn't pushed, users cannot install from AUR.

### AUR Repository Details

- **AUR Git URL**: `ssh://aur@aur.archlinux.org/obsbot-camera-control.git`
- **Branch**: `master` (AUR requires this, not `main`)
- **Required Files**: `PKGBUILD` and `.SRCINFO` only
- **Authentication**: SSH key must be configured on AUR account

### After Publishing

1. Verify package appears at: https://aur.archlinux.org/packages/obsbot-camera-control
2. Test installation: `yay -S obsbot-camera-control` or `paru -S obsbot-camera-control`
3. Monitor AUR comments for issues

## Common Tasks

### Adding New Camera Controls

1. Add to `CameraController` API if needed
2. Add UI controls to appropriate widget:
   - Tracking controls → `TrackingControlWidget`
   - Image settings → `CameraSettingsWidget`
   - Presets → `PTZControlWidget`
3. Add getter/setter to widget header
4. Connect signals/slots
5. Add to preset structs if it should be saved in presets
6. Update `MainWindow` connections if needed

### Adding New Presets Fields

To add fields to PTZ presets:
1. Update `PTZControlWidget::PresetState` struct
2. Update `onStorePreset()` to capture the value
3. Update `onRecallPreset()` to restore the value
4. Update `updatePresetLabel()` to display it

To add fields to Image presets:
1. Update `PTZControlWidget::ImagePresetState` struct
2. Update `onStoreImagePreset()` to capture from `m_settingsWidget`
3. Update `onRecallImagePreset()` to restore to `m_settingsWidget`
4. Ensure `CameraSettingsWidget` has getter/setter methods

### Debugging

- **Camera not detected**: Check `lsusb` output, verify UVC support
- **Preview issues**: Check `v4l2-ctl --list-devices`
- **Build errors**: Check Qt6 version, verify dependencies
- **Settings not saving**: Check `~/.config/obsbot-control/camera-config.json`

## Integration Tests

Manual test checklist before release:

- [ ] Camera detection works
- [ ] PTZ controls respond correctly
- [ ] Auto-framing locks out PTZ controls
- [ ] Presets save and recall correctly (both PTZ and Image)
- [ ] Preview opens and closes cleanly
- [ ] System tray minimize/restore works
- [ ] Settings persist across restarts
- [ ] Virtual camera support (if v4l2loopback installed)

## Dependencies

**Build**:
- cmake
- make
- gcc/g++
- qt6-base
- qt6-multimedia
- pkg-config

**Runtime**:
- qt6-base
- qt6-multimedia
- glibc
- gcc-libs

**Optional**:
- v4l2loopback-dkms (virtual camera)
- lsof (camera usage detection)

## Resources

- **Documentation**: `docs/` directory
- **Build Instructions**: `docs/BUILD.md`
- **GitHub Issues**: https://github.com/aaronsb/obsbot-camera-control/issues
- **AUR Page**: https://aur.archlinux.org/packages/obsbot-camera-control

## Notes for Claude

When working on this project:

1. **Respect the architecture**: PTZ controls live in TrackingControlWidget, not PTZControlWidget
2. **Check camera capabilities**: Code has branching for Meet 2 vs Tiny 2 family
3. **Build before committing**: Always run `./build.sh build --confirm`
4. **Version updates**: tag and cut a GitHub release; arch-repo publishes
5. **Tag management**: tags must exist and be pushed before a release is cut
6. **AUR**: not touched from here — arch-repo is the only writer
