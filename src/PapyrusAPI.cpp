#include "PapyrusAPI.h"

#include "WetController.h"
#include "Settings.h"

namespace SWE::Papyrus {
    bool SetExternalWetness(RE::StaticFunctionTag*, RE::Actor* a, RE::BSFixedString key, float value,
                            float durationSec) {
        SWE::WetController::GetSingleton()->SetExternalWetness(a, key.c_str(), value, durationSec);
        return true;
    }
    bool ClearExternalWetness(RE::StaticFunctionTag*, RE::Actor* a, RE::BSFixedString key) {
        SWE::WetController::GetSingleton()->ClearExternalWetness(a, key.c_str());
        return true;
    }
    float GetExternalWetness(RE::StaticFunctionTag*, RE::Actor* a, RE::BSFixedString key) {
        return SWE::WetController::GetSingleton()->GetExternalWetness(a, key.c_str());
    }
    float GetFinalWetness(RE::StaticFunctionTag*, RE::Actor* a) {
        return SWE::WetController::GetSingleton()->GetFinalWetnessForActor(a);
    }
    bool SetExternalWetnessMask(RE::StaticFunctionTag*, RE::Actor* a, RE::BSFixedString key, float value,
                                float durationSec, std::int32_t catMask) {
        const std::uint32_t raw = static_cast<std::uint32_t>(catMask);
        const std::uint8_t catBits = static_cast<std::uint8_t>(raw & SWE_CAT_MASK_4BIT);
        const std::uint32_t flags = (raw & ~SWE_CAT_MASK_4BIT);
        SWE::WetController::GetSingleton()->SetExternalWetnessMask(a, key.c_str(), value, durationSec, catBits, flags);
        return true;
    }
    bool SetExternalWetnessEx(RE::StaticFunctionTag*, RE::Actor* a, RE::BSFixedString key, float value,
                              float durationSec, std::int32_t catMask, float maxGloss, float maxSpec, float minGloss,
                              float minSpec, float glossBoost, float specBoost, float skinHairMul) {
        SWE::WetController::OverrideParams ov{};
        ov.maxGloss = maxGloss;
        ov.maxSpec = maxSpec;
        ov.minGloss = minGloss;
        ov.minSpec = minSpec;
        ov.glossBoost = glossBoost;
        ov.specBoost = specBoost;
        ov.skinHairMul = skinHairMul;
        SWE::WetController::GetSingleton()->SetExternalWetnessEx(a, key.c_str(), value, durationSec,
                                                                 static_cast<std::uint8_t>(catMask & 0x0F), ov);
        return true;
    }
    bool IsNearHeatSource(RE::StaticFunctionTag*, RE::Actor* a, float radius) {
        auto* wc = SWE::WetController::GetSingleton();
        if (!a || !wc) return false;

        const float r = (radius > 0.0f) ? radius : std::max(50.0f, Settings::nearFireRadius.load());
        return wc->IsNearHeatSource(a, r);
    }
    bool IsUnderRoof(RE::StaticFunctionTag*, RE::Actor* a) {
        auto* wc = SWE::WetController::GetSingleton();
        return (a && wc) ? wc->IsUnderRoof(a) : false;
    }
    bool IsActorInExteriorWet(RE::StaticFunctionTag*, RE::Actor* a) {
        auto* wc = SWE::WetController::GetSingleton();
        return (a && wc) ? wc->IsActorInExteriorWet(a) : false;
    }

    bool Register(RE::BSScript::IVirtualMachine* vm) {
        vm->RegisterFunction("SetExternalWetness", "SWE", SetExternalWetness);
        vm->RegisterFunction("ClearExternalWetness", "SWE", ClearExternalWetness);
        vm->RegisterFunction("GetExternalWetness", "SWE", GetExternalWetness);
        vm->RegisterFunction("GetFinalWetness", "SWE", GetFinalWetness);
        vm->RegisterFunction("SetExternalWetnessMask", "SWE", SetExternalWetnessMask);
        vm->RegisterFunction("SetExternalWetnessEx", "SWE", SetExternalWetnessEx);
        vm->RegisterFunction("IsNearHeatSource", "SWE", IsNearHeatSource);
        vm->RegisterFunction("IsUnderRoof", "SWE", IsUnderRoof);
        vm->RegisterFunction("IsActorInExteriorWet", "SWE", IsActorInExteriorWet);
        return true;
    }
}
