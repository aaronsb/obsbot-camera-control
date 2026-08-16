---
commands: build\.sh|makepkg|make package
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
3. Check the recipe: `make package` — clean chroot build plus namcap, and it
   fails on a namcap error rather than printing one
4. Tag: `git tag -a vX.Y.Z -m "Release X.Y.Z"`
5. Push the tag AND the commits
6. Cut the GitHub release: `gh release create vX.Y.Z --generate-notes`

Do NOT touch `pkgver`, `pkgrel` or `sha256sums`, and do not commit a `.SRCINFO`.
arch-repo overwrites all four.

## Publishing

Nothing to do. aaronsb/arch-repo watches this repository, reads `./PKGBUILD`
from the default branch, takes the version from the newest published release,
builds it in a clean container, lints it, signs it, and pushes to the AUR and
the `[aaronsb]` pacman repository.

- Never push to the AUR from here. Two writers to one ref is how a PKGBUILD and
  its `.SRCINFO` drift apart.
- The tag must exist and be pushed before the release is cut — the recipe
  sources from `#tag=v${pkgver}`.
- A packaging fix needs no release: change the recipe on the default branch and
  arch-repo ships it as a `pkgrel` bump.

## Version Scheme

Semantic versioning: MAJOR.MINOR.PATCH
- MAJOR: Breaking changes (SDK update, dropped camera support)
- MINOR: New features (new controls, new camera support)
- PATCH: Bug fixes, build fixes
- `pkgrel`: packaging-only changes (reset to 1 on pkgver bump)
