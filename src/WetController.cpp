#include "WetController.h"

#include <algorithm>
#include <functional>

#include "Settings.h"

#include "RE/B/BSLightingShaderProperty.h"
#include "RE/N/NiNode.h"

using namespace std::chrono_literals;

namespace SWE {

    static inline float clampf(float v, float lo, float hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }

    static void ForEachGeometry(RE::NiAVObject* obj, const std::function<void(RE::BSGeometry*)>& fn) {
        if (!obj) return;

        if (auto* geom = obj->AsGeometry()) {
            fn(geom);
        }
        if (auto* node = obj->AsNode()) {
            for (auto& child : node->GetChildren()) {
                ForEachGeometry(child.get(), fn);
            }
        }
    }

    static inline bool IsActorSwimming(const RE::Actor* a) {
        if (!a) return false;
        return a->GetActorRuntimeData().boolBits.any(RE::Actor::BOOL_BITS::kSwimming);
    }
    static inline bool IsActorInWaterFlag(const RE::Actor* a) {
        if (!a) return false;
        const auto& rd = a->GetActorRuntimeData();
        return rd.boolBits.any(RE::Actor::BOOL_BITS::kInWater) || rd.boolFlags.any(RE::Actor::BOOL_FLAGS::kUnderwater);
    }
    static inline bool IsActorWetByWater(RE::Actor* a) {
        if (!a) return false;
        const auto& rd = a->GetActorRuntimeData();

        if (a->IsInWater()) return true;

        if (rd.boolBits.any(RE::Actor::BOOL_BITS::kSwimming)) return true;
        if (rd.boolBits.any(RE::Actor::BOOL_BITS::kInWater)) return true;
        if (rd.boolFlags.any(RE::Actor::BOOL_FLAGS::kUnderwater)) return true;

        if (rd.underWaterTimer > 0.0f) return true;

        return false;
    }
    static RE::BSLightingShaderProperty* FindLightingProp(RE::BSGeometry* g) {
        if (!g) return nullptr;
        auto& rdata = g->GetGeometryRuntimeData();
        for (auto& p : rdata.properties) {
            if (!p) continue;
            if (auto* l = skyrim_cast<RE::BSLightingShaderProperty*>(p.get())) return l;
        }
        return nullptr;
    }

    void WetController::Install() {
        _lastTick = std::chrono::steady_clock::now();
    }

    void WetController::Start() {
        if (_running.exchange(true)) return;

        _lastTick = std::chrono::steady_clock::now();

        _timerAlive.store(true);
        _timerThread = std::thread([this]() {
            while (_timerAlive.load()) {
                const int ms = std::max(10, Settings::updateIntervalMs.load());
                std::this_thread::sleep_for(std::chrono::milliseconds(ms));

                SKSE::GetTaskInterface()->AddTask([this]() {
                    if (!_running.load()) return;
                    this->TickGameThread();
                });
            }
        });
    }

    void WetController::Stop() {
        _running.store(false);
        _timerAlive.store(false);
        if (_timerThread.joinable()) {
            _timerThread.join();
        }
    }

    void WetController::ScheduleNextTick() {
        SKSE::GetTaskInterface()->AddTask([this]() {
            if (!_running.load()) return;
            this->TickGameThread();
            if (_running.load()) this->ScheduleNextTick();
        });
    }

    void WetController::OnPreLoadGame() {
        _wet.clear();
        _matCache.clear();
    }

    void WetController::OnPostLoadGame() { RefreshNow(); }

    void WetController::RefreshNow() {
        SKSE::GetTaskInterface()->AddTask([this]() { TickGameThread(); });
    }

    float WetController::GetPlayerWetness() const {
        auto* pc = RE::PlayerCharacter::GetSingleton();
        if (!pc) return 0.f;
        auto it = _wet.find(pc->GetFormID());
        return (it != _wet.end()) ? it->second.wetness : 0.f;
    }

    void WetController::SetPlayerWetnessSnapshot(float w) {
        auto* pc = RE::PlayerCharacter::GetSingleton();
        if (!pc) return;
        _wet[pc->GetFormID()].wetness = clampf(w, 0.f, 1.f);
    }

    bool WetController::IsRainingOrSnowing() const {
        auto* sky = RE::Sky::GetSingleton();
        if (!sky || !sky->currentWeather) return false;

        const auto flags = sky->currentWeather->data.flags;
        const bool rainy = flags.any(RE::TESWeather::WeatherDataFlag::kRainy);
        const bool snowy = flags.any(RE::TESWeather::WeatherDataFlag::kSnow);
        return Settings::affectInSnow.load() ? (rainy || snowy) : rainy;
    }

    bool WetController::IsSnowingCurrent() const {
        auto* sky = RE::Sky::GetSingleton();
        if (!sky || !sky->currentWeather) return false;
        return sky->currentWeather->data.flags.any(RE::TESWeather::WeatherDataFlag::kSnow);
    }

    bool WetController::IsActorInExteriorWet(RE::Actor* a) const {
        if (!a) return false;
        auto* cell = a->GetParentCell();
        if (!cell) return false;
        if (cell->IsInteriorCell() && Settings::ignoreInterior.load()) return false;
        return IsRainingOrSnowing();
    }

    void WetController::TickGameThread() {
        if (!Settings::modEnabled.load() || !_running.load()) return;

        const auto now = std::chrono::steady_clock::now();
        const auto wantDelta = std::chrono::milliseconds(std::max(10, Settings::updateIntervalMs.load()));
        const auto elapsed = now - _lastTick;
        if (elapsed < wantDelta) return;

        float dt = std::chrono::duration<float>(elapsed).count();
        _lastTick = now;
        dt = clampf(dt, 0.0f, 0.2f);

        if (auto* ui = RE::UI::GetSingleton()) {
            if (ui->GameIsPaused() || ui->IsMenuOpen(RE::MainMenu::MENU_NAME)) return;
        }

        RE::Actor* player = RE::PlayerCharacter::GetSingleton();
        if (player) UpdateActorWetness(player, dt);

        if (Settings::affectNPCs.load()) {
            if (auto* proc = RE::ProcessLists::GetSingleton()) {
                const int radius = Settings::npcRadius.load();
                const bool useRad = (radius > 0);
                const float radiusSq = static_cast<float>(radius) * static_cast<float>(radius);
                const RE::NiPoint3 pcPos = player ? player->GetPosition() : RE::NiPoint3();

                for (RE::ActorHandle& h : proc->highActorHandles) {
                    RE::NiPointer<RE::Actor> ap = h.get();
                    RE::Actor* a = ap.get();
                    if (!a || a == player) continue;
                    if (useRad) {
                        const float d2 = a->GetPosition().GetSquaredDistance(pcPos);
                        if (d2 > radiusSq) continue;
                    }
                    UpdateActorWetness(a, dt);
                }
            }
        }
    }

    void WetController::UpdateActorWetness(RE::Actor* a, float dt) {
        if (!a) return;

        auto& wd = _wet[a->GetFormID()];
        wd.lastSeen = std::chrono::steady_clock::now();

        const bool inWater = IsActorWetByWater(a);
        const bool inPrecip = Settings::rainSnowEnabled.load() && IsActorInExteriorWet(a);

        const float soakWaterRate =
            (Settings::secondsToSoakWater.load() > 0.01f) ? (1.f / Settings::secondsToSoakWater.load()) : 1.0f;
        const float soakRainRate =
            (Settings::secondsToSoakRain.load() > 0.01f) ? (1.f / Settings::secondsToSoakRain.load()) : 1.0f;
        const float dryRate = (Settings::secondsToDry.load() > 0.01f) ? (1.f / Settings::secondsToDry.load()) : 1.0f;

        float w = wd.wetness;
        if (inWater) {
            w += soakWaterRate * dt;
        } else if (inPrecip) {
            const float snowFactor = (IsSnowingCurrent() ? 0.8f : 1.0f);
            w += soakRainRate * snowFactor * dt;
        } else {
            w -= dryRate * dt;
        }
        w = clampf(w, 0.f, 1.f);
        wd.wetness = w;

        if (std::abs(wd.lastAppliedWet - w) > 0.0025f) {
            ApplyWetnessMaterials(a, w);
            wd.lastAppliedWet = w;
        }

        if (a->IsPlayerRef()) {
            static auto lastToast = std::chrono::steady_clock::now();
            if (std::chrono::steady_clock::now() - lastToast > 1s && std::abs(wd.lastAppliedWet - w) > 0.05f) {
                RE::DebugNotification((std::string("SWE: wetness=") + std::to_string(w)).c_str());
                lastToast = std::chrono::steady_clock::now();
            }
        }
    }

    void SWE::WetController::ApplyWetnessMaterials(RE::Actor* a, float wet) {
        if (!a) return;

        RE::NiAVObject* third = a->Get3D();
        RE::NiAVObject* first = nullptr;
        if (a->IsPlayerRef() && third) {
            first = third->GetObjectByName("1st Person");
            if (!first) first = third->GetObjectByName("1stPerson");
        }
        RE::NiAVObject* roots[2] = {third, first};

        const bool anyToggle = Settings::affectSkin.load() || Settings::affectHair.load() ||
                               Settings::affectArmor.load() || Settings::affectWeapons.load();
        if (!anyToggle) return;

        const float maxGloss = Settings::maxGlossiness.load();
        const float maxSpec = Settings::maxSpecularStrength.load();
        const float glBoost = std::min(60.0f, Settings::glossinessBoost.load());
        const float scBoost = Settings::specularScaleBoost.load();
        wet = std::clamp(wet, 0.0f, 1.0f);

        int geomsTouched = 0, propsTouched = 0;

        auto touchGeom = [&](RE::BSGeometry* g) {
            auto* lsp = FindLightingProp(g);
            if (!lsp) return;

            auto* mat = skyrim_cast<RE::BSLightingShaderMaterialBase*>(lsp->material);
            if (!mat) return;

            auto it = _matCache.find(lsp);
            if (it == _matCache.end()) {
                _matCache.emplace(
                    lsp, MatSnapshot{
                             mat->materialAlpha, mat->specularPower,
                             mat->specularColorScale, mat->specularColor.red, mat->specularColor.green,
                             mat->specularColor.blue});
                it = _matCache.find(lsp);
            }
            const MatSnapshot& base = it->second;

            auto* sp = static_cast<RE::BSShaderProperty*>(lsp);
            if (sp) {
                if (!sp->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kSpecular)) {
                    sp->flags.set(RE::BSShaderProperty::EShaderPropertyFlag::kSpecular);
                    sp->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kSpecular, true);
                }
            }

            RE::NiColor newSpec{base.baseSpecR, base.baseSpecG, base.baseSpecB};
            if ((newSpec.red + newSpec.green + newSpec.blue) < 0.05f) {
                newSpec = {0.7f, 0.7f, 0.7f};
            }
            const float newGloss = std::clamp(base.baseSpecularPower + wet * glBoost, 5.0f, maxGloss);
            const float baseScale = std::max(0.05f, base.baseSpecularScale);
            const float newScale = std::clamp(baseScale * (1.0f + wet * scBoost), 0.0f, maxSpec);

            mat->specularPower = newGloss;
            mat->specularColor = newSpec;
            mat->specularColorScale = newScale;

            sp->SetMaterial(mat, true);
            lsp->DoClearRenderPasses();
            (void)lsp->SetupGeometry(g);
            (void)lsp->FinishSetupGeometry(g);

            ++propsTouched;
        };

        for (RE::NiAVObject* root : roots) {
            if (!root) continue;
            ForEachGeometry(root, [&](RE::BSGeometry* g) {
                ++geomsTouched;
                touchGeom(g);
            });
        }

        static auto lastToast = std::chrono::steady_clock::now();
        if (a->IsPlayerRef() && std::chrono::steady_clock::now() - lastToast > 1s) {
            RE::DebugNotification((std::string("SWE touched ") + std::to_string(propsTouched) + " props / " +
                                   std::to_string(geomsTouched) + " geoms")
                                      .c_str());
            lastToast = std::chrono::steady_clock::now();
        }
    }
}
