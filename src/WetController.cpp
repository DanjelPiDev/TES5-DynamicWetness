#include "WetController.h"

#include <algorithm>
#include <functional>

#include "Settings.h"
#include "REL/Relocation.h"

#include "RE/B/BSLightingShaderMaterialBase.h"
#include "RE/B/BSTextureSet.h"

using namespace std::chrono_literals;

namespace SWE {
    enum class MatCat { SkinFace, Hair, ArmorClothing, Weapon, Other };
    static inline float clampf(float v, float lo, float hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }

    static inline void SetSpecularEnabled(RE::BSShaderProperty* sp, bool on) {
        if (!sp) return;
        if (on)
            sp->flags.set(RE::BSShaderProperty::EShaderPropertyFlag::kSpecular);
        else
            sp->flags.reset(RE::BSShaderProperty::EShaderPropertyFlag::kSpecular);

        sp->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kSpecular, on);
    }
    static inline bool NameHas(const RE::NiAVObject* o, std::string_view s) {
        if (!o) return false;
        std::string n = o->name.c_str();
        std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return std::tolower(c); });
        std::string t(s);
        std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return std::tolower(c); });
        return n.find(t) != std::string::npos;
    }
    static bool LooksLikeHeatSource(const RE::TESObjectREFR* r) {
        if (!r) return false;

        auto lc = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
            return s;
        };

        if (auto* base = r->GetBaseObject()) {
            if (const char* nm = base->GetName()) {
                std::string n = lc(nm);
                if (n.find("campfire") != std::string::npos || n.find("fireplace") != std::string::npos ||
                    n.find("brazier") != std::string::npos || n.find("hearth") != std::string::npos ||
                    n.find("embers") != std::string::npos || n.find("forge") != std::string::npos ||
                    n.find("smelter") != std::string::npos)
                    return true;

                if (n.find("fire") != std::string::npos && n.find("torch") == std::string::npos &&
                    n.find("candle") == std::string::npos)
                    return true;
            }
        }

        if (auto* root = r->Get3D()) {
            const RE::NiAVObject* cur = root;
            for (int i = 0; i < 4 && cur; ++i) {
                if (NameHas(cur, "campfire") || NameHas(cur, "fireplace") || NameHas(cur, "brazier") ||
                    NameHas(cur, "hearth") || NameHas(cur, "embers") || NameHas(cur, "forge") ||
                    NameHas(cur, "smelter")) {
                    return true;
                }

                if (NameHas(cur, "fire") && !NameHas(cur, "torch") && !NameHas(cur, "candle")) {
                    return true;
                }
                cur = cur->parent;
            }
        }

        return false;
    }
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
    static inline float GetSubmergedLevel(RE::Actor* a, float z, RE::TESObjectCELL* cell) {
        using func_t = float (*)(RE::Actor*, float, RE::TESObjectCELL*);
        static REL::Relocation<func_t> func{REL::RelocationID(36452, 37448)};
        return func(a, z, cell);
    }
    static inline float ComputeSubmergeLevel(RE::Actor* a) {
        if (!a) return 0.0f;

        if (auto* cell = a->GetParentCell()) {
            const float z = a->GetPosition().z;
            const float s = GetSubmergedLevel(a, z, cell);
            if (s > 0.0f) {
                return std::clamp(s, 0.0f, 1.0f);
            }
        }

        const auto& rd = a->GetActorRuntimeData();
        if (rd.boolFlags.any(RE::Actor::BOOL_FLAGS::kUnderwater)) return 1.0f;
        if (!(rd.boolBits.any(RE::Actor::BOOL_BITS::kInWater) || a->IsInWater())) return 0.0f;

        auto* root = a->Get3D();
        if (!root) return rd.boolBits.any(RE::Actor::BOOL_BITS::kSwimming) ? 0.5f : 0.05f;
        const auto& wb = root->worldBound;
        const float minZ = wb.center.z - wb.radius;
        const float maxZ = wb.center.z + wb.radius;
        const float waterZ = a->GetPosition().z;
        if (waterZ <= minZ) return 0.0f;
        if (waterZ >= maxZ) return 1.0f;
        const float h = std::max(0.0001f, (maxZ - minZ));
        return std::clamp((waterZ - minZ) / h, 0.0f, 1.0f);
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

        const bool inWaterFlag = a->IsInWater() || rd.boolBits.any(RE::Actor::BOOL_BITS::kInWater);
        const bool underwater = rd.boolFlags.any(RE::Actor::BOOL_FLAGS::kUnderwater);
        const bool swimming = rd.boolBits.any(RE::Actor::BOOL_BITS::kSwimming);

        const bool hasContact = inWaterFlag || underwater || swimming;
        if (!hasContact) return false;

        const float minSub = std::clamp(Settings::minSubmergeToSoak.load(), 0.0f, 0.99f);
        if (minSub <= 0.0001f) return true;

        float s = underwater ? 1.0f : ComputeSubmergeLevel(a);
        if (swimming) s = std::max(s, 0.5f);

        return s >= minSub;
    }
    static bool TexLooksLikeBodySkin(RE::BSLightingShaderMaterialBase* mb) {
        RE::BSTextureSet* ts = mb->textureSet.get();
        if (!ts) return false;

        using Tex = RE::BSTextureSet::Texture;
        auto texPathDN = [&](int dn) -> const char* {
            Tex slot = (dn == 0) ? Tex::kDiffuse : Tex::kNormal;
            return ts->GetTexturePath(slot);
        };

        auto has = [&](int idx, std::string_view needle) {
            const char* p = texPathDN(idx);
            if (!p || !p[0]) return false;
            std::string s(p);
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
            return s.find(needle) != std::string::npos;
        };

        const bool inActors = has(0, "actors/character") || has(1, "actors/character");
        const bool skinish = has(0, "body") || has(1, "body") || has(0, "hand") || has(1, "hand") || has(0, "feet") ||
                             has(1, "feet") || has(0, "skin") || has(1, "skin");
        const bool armorish = has(0, "armor/") || has(1, "armor/") || has(0, "clothes/") || has(1, "clothes/");
        return inActors && skinish && !armorish;
    }
    static bool LooksLikeBodySkin(const RE::NiAVObject* o) {
        const RE::NiAVObject* cur = o;
        for (int i = 0; i < 4 && cur; ++i) {
            const bool isSkiny = NameHas(cur, "body") || NameHas(cur, "hands") || NameHas(cur, "hand") ||
                                 NameHas(cur, "feet") || NameHas(cur, "foot") || NameHas(cur, "skin") ||
                                 NameHas(cur, "femalebody") || NameHas(cur, "malebody");
            const bool looksArmor = NameHas(cur, "armor") || NameHas(cur, "cuirass") || NameHas(cur, "gauntlet") ||
                                    NameHas(cur, "glove") || NameHas(cur, "boot") || NameHas(cur, "shoe") ||
                                    NameHas(cur, "robe");
            if (isSkiny && !looksArmor) return true;
            cur = cur->parent;
        }
        return false;
    }
    static MatCat ClassifyGeom(RE::BSGeometry* g, RE::BSLightingShaderProperty* lsp) {
        if (lsp && lsp->material) {
            if (auto* mb = static_cast<RE::BSLightingShaderMaterialBase*>(lsp->material)) {
                using F = RE::BSLightingShaderMaterialBase::Feature;
                switch (mb->GetFeature()) {
                    case F::kFaceGen:
                    case F::kFaceGenRGBTint:
                        return MatCat::SkinFace;
                    case F::kHairTint:
                        return MatCat::Hair;
                    default:
                        break;
                }
            }
        }

        if (g) {
            auto& grt = g->GetGeometryRuntimeData();
            if (auto* si = grt.skinInstance.get()) {
                if (auto* dsi = netimmerse_cast<RE::BSDismemberSkinInstance*>(si)) {
                    const auto& rd = dsi->GetRuntimeData();
                    for (int i = 0; i < rd.numPartitions; ++i) {
                        const std::uint16_t slot = rd.partitions[i].slot;
                        switch (slot) {
                            case 30:
                                return MatCat::SkinFace;
                            case 31:
                                return MatCat::Hair;
                            case 41:
                                return MatCat::Weapon;
                            case 32:
                            case 33:
                            case 37: {
                                auto* mb =
                                    lsp ? static_cast<RE::BSLightingShaderMaterialBase*>(lsp->material) : nullptr;
                                if ((mb && TexLooksLikeBodySkin(mb)) || LooksLikeBodySkin(g)) return MatCat::SkinFace;
                                return MatCat::ArmorClothing;
                            }
                            case 34:
                            case 35:
                            case 36:
                            case 38:
                            case 39:
                            case 40:
                                return MatCat::ArmorClothing;
                            default:
                                break;
                        }
                    }
                }
            }
        }

        const RE::NiAVObject* cur = g;
        for (int i = 0; i < 4 && cur; ++i) {
            if (NameHas(cur, "hair")) return MatCat::Hair;
            if (NameHas(cur, "head") || NameHas(cur, "face")) return MatCat::SkinFace;
            if (LooksLikeBodySkin(cur)) return MatCat::SkinFace;
            if (NameHas(cur, "weapon") || NameHas(cur, "sword") || NameHas(cur, "bow") || NameHas(cur, "dagger"))
                return MatCat::Weapon;
            cur = cur->parent;
        }

        return MatCat::ArmorClothing;
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

                        if (d2 > radiusSq) {
                            auto it = _wet.find(a->GetFormID());
                            if (it != _wet.end() &&
                                (it->second.lastAppliedWet > 0.0005f || it->second.wetness > 0.0005f)) {
                                ApplyWetnessMaterials(a, 0.0f);
                                it->second.wetness = 0.0f;
                                it->second.lastAppliedWet = 0.0f;
                            }
                            continue;
                        }
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

        const bool precipNow = Settings::rainSnowEnabled.load() && IsRainingOrSnowing();

        bool isInterior = false;
        if (auto* cell = a->GetParentCell()) {
            isInterior = cell->IsInteriorCell();
        }

        const float soakWaterRate =
            (Settings::secondsToSoakWater.load() > 0.01f) ? (1.f / Settings::secondsToSoakWater.load()) : 1.0f;
        const float soakRainRate =
            (Settings::secondsToSoakRain.load() > 0.01f) ? (1.f / Settings::secondsToSoakRain.load()) : 1.0f;
        const float dryRate = (Settings::secondsToDry.load() > 0.01f) ? (1.f / Settings::secondsToDry.load()) : 1.0f;

        float dryMul = 1.0f;
        if (!inWater) {
            const float rad = std::max(50.0f, Settings::nearFireRadius.load());
            const bool nearHeat = IsNearHeatSource(a, rad);

            const bool allowHeatDry = nearHeat && (!precipNow || isInterior);
            if (allowHeatDry) {
                dryMul = std::max(1.0f, Settings::dryMultiplierNearFire.load());
            }
        }

        float w = wd.wetness;

        if (inWater) {
            w += soakWaterRate * dt;
        } else {
            if (precipNow && !isInterior) {
                const float snowFactor = (IsSnowingCurrent() ? 0.8f : 1.0f);
                w += soakRainRate * snowFactor * dt;
            } else {
                w -= dryRate * dryMul * dt;
            }
        }

        w = clampf(w, 0.f, 1.f);
        wd.wetness = w;

        if (std::abs(wd.lastAppliedWet - w) > 0.0025f) {
            ApplyWetnessMaterials(a, w);
            wd.lastAppliedWet = w;
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
        const float minGloss = std::min(Settings::minGlossiness.load(), maxGloss);
        const float minSpec = std::min(Settings::minSpecularStrength.load(), maxSpec);
        const float glBoost = std::min(60.0f, Settings::glossinessBoost.load());
        const float scBoost = Settings::specularScaleBoost.load();

        wet = std::clamp(wet, 0.0f, 1.0f);

        int geomsTouched = 0, propsTouched = 0;

        auto touchGeom = [&](RE::BSGeometry* g) {
            auto* lsp = FindLightingProp(g);
            if (!lsp) return;

            const MatCat cat = ClassifyGeom(g, lsp);
            if ((cat == MatCat::SkinFace && !Settings::affectSkin.load()) ||
                (cat == MatCat::Hair && !Settings::affectHair.load()) ||
                (cat == MatCat::ArmorClothing && !Settings::affectArmor.load()) ||
                (cat == MatCat::Weapon && !Settings::affectWeapons.load())) {
                return;
            }

            const float catMul = (cat == MatCat::SkinFace || cat == MatCat::Hair)
                                     ? std::max(0.1f, Settings::skinHairResponseMul.load())
                                     : 1.0f;

            auto* mat = static_cast<RE::BSLightingShaderMaterialBase*>(lsp->material);
            if (!mat) return;

            auto it = _matCache.find(lsp);
            if (it == _matCache.end()) {
                bool hadSpec = false;
                if (auto* sp0 = static_cast<RE::BSShaderProperty*>(lsp)) {
                    hadSpec = sp0->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kSpecular);
                }
                _matCache.emplace(lsp, MatSnapshot{.baseAlpha = mat->materialAlpha,
                                                   .baseSpecularPower = mat->specularPower,
                                                   .baseSpecularScale = mat->specularColorScale,
                                                   .baseSpecR = mat->specularColor.red,
                                                   .baseSpecG = mat->specularColor.green,
                                                   .baseSpecB = mat->specularColor.blue,
                                                   .hadSpecular = hadSpec});
                it = _matCache.find(lsp);
            }
            const MatSnapshot& base = it->second;

            auto* sp = static_cast<RE::BSShaderProperty*>(lsp);
            if (wet <= 0.0005f) {
                if (sp) {
                    SetSpecularEnabled(sp, base.hadSpecular);
                }
                mat->materialAlpha = base.baseAlpha;
                mat->specularPower = base.baseSpecularPower;
                mat->specularColorScale = base.baseSpecularScale;
                mat->specularColor = {base.baseSpecR, base.baseSpecG, base.baseSpecB};

                sp->SetMaterial(mat, true);
                lsp->DoClearRenderPasses();
                (void)lsp->SetupGeometry(g);
                (void)lsp->FinishSetupGeometry(g);
                propsTouched;
                return;
            }

            if (sp) {
                SetSpecularEnabled(sp, true);
            }
            RE::NiColor newSpec{base.baseSpecR, base.baseSpecG, base.baseSpecB};
            if ((newSpec.red + newSpec.green + newSpec.blue) < 0.05f) {
                newSpec = {0.7f, 0.7f, 0.7f};
            }
            float newGloss = base.baseSpecularPower + wet * glBoost * catMul;
            newGloss = std::clamp(newGloss, minGloss, maxGloss);

            float newScale = base.baseSpecularScale + wet * scBoost * catMul;
            newScale = std::clamp(newScale, minSpec, maxSpec);

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
            lastToast = std::chrono::steady_clock::now();
        }
    }

    bool WetController::IsNearHeatSource(const RE::Actor* a, float radius) const {
        if (!a || radius <= 0.f) return false;
        auto* cell = a->GetParentCell();
        if (!cell) return false;

        const RE::NiPoint3 center = a->GetPosition();
        const float r2 = radius * radius;

        bool found = false;
        cell->ForEachReference([&](RE::TESObjectREFR& ref) {
            if (found) return RE::BSContainer::ForEachResult::kStop;

            if (&ref == a || !ref.Is3DLoaded()) return RE::BSContainer::ForEachResult::kContinue;

            if (ref.GetPosition().GetSquaredDistance(center) > r2) return RE::BSContainer::ForEachResult::kContinue;

            if (LooksLikeHeatSource(&ref)) {
                found = true;
                return RE::BSContainer::ForEachResult::kStop;
            }
            return RE::BSContainer::ForEachResult::kContinue;
        });

        return found;
    }
}
