---
commands: publish-aur|build\.sh|makepkg
files: PKGBUILD|\.SRCINFO$
keywords: release|version bump|publish|aur
---
# Build & Release

## Build

```bash
./build.sh build --confirm    # Standard build
./build.sh install --confirm  # Build + install to ~/.local/bin
./build.sh clean --confirm    # Clean rebuild
```

Binary lands in `./bin/obsbot-gui` (build) or `~/.local/bin/obsbot-gui` (install).

## Release Checklist

Before bumping version:
1. All changes committed, build tested: `./build.sh build --confirm`
2. Binary tested: `./bin/obsbot-gui`
3. Update `pkgver` in PKGBUILD (reset `pkgrel` to 1)
4. Regenerate: `makepkg --printsrcinfo > .SRCINFO`
5. Commit PKGBUILD + .SRCINFO
6. Tag: `git tag -a vX.Y.Z -m "Release X.Y.Z"`
7. Push tag AND commits — tag must exist on GitHub before AUR publish

## AUR Publishing

Preferred: `./publish-aur.sh` (handles tag verification, .SRCINFO regen, AUR push)

Critical details:
- AUR uses `master` branch, main repo uses `main`
- AUR git: `ssh://aur@aur.archlinux.org/obsbot-camera-control.git`
- Only PKGBUILD and .SRCINFO go to AUR
- Tag version MUST match `pkgver` — PKGBUILD sources from `#tag=v${pkgver}`
- If tag doesn't exist or isn't pushed, AUR users can't install

## Version Scheme

Semantic versioning: MAJOR.MINOR.PATCH
- MAJOR: Breaking changes (SDK update, dropped camera support)
- MINOR: New features (new controls, new camera support)
- PATCH: Bug fixes, build fixes
- `pkgrel`: packaging-only changes (reset to 1 on pkgver bump)
