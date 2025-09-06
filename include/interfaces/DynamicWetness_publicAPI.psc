Scriptname DynamicWetness_PublicAPI Hidden

; =========================
; Categories (bitmask, 4 bits)
; =========================
; 1 = Skin/Face, 2 = Hair, 4 = Armor/Clothing, 8 = Weapon
; Combine multiple by sum: e.g., Skin+Hair = 1+2 = 3

; =========================
; Flags
; =========================
; 1 = PASSTHROUGH  -> add to the post-blend value (no clamp before addition)
; 2 = ZERO_BASE    -> force base for affected categories to 0 while this source is active
; 4 = NO_AUTODRY   -> suppress automatic drying for affected categories when base would decrease

; =========================
; Core functions (external wetness signal)
; =========================

; Set/update an external source (default: Skin only). durationSec < 0 => infinite.
Function SetExternalWetness(Actor akActor, String key, Float value, Float durationSec = -1.0) Global Native

; As above, but with category mask and flags.
Function SetExternalWetnessMask(Actor akActor, String key, Float intensity01, Float durationSec = -1.0, Int catMask = 1, Int flags = 0) Global Native

; Extended variant with optional material overrides (negative values = ignore / do not override).
Function SetExternalWetnessEx(Actor akActor, String key, Float value, Float durationSec, Int catMask, Float maxGloss = -1.0, Float maxSpec = -1.0, Float minGloss = -1.0, Float minSpec = -1.0, Float glossBoost = -1.0, Float specBoost = -1.0, Float skinHairMul = -1.0) Global Native

; Remove this external source (key is trimmed & lowercased internally).
Function ClearExternalWetness(Actor akActor, String key) Global Native

; =========================
; Queries
; =========================

; Raw: base wetness currently computed from water contact etc. (0..1), without external sources.
Float Function GetBaseWetness(Actor akActor) Global Native

; Final: wetness after categories/blending/external sources are applied (0..1).
Float Function GetFinalWetness(Actor akActor) Global Native

; Value of a specific external source (0..1) — returns 0 if not present.
Float Function GetExternalWetness(Actor akActor, String key) Global Native

; Convenience / environment
Bool  Function IsActorWetByWater(Actor akActor) Global Native    ; true if water contact is strong enough
Float Function GetSubmergedLevel(Actor akActor) Global Native     ; 0..1
Bool  Function IsWetWeatherAround(Actor akActor) Global Native    ; rain/snow active and relevant
