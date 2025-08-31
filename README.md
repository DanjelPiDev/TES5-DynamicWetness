# Dynamic Speed Controller

> [![Download Latest Release](https://img.shields.io/github/v/release/DanjelPiDev/TES5-DynamicSpeedController)](https://github.com/DanjelPiDev/TES5-DynamicSpeedController/releases/latest)

A lightweight SKSE plugin that adjusts your **SpeedMult** and **attack speed** based on your current state (drawn, sneak, jog, sprint, combat). It includes optional NPC support, weapon-weight and actor-size based attack scaling, a diagonal movement fix, sprint animation sync, and smooth acceleration options.  
No ESP and no scripts in your save; it is just a DLL with a JSON config. Optional in-game configuration via an SKSE menu.

> Tested on current AE on my setup. For the cleanest experience, save in the pause menu before changing settings.

---

## Table of Contents

- [Features](#features)
- [Requirements](#requirements)
- [Installation](#installation)
- [Updating](#updating)
- [In-Game Configuration](#in-game-configuration)
- [Screenshots](#screenshots)
- [JSON Configuration](#json-configuration)
  - [Key Explanations](#key-explanations)
- [Tips](#tips)
- [Troubleshooting](#troubleshooting)
- [Uninstall](#uninstall)
- [Build From Source](#build-from-source)
- [Roadmap](#roadmap)

---

## Features

- **Per-state movement tuning** for Default, Jogging, Drawn, Sneak, and Sprint. You can decide that combat keeps full speed.
- **Optional NPC support** for movement, diagonal fix, smoothing, and attack scaling.
- **Attack speed scaling** by weapon weight and actor size with base multiplier, pivot, slope, and clamps; optionally only when drawn.
- **Diagonal speed fix** that respects your minimum SpeedMult and can apply to NPCs.
- **Smooth acceleration** with Expo, Rate Limit, or Expo-then-Rate, plus a bypass on major state changes.
- **Sprint animation sync** that matches animation rate to your current SpeedMult with its own smoothing and clamps.
- **Jogging toggle** via hotkey or user event name (defaults: Toggle = `Shout`, Sprint latch = `Sprint`).
- **Location rules** for specific locations or location types, with replace or add behavior and a choice to affect default only or all states.
- **Safety floor** so SpeedMult never drops below your minimum.
- **Beast form awareness** to ignore modifiers in Werewolf or Vampire Lord forms if desired.
- **Slope / Terrain effects** that adjust movement speed dynamically based on uphill/downhill angle, including stairs, ramps, and uneven ground. Separate multipliers for uphill and downhill, min/max clamps, and smooth blending. Works in real time for both keyboard and controller, and can optionally affect NPCs.
- **Lightweight and script-free**; no Papyrus, no save bloat.
- **Weather presets** to adjust speed based on current or mod-added weather, with replace/add modes and option to ignore interiors.

---

## Requirements

- **SKSE** for Skyrim SE/AE
- **Optional:** SKSE Menu Framework (for the in-game menu pages: General, Speed, Attack, Location Rules)

---

## Installation

1. Drop the DLL and `SpeedController.json` into `Data\SKSE\Plugins\`.
2. Start the game; settings are auto-loaded.

---

## Updating

1. Back up your current `SpeedController.json` if you want to keep your values.
2. Replace the DLL and `SpeedController.json` in `Data\SKSE\Plugins\`.
3. Start the game; settings auto-load. The plugin restores a safe baseline after load.

---

## In-Game Configuration

If the SKSE menu is present, open the Mod Control Panel and select **Dynamic Speed Controller**. Pages:

**General**
- Toggle NPC scaling, beast-form ignore, diagonal fix (player and NPCs).
- Event debounce settings.
- Smoothing enable, mode, half-life, max change per second, and bypass on major state changes.
- Save settings to JSON.

**Speed**
- Reductions for Default, Jogging, Drawn, Sneak, and an extra increase for Sprint.
- Option for sprint bonus to also apply during combat.
- Minimum final SpeedMult clamp.
- Sprint animation sync to movement speed with dedicated smoothing and clamps.
- Bind a toggle user event or a scancode; set the sprint event name.

**Attack**
- Enable attack speed scaling and optionally restrict it to when weapons are drawn.
- Base multiplier, weight pivot and slope.
- Optional actor scale with own slope.
- Min and max clamps. Updates on equip.

**Location Rules**
- Choose whether rules affect Default only or all states.
- Replace or Add behavior.
- Add specific locations by `Plugin|0xFormID`, or press **Use Current Location**.
- Add location types by keyword (e.g., `LocTypeCity`).
- Inline edit, remove entries, and save the list.

**Weather Presets**
- Adjust movement speed based on active weather (supports modded weather).
- Replace or Add behavior, or ignore completely.
- Option to ignore interiors (default: enabled).
- Add specific weathers by `Plugin|0xFormID`, or press **Use Current Weather**.
- Inline edit, highlight current weather, remove entries, and save the list.

---

## Screenshots

<div align="center">
	<img src="images/20250829221219_1.jpg" alt="General Settings" width="300">
	<img src="images/20250829221221_1.jpg" alt="General Settings" width="300">
	<img src="images/20250829221225_1.jpg" alt="General Settings" width="300">
	<img src="images/20250829221228_1.jpg" alt="Speed Settings" width="300">
	<img src="images/20250829221232_1.jpg" alt="Speed Settings" width="300">
	<img src="images/20250829221233_1.jpg" alt="Speed Settings - Add Location" width="300">
	<img src="images/20250829221235_1.jpg" alt="Speed Settings" width="300">
	<img src="images/20250829221245_1.jpg" alt="Attack Rules" width="300">
	<img src="images/20250829221247_1.jpg" alt="Attack Rules" width="300">
	<img src="images/20250829221251_1.jpg" alt="Attack Rules" width="300">
	<img src="images/20250829221256_1.jpg" alt="Location Rules" width="300">
	<img src="images/20250829221301_1.jpg" alt="Weather Rules" width="300">
	<img src="images/20250829221304_1.jpg" alt="Weather Rules" width="300">
</div>

---

## JSON Configuration

Everything lives in `Data/SKSE/Plugins/SpeedController.json`. The in-game menu reads and writes the same file.

This is an example configuration with explanations below.
```json
{
    "kArmorAffectsAttackSpeed": false,
    "kArmorAffectsMovement": false,
    "kArmorMoveMax": 0.0,
    "kArmorMoveMin": -60.0,
    "kArmorWeightPivot": 20.0,
    "kArmorWeightSlopeAtk": -0.009999999776482582,
    "kArmorWeightSlopeSM": -1.0,
    "kAttackBase": 0.800000011920929,
    "kAttackOnlyWhenDrawn": true,
    "kAttackSpeedEnabled": true,
    "kEnableDiagonalSpeedFix": false,
    "kEnableDiagonalSpeedFixForNPCs": false,
    "kEnableSpeedScalingForNPCs": false,
    "kEventDebounceMs": 10,
    "kIgnoreBeastForms": true,
    "kIncreaseSprinting": 45.0,
    "kLocationAffects": "default",
    "kLocationMode": "replace",
    "kMaxAttackMult": 1.7999999523162842,
    "kMinAttackMult": 0.6000000238418579,
    "kMinFinalSpeedMult": 10.0,
    "kNoReductionInCombat": true,
    "kNpcPercentOfPlayer": 50.0,
    "kNpcRadius": 2048,
    "kOnlySlowDown": true,
    "kReduceDrawn": 15.0,
    "kReduceInLocationSpecific": [
        {
            "form": "cceejsse001-hstead.esm|0x000F1E",
            "value": 60.0
        }
    ],
    "kReduceInLocationType": [],
    "kReduceJoggingOutOfCombat": 10.0,
    "kReduceOutOfCombat": 51.0,
    "kReduceSneak": 53.0,
    "kScaleSlope": 0.25,
    "kSlopeAffectsNPCs": false,
    "kSlopeClampEnabled": true,
    "kSlopeDownhillPerDeg": 0.4000000059604645,
    "kSlopeEnabled": true,
    "kSlopeLookbackUnits": 20.0,
    "kSlopeMaxAbs": 25.0,
    "kSlopeMaxFinal": 120.0,
    "kSlopeMaxHistorySec": 1.600000023841858,
    "kSlopeMedianN": 3,
    "kSlopeMethod": 1,
    "kSlopeMinFinal": 25.0,
    "kSlopeMinXYPerFrame": 0.25,
    "kSlopeTau": 0.1599999964237213,
    "kSlopeUphillPerDeg": 0.6000000238418579,
    "kSmoothingAffectsNPCs": false,
    "kSmoothingBypassOnStateChange": false,
    "kSmoothingEnabled": true,
    "kSmoothingHalfLifeMs": 300.0,
    "kSmoothingMaxChangePerSecond": 15.0,
    "kSmoothingMode": "ExpoThenRate",
    "kSprintAffectsCombat": false,
    "kSprintAnimMax": 0.5,
    "kSprintAnimMin": 0.25,
    "kSprintAnimMode": 2,
    "kSprintAnimOwnSmoothing": true,
    "kSprintAnimRatePerSec": 12.0,
    "kSprintAnimTau": 0.20000000298023224,
    "kSprintEventName": "Sprint",
    "kSyncSprintAnimToSpeed": true,
    "kToggleSpeedEvent": "Shout",
    "kToggleSpeedKey": 269,
    "kUseMaxArmorWeight": false,
    "kUsePlayerScale": true,
    "kWeatherAffects": "default",
    "kWeatherEnabled": true,
    "kWeatherMode": "add",
    "kWeatherPresets": [
        {
            "form": "Skyrim.esm|0x10A242",
            "value": 20.0
        }
    ],
    "kWeightPivot": 10.0,
    "kWeightSlope": -0.029999999329447746
}
```

### Key Explanations
- kReduceOutOfCombat, kReduceJoggingOutOfCombat, kReduceDrawn, kReduceSneak
Reductions per state. Jogging is a toggle variant of Default.

- kIncreaseSprinting
  - Extra increase while sprinting. If kSprintAffectsCombat is true, this bonus also applies in combat.

- kNoReductionInCombat
  - When true, combat keeps full base speed.

- kMinFinalSpeedMult
  - Safety floor for the final SpeedMult.

- kEnableDiagonalSpeedFix, kEnableDiagonalSpeedFixForNPCs
  - Removes the diagonal advantage, respects kMinFinalSpeedMult, uses a gentler penalty during sprint.

- Smoothing
  - kSmoothingEnabled, kSmoothingMode (Expo, Rate, ExpoThenRate), kSmoothingHalfLifeMs, kSmoothingMaxChangePerSecond, and kSmoothingBypassOnStateChange control how quickly current deltas approach targets, with optional bypass on state changes like draw, sneak, sprint.

- Sprint animation sync
  - kSyncSprintAnimToSpeed, kOnlySlowDown, kSprintAnimMin, kSprintAnimMax, kSprintAnimOwnSmoothing, kSprintAnimMode, kSprintAnimTau, kSprintAnimRatePerSec.

- Attack scaling
  - kAttackSpeedEnabled, kAttackOnlyWhenDrawn, kAttackBase, kWeightPivot, kWeightSlope, kUsePlayerScale, kScaleSlope, kMinAttackMult, kMaxAttackMult govern weapon-weight and actor-scale based attack speed. Updates on equip.

- Input bindings
  - kToggleSpeedEvent, kToggleSpeedKey, kSprintEventName drive jogging and sprint detection via game user events or a scancode.

- Location rules
  - kReduceInLocationSpecific and kReduceInLocationType define rules per location or keyword.
kLocationAffects: "default" or "all".
kLocationMode: "replace" or "add".

- Misc
  - kEnableSpeedScalingForNPCs applies scaling rules to NPCs.
kIgnoreBeastForms disables modifiers in Werewolf and Vampire Lord forms.
kEventDebounceMs reduces spam from rapid input changes.

## Tips
- When you change input bindings, listeners update immediately and the player gets a refresh.

- Controller thumbstick axes are filtered with a dead zone and feed the diagonal correction.

- The plugin nudges a harmless actor value to trigger Skyrim's movement recompute, then compensates it so gameplay values stay intact. This avoids stuck movement after loads or rapid state changes.

## Troubleshooting
**Inventory weight looks off or movement feels stuck after a load**
The plugin restores a safe baseline and performs a refresh. If needed, open the menu and press Save Settings, or briefly toggle jogging to rewrite the current delta.

**Diagonal still feels weird**
Ensure the diagonal fix is enabled for the player. If your reductions are very strong, raise kMinFinalSpeedMult so the fix has headroom.

**Attack speed does not change**
Verify attack scaling is enabled and, if using only when drawn, that your weapon is drawn. Re-equip to force an update.

## Uninstall
Delete the DLL and the JSON. The plugin reverts its deltas and leaves no scripts in your save.

## Build From Source
- Clone the repository

- Configure with CMake for MSVC on Windows.

- Link against SKSE and CommonLibSSE/NG as appropriate for your target runtime.

- Build the release DLL and place it in Data\SKSE\Plugins\.

## Roadmap
[ ] Additional state hooks and alternate smoothing presets

[ ] Optional per-weapon overrides

[ ] More granular NPC filters
# TES5-DynamicWetness
