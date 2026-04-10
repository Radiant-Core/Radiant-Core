# Radiant Core 2.3.0 Release Notes

**Release Date**: April 9, 2026  
**Git Tag**: v2.3.0

---

## Overview

Radiant Core 2.3.0 is a **maintenance release** that updates all version strings across the codebase and build infrastructure to 2.3.0. This release contains no consensus changes, no hard fork, and no policy changes. It is a non-mandatory update primarily for versioning consistency across the build system, documentation, and release artifacts.

---

## Version Updates

### Core Version

| Component | Previous | New |
|-----------|----------|-----|
| CMake Project | 2.2.0 | 2.3.0 |
| GUI macOS App | 2.2.0 | 2.3.0 |
| macOS App Build | 2.1.2 | 2.3.0 |

### Build Scripts

All build scripts have been updated with new default version strings:

- `scripts/build-gui-release.sh`: Default version 2.3.0
- `scripts/build-docker-release.sh`: LABEL version 2.3.0 (builder + runtime images)
- `scripts/build-all-releases.sh`: Default version 2.3.0
- `scripts/build-macos-app.sh`: Default version 2.3.0

### GitHub Release URLs

- `gui/radiant_node_web.py`: `GITHUB_RELEASE_URL` updated to v2.3.0
- `macos-app-build/radiant_node_web.py`: `GITHUB_RELEASE_URL` updated to v2.3.0

### Documentation

- `README.md`: Header updated to "Radiant Core 2.3.0"
- `docker/README.md`: Docker build examples use v2.3.0
- `doc/docker-guide.md`: All version references updated to v2.3.0
- `doc/upgrades.md`: Added section 14 documenting v2.3.0 changes
- `releases/README.txt`: Latest release updated to v2.3.0

---

## Files Modified

```
CMakeLists.txt
gui/setup.py
macos-app-build/setup.py
gui/radiant_node_web.py
macos-app-build/radiant_node_web.py
scripts/build-gui-release.sh
scripts/build-docker-release.sh
scripts/build-all-releases.sh
scripts/build-macos-app.sh
README.md
docker/README.md
doc/docker-guide.md
doc/upgrades.md
releases/README.txt
```

---

## Upgrade Instructions

### For All Users

**This release is optional.** There are no consensus changes, no hard fork, and no policy changes. You may upgrade at your convenience for versioning consistency.

#### If Upgrading:
1. Download Radiant Core 2.3.0 for your platform
2. Replace your existing binaries
3. Restart your node
4. Verify version: `radiantd --version` should show `v2.3.0`

#### If Staying on Current Version:
- No action required
- Previous versions remain fully compatible with the network

---

## Compatibility

| Component | Status |
|-----------|--------|
| Consensus | Unchanged — fully compatible with v2.1.x and v2.2.x |
| Network Protocol | Unchanged |
| RPC API | Unchanged |
| Wallet Format | Unchanged |
| Blockchain Data | Unchanged |

---

## Credits

Version bump and release preparation by the Radiant Core development team.

---

## Previous Releases

- [v2.2.0](release-notes-2.2.0.md) - Fee Cap Adjustment (0.5 RXD/kB max)
- [v2.1.0](release-notes-2.1.0.md) - V2 Hard Fork (OP_BLAKE3, OP_K12, OP_LSHIFT, OP_RSHIFT, OP_2MUL, OP_2DIV)
- [v2.0.0](release-notes-2.0.0.md) - Phoenix Release (ASERT DAA, Fee Policy)

