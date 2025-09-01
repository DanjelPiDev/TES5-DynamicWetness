#pragma once
namespace SWE::Papyrus {
    bool Register(RE::BSScript::IVirtualMachine* vm);

    bool SetExternalWetness(RE::StaticFunctionTag*, RE::Actor* a, RE::BSFixedString key, float value, float durationSec);
    bool ClearExternalWetness(RE::StaticFunctionTag*, RE::Actor* a, RE::BSFixedString key);
    float GetExternalWetness(RE::StaticFunctionTag*, RE::Actor* a, RE::BSFixedString key);
    float GetFinalWetness(RE::StaticFunctionTag*, RE::Actor* a);
}
