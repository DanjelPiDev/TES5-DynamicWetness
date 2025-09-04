#pragma once
#include <chrono>
#include <unordered_map>

#include "Settings.h"

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

namespace SWE {

    class WetController {
    public:
        static WetController* GetSingleton() {
            static WetController inst;
            return std::addressof(inst);
        }
        void Start();
        void Stop();
        bool IsRunning() const { return _running.load(); }

        void Install();
        void OnPreLoadGame();
        void OnPostLoadGame();

        void Serialize(SKSE::SerializationInterface* intfc);
        void Deserialize(SKSE::SerializationInterface* intfc, std::uint32_t version, std::uint32_t length);

        void RefreshNow();
        float GetPlayerWetness() const;
        float GetBaseWetnessForActor(RE::Actor* a);
        void SetPlayerWetnessSnapshot(float w);

        void SetExternalWetness(RE::Actor* a, std::string key, float value, float durationSec = -1.f);
        void ClearExternalWetness(RE::Actor* a, std::string key);
        float GetExternalWetness(RE::Actor* a, std::string key);
        float GetFinalWetnessForActor(RE::Actor* a);

        float GetSubmergedLevel(RE::Actor* a) const;
        bool IsActorWetByWater(RE::Actor* a) const;
        bool IsWetWeatherAround(RE::Actor* a) const;
        bool IsNearHeatSource(const RE::Actor* a, float radius) const;
        bool IsUnderRoof(RE::Actor* a) const;
        bool IsActorInExteriorWet(RE::Actor* a) const;

        struct OverrideParams {
            float maxGloss{-1.f};
            float maxSpec{-1.f};
            float minGloss{-1.f};
            float minSpec{-1.f};
            float glossBoost{-1.f};
            float specBoost{-1.f};
            float skinHairMul{-1.f};
        };
        void SetExternalWetnessMask(RE::Actor* a, const std::string& key, float intensity01, float durationSec,
                                    std::uint8_t catMask, std::uint32_t flags = 0);
        void SetExternalWetnessEx(RE::Actor* a, std::string key, float value, float durationSec, std::uint8_t catMask,
                                  const OverrideParams& ov);

    private:
        WetController() = default;
        ~WetController() = default;
        WetController(const WetController&) = delete;
        WetController& operator=(const WetController&) = delete;

        std::atomic<bool> _running{false};
        std::atomic<bool> _timerAlive{false};
        std::thread _timerThread;

        void TickGameThread();
        void ScheduleNextTick();

        struct ExternalSource {
            float value{0.f};             // 0...1
            float expiryRemainingSec = -1.f;
            std::uint8_t catMask{0};
            std::uint32_t flags = 0;  // behavior flags
            OverrideParams ov;
        };

        struct WetData {
            float wetness{0.f};  // 0...1
            float lastAppliedWet{-1.f};
            float baseWetness{0.f};
            std::chrono::steady_clock::time_point lastSeen;
            std::chrono::steady_clock::time_point lastRoofProbe{};
            bool lastRoofCovered{false};
            std::chrono::steady_clock::time_point lastHeatProbe{};
            bool cachedNearHeat{false};
            std::unordered_map<std::string, ExternalSource> extSources;
            std::chrono::steady_clock::time_point lastWaterfallProbe{};
            bool cachedInsideWaterfall{false};

            struct CatOverrides {
                bool any{false};
                float maxGloss{-1.f}, maxSpec{-1.f}, minGloss{-1.f}, minSpec{-1.f};
                float glossBoost{-1.f}, specBoost{-1.f}, skinHairMul{-1.f};
            };
            CatOverrides activeOv[4]{};  // 0=Skin, 1=Hair, 2=Armor, 3=Weapon
            float lastAppliedCat[4]{-1.f, -1.f, -1.f, -1.f};
        };

        std::unordered_map<uint32_t, WetData> _wet;

        std::chrono::steady_clock::time_point _lastTick = std::chrono::steady_clock::now();

        void UpdateActorWetness(RE::Actor* a, float dt, const std::vector<Settings::FormSpec>& overrides,
                                bool allowEnvWet = true, bool manualMode = false);
        void ApplyWetnessMaterials(RE::Actor* a, const float wetByCat[4]);

        bool IsRainingOrSnowing() const;
        bool IsSnowingCurrent() const;
        bool IsInsideWaterfallFX(const RE::Actor* a, const RE::TESObjectREFR* wfRef, float padX, float padY, float padZ,
                                 bool requireBelowTop) const;

        bool RayHitsCover(const RE::NiPoint3& from, const RE::NiPoint3& to,
                          const RE::TESObjectREFR* ignoreRef = nullptr) const;

        void ComputeWetByCategory(WetData& wd, float baseWet, float outWetByCat[4], float dt, bool envDominates);
        float GetGameHours() const;

        mutable std::recursive_mutex _mtx;

        struct MatSnapshot {
            float baseAlpha{1.f};
            float baseSpecularPower{20.f};
            float baseSpecularScale{1.f};
            float baseSpecR{1.f}, baseSpecG{1.f}, baseSpecB{1.f};
            bool hadSpecular{false};
        };
        std::unordered_map<const RE::BSLightingShaderProperty*, MatSnapshot> _matCache;
        friend class DebugAccess;
    };

}
