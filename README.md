# Skyrim Wet Effect

A lightweight SKSE plugin that makes characters appear **wet** when exposed to water, rain, or snow.  
Unlike script-based approaches, this is a pure DLL implementation with smooth transitions, no save bloat, and full configuration support.

> More is coming, stay tuned!

---

## Features
- Dynamic wetness system:
  - Characters gradually **soak** when in water or under rain/snow.
  - Seamless **drying** when leaving water or shelter.
- Applies to **Player** and optionally **NPCs** (with adjustable radius).
- Supports **skin, hair, armor, and weapons** independently.
- Works alongside **ENB wet surfaces** and mods using shader-based drip effects.
- Highly configurable via JSON and optional in-game SKSEMenuFramework UI.

---

## Configuration
Settings can be adjusted either:
- Through the in-game menu (**SKSEMenuFramework required**)

You can tweak:
- Soak/dry times  
- Rain/snow effects  
- Minimum submersion depth (Not really good for now, refactoring needed)
- NPC update radius  
- Maximum glossiness & specular strength (Random numbers, need to test it more)

## Installation
1. Install [SKSE64](https://skse.silverlock.org/) (AE/SE supported).
2. Drop the DLL into your `Data/SKSE/Plugins/` folder.
3. Install [SKSEMenuFramework](https://www.nexusmods.com/skyrimspecialedition/mods/120352) for in-game configuration.

---

## Credits
- Original idea: *Soaking Wet - Character Wetness Effect* (no source provided).  
- This is a **from-scratch reimplementation**, built for stability, maintainability, and open development.