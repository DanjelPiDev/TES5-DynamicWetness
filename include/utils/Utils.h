#pragma once
#include "PapyrusAPI.h"

namespace SWE::Util {

    static constexpr unsigned SWE_ENV_WATER = SWE::Papyrus::SWE_ENV_WATER;
    static constexpr unsigned SWE_ENV_WET_WEATHER = SWE::Papyrus::SWE_ENV_WET_WEATHER;
    static constexpr unsigned SWE_ENV_NEAR_HEAT = SWE::Papyrus::SWE_ENV_NEAR_HEAT;
    static constexpr unsigned SWE_ENV_UNDER_ROOF = SWE::Papyrus::SWE_ENV_UNDER_ROOF;
    static constexpr unsigned SWE_ENV_EXTERIOR_OPEN = SWE::Papyrus::SWE_ENV_EXTERIOR_OPEN;

    struct EnvState {
        bool inWater{false};
        bool wetWeather{false};
        bool nearHeat{false};
        bool underRoof{false};
        bool exteriorOpen{false};
    };

    float GetSubmergedLevel(RE::Actor* a) noexcept;  // 0..1
    bool IsActorWetByWater(RE::Actor* a) noexcept;   // respects minSubmerge etc.
    bool IsWetWeatherAround(RE::Actor* a) noexcept;  // world is precipitating and actor in valid area
    bool IsActorInExteriorWet(RE::Actor* a) noexcept;
    bool IsNearHeatSource(RE::Actor* a, float radius) noexcept;
    bool IsUnderRoof(RE::Actor* a) noexcept;

    EnvState QueryEnvironment(RE::Actor* a) noexcept;

    inline bool IsActorInExteriorOpen(RE::Actor* a) noexcept { return QueryEnvironment(a).exteriorOpen; }
    inline bool IsNearHeatSource(RE::Actor* a) noexcept { return QueryEnvironment(a).nearHeat; }

    float GetFinalWetness(RE::Actor* a) noexcept;
}
