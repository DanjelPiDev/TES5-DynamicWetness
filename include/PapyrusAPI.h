#pragma once
namespace SWE::Papyrus {
    // Category bitmask (low 4 bits)
    static constexpr std::uint32_t SWE_CAT_SKIN_FACE = 1u << 0;
    static constexpr std::uint32_t SWE_CAT_HAIR = 1u << 1;
    static constexpr std::uint32_t SWE_CAT_ARMOR_CLOTH = 1u << 2;
    static constexpr std::uint32_t SWE_CAT_WEAPON = 1u << 3;
    static constexpr std::uint32_t SWE_CAT_MASK_4BIT = 0x0Fu;

    // Extra flags (high bits) for SetExternalWetnessMask
    // modify how DW handles the external source internally
    static constexpr std::uint32_t SWE_FLAG_PASSTHROUGH = 1u << 16;  // add after DW mixing/drying
    static constexpr std::uint32_t SWE_FLAG_NO_AUTODRY = 1u << 17;   // constant until cleared
    static constexpr std::uint32_t SWE_FLAG_ZERO_BASE = 1u << 18;  // sets base wetness in the marked categories to 0

    // Environment bitmask for querying actor state
    static constexpr std::uint32_t SWE_ENV_WATER = 1u << 0;
    static constexpr std::uint32_t SWE_ENV_WET_WEATHER = 1u << 1;
    static constexpr std::uint32_t SWE_ENV_NEAR_HEAT = 1u << 2;
    static constexpr std::uint32_t SWE_ENV_UNDER_ROOF = 1u << 3;
    static constexpr std::uint32_t SWE_ENV_EXTERIOR_OPEN = 1u << 4;

    static constexpr std::uint32_t SWE_MASK_SKIN_PASSTHROUGH =
        SWE_CAT_SKIN_FACE | SWE_FLAG_PASSTHROUGH | SWE_FLAG_NO_AUTODRY | SWE_FLAG_ZERO_BASE;

    bool Register(RE::BSScript::IVirtualMachine* vm);

    bool SetExternalWetnessMask(RE::StaticFunctionTag*, RE::Actor* a, RE::BSFixedString key, float value,
                                float durationSec, std::int32_t catMask);
    bool ClearExternalWetness(RE::StaticFunctionTag*, RE::Actor* a, RE::BSFixedString key);
    float GetBaseWetness(RE::StaticFunctionTag*, RE::Actor* a);
    float GetExternalWetness(RE::StaticFunctionTag*, RE::Actor* a, RE::BSFixedString key);
    float GetFinalWetness(RE::StaticFunctionTag*, RE::Actor* a);
    bool SetExternalWetnessMask(RE::StaticFunctionTag*, RE::Actor* a, RE::BSFixedString key, float value,
                                float durationSec, std::int32_t catMask);
    bool SetExternalWetnessEx(RE::StaticFunctionTag*, RE::Actor* a, RE::BSFixedString key, float value,
                              float durationSec, std::int32_t catMask, float maxGloss, float maxSpec, float minGloss,
                              float minSpec, float glossBoost, float specBoost, float skinHairMul);
    bool IsNearHeatSource(RE::StaticFunctionTag*, RE::Actor* a, float radius);
    bool IsUnderRoof(RE::StaticFunctionTag*, RE::Actor* a);
    bool IsActorInExteriorWet(RE::StaticFunctionTag*, RE::Actor* a);
}
