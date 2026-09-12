# K2 High FPS Fix

By d3m0. Version 0.1.0.

Fixes high-FPS movement, dialogue, mouse sensitivity, and animation issues in KOTOR II's Aspyr build.

Includes post-combat movement, dialogue letterbox timing, mouse free-look sensitivity, UV scrolling and jitter, lightsaber trails, dangly meshes, and water/force-field timing. Preserves partial animation updates across FPS changes. Does not cap FPS or change the global game clock.

## Installation

Load `K2HighFPSFixes.kpatch` in Kotor Patch Manager and apply it to the unmodified Windows GOG Aspyr version of KOTOR II. Keep the original executable backup.

Only `kotor2_gog_aspyr` is registered. No legacy, Steam, or 3C-FD executable variants are registered in this version. No separate TSLRCM or Community Patch package is included.

## Status

Initial test build. The compiled x86 hooks passed isolated timing, variable-FPS, register/FPU-preservation, and DLL-relocation tests against wrapper layouts from Kotor Patch Manager 0.6.3 and 0.7.0. This is not an in-game or Windows installation test; gameplay and visual validation are still required.

## Source

`src` contains the manifest, K2 hooks, compiled DLL, C++ source, and reproducible build script. The current K1 all-in-one 1.0.0 at commit `422a367e` supplied the timing algorithms, not the obsolete individual patches.

Build with Python 3.11+, Clang, and lld-link:

```sh
python src/dll/src/build.py --exe /path/to/unmodified/swkotor2.exe
```

The optional `--exe` checks the supported SHA-256 and every original hook byte. It does not modify the executable.
