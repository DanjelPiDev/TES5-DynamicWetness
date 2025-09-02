#include "utils/Utils.h"

#include "WetController.h"

extern "C" {
    float SWE_GetActorSubmergeLevel(RE::Actor* a);
    bool SWE_IsActorInWater(RE::Actor* a);
    bool SWE_IsWetWeatherAround(RE::Actor* a);
    unsigned SWE_GetEnvMask(RE::Actor* a);
    bool SWE_IsNearHeatSource(RE::Actor* a, float radius);
    bool SWE_IsUnderRoof(RE::Actor* a);
    bool SWE_IsActorInExteriorWet(RE::Actor* a);
}

namespace SWE::Util {

    float GetSubmergedLevel(RE::Actor* a) noexcept { return a ? SWE_GetActorSubmergeLevel(a) : 0.0f; }

    bool IsActorWetByWater(RE::Actor* a) noexcept { return a ? SWE_IsActorInWater(a) : false; }

    bool IsWetWeatherAround(RE::Actor* a) noexcept { return a ? SWE_IsWetWeatherAround(a) : false; }

    bool IsActorInExteriorWet(RE::Actor* a) noexcept { return a ? SWE_IsActorInExteriorWet(a) : false; }

    bool IsNearHeatSource(RE::Actor* a, float radius) noexcept {
        return (a && radius > 0.f) ? SWE_IsNearHeatSource(a, radius) : false;
    }

    bool IsUnderRoof(RE::Actor* a) noexcept { return a ? SWE_IsUnderRoof(a) : false; }

    EnvState QueryEnvironment(RE::Actor* a) noexcept {
        EnvState e{};
        if (!a) return e;
        const unsigned m = SWE_GetEnvMask(a);
        e.inWater = (m & SWE_ENV_WATER) != 0u;
        e.wetWeather = (m & SWE_ENV_WET_WEATHER) != 0u;
        e.nearHeat = (m & SWE_ENV_NEAR_HEAT) != 0u;
        e.underRoof = (m & SWE_ENV_UNDER_ROOF) != 0u;
        e.exteriorOpen = (m & SWE_ENV_EXTERIOR_OPEN) != 0u;
        return e;
    }

    float GetFinalWetness(RE::Actor* a) noexcept {
        if (!a) return 0.0f;
        if (auto* wc = SWE::WetController::GetSingleton()) {
            return wc->GetFinalWetnessForActor(a);
        }
        return 0.0f;
    }
}
