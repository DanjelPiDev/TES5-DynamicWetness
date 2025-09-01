#pragma once
#include <chrono>
#include <unordered_map>

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

        void RefreshNow();
        float GetPlayerWetness() const;
        void SetPlayerWetnessSnapshot(float w);

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

        struct WetData {
            float wetness{0.f};  // 0...1
            float lastAppliedWet{-1.f};
            std::chrono::steady_clock::time_point lastSeen;
            std::chrono::steady_clock::time_point lastRoofProbe{};
            bool lastRoofCovered{false};
            std::chrono::steady_clock::time_point lastHeatProbe{};
            bool cachedNearHeat{false};
        };

        // Keyed by FormID to keep it simple and robust across handles
        std::unordered_map<uint32_t, WetData> _wet;

        std::chrono::steady_clock::time_point _lastTick = std::chrono::steady_clock::now();

        void UpdateActorWetness(RE::Actor* a, float dt);
        void ApplyWetnessMaterials(RE::Actor* a, float wet);

        bool IsActorInExteriorWet(RE::Actor* a) const;
        bool IsRainingOrSnowing() const;
        bool IsSnowingCurrent() const;

        bool IsNearHeatSource(const RE::Actor* a, float radius) const;
        bool RayHitsCover(const RE::NiPoint3& from, const RE::NiPoint3& to,
                          const RE::TESObjectREFR* ignoreRef = nullptr) const;
        bool IsUnderRoof(RE::Actor* a) const;

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
