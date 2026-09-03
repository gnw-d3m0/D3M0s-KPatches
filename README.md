# D3M0's KOTOR 1 KPatch Fixes

Small runtime fixes for Star Wars: Knights of the Old Republic designed for Kotor Patch Manager 0.6.3.

## Included patches

### DialogueLetterboxFix.kpatch
Prevents the subtitle display and dialogue letterboxing delay that can occur at high frame rates.

### PostCombatFix.kpatch
Fixes the brief post-combat movement freeze with no simulated 60 FPS delay.

### PostCombatFix60fps.kpatch
Fixes the same post-combat movement issue while preserving timing closer to the game's original 60 FPS behavior.

Only use one of the two post-combat patches at a time. DialogueLetterboxFix can be used alongside either post-combat patch.

## Supported executables

These patches target the KOTOR 1.03 executables supported by Kotor Patch Manager 0.6.3:

- GOG 1.03
- Steam 1.03
- Supported CD/no-CD 1.03 executable

## Installation

1. Install Kotor Patch Manager 0.6.3.
2. Place the desired `.kpatch` file in Kotor Patch Manager's patches folder.
3. Open Kotor Patch Manager and install the patch.

For the post-combat fix, install either `PostCombatFix.kpatch` or `PostCombatFix60fps.kpatch`, not both.
