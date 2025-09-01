#include "WetController.h"

#include <algorithm>
#include <functional>

#include "Settings.h"
#include "REL/Relocation.h"

#include "RE/B/BSLightingShaderMaterialBase.h"
#include "RE/B/BSTextureSet.h"

#include "RE/B/bhkCollisionObject.h"
#include "RE/B/bhkPickData.h"
#include "RE/B/bhkWorld.h"
#include "RE/T/TESObjectCELL.h"
#include "RE/H/hkpWorldRayCastInput.h"
#include "RE/H/hkpWorldRayCastOutput.h"

using namespace std::chrono_literals;

#ifndef SWE_RAY_DEBUG
    #define SWE_RAY_DEBUG 0
#endif

#ifndef SWE_ROOF_SAMPLES
    #define SWE_ROOF_SAMPLES 5
#endif

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
    static bool LooksLikeWaterfall(RE::TESObjectREFR* r) {
        if (!r) return false;

        auto lc = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
            return s;
        };

        if (auto* base = r->GetBaseObject()) {

#if defined(CLASSIC_CLIB_HAS_EDITORID) || defined(SKYRIM_AE) || defined(SKYRIM_SE)
            if (const char* ed = base->GetFormEditorID(); ed && *ed) {
                std::string e = lc(ed);
                if (e.find("waterfall") != std::string::npos && e.find("splash") == std::string::npos &&
                    e.find("foam") == std::string::npos)
                    return true;
            }
#endif

            if (auto* tm = skyrim_cast<RE::TESModel*>(base)) {
                if (const char* model = tm->GetModel(); model && model[0]) {
                    std::string m = lc(model);
                    const bool hasWF = (m.find("waterfall") != std::string::npos) ||
                                       (m.find("fx\\waterfall") != std::string::npos) ||
                                       (m.find("fx/waterfall") != std::string::npos);
                    const bool isAux = (m.find("splash") != std::string::npos) ||
                                       (m.find("ripple") != std::string::npos) ||
                                       (m.find("foam") != std::string::npos);
                    if (!hasWF && isAux) return false;
                    if (hasWF) return true;
                }
            }
        }

        if (auto* root = r->Get3D()) {
            const RE::NiAVObject* cur = root;
            for (int i = 0; i < 4 && cur; ++i) {
                if (NameHas(cur, "waterfall") || NameHas(cur, "falls")) return true;
                cur = cur->parent;
            }
            bool hit = false;
            ForEachGeometry(root, [&](RE::BSGeometry* g) {
                if (hit) return;
                if (NameHas(g, "waterfall") || NameHas(g, "falls")) hit = true;
            });
            if (hit) return true;
        }

        return false;
    }
    static bool HasAuxKeywords(const RE::NiAVObject* root) {
        if (!root) return false;
        auto hasAny = [](const RE::NiAVObject* o) {
            return NameHas(o, "splash") || NameHas(o, "foam") || NameHas(o, "mist") || NameHas(o, "spray") ||
                   NameHas(o, "ripple") || NameHas(o, "droplet");
        };
        const RE::NiAVObject* cur = root;
        for (int i = 0; i < 4 && cur; ++i) {
            if (hasAny(cur)) return true;
            cur = cur->parent;
        }
        bool hit = false;
        ForEachGeometry(const_cast<RE::NiAVObject*>(root), [&](RE::BSGeometry* g) {
            if (hit) return;
            if (hasAny(g)) hit = true;
        });
        return hit;
    }
    static bool MostlyParticleOrEffect(const RE::NiAVObject* root) {
        if (!root) return false;
        int particles = 0, lighting = 0;
        ForEachGeometry(const_cast<RE::NiAVObject*>(root), [&](RE::BSGeometry* g) {
            auto& rdata = g->GetGeometryRuntimeData();
            for (auto& p : rdata.properties) {
                if (!p) continue;
                if (skyrim_cast<RE::BSLightingShaderProperty*>(p.get())) {
                    ++lighting;
                } else if (skyrim_cast<RE::BSParticleShaderProperty*>(p.get()) ||
                           skyrim_cast<RE::BSEffectShaderProperty*>(p.get())) {
                    ++particles;
                }
            }
        });
        return particles > 0 && lighting == 0;
    }
    static inline bool LooksLikeTallWaterSheet(const RE::NiPoint3& bmin, const RE::NiPoint3& bmax) {
        const float H = bmax.z - bmin.z;
        const float W = bmax.x - bmin.x;
        const float D = bmax.y - bmin.y;
        return (H > 200.f) && (H > std::max(W, D) * 1.15f);
    }
    static inline bool LooksLikeWideTallWaterSheet(const RE::NiPoint3& bmin, const RE::NiPoint3& bmax) {
        const float H = bmax.z - bmin.z;
        const float W = bmax.x - bmin.x;
        const float D = bmax.y - bmin.y;
        const float horiz = std::max(W, D);
        return (H >= 220.f) && (horiz >= 96.f) && (H >= horiz * 1.1f);
    }
    static inline float Dist2XY(const RE::NiPoint3& a, const RE::NiPoint3& b) {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        return dx * dx + dy * dy;
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
    static inline std::uint32_t MakeFilterInfo(RE::COL_LAYER layer, std::uint16_t systemGroup = 0xFFFF,
                                               std::uint8_t subSystemId = 0, std::uint8_t subSystemNoCollide = 0) {
        return (static_cast<std::uint32_t>(layer) & 0x3F) | ((static_cast<std::uint32_t>(subSystemId) & 0x1F) << 6) |
               ((static_cast<std::uint32_t>(subSystemNoCollide) & 0x1F) << 11) |
               ((static_cast<std::uint32_t>(systemGroup) & 0xFFFF) << 16);
    }
    static constexpr RE::COL_LAYER kRoofLayersPrimary[] = {
        RE::COL_LAYER::kStatic,        RE::COL_LAYER::kAnimStatic,  RE::COL_LAYER::kTransparentWall,
        RE::COL_LAYER::kInvisibleWall, RE::COL_LAYER::kTransparent,
        RE::COL_LAYER::kLOS,
    };

    static constexpr RE::COL_LAYER kRoofLayersFallback[] = {
        RE::COL_LAYER::kProps,         RE::COL_LAYER::kTrees,       RE::COL_LAYER::kClutterLarge,
        RE::COL_LAYER::kDoorDetection, RE::COL_LAYER::kPathingPick,
    };

    static inline std::uint32_t RayFilter(RE::COL_LAYER lyr) { return MakeFilterInfo(lyr, 0xFFFF, 0, 0); }
    static inline RE::bhkWorld* GetBhkWorldFromActorCell(const RE::Actor* a) {
        if (!a) return nullptr;
        if (auto* cell = a->GetParentCell()) {
            return cell->GetbhkWorld();
        }
        return nullptr;
    }
    static inline float ActorHeadZ(const RE::Actor* a) {
        if (!a) return 0.f;
        float z = a->GetPosition().z + 120.0f;
        if (auto* root = a->Get3D()) {
            const auto& wb = root->worldBound;
            z = std::max(z, wb.center.z + wb.radius * 0.6f);
        }
        return z;
    }
    static const char* LayerName(std::uint32_t lyr) {
        using L = RE::COL_LAYER;
        switch (static_cast<L>(lyr & 0x7F)) {
            case L::kStatic:
                return "Static";
            case L::kAnimStatic:
                return "AnimStatic";
            case L::kTransparent:
                return "Transparent";
            case L::kTransparentWall:
                return "TransparentWall";
            case L::kInvisibleWall:
                return "InvisibleWall";
            case L::kLOS:
                return "LOS";
            case L::kProps:
                return "Props";
            case L::kTrees:
                return "Trees";
            case L::kClutterLarge:
                return "ClutterLarge";
            case L::kDoorDetection:
                return "DoorDetection";
            case L::kPathingPick:
                return "PathingPick";
            case L::kDroppingPick:
                return "DroppingPick";
            case L::kGround:
                return "Ground";
            case L::kWater:
                return "Water";
            default:
                return "Other";
        }
    }
    
    static inline RE::hkVector4 ToHK(const RE::NiPoint3& p) {
        const float s = RE::bhkWorld::GetWorldScale();
        return RE::hkVector4{p.x * s, p.y * s, p.z * s, 0.0f};
    }

    static inline bool CastOnce(RE::bhkWorld* bw, const RE::NiPoint3& fromW, const RE::NiPoint3& toW,
                                std::uint32_t filterInfo, bool enableCollectionFilter) {
        if (!bw) return false;

        RE::bhkPickData pd{};
        pd.rayInput.from = ToHK(fromW);
        pd.rayInput.to = ToHK(toW);

        const float s = RE::bhkWorld::GetWorldScale();
        const RE::NiPoint3 dW{toW.x - fromW.x, toW.y - fromW.y, toW.z - fromW.z};
        pd.ray = RE::hkVector4{dW.x * s, dW.y * s, dW.z * s, 0.0f};

        pd.rayInput.filterInfo = filterInfo;
        pd.rayInput.enableShapeCollectionFilter = enableCollectionFilter;
        pd.rayOutput.Reset();

        return bw->PickObject(pd) && pd.rayOutput.HasHit();
    }
    static std::string NormalizeKey(std::string key) {
        key.erase(key.begin(), std::find_if(key.begin(), key.end(), [](unsigned char c) { return !std::isspace(c); }));
        key.erase(std::find_if(key.rbegin(), key.rend(), [](unsigned char c) { return !std::isspace(c); }).base(),
                  key.end());
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::tolower(c); });
        return key;
    }
    static bool BuildWorldAABB(RE::NiAVObject* root, RE::NiPoint3& outMin, RE::NiPoint3& outMax) {
        if (!root) return false;
        bool any = false;
        RE::NiPoint3 mn{+FLT_MAX, +FLT_MAX, +FLT_MAX};
        RE::NiPoint3 mx{-FLT_MAX, -FLT_MAX, -FLT_MAX};
        ForEachGeometry(root, [&](RE::BSGeometry* g) {
            const auto& wb = g->worldBound;
            const RE::NiPoint3 c = wb.center;
            const float r = wb.radius;
            mn.x = std::min(mn.x, c.x - r);
            mx.x = std::max(mx.x, c.x + r);
            mn.y = std::min(mn.y, c.y - r);
            mx.y = std::max(mx.y, c.y + r);
            mn.z = std::min(mn.z, c.z - r);
            mx.z = std::max(mx.z, c.z + r);
            any = true;
        });
        if (!any) {
            const auto& wb = root->worldBound;
            const RE::NiPoint3 c = wb.center;
            const float r = wb.radius;
            mn = {c.x - r, c.y - r, c.z - r};
            mx = {c.x + r, c.y + r, c.z + r};
        }
        outMin = mn;
        outMax = mx;
        return true;
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
        // If it is precipitating outside and we are in an exterior, probe roof cover
        bool inPrecipOnActor = false;
        if (precipNow && !isInterior) {
            const auto tnow = std::chrono::steady_clock::now();
            if (wd.lastRoofProbe.time_since_epoch().count() == 0 || (tnow - wd.lastRoofProbe) > 800ms) {
                wd.lastRoofCovered = IsUnderRoof(a);
                wd.lastRoofProbe = tnow;
            }
            inPrecipOnActor = !wd.lastRoofCovered;
        }

        const float soakWaterRate =
            (Settings::secondsToSoakWater.load() > 0.01f) ? (1.f / Settings::secondsToSoakWater.load()) : 1.0f;
        const float soakRainRate =
            (Settings::secondsToSoakRain.load() > 0.01f) ? (1.f / Settings::secondsToSoakRain.load()) : 1.0f;
        const float soakWaterfallRate =
            (Settings::secondsToSoakWaterfall.load() > 0.01f) ? (1.f / Settings::secondsToSoakWaterfall.load()) : 1.0f;
        const float dryRate = (Settings::secondsToDry.load() > 0.01f) ? (1.f / Settings::secondsToDry.load()) : 1.0f;

        float dryMul = 1.0f;
        if (!inWater) {
            const auto now = std::chrono::steady_clock::now();
            if (wd.lastHeatProbe.time_since_epoch().count() == 0 || (now - wd.lastHeatProbe) > 1s) {
                wd.cachedNearHeat = IsNearHeatSource(a, std::max(50.0f, Settings::nearFireRadius.load()));
                wd.lastHeatProbe = now;
            }
            if (wd.cachedNearHeat && !inPrecipOnActor) {
                dryMul = std::max(1.0f, Settings::dryMultiplierNearFire.load());
            }
        }

        bool nearWaterfall = false;
        if (!inWater && Settings::waterfallEnabled.load()) {
            const auto now = std::chrono::steady_clock::now();
            if (wd.lastWaterfallProbe.time_since_epoch().count() == 0 || (now - wd.lastWaterfallProbe) > 800ms) {
                const float r2 = Settings::nearWaterfallRadius.load() * Settings::nearWaterfallRadius.load();
                bool found = false;
                if (auto* cell = a->GetParentCell()) {
                    const RE::NiPoint3 center = a->GetPosition();
                    cell->ForEachReference([&](RE::TESObjectREFR& ref) {
                        if (found) return RE::BSContainer::ForEachResult::kStop;
                        if (&ref == a) return RE::BSContainer::ForEachResult::kContinue;

                        bool plausible = LooksLikeWaterfall(&ref);
                        RE::NiPoint3 bmin{}, bmax{};
                        RE::NiAVObject* root = ref.Get3D();

                        RE::NiPoint3 testPos = ref.GetPosition();
                        if (root && BuildWorldAABB(root, bmin, bmax)) {
                            testPos.x = 0.5f * (bmin.x + bmax.x);
                            testPos.y = 0.5f * (bmin.y + bmax.y);
                            if (LooksLikeTallWaterSheet(bmin, bmax)) plausible = true;
                        }

                        const float r2xy = Settings::nearWaterfallRadius.load() * Settings::nearWaterfallRadius.load();
                        if (Dist2XY(testPos, center) > r2xy) return RE::BSContainer::ForEachResult::kContinue;

                        if (!plausible) {
                            return RE::BSContainer::ForEachResult::kContinue;
                        }

                        if (!ref.Is3DLoaded() || !root) {
                            return RE::BSContainer::ForEachResult::kContinue;
                        }
                        bool requireBelowTop = true;
                        if (BuildWorldAABB(root, bmin, bmax)) {
                            if (ActorHeadZ(a) - bmax.z > 256.0f) {
                                requireBelowTop = false;
                            }
                        }

                        const bool inside = IsInsideWaterfallFX(
                            a, &ref, std::max(0.f, Settings::waterfallWidthPad.load()),
                            std::max(0.f, Settings::waterfallDepthPad.load()),
                            std::max(0.f, Settings::waterfallZPad.load()), requireBelowTop /* vorher true */);

                        if (inside) {
                            found = true;
                            return RE::BSContainer::ForEachResult::kStop;
                        }
                        return RE::BSContainer::ForEachResult::kContinue;
                    });

                }
                wd.cachedInsideWaterfall = found;
                wd.lastWaterfallProbe = now;
            }
            nearWaterfall = wd.cachedInsideWaterfall;
        }

        float w = wd.wetness;

        if (inWater) {
            w += soakWaterRate * dt;
        } else if (nearWaterfall) {
            w += soakWaterfallRate * dt;
        } else if (inPrecipOnActor) {
            const float snowFactor = (IsSnowingCurrent() ? 0.8f : 1.0f);
            w += soakRainRate * snowFactor * dt;
        } else {
            w -= dryRate * dryMul * dt;
        }

        w = clampf(w, 0.f, 1.f);
        w = ApplyExternalSources(a, wd, w);

        wd.wetness = w;

        if (w <= 0.0005f) {
            if (wd.lastAppliedWet > 0.0005f) {
                ApplyWetnessMaterials(a, 0.0f);
                wd.lastAppliedWet = 0.0f;
            }
        } else if (std::abs(wd.lastAppliedWet - w) > 0.0025f) {
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

        if (wet > 0.0005f) {
            const bool anyToggle = Settings::affectSkin.load() || Settings::affectHair.load() ||
                                   Settings::affectArmor.load() || Settings::affectWeapons.load();
            if (!anyToggle) return;
        }

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
            const bool toggledOff = (cat == MatCat::SkinFace && !Settings::affectSkin.load()) ||
                                    (cat == MatCat::Hair && !Settings::affectHair.load()) ||
                                    (cat == MatCat::ArmorClothing && !Settings::affectArmor.load()) ||
                                    (cat == MatCat::Weapon && !Settings::affectWeapons.load());

            if (toggledOff && wet > 0.0005f) {
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
                ++propsTouched;
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

    bool WetController::IsInsideWaterfallFX(const RE::Actor* a, const RE::TESObjectREFR* wfRef, float padX, float padY,
                                            float padZ, bool requireBelowTop) const {
        if (!a || !wfRef) return false;
        auto* root = wfRef->Get3D();
        if (!root) return false;

        RE::NiPoint3 bmin, bmax;
        if (!BuildWorldAABB(root, bmin, bmax)) return false;

        bmin.x -= padX;
        bmax.x += padX;
        bmin.y -= padY;
        bmax.y += padY;
        bmin.z -= padZ;
        bmax.z += padZ;

        const RE::NiPoint3 pFoot = a->GetPosition();
        RE::NiPoint3 pHead = pFoot;
        pHead.z = ActorHeadZ(a);

        auto inside = [&](const RE::NiPoint3& p) {
            return (p.x >= bmin.x && p.x <= bmax.x) && (p.y >= bmin.y && p.y <= bmax.y) &&
                   (p.z >= bmin.z && p.z <= bmax.z);
        };

        if (requireBelowTop) {
            const float topZ = bmax.z;
            if (pFoot.z > topZ + 16.0f && pHead.z > topZ + 16.0f) return false;
        }

        return inside(pFoot) || inside(pHead);
    }

    bool WetController::RayHitsCover(const RE::NiPoint3& from, const RE::NiPoint3& to,
                                     const RE::TESObjectREFR* ignoreRef) const {
        const RE::Actor* a = ignoreRef ? ignoreRef->As<RE::Actor>() : nullptr;
        auto* bw = GetBhkWorldFromActorCell(a);
        if (!bw) {
            return false;
        }

#if SWE_RAY_DEBUG
        // DebugRayScan(bw, from, to);
#endif

        static constexpr RE::COL_LAYER kPrim[] = {RE::COL_LAYER::kLOS, RE::COL_LAYER::kStatic,
                                                  RE::COL_LAYER::kTransparentWall, RE::COL_LAYER::kInvisibleWall};

        for (auto lyr : kPrim) {
            const auto fi = MakeFilterInfo(lyr, 0xFFFF, 0, 0);
            if (CastOnce(bw, from, to, fi, true)) return true;

        }
        return false;
    }

    bool WetController::IsUnderRoof(RE::Actor* a) const {
        if (!a || !IsRainingOrSnowing()) return false;

        const RE::NiPoint3 base = a->GetPosition();
        const float headZ = ActorHeadZ(a) + 5.0f;
        constexpr float toAbove = 4000.0f;
        constexpr float off = 60.0f;

#if SWE_ROOF_SAMPLES == 9
        const RE::NiPoint3 starts[] = {
            {base.x, base.y, headZ},
            {base.x + off, base.y, headZ},
            {base.x - off, base.y, headZ},
            {base.x, base.y + off, headZ},
            {base.x, base.y - off, headZ},
            {base.x + off, base.y + off, headZ},
            {base.x - off, base.y + off, headZ},
            {base.x + off, base.y - off, headZ},
            {base.x - off, base.y - off, headZ},
        };
#elif SWE_ROOF_SAMPLES == 5
        const RE::NiPoint3 starts[] = {
            {base.x, base.y, headZ},       {base.x + off, base.y, headZ}, {base.x - off, base.y, headZ},
            {base.x, base.y + off, headZ}, {base.x, base.y - off, headZ},
        };
#else
        const RE::NiPoint3 starts[] = {
            {base.x, base.y, headZ},
            {base.x + off, base.y, headZ},
            {base.x, base.y + off, headZ},
        };
#endif

        for (const auto& s : starts) {
            const RE::NiPoint3 from{s.x, s.y, headZ + 2.0f};
            const RE::NiPoint3 to{s.x, s.y, headZ + toAbove};
            if (RayHitsCover(from, to, a)) return true;
        }
        return false;
    }

    float WetController::GetGameHours() const {
        auto* cal = RE::Calendar::GetSingleton();
        return cal ? cal->GetDaysPassed() * 24.0f : 0.0f;
    }

    void WetController::SetExternalWetness(RE::Actor* a, std::string key, float value, float durationSec) {
        if (!a) return;
        key = NormalizeKey(std::move(key));
        if (key.empty()) return;
        value = clampf(value, 0.f, 1.f);
        std::scoped_lock l(_mtx);
        auto& wd = _wet[a->GetFormID()];
        auto& src = wd.extSources[key];
        src.value = value;
        if (durationSec > 0.f) {
            src.expiryGameHours = GetGameHours() + (durationSec / 3600.f);
        } else {
            src.expiryGameHours = -1.f;
        }
    }

    void WetController::ClearExternalWetness(RE::Actor* a, std::string key) {
        if (!a) return;
        key = NormalizeKey(std::move(key));
        if (key.empty()) return;
        std::scoped_lock l(_mtx);
        auto itA = _wet.find(a->GetFormID());
        if (itA == _wet.end()) return;
        itA->second.extSources.erase(key);
    }

    float WetController::GetExternalWetness(RE::Actor* a, std::string key) {
        if (!a) return 0.f;
        key = NormalizeKey(std::move(key));
        if (key.empty()) return 0.f;
        std::scoped_lock l(_mtx);
        auto itA = _wet.find(a->GetFormID());
        if (itA == _wet.end()) return 0.f;
        auto it = itA->second.extSources.find(key);
        return (it != itA->second.extSources.end()) ? it->second.value : 0.f;
    }

    float WetController::GetFinalWetnessForActor(RE::Actor* a) {
        if (!a) return 0.f;
        std::scoped_lock l(_mtx);
        auto it = _wet.find(a->GetFormID());
        return (it != _wet.end()) ? it->second.wetness : 0.f;
    }

    float WetController::ApplyExternalSources(RE::Actor* a, WetData& wd, float baseWet) {
        std::scoped_lock l(_mtx);
        const float nowH = GetGameHours();

        for (auto it = wd.extSources.begin(); it != wd.extSources.end();) {
            if (it->second.expiryGameHours >= 0.f && nowH >= it->second.expiryGameHours)
                it = wd.extSources.erase(it);
            else
                ++it;
        }

        if (wd.extSources.empty()) return baseWet;

        float sum = 0.f, mx = 0.f;
        for (auto& [k, s] : wd.extSources) {
            sum += s.value;
            mx = std::max(mx, s.value);
        }

        switch (Settings::externalBlendMode.load()) {
            default:
            case 0:
                return std::max(baseWet, mx);
            case 1:
                return clampf(baseWet + sum, 0.f, 1.f);
            case 2: {
                float rest = std::max(0.f, sum - mx);
                float w = clampf(Settings::externalAddWeight.load(), 0.f, 1.f);
                return clampf(std::max(baseWet, mx) + rest * w, 0.f, 1.f);
            }
        }
    }


    /*
    * =================================
    * Serialization and Deserialization
    * =================================
    */
    void WetController::Serialize(SKSE::SerializationInterface* intfc) {
        std::scoped_lock l(_mtx);

        // Header: Magic + Anzahl
        const std::uint32_t magic = 'SWET';
        intfc->WriteRecordData(&magic, sizeof(magic));

        std::uint32_t count = 0;
        for (auto& [fid, wd] : _wet) {
            if (wd.wetness > 0.0005f || !wd.extSources.empty()) ++count;
        }
        intfc->WriteRecordData(&count, sizeof(count));

        for (auto& [fid, wd] : _wet) {
            if (wd.wetness <= 0.0005f && wd.extSources.empty()) continue;

            intfc->WriteRecordData(&fid, sizeof(fid));

            // Wetness + lastAppliedWet
            intfc->WriteRecordData(&wd.wetness, sizeof(wd.wetness));
            intfc->WriteRecordData(&wd.lastAppliedWet, sizeof(wd.lastAppliedWet));

            // Externe sources
            std::uint16_t n = static_cast<std::uint16_t>(std::min<std::size_t>(wd.extSources.size(), 0xFFFF));
            intfc->WriteRecordData(&n, sizeof(n));
            for (auto& [key, src] : wd.extSources) {
                std::string k = key;
                if (k.size() > 1024) k.resize(1024);
                std::uint16_t klen = static_cast<std::uint16_t>(k.size());
                intfc->WriteRecordData(&klen, sizeof(klen));
                if (klen) intfc->WriteRecordData(k.data(), klen);

                intfc->WriteRecordData(&src.value, sizeof(src.value));
                intfc->WriteRecordData(&src.expiryGameHours, sizeof(src.expiryGameHours));
            }
        }
    }

    void WetController::Deserialize(SKSE::SerializationInterface* intfc, std::uint32_t version, std::uint32_t length) {
        if (version == 1) {
            float playerWet = 0.0f;
            if (length >= sizeof(float)) {
                intfc->ReadRecordData(&playerWet, sizeof(playerWet));
            }
            SetPlayerWetnessSnapshot(playerWet);
            return;
        }

        auto read = [&](void* dst, std::uint32_t sz) -> bool { return intfc->ReadRecordData(dst, sz) == sz; };

        std::uint32_t magic = 0;
        if (!read(&magic, sizeof(magic)) || magic != 'SWET') {
            return;
        }

        std::uint32_t count = 0;
        if (!read(&count, sizeof(count))) return;

        std::scoped_lock l(_mtx);
        _wet.clear();

        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint32_t oldFID = 0;
            if (!read(&oldFID, sizeof(oldFID))) break;

            std::uint32_t newFID = 0;
            if (!intfc->ResolveFormID(oldFID, newFID)) {
                float tmpWet, tmpLast;
                std::uint16_t nsrc = 0;
                if (!read(&tmpWet, sizeof(tmpWet)) || !read(&tmpLast, sizeof(tmpLast)) || !read(&nsrc, sizeof(nsrc)))
                    break;
                for (std::uint16_t s = 0; s < nsrc; ++s) {
                    std::uint16_t klen = 0;
                    if (!read(&klen, sizeof(klen))) break;
                    if (klen) {
                        std::string dump(klen, '\0');
                        if (!read(dump.data(), klen)) break;
                    }
                    float v, exp;
                    if (!read(&v, sizeof(v)) || !read(&exp, sizeof(exp))) break;
                }
                continue;
            }

            float wet = 0.f, last = -1.f;
            std::uint16_t nsrc = 0;
            if (!read(&wet, sizeof(wet)) || !read(&last, sizeof(last)) || !read(&nsrc, sizeof(nsrc))) break;

            WetData wd{};
            wd.wetness = clampf(wet, 0.f, 1.f);
            wd.lastAppliedWet = -1.f;

            for (std::uint16_t s = 0; s < nsrc; ++s) {
                std::uint16_t klen = 0;
                if (!read(&klen, sizeof(klen))) break;
                std::string key;
                if (klen) {
                    key.resize(klen);
                    if (!read(key.data(), klen)) break;
                }
                float v = 0.f, expH = -1.f;
                if (!read(&v, sizeof(v)) || !read(&expH, sizeof(expH))) break;

                std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::tolower(c); });
                key.erase(key.begin(),
                          std::find_if(key.begin(), key.end(), [](unsigned char c) { return !std::isspace(c); }));
                key.erase(
                    std::find_if(key.rbegin(), key.rend(), [](unsigned char c) { return !std::isspace(c); }).base(),
                    key.end());

                wd.extSources[key] = ExternalSource{.value = clampf(v, 0.f, 1.f), .expiryGameHours = expH};
            }

            _wet[newFID] = std::move(wd);
        }
        SKSE::GetTaskInterface()->AddTask([this]() { this->RefreshNow(); });
    }
}
