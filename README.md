# Dynamic Wetness

> [![Download Latest Release](https://img.shields.io/github/v/release/DanjelPiDev/TES5-DynamicWetness)](https://github.com/DanjelPiDev/TES5-DynamicWetness/releases/latest)

A lightweight SKSE plugin that makes characters appear **wet** when exposed to water, rain, or snow.  
Unlike script-based approaches, this is a pure DLL implementation with smooth transitions, no save bloat, and full configuration support.

> More is coming, stay tuned!

---

## Features
- **Player and NPC wetness** with seamless transitions (soaking/drying).
- **Configurable** soak and dry times.
- Wetness from **water, rain, and snow**, with **roof/under-cover** checks for precipitation.
- **Roof detection:** Detects roofs automatically and stops additional rain/snow wetness.
- **Heat sources** (fireplaces, forges, etc.) speed up drying (configurable multiplier).
- **Skin & Hair response multiplier** to make the effect more visible where it matters.
- **NPC tracking modes:** Automatic (reacts to environment) or per-NPC Manual (instant wetness); optional opt-in list and **radius culling**.
- **Heuristic eye detection:** prevents eyes from getting glossy/wet.
- **PBR-friendly mode (heuristic):** softer caps and behavior for armor/weapons and likely PBR materials.
- Full configuration via **SKSEMenuFramework** (sliders, inputs, save/reset), plus handy **presets**.
- Works alongside **ENB wet surfaces** and shader-based drip effects; you can lower the specular effect via config (`MaxSpecularStrength`) if it’s too strong.

---

## Installation
1. Install [SKSE64](https://skse.silverlock.org/) (AE/SE supported).
2. Drop the DLL into your `Data/SKSE/Plugins/` folder.
3. Install [SKSEMenuFramework](https://www.nexusmods.com/skyrimspecialedition/mods/120352) for in-game configuration.
4. Load your save, press F1 -> done.

---

## Configuration
You can adjust everything in the in-game menu (**SKSEMenuFramework** required).

You can tweak e.g.:
- Soak/dry times  
- Rain/snow effects & roof behavior  
- NPC update radius and tracking mode  
- Maximum glossiness & specular strength (and more)

---

## Build (Developers)

### Prerequisites
- **Visual Studio 2022** (MSVC v143), **CMake**, **Ninja** (as in the provided `CMakePresets.json`)  
- **CommonLibSSE** (found via `find_package(CommonLibSSE CONFIG REQUIRED)`)  
- **vcpkg** (triplet `x64-windows-skse`) for other deps  
- **[DirectXTex](https://github.com/microsoft/DirectXTex) (static lib)**, required for on-the-fly merging of specular maps

### Build DirectXTex (once)
1. Clone **Microsoft/DirectXTex**.  
2. Build the desktop solution **x64 / Release** (e.g. `DirectXTex_Desktop_2022.sln`) **or** use CMake to generate an x64 Release static lib.  
3. Note the two paths:
   - **Include**: the folder that contains `DirectXTex.h` (e.g. `E:\DX\DirectXTex\DirectXTex\`)  
   - **Lib**: the built static library (e.g. `E:\DX\DirectXTex\DirectXTex\Bin\Desktop_2022_Win10\x64\Release\DirectXTex.lib`)

### Tell CMake where DirectXTex is
This project’s `CMakeLists.txt` expects **two environment variables**:

- `DIRECTXTEX_INCLUDE` -> path to the **include** directory (with `DirectXTex.h`)  
- `DIRECTXTEX_LIB` -> full path to **DirectXTex.lib** (x64 Release)

#### Set Environment Variables  
```
DIRECTXTEX_INCLUDE = X:\...\DirectXTex\DirectXTex
DIRECTXTEX_LIB     = X:\...\DirectXTex\Bin\Desktop_2022_Win10\x64\Release\DirectXTex.lib
```

> After that, simply build the dll (e.g.: `release-msvc`).  
> The `CMakeLists.txt` imports the Lib as `DirectXTex::DirectXTex`, defines `SWE_USE_DIRECTX_TEX` and links it to: `windowscodecs`/`ole32`.

---

## Credits
- Original idea: *Soaking Wet - Character Wetness Effect* (no source provided).  
- This is a **from-scratch reimplementation**, built for stability, maintainability, and open development.