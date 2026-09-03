# D3M0's KOTOR 1 KPatch Fixes

Runtime fixes for Star Wars: Knights of the Old Republic designed for Kotor Patch Manager 0.6.3.

## Patches

### DialogueLetterboxFix
Corrects high-FPS timing in the dialogue letterbox animation, preventing delayed black bars/subtitle presentation and the corresponding initial dialogue skip delay.

### PostCombatFix
Fixes the brief post-combat movement freeze with no simulated 60 FPS delay.

### PostCombatFix60fps
Fixes the same post-combat movement issue while preserving timing closer to the game's original 60 FPS behavior.

Only use one of the two post-combat patches at a time. `DialogueLetterboxFix` can be used alongside either post-combat patch.

## Supported executables

These patches target the KOTOR 1.03 executables supported by Kotor Patch Manager 0.6.3:

- GOG 1.03
- Steam 1.03
- Supported CD/no-CD 1.03 executable

## Installation

1. Download the desired `.kpatch` file from Releases.
2. Install Kotor Patch Manager 0.6.3.
3. Place the `.kpatch` file in Kotor Patch Manager's patches folder.
4. Open Kotor Patch Manager and install the patch.

For the post-combat fix, install either `PostCombatFix.kpatch` or `PostCombatFix60fps.kpatch`, not both.
