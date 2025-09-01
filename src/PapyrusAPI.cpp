#include "PapyrusAPI.h"

#include "WetController.h"

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

    bool Register(RE::BSScript::IVirtualMachine* vm) {
        vm->RegisterFunction("SetExternalWetness", "SWE", SetExternalWetness);
        vm->RegisterFunction("ClearExternalWetness", "SWE", ClearExternalWetness);
        vm->RegisterFunction("GetExternalWetness", "SWE", GetExternalWetness);
        vm->RegisterFunction("GetFinalWetness", "SWE", GetFinalWetness);
        return true;
    }
}
