#include "WetController.h"

#include <algorithm>
#include <functional>

#include "PapyrusAPI.h"
#include "RE/B/BSLightingShaderMaterialBase.h"
#include "RE/B/BSTextureSet.h"
#include "RE/B/bhkCollisionObject.h"
#include "RE/B/bhkPickData.h"
#include "RE/B/bhkWorld.h"
#include "RE/H/hkpWorldRayCastInput.h"
#include "RE/H/hkpWorldRayCastOutput.h"
#include "RE/T/TESObjectCELL.h"
#include "REL/Relocation.h"
#include "Settings.h"

using namespace std::chrono_literals;

#ifndef SWE_RAY_DEBUG
    #define SWE_RAY_DEBUG 0
#endif
#ifndef SWE_WF_DEBUG
    #define SWE_WF_DEBUG 0
#endif

#ifndef SWE_ROOF_SAMPLES
    #define SWE_ROOF_SAMPLES 5
#endif

namespace SWE {
    enum class MatCat { SkinFace, Hair, ArmorClothing, Weapon, Other };

    static inline float clampf(float v, float lo, float hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }

    template <class Fn>
    static void ForEachRefInRange(const RE::NiPoint3& center, float radius, Fn&& fn) {
        if (auto* tes = RE::TES::GetSingleton()) {
            tes->ForEachReferenceInRange(center, radius, [&](RE::TESObjectREFR& ref) { return fn(ref); });
        }
    }

    static inline int CatIndex(MatCat c) {
        switch (c) {
            case MatCat::SkinFace:
                return 0;
            case MatCat::Hair:
                return 1;
            case MatCat::ArmorClothing:
                return 2;
            case MatCat::Weapon:
                return 3;
            default:
                return 2;
        }
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
    static inline std::string lc_norm_path(const char* p) {
        if (!p || !p[0]) return {};
        std::string s(p);
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        std::replace(s.begin(), s.end(), '\\', '/');
        s.erase(std::unique(s.begin(), s.end(), [](char a, char b) { return a == '/' && b == '/'; }), s.end());
        return s;
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
    static bool LooksLikeEyeName(const RE::NiAVObject* o) {
        if (!o) return false;
        const RE::NiAVObject* cur = o;
        for (int i = 0; i < 4 && cur; ++i) {
            if (NameHas(cur, "eyebrow") || NameHas(cur, "brow") || NameHas(cur, "eyelash") || NameHas(cur, "lash"))
                return false;
            if (NameHas(cur, "eye") || NameHas(cur, "eyes") || NameHas(cur, "eyeball") || NameHas(cur, "iris") ||
                NameHas(cur, "pupil"))
                return true;
            cur = cur->parent;
        }
        return false;
    }

    static bool TexLooksLikeEye(RE::BSLightingShaderMaterialBase* mb) {
        if (!mb || !mb->textureSet) return false;
        auto hasEyes = [&](RE::BSTextureSet::Texture t) {
            std::string p = lc_norm_path(mb->textureSet->GetTexturePath(t));
            if (p.empty()) return false;
            if (p.find("brow") != std::string::npos || p.find("lash") != std::string::npos) return false;
            return p.find("/eyes/") != std::string::npos || p.find("_eye") != std::string::npos ||
                   p.find("eyes") != std::string::npos;
        };
        return hasEyes(RE::BSTextureSet::Texture::kDiffuse) || hasEyes(RE::BSTextureSet::Texture::kNormal);
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
    static std::uint32_t ComputeGeomStamp(RE::NiAVObject* root) {
        if (!root) return 0;
        std::uint64_t acc = 1469598103934665603ull;
        std::uint32_t cnt = 0;
        ForEachGeometry(root, [&](RE::BSGeometry* g) {
            if (auto* l = FindLightingProp(g)) {
                ++cnt;
                acc ^= reinterpret_cast<std::uintptr_t>(l);
                acc *= 1099511628211ull;
            }
        });
        return static_cast<std::uint32_t>(cnt) ^ static_cast<std::uint32_t>(acc) ^
               static_cast<std::uint32_t>(acc >> 32);
    }
    static bool IsEyeGeometry(RE::BSGeometry* g, RE::BSLightingShaderProperty* lsp) {
        if (!g) return false;
        if (LooksLikeEyeName(g)) return true;
        if (lsp && lsp->material) {
            if (auto* mb = static_cast<RE::BSLightingShaderMaterialBase*>(lsp->material)) {
                if (TexLooksLikeEye(mb)) return true;
            }
        }
        return false;
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

    static bool LooksLikeWorkFurniture(const RE::TESObjectREFR* r) {
        if (!r) return false;
        const auto lc = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            return s;
        };

        const char* ed = r->GetBaseObject() ? r->GetBaseObject()->GetFormEditorID() : nullptr;
        std::string name = lc(ed ? ed : "");
        const char* keys[] = {"workbench", "forge",   "smelter", "grindstone", "tanning",
                              "alchemy",   "enchant", "cooking", "chopping",   "choppingblock",
                              "sawmill",   "mine",    "mining",  "ore",        "blacksmith"};
        for (auto* k : keys)
            if (name.find(k) != std::string::npos) return true;
        return false;
    }

    static RE::TESObjectREFR* ToRefPtr(const RE::NiPointer<RE::TESObjectREFR>& p) { return p.get(); }

    static RE::TESObjectREFR* ToRefPtr(const RE::ObjectRefHandle& h) {
        auto np = h.get();
        return np ? np.get() : nullptr;
    }

    static bool IsActorWorkingFurniture(const RE::Actor* a) {
        if (!a) return false;

        const auto occ = a->GetOccupiedFurniture();
        RE::TESObjectREFR* ref = ToRefPtr(occ);
        if (!ref) return false;

        return LooksLikeWorkFurniture(ref);
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
    static inline std::string to_string_compat(const char* s) { return s ? std::string(s) : std::string{}; }
    static inline std::string to_string_compat(std::string_view sv) { return std::string(sv); }
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
    
    static inline std::string_view filename_no_ext(std::string_view path) {
        const size_t slash = path.find_last_of('/');
        std::string_view file = (slash == std::string::npos) ? path : path.substr(slash + 1);
        const size_t dot = file.find_last_of('.');
        return (dot == std::string::npos) ? file : file.substr(0, dot);
    }

    static bool has_suffix_token(std::string_view path, std::string_view token) {
        const std::string_view base = filename_no_ext(path);
        if (base.size() < token.size()) return false;
        const size_t pos = base.size() - token.size();
        if (base.substr(pos) != token) return false;
        if (pos == 0) return true;
        const char prev = base[pos - 1];
        return prev == '_' || prev == '-' || prev == '.';
    }

    static bool has_ambiguous_p_suffix(std::string_view path) { return has_suffix_token(path, "p"); }

    static bool contains_word(std::string_view s, std::string_view w) { return s.find(w) != std::string::npos; }

    static bool strong_pbr_signal(std::string_view p) {
        if (contains_word(p, "/pbr/") || contains_word(p, "_pbr") || contains_word(p, "/pbr_")) return true;

        if (has_suffix_token(p, "orm") || has_suffix_token(p, "rma") || has_suffix_token(p, "rmao") ||
            has_suffix_token(p, "rmaos") || has_suffix_token(p, "rmos") || has_suffix_token(p, "mrao") ||
            has_suffix_token(p, "maos"))
            return true;

        if (contains_word(p, "roughness") || contains_word(p, "rough") || contains_word(p, "metalness") ||
            contains_word(p, "metallic") || contains_word(p, "metal"))
            return true;

        return false;
    }

    // TruePBR sets kVertexLighting
    static inline bool IsTruePBR_CS(const RE::BSLightingShaderProperty* lsp) {
        if (!lsp) return false;
        return lsp->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kVertexLighting);
    }
    static bool MaterialLooksPBR(RE::BSLightingShaderMaterialBase* mb) {
        if (!mb) return false;
        RE::BSTextureSet* ts = mb->textureSet.get();
        auto slot = [&](RE::BSTextureSet::Texture t) -> std::string {
            return lc_norm_path(ts ? ts->GetTexturePath(t) : nullptr);
        };

        std::string paths[6] = {
            slot(RE::BSTextureSet::Texture::kDiffuse),         slot(RE::BSTextureSet::Texture::kNormal),
            slot(RE::BSTextureSet::Texture::kSpecular),        slot(RE::BSTextureSet::Texture::kGlowMap),
            slot(RE::BSTextureSet::Texture::kEnvironmentMask), slot(RE::BSTextureSet::Texture::kBacklightMask),
        };

        bool anyStrong = false;
        bool anyAmbigP = false;

        for (const auto& p : paths) {
            if (p.empty()) continue;
            if (strong_pbr_signal(p)) anyStrong = true;
            if (has_ambiguous_p_suffix(p)) anyAmbigP = true;
        }

        // _p only textures are ambiguous, could be Parallax Height or could be ORM
        return anyStrong || (anyAmbigP && anyStrong);
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
    static inline std::uint32_t MakeFilterInfo(RE::COL_LAYER layer, std::uint16_t systemGroup = 0xFFFF,
                                               std::uint8_t subSystemId = 0, std::uint8_t subSystemNoCollide = 0) {
        return (static_cast<std::uint32_t>(layer) & 0x3F) | ((static_cast<std::uint32_t>(subSystemId) & 0x1F) << 6) |
               ((static_cast<std::uint32_t>(subSystemNoCollide) & 0x1F) << 11) |
               ((static_cast<std::uint32_t>(systemGroup) & 0xFFFF) << 16);
    }
    static constexpr RE::COL_LAYER kRoofLayersPrimary[] = {
        RE::COL_LAYER::kStatic,        RE::COL_LAYER::kAnimStatic,  RE::COL_LAYER::kTransparentWall,
        RE::COL_LAYER::kInvisibleWall, RE::COL_LAYER::kTransparent, RE::COL_LAYER::kLOS,
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
    static bool LooksLikeWaterfall(RE::TESObjectREFR* r) {
        if (!r) return false;

        RE::TESForm* base = r->GetBaseObject();
        if (!base) return false;

        auto lc_contains = [](const char* p, std::initializer_list<const char*> needles) -> bool {
            if (!p || !p[0]) return false;
            std::string s(p);
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            for (auto n : needles) {
                if (s.find(n) != std::string::npos) return true;
            }
            return false;
        };

        const char* ed = base->GetFormEditorID();
        const char* model = nullptr;
        if (auto* tm = base->As<RE::TESModel>()) {
            model = tm->GetModel();
        }

        const bool hardName = lc_contains(ed, {"waterfall", "water fall", "fxwaterfall"}) ||
                              lc_contains(model, {"waterfall", "water fall", "fxwaterfall"});

        RE::NiAVObject* root = r->Get3D();
        if (!root) return hardName;

        bool auxName =
            HasAuxKeywords(root) || NameHas(root, "waterfall") || NameHas(root, "water fall") || NameHas(root, "falls");

        RE::NiPoint3 bmin{}, bmax{};
        if (!BuildWorldAABB(root, bmin, bmax)) {
            return hardName;
        }

        const float H = bmax.z - bmin.z;
        const bool particleOnly = MostlyParticleOrEffect(root);
        const bool wideTall = LooksLikeWideTallWaterSheet(bmin, bmax);

        bool match = false;
        if (wideTall) {
            match = hardName || auxName || !particleOnly;
        } else if (particleOnly && H >= 160.f) {
            match = hardName || auxName;
        }

#if SWE_WF_DEBUG
        logger::info(
            "[SWE] WF cand: {:08X} hardName={} auxName={} particleOnly={} H={:.1f} ED='{}' MD='{}'",
            r->GetFormID(), (int)hardName, (int)auxName, (int)particleOnly, H, ed ? ed : "",
            model ? model : "");
        if (match) {
            logger::info("[SWE] WF MATCH: {:08X} ED='{}' MD='{}'", r->GetFormID(), ed ? ed : "", model ? model : "");
        }
#endif

        return match;
    }

    void WetController::Install() {
        _lastTick = std::chrono::steady_clock::now();
        _lastGameHours = GetGameHours();
        _hasLastGameHours = true;
        _carrySkipSec = 0.0;
    }

    void WetController::Start() {
        if (_running.exchange(true)) return;

        _lastTick = std::chrono::steady_clock::now();
        _lastGameHours = GetGameHours();
        _hasLastGameHours = true;
        _carrySkipSec = 0.0;

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

    bool WetController::IsRainingCurrent() const {
        auto* sky = RE::Sky::GetSingleton();
        if (!sky || !sky->currentWeather) return false;
        return sky->currentWeather->data.flags.any(RE::TESWeather::WeatherDataFlag::kRainy);
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

        const bool rainNow = Settings::rainEnabled.load() && IsRainingCurrent();
        const bool snowNow = Settings::snowEnabled.load() && IsSnowingCurrent();
        return rainNow || snowNow;
    }


    void WetController::TickGameThread() {
        if (!Settings::modEnabled.load() || !_running.load()) return;

        float ghNow = GetGameHours();
        if (!_hasLastGameHours) {
            _lastGameHours = ghNow;
            _hasLastGameHours = true;
        }

        float ghDelta = ghNow - _lastGameHours;
        if (ghDelta < 0.0f) ghDelta = 0.0f;
        float ghDeltaSec = ghDelta * 3600.0f;
        _lastGameHours = ghNow;

        const auto now = std::chrono::steady_clock::now();
        const auto wantDelta = std::chrono::milliseconds(std::max(10, Settings::updateIntervalMs.load()));
        const auto elapsed = now - _lastTick;
        if (elapsed < wantDelta) {
            if (auto* ui0 = RE::UI::GetSingleton()) {
                if (ui0->GameIsPaused() || ui0->IsMenuOpen(RE::MainMenu::MENU_NAME)) {
                    _carrySkipSec += ghDeltaSec;
                }
            }
            return;
        }

        float dt = std::chrono::duration<float>(elapsed).count();
        _lastTick = now;
        dt = clampf(dt, 0.0f, 0.2f);

        if (auto* ui = RE::UI::GetSingleton()) {
            if (ui->GameIsPaused() || ui->IsMenuOpen(RE::MainMenu::MENU_NAME)) {
                _carrySkipSec += ghDeltaSec;
                return;
            }
        }

        double effDt = static_cast<double>(dt) + _carrySkipSec + static_cast<double>(ghDeltaSec);
        _carrySkipSec = 0.0;

        const auto overridesSnap = Settings::SnapshotActorOverrides();
        const auto trackedSnap = Settings::SnapshotTrackedActors();

        std::unordered_set<std::uint32_t> allow;
        const bool optIn = Settings::npcOptInOnly.load();
        std::unordered_set<std::uint32_t> allowIDs;

        if (optIn) {
            for (const auto& fs : trackedSnap)
                if (fs.enabled && fs.id) allowIDs.insert(fs.id);
            for (const auto& fs : overridesSnap)
                if (fs.enabled && fs.id) allowIDs.insert(fs.id);
        }

        auto local_id = [](std::uint32_t id) -> std::uint32_t {
            return ((id >> 24) == 0xFEu) ? (id & 0x00000FFFu) : (id & 0x00FFFFFFu);
        };

        auto same_local = [&](std::uint32_t a, std::uint32_t b) -> bool {
            if (!a || !b) return false;
            return local_id(a) == local_id(b);
        };

        auto lower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return s;
        };

        auto get_base_plugin = [](RE::Actor* a) -> std::string {
            if (!a) return {};
            if (auto* ab = a->GetActorBase()) {
                if (auto* f = ab->GetFile(0)) {
                    return to_string_compat(f->GetFilename());
                }
            }
            return {};
        };

        auto isAllowed = [&](RE::Actor* a) -> bool {
            if (!optIn || !a) return true;

            const std::uint32_t refID = a->GetFormID();
            const std::uint32_t baseID = (a->GetActorBase() ? a->GetActorBase()->GetFormID() : 0);

            if (allowIDs.count(refID) || allowIDs.count(baseID)) return true;

            // ESL safe check
            const std::string plugin = lower(get_base_plugin(a));
            if (!plugin.empty()) {
                auto matches = [&](const Settings::FormSpec& fs) {
                    if (!fs.enabled || !fs.id || fs.plugin.empty()) return false;
                    if (lower(fs.plugin) != plugin) return false;
                    return same_local(fs.id, baseID) || same_local(fs.id, refID);
                };
                if (std::any_of(trackedSnap.begin(), trackedSnap.end(), matches)) return true;
                if (std::any_of(overridesSnap.begin(), overridesSnap.end(), matches)) return true;
            }

            return false;
        };


        RE::Actor* player = RE::PlayerCharacter::GetSingleton();
        if (player) UpdateActorWetness(player, static_cast<float>(effDt), overridesSnap, true);

        if (Settings::affectNPCs.load()) {
            if (auto* proc = RE::ProcessLists::GetSingleton()) {

                std::unordered_set<std::uint32_t> allow;
                const bool optIn = Settings::npcOptInOnly.load();
                if (optIn) {
                    for (const auto& fs : trackedSnap)
                        if (fs.enabled && fs.id) allowIDs.insert(fs.id);
                    for (const auto& fs : overridesSnap)
                        if (fs.enabled && fs.id) allowIDs.insert(fs.id);
                }

                auto resolveAutoWet = [&](RE::Actor* a) -> bool {
                    const std::uint32_t refID = a->GetFormID();
                    const std::uint32_t baseID = (a->GetActorBase() ? a->GetActorBase()->GetFormID() : 0);
                    for (const auto& fs : trackedSnap) {
                        if (!fs.id) continue;
                        if ((fs.id == refID) || (fs.id == baseID)) return fs.autoWet;
                    }
                    return true;  // default Automatic
                };

                const int radius = Settings::npcRadius.load();
                const bool useRad = (radius > 0);
                const float radiusSq = static_cast<float>(radius) * static_cast<float>(radius);
                const RE::NiPoint3 pcPos = player ? player->GetPosition() : RE::NiPoint3();

                for (RE::ActorHandle& h : proc->highActorHandles) {
                    RE::Actor* a = h.get().get();
                    if (!a || a == player) continue;

                    const std::uint32_t refID = a->GetFormID();
                    const std::uint32_t baseID = (a->GetActorBase() ? a->GetActorBase()->GetFormID() : 0);
                    const bool selected = isAllowed(a);

                    if (useRad && player) {
                        const float d2 = a->GetPosition().GetSquaredDistance(pcPos);
                        if (d2 > radiusSq) {
                            auto it = _wet.find(refID);
                            if (it != _wet.end() &&
                                (it->second.lastAppliedWet > 0.0005f || it->second.wetness > 0.0005f)) {
                                const float zeros[4]{0, 0, 0, 0};
                                ApplyWetnessMaterials(a, zeros);
                                it->second.wetness = 0.0f;
                                it->second.lastAppliedWet = 0.0f;
                                it->second.lastAppliedCat[0] = it->second.lastAppliedCat[1] =
                                    it->second.lastAppliedCat[2] = it->second.lastAppliedCat[3] = 0.0f;
                                it->second.extSources.clear();
                            }
                            continue;
                        }
                    }

                    if (!selected) {
                        auto it = _wet.find(refID);
                        if (it != _wet.end() && (it->second.lastAppliedWet > 0.0005f || it->second.wetness > 0.0005f)) {
                            const float zeros[4]{0, 0, 0, 0};
                            ApplyWetnessMaterials(a, zeros);
                            it->second.wetness = 0.0f;
                            it->second.lastAppliedWet = 0.0f;
                            it->second.lastAppliedCat[0] = it->second.lastAppliedCat[1] = it->second.lastAppliedCat[2] =
                                it->second.lastAppliedCat[3] = 0.0f;
                            it->second.extSources.clear();
                        }
                        continue;
                    }

                    const bool autoWet = resolveAutoWet(a);
                    const bool manualMode = !autoWet;
                    const bool allowEnvWet = autoWet;

                    UpdateActorWetness(a, static_cast<float>(effDt), overridesSnap, allowEnvWet, manualMode);
                }
            }
        }
    }

    void WetController::UpdateActorWetness(RE::Actor* a, float dt, const std::vector<Settings::FormSpec>& overrides, bool allowEnvWet, bool manualMode) {
        if (!a) return;

        auto getOverride = [&](float& outW, std::uint8_t& outMask) -> bool {
            if (!manualMode) return false;
            const std::uint32_t refID = a->GetFormID();
            const std::uint32_t baseID = (a->GetActorBase() ? a->GetActorBase()->GetFormID() : 0);
            for (const auto& fs : overrides) {
                if (!fs.enabled || fs.id == 0) continue;
                if (fs.id == refID || fs.id == baseID) {
                    outW = clampf(fs.value, 0.f, 1.f);
                    outMask = (fs.mask & 0x0F);
                    return true;
                }
            }
            return false;
        };

        auto& wd = _wet[a->GetFormID()];
        wd.lastSeen = std::chrono::steady_clock::now();

        const bool inWater = allowEnvWet && IsActorWetByWater(a);
        const bool precipRain = allowEnvWet && Settings::rainEnabled.load() && IsRainingCurrent();
        const bool precipSnow = allowEnvWet && Settings::snowEnabled.load() && IsSnowingCurrent();
        const bool precipNow = (precipRain || precipSnow);

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
        const float soakSnowRate =
            (Settings::secondsToSoakSnow.load() > 0.01f) ? (1.f / Settings::secondsToSoakSnow.load()) : 1.0f;
        const float soakWaterfallRate =
            (Settings::secondsToSoakWaterfall.load() > 0.01f) ? (1.f / Settings::secondsToSoakWaterfall.load()) : 1.0f;


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
        if (allowEnvWet && !inWater && Settings::waterfallEnabled.load()) {
            const auto now = std::chrono::steady_clock::now();
            if (wd.lastWaterfallProbe.time_since_epoch().count() == 0 || (now - wd.lastWaterfallProbe) > 800ms) {
                // const float r2 = Settings::nearWaterfallRadius.load() * Settings::nearWaterfallRadius.load();
                bool found = false;
                if (auto* cell = a->GetParentCell()) {
                    const RE::NiPoint3 center = a->GetPosition();
                    cell->ForEachReference([&](RE::TESObjectREFR& ref) {
                        if (found) return RE::BSContainer::ForEachResult::kStop;
                        if (&ref == a) return RE::BSContainer::ForEachResult::kContinue;

                        RE::NiPoint3 bmin{}, bmax{};
                        RE::NiAVObject* root = ref.Get3D();

                        auto dist2AABB_XY = [&](const RE::NiPoint3& p) -> float {
                            float dx = 0.f, dy = 0.f;
                            if (p.x < bmin.x)
                                dx = bmin.x - p.x;
                            else if (p.x > bmax.x)
                                dx = p.x - bmax.x;
                            if (p.y < bmin.y)
                                dy = bmin.y - p.y;
                            else if (p.y > bmax.y)
                                dy = p.y - bmax.y;
                            return dx * dx + dy * dy;
                        };

                        float d2xy = FLT_MAX;
                        float dzAbs = FLT_MAX;

                        if (root && BuildWorldAABB(root, bmin, bmax)) {
                            d2xy = dist2AABB_XY(center);
                            const float zc = 0.5f * (bmin.z + bmax.z);
                            dzAbs = std::abs(center.z - zc);
                        } else {
                            const RE::NiPoint3 rp = ref.GetPosition();
                            d2xy = Dist2XY(rp, center);
                            dzAbs = std::abs(rp.z - center.z);
                        }

                        const float r2xy = Settings::nearWaterfallRadius.load() * Settings::nearWaterfallRadius.load();
                        if (d2xy > r2xy) return RE::BSContainer::ForEachResult::kContinue;

                        if (dzAbs > std::max(1200.f, Settings::nearWaterfallRadius.load() * 1.5f))
                            return RE::BSContainer::ForEachResult::kContinue;

                        bool plausible = LooksLikeWaterfall(&ref);

#if SWE_WF_DEBUG
                        {
                            RE::TESForm* base = ref.GetBaseObject();
                            const char* ed = base ? base->GetFormEditorID() : "";
                            const char* mdl = "";
                            if (auto* tm = base ? base->As<RE::TESModel>() : nullptr) mdl = tm->GetModel();

                            logger::info(
                                "[SWE] WF scan: {:08X} FT={} ED='{}' MD='{}' 3D={} plausible={}",
                                ref.GetFormID(), base ? (int)base->GetFormType() : -1, ed ? ed : "", mdl ? mdl : "",
                                ref.Is3DLoaded(), (int)plausible);
                        }
#endif

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

                        const bool inside =
                            IsInsideWaterfallFX(a, &ref, std::max(0.f, Settings::waterfallWidthPad.load()),
                                                std::max(0.f, Settings::waterfallDepthPad.load()),
                                                std::max(0.f, Settings::waterfallZPad.load()), requireBelowTop);

#if SWE_WF_DEBUG
                        if (inside) {
                            logger::info("[SWE] WF inside: {:08X}", ref.GetFormID());
                        }
#endif

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

        float wPrevMax = std::max(std::max(wd.lastAppliedCat[0], wd.lastAppliedCat[1]),
                                  std::max(wd.lastAppliedCat[2], wd.lastAppliedCat[3]));
        float w = std::max(wd.wetness, wPrevMax);

        const bool envDominates = allowEnvWet && (inWater || nearWaterfall || inPrecipOnActor);

        if (inWater) {
            w += soakWaterRate * dt;
        } else if (nearWaterfall) {
            w += soakWaterfallRate * dt;
        } else if (inPrecipOnActor) {
            float inc = 0.0f;
            if (precipRain) inc = std::max(inc, soakRainRate * dt);
            if (precipSnow) inc = std::max(inc, soakSnowRate * dt);
            w += inc;
        }

        {
            const bool actEnabled = Settings::activityWetEnabled.load();
            const int actMask = (Settings::activityCatMask.load() & 0x0F);

            bool condRun = false;
            bool condSneak = false;
            bool condWork = false;

            if (actEnabled && actMask != 0) {
                if (Settings::activityTriggerRunning.load()) {
                    condRun = a->IsRunning() && !inWater;
                }
                if (Settings::activityTriggerSneaking.load()) {
                    condSneak = a->IsSneaking() && !inWater;
                }
                if (Settings::activityTriggerWorking.load()) {
                    condWork = IsActorWorkingFurniture(a) && !inWater;
                    if (!condWork && a->IsPlayerRef()) {
                        if (auto* ui = RE::UI::GetSingleton()) {
                            condWork = ui->IsMenuOpen("Crafting Menu") || ui->IsMenuOpen("Alchemy Menu") ||
                                       ui->IsMenuOpen("Enchanting Menu") || ui->IsMenuOpen("Cooking Menu");
                        }
                    }
                }

                const bool anyAct = condRun || condSneak || condWork;

                const float upRate = (Settings::secondsToSoakActivity.load() > 0.01f)
                                         ? (1.f / Settings::secondsToSoakActivity.load())
                                         : 1.f;
                const float downRate = (Settings::secondsToDryActivity.load() > 0.01f)
                                           ? (1.f / Settings::secondsToDryActivity.load())
                                           : 1.f;

                if (anyAct && !envDominates) {
                    wd.activityLevel = clampf(wd.activityLevel + upRate * dt, 0.f, 1.f);
                } else {
                    wd.activityLevel = clampf(wd.activityLevel - downRate * dt, 0.f, 1.f);
                }

                if (wd.activityLevel > 0.0005f) {
                    auto& src = wd.extSources["__activity"];
                    src.value = wd.activityLevel;
                    src.expiryRemainingSec = -1.f;
                    src.catMask = static_cast<std::uint8_t>(actMask);
                    src.flags = 0;
                } else {
                    wd.extSources.erase("__activity");
                }
            } else {
                wd.activityLevel = 0.f;
                wd.extSources.erase("__activity");
            }
        }

        auto purgeActivity = [&]() {
            wd.activityLevel = 0.0f;
            auto it = wd.extSources.find("__activity");
            if (it != wd.extSources.end()) {
                wd.extSources.erase(it);
            }
        };

        if (envDominates) {
            purgeActivity();
        }

        bool hasOtherExternal = std::any_of(wd.extSources.begin(), wd.extSources.end(), [](const auto& kv) {
            const auto& key = kv.first;
            const auto& src = kv.second;
            if (key == "__activity") return false;
            if (src.expiryRemainingSec == 0.f) return false;
            return src.value > 0.f && (src.catMask & SWE::Papyrus::SWE_CAT_MASK_4BIT) != 0;
        });
        if (hasOtherExternal) {
            purgeActivity();
        }

        w = clampf(w, 0.f, 1.f);
        // wd.baseWetness = w;

        float wetByCat[4]{};
        ComputeWetByCategory(wd, w, wetByCat, dt, envDominates, dryMul);

        float forcedW = -1.0f;
        std::uint8_t forcedMask = 0;
        const bool hasOv = getOverride(forcedW, forcedMask);

        if (hasOv) {
            for (int ci = 0; ci < 4; ++ci) {
                if (forcedMask & (1u << ci)) {
                    wetByCat[ci] = forcedW;
                }
            }
        }

        if (!Settings::affectSkin.load()) wetByCat[0] = 0.0f;
        if (!Settings::affectHair.load()) wetByCat[1] = 0.0f;
        if (!Settings::affectArmor.load()) wetByCat[2] = 0.0f;
        if (!Settings::affectWeapons.load()) wetByCat[3] = 0.0f;

        float wFinal = std::max(std::max(wetByCat[0], wetByCat[1]), std::max(wetByCat[2], wetByCat[3]));
        wd.wetness = wFinal;

        const float prevMax = std::max(std::max(wd.lastAppliedCat[0], wd.lastAppliedCat[1]),
                                       std::max(wd.lastAppliedCat[2], wd.lastAppliedCat[3]));
        if (wFinal <= 0.0005f) {
            if (prevMax > 0.0005f) {
                const float zeros[4]{0, 0, 0, 0};
                ApplyWetnessMaterials(a, zeros);
                wd.lastAppliedCat[0] = wd.lastAppliedCat[1] = wd.lastAppliedCat[2] = wd.lastAppliedCat[3] = 0.f;
                wd.lastAppliedWet = 0.0f;

                wd.simCat[0] = wd.simCat[1] = wd.simCat[2] = wd.simCat[3] = 0.f;
                wd.simInit = true;
            }
        } else {
            bool anyChange = false;
            for (int i = 0; i < 4; ++i) {
                if (std::abs(wd.lastAppliedCat[i] - wetByCat[i]) > 0.0025f) {
                    anyChange = true;
                    break;
                }
            }

            bool geomChanged = false;
            if (!anyChange) {
                const auto now = std::chrono::steady_clock::now();
                if (wd.lastGeomProbe.time_since_epoch().count() == 0 || (now - wd.lastGeomProbe) > 250ms) {
                    RE::NiAVObject* third = a->Get3D();
                    RE::NiAVObject* first = nullptr;
                    if (a->IsPlayerRef() && third) {
                        first = third->GetObjectByName("1st Person");
                        if (!first) first = third->GetObjectByName("1stPerson");
                    }
                    std::uint32_t stamp = 0;
                    if (third) stamp ^= ComputeGeomStamp(third);
                    if (first) stamp ^= ComputeGeomStamp(first);

                    geomChanged = (stamp != wd.lastGeomStamp);
                    wd.lastGeomStamp = stamp;
                    wd.lastGeomProbe = now;
                }
            }

            if (anyChange || geomChanged) {
                ApplyWetnessMaterials(a, wetByCat);
                for (int i = 0; i < 4; ++i) wd.lastAppliedCat[i] = wetByCat[i];
                wd.lastAppliedWet = wFinal;
            }
        }
    }

    void SWE::WetController::ApplyWetnessMaterials(RE::Actor* a, const float wetByCat[4]) {
        if (!a) return;

        RE::NiAVObject* third = a->Get3D();
        RE::NiAVObject* first = nullptr;
        if (a->IsPlayerRef() && third) {
            first = third->GetObjectByName("1st Person");
            if (!first) first = third->GetObjectByName("1stPerson");
        }
        RE::NiAVObject* roots[2] = {third, first};

        const float maxWet = std::max(std::max(wetByCat[0], wetByCat[1]), std::max(wetByCat[2], wetByCat[3]));
        /*
        if (maxWet > 0.0005f) {
            const bool anyToggle = Settings::affectSkin.load() || Settings::affectHair.load() ||
                                   Settings::affectArmor.load() || Settings::affectWeapons.load();
            if (!anyToggle) return;
        }
        */

        const float defMaxGloss = Settings::maxGlossiness.load();
        const float defMaxSpec = Settings::maxSpecularStrength.load();
        const float defMinGloss = std::min(Settings::minGlossiness.load(), defMaxGloss);
        const float defMinSpec = std::min(Settings::minSpecularStrength.load(), defMaxSpec);
        const float defGlBoost = std::min(60.0f, Settings::glossinessBoost.load());
        const float defScBoost = Settings::specularScaleBoost.load();
        const float defSkinHair = std::max(0.1f, Settings::skinHairResponseMul.load());

        auto& wd = _wet[a->GetFormID()];

        int geomsTouched = 0, propsTouched = 0;

        auto touchGeom = [&](RE::BSGeometry* g) {
            auto* lsp = FindLightingProp(g);
            if (!lsp) return;

            if (IsEyeGeometry(g, lsp)) {
                auto it = _matCache.find(lsp);
                if (it != _matCache.end()) {
                    auto* mat = static_cast<RE::BSLightingShaderMaterialBase*>(lsp->material);
                    auto* sp = static_cast<RE::BSShaderProperty*>(lsp);
                    const MatSnapshot& base = it->second;
                    if (mat && sp) {
                        SetSpecularEnabled(sp, base.hadSpecular);
                        mat->materialAlpha = base.baseAlpha;
                        mat->specularPower = base.baseSpecularPower;
                        mat->specularColorScale = base.baseSpecularScale;
                        mat->specularColor = {base.baseSpecR, base.baseSpecG, base.baseSpecB};
                        sp->SetMaterial(mat, true);
                        lsp->DoClearRenderPasses();
                        (void)lsp->SetupGeometry(g);
                        (void)lsp->FinishSetupGeometry(g);
                    }
                }
                return;
            }

            const MatCat cat = ClassifyGeom(g, lsp);
            const bool toggledOff = (cat == MatCat::SkinFace && !Settings::affectSkin.load()) ||
                                    (cat == MatCat::Hair && !Settings::affectHair.load()) ||
                                    (cat == MatCat::ArmorClothing && !Settings::affectArmor.load()) ||
                                    (cat == MatCat::Weapon && !Settings::affectWeapons.load());

            const int ci = CatIndex(cat);
            float wet = std::clamp(wetByCat[ci], 0.0f, 1.0f);

            if (toggledOff) {
                wet = 0.0f;
            }

            const auto& ov = wd.activeOv[ci];
            const float effMaxGloss = (ov.maxGloss >= 0.f) ? std::min(defMaxGloss, ov.maxGloss) : defMaxGloss;
            const float effMaxSpec = (ov.maxSpec >= 0.f) ? std::min(defMaxSpec, ov.maxSpec) : defMaxSpec;
            const float effMinGloss = (ov.minGloss >= 0.f) ? std::max(defMinGloss, ov.minGloss) : defMinGloss;
            const float effMinSpec = (ov.minSpec >= 0.f) ? std::max(defMinSpec, ov.minSpec) : defMinSpec;
            const float effGlBoost = (ov.glossBoost >= 0.f) ? std::min(60.0f, ov.glossBoost) : defGlBoost;
            const float effScBoost = (ov.specBoost >= 0.f) ? ov.specBoost : defScBoost;
            const float catMul = (cat == MatCat::SkinFace || cat == MatCat::Hair)
                                     ? ((ov.skinHairMul >= 0.f) ? std::max(0.1f, ov.skinHairMul) : defSkinHair)
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

            const bool isArmorOrWeap = (cat == MatCat::ArmorClothing || cat == MatCat::Weapon);

            const bool likelyPBR = MaterialLooksPBR(mat);
            const bool csTruePBR = isArmorOrWeap && lsp && IsTruePBR_CS(lsp);

            //const bool pbrMode = Settings::pbrFriendlyMode.load() && (isArmorOrWeap || likelyPBR);
            const bool pbrish = Settings::pbrFriendlyMode.load() && (likelyPBR || csTruePBR);

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
                if (pbrish) {
                    SetSpecularEnabled(sp, base.hadSpecular);
                } else {
                    SetSpecularEnabled(sp, true);
                }
            }


            RE::NiColor newSpec{base.baseSpecR, base.baseSpecG, base.baseSpecB};
            if ((newSpec.red + newSpec.green + newSpec.blue) < 0.05f) {
                if (!pbrish) {
                    newSpec = {0.7f, 0.7f, 0.7f};
                }
            }
            float newGloss = base.baseSpecularPower + wet * effGlBoost * catMul;
            newGloss = std::clamp(newGloss, effMinGloss, effMaxGloss);

            float newScale = base.baseSpecularScale + wet * effScBoost * catMul;
            newScale = std::clamp(newScale, effMinSpec, effMaxSpec);

            // PBR Clearcoat simulation on wetness for armor and weapons
            if (wet > 0.0005f && pbrish && isArmorOrWeap && Settings::pbrClearcoatOnWet.load()) {
                if (sp) SetSpecularEnabled(sp, true);
                if ((mat->specularColor.red + mat->specularColor.green + mat->specularColor.blue) < 0.05f) {
                    mat->specularColor = {Settings::pbrClearcoatSpec.load(), Settings::pbrClearcoatSpec.load(),
                                          Settings::pbrClearcoatSpec.load()};
                }
                const float ccMul = std::clamp(Settings::pbrClearcoatScale.load(), 0.0f, 1.0f);
                newScale = base.baseSpecularScale + (newScale - base.baseSpecularScale) * ccMul;
                newGloss = base.baseSpecularPower + (newGloss - base.baseSpecularPower) * ccMul;
            }

            if (pbrish && isArmorOrWeap) {
                const float amul = std::clamp(Settings::pbrArmorWeapMul.load(), 0.0f, 1.0f);
                const float pbrG = Settings::pbrMaxGlossArmor.load();
                const float pbrS = Settings::pbrMaxSpecArmor.load();

                newGloss = base.baseSpecularPower + (newGloss - base.baseSpecularPower) * amul;
                newScale = base.baseSpecularScale + (newScale - base.baseSpecularScale) * amul;
                newGloss = std::min(newGloss, pbrG);
                newScale = std::min(newScale, pbrS);
            }

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

    void WetController::ComputeWetByCategory(WetData& wd, float baseWet, float outWetByCat[4], float dt,
                                             bool envDominates, float dryMul) {
        std::scoped_lock l(_mtx);

        for (auto it = wd.extSources.begin(); it != wd.extSources.end();) {
            if (it->second.expiryRemainingSec >= 0.f) {
                it->second.expiryRemainingSec -= std::max(0.f, dt);
                if (it->second.expiryRemainingSec <= 0.f) {
                    it = wd.extSources.erase(it);
                    continue;
                }
            }
            ++it;
        }

        if (!wd.simInit) {
            for (int i = 0; i < 4; ++i) wd.simCat[i] = wd.lastAppliedCat[i];
            wd.simInit = true;
        }

        // Important: Environmental wetness sources override everything else
        if (envDominates) {
            for (int i = 0; i < 4; ++i) {
                wd.activeOv[i] = {};
                outWetByCat[i] = baseWet;
                wd.simCat[i] = baseWet;
            }
            wd.simInit = true;
            return;
        }

        float last[4] = {wd.lastAppliedCat[0], wd.lastAppliedCat[1], wd.lastAppliedCat[2], wd.lastAppliedCat[3]};
        float baseByCat[4] = {wd.simCat[0], wd.simCat[1], wd.simCat[2], wd.simCat[3]};

        auto dryStep = [&](float secs) -> float {
            const float r = (secs > 0.01f) ? (1.f / secs) : 1.f;
            return r * dryMul * dt;
        };

        const float secSkin = Settings::secondsToDrySkin.load();
        const float secHair = Settings::secondsToDryHair.load();
        const float secArmor = Settings::secondsToDryArmor.load();
        const float secWeap = Settings::secondsToDryWeapon.load();

        baseByCat[0] = clampf(baseByCat[0] - dryStep(secSkin), 0.f, 1.f);
        baseByCat[1] = clampf(baseByCat[1] - dryStep(secHair), 0.f, 1.f);
        baseByCat[2] = clampf(baseByCat[2] - dryStep(secArmor), 0.f, 1.f);
        baseByCat[3] = clampf(baseByCat[3] - dryStep(secWeap), 0.f, 1.f);

        float passthrough[4] = {0.f, 0.f, 0.f, 0.f};
        bool zeroBase[4] = {false, false, false, false};
        bool noAutoDry[4] = {false, false, false, false};

        auto mergeOv = [&](WetData::CatOverrides& ov, const OverrideParams& sOv) {
            ov.any = true;
            if (sOv.maxGloss >= 0.f)
                ov.maxGloss = (ov.maxGloss < 0.f) ? sOv.maxGloss : std::min(ov.maxGloss, sOv.maxGloss);
            if (sOv.maxSpec >= 0.f) ov.maxSpec = (ov.maxSpec < 0.f) ? sOv.maxSpec : std::min(ov.maxSpec, sOv.maxSpec);
            if (sOv.minGloss >= 0.f)
                ov.minGloss = (ov.minGloss < 0.f) ? sOv.minGloss : std::max(ov.minGloss, sOv.minGloss);
            if (sOv.minSpec >= 0.f) ov.minSpec = (ov.minSpec < 0.f) ? sOv.minSpec : std::max(ov.minSpec, sOv.minSpec);
            if (sOv.glossBoost >= 0.f) ov.glossBoost = std::max(ov.glossBoost, sOv.glossBoost);
            if (sOv.specBoost >= 0.f) ov.specBoost = std::max(ov.specBoost, sOv.specBoost);
            if (sOv.skinHairMul >= 0.f) ov.skinHairMul = std::max(ov.skinHairMul, sOv.skinHairMul);
        };

        for (auto& [k, s] : wd.extSources) {
            if (s.expiryRemainingSec == 0.f) continue;
            const bool isPT = (s.flags & SWE::Papyrus::SWE_FLAG_PASSTHROUGH) != 0;
            const bool zBase = (s.flags & SWE::Papyrus::SWE_FLAG_ZERO_BASE) != 0;
            const bool nad = (s.flags & SWE::Papyrus::SWE_FLAG_NO_AUTODRY) != 0;

            for (int ci = 0; ci < 4; ++ci)
                if (s.catMask & (1u << ci)) {
                    mergeOv(wd.activeOv[ci], s.ov);
                    if (isPT) passthrough[ci] += s.value;
                    if (zBase) zeroBase[ci] = true;
                    if (nad) noAutoDry[ci] = true;
                }
        }

        // ZERO_BASE / NO_AUTODRY
        const bool isDrying = !envDominates;
        for (int ci = 0; ci < 4; ++ci) {
            if (zeroBase[ci]) {
                baseByCat[ci] = 0.f;
            } else if (noAutoDry[ci] && isDrying) {
                baseByCat[ci] = last[ci];
            }
        }

        auto blendOne = [&](int ci) -> float {
            float sum = 0.f, mx = 0.f;
            bool any = false;
            for (auto& [k, s] : wd.extSources) {
                if (s.expiryRemainingSec == 0.f) continue;
                if ((s.flags & SWE::Papyrus::SWE_FLAG_PASSTHROUGH) != 0) continue;
                if ((s.catMask & (1u << ci)) == 0) continue;
                any = true;
                sum += s.value;
                mx = std::max(mx, s.value);
            }
            if (!any) return baseByCat[ci];

            switch (Settings::externalBlendMode.load()) {
                default:
                case 0:
                    return std::max(baseByCat[ci], mx);
                case 1:
                    return clampf(baseByCat[ci] + sum, 0.f, 1.f);
                case 2: {
                    float rest = std::max(0.f, sum - mx);
                    float w = clampf(Settings::externalAddWeight.load(), 0.f, 1.f);
                    return clampf(std::max(baseByCat[ci], mx) + rest * w, 0.f, 1.f);
                }
            }
        };

        for (int ci = 0; ci < 4; ++ci) {
            outWetByCat[ci] = clampf(blendOne(ci) + passthrough[ci], 0.f, 1.f);
            wd.simCat[ci] = outWetByCat[ci];
        }
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
        if (!a) return false;
        const bool anyPrecip = (Settings::rainEnabled.load() && IsRainingCurrent()) ||
                               (Settings::snowEnabled.load() && IsSnowingCurrent());
        if (!anyPrecip) return false;

        const RE::NiPoint3 base = a->GetPosition();
        const float headZ = ActorHeadZ(a) + 5.0f;
        constexpr float toAbove = 4000.0f;
        constexpr float off = 10.0f;

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

    float WetController::GetSubmergedLevel(RE::Actor* a) const { return ComputeSubmergeLevel(a); }
    bool WetController::IsActorWetByWater(RE::Actor* a) const { return SWE::IsActorWetByWater(a); }
    bool WetController::IsWetWeatherAround(RE::Actor* a) const {
        if (!a) return false;
        auto* cell = a->GetParentCell();
        if (cell && cell->IsInteriorCell() && Settings::ignoreInterior.load()) return false;

        const bool rainNow = Settings::rainEnabled.load() && IsRainingCurrent();
        const bool snowNow = Settings::snowEnabled.load() && IsSnowingCurrent();
        return rainNow || snowNow;
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
        src.expiryRemainingSec = (durationSec > 0.f) ? durationSec : -1.f;
        if (src.catMask == 0) {
            src.catMask = SWE::Papyrus::SWE_CAT_SKIN_FACE;
        }
    }

    void WetController::SetExternalWetnessMask(RE::Actor* a, const std::string& key, float intensity01,
                                               float durationSec, std::uint8_t catMask, std::uint32_t flags) {
        if (!a) return;
        if ((catMask & SWE::Papyrus::SWE_CAT_MASK_4BIT) == 0) return;

        std::string normKey = NormalizeKey(key);
        if (normKey.empty()) return;

        std::scoped_lock l(_mtx);
        auto& wd = _wet[a->GetFormID()];

        ExternalSource& src = wd.extSources[normKey];
        src.value = clampf(intensity01, 0.f, 1.f);
        src.expiryRemainingSec = (durationSec > 0.f) ? durationSec : -1.f;
        src.catMask = static_cast<std::uint8_t>(catMask & SWE::Papyrus::SWE_CAT_MASK_4BIT);
        src.flags = flags;
    }

    float WetController::GetBaseWetnessForActor(RE::Actor* a) {
        if (!a) return 0.f;
        std::scoped_lock l(_mtx);
        auto it = _wet.find(a->GetFormID());
        return (it != _wet.end()) ? it->second.baseWetness : 0.f;
    }

    void WetController::SetExternalWetnessEx(RE::Actor* a, std::string key, float value, float durationSec,
                                             std::uint8_t catMask, const OverrideParams& ov) {
        if (!a) return;
        key = NormalizeKey(std::move(key));
        if (key.empty()) return;

        std::scoped_lock l(_mtx);
        auto& src = _wet[a->GetFormID()].extSources[key];
        src.value = clampf(value, 0.f, 1.f);
        src.expiryRemainingSec = (durationSec > 0.f) ? durationSec : -1.f;
        src.catMask = static_cast<std::uint8_t>(catMask & SWE::Papyrus::SWE_CAT_MASK_4BIT);
        // Important: do NOT touch src.flags -> keep existing flags
        src.ov = ov;
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

    /*
     * =================================
     * Serialization and Deserialization
     * =================================
     */
    void WetController::Serialize(SKSE::SerializationInterface* intfc) {
        std::scoped_lock l(_mtx);

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
                float remainSec = src.expiryRemainingSec;
                intfc->WriteRecordData(&remainSec, sizeof(remainSec));

                intfc->WriteRecordData(&src.catMask, sizeof(src.catMask));

                std::uint32_t flags = src.flags;
                intfc->WriteRecordData(&flags, sizeof(flags));

                intfc->WriteRecordData(&src.ov.maxGloss, sizeof(float));
                intfc->WriteRecordData(&src.ov.maxSpec, sizeof(float));
                intfc->WriteRecordData(&src.ov.minGloss, sizeof(float));
                intfc->WriteRecordData(&src.ov.minSpec, sizeof(float));
                intfc->WriteRecordData(&src.ov.glossBoost, sizeof(float));
                intfc->WriteRecordData(&src.ov.specBoost, sizeof(float));
                intfc->WriteRecordData(&src.ov.skinHairMul, sizeof(float));
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
                    float v, expLike;
                    if (!read(&v, sizeof(v)) || !read(&expLike, sizeof(expLike))) break;

                    if (version >= 3) {
                        std::uint8_t mask;
                        float ftmp;
                        if (!read(&mask, sizeof(mask))) break;
                        if (!read(&ftmp, sizeof(float))) break;
                        if (!read(&ftmp, sizeof(float))) break;
                        if (!read(&ftmp, sizeof(float))) break;
                        if (!read(&ftmp, sizeof(float))) break;
                        if (!read(&ftmp, sizeof(float))) break;
                        if (!read(&ftmp, sizeof(float))) break;
                        if (!read(&ftmp, sizeof(float))) break;
                    }
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
                std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::tolower(c); });
                key.erase(key.begin(),
                          std::find_if(key.begin(), key.end(), [](unsigned char c) { return !std::isspace(c); }));
                key.erase(
                    std::find_if(key.rbegin(), key.rend(), [](unsigned char c) { return !std::isspace(c); }).base(),
                    key.end());

                ExternalSource src{};

                float v = 0.f;
                if (!read(&v, sizeof(v))) break;
                src.value = clampf(v, 0.f, 1.f);

                float expLike = -1.f;
                if (!read(&expLike, sizeof(expLike))) break;

                std::uint8_t mask = 0x0F;
                if (!read(&mask, sizeof(mask))) break;
                src.catMask = (mask & 0x0F) ? (mask & 0x0F) : 0x0F;

                if (version >= 5) {
                    std::uint32_t f = 0;
                    if (!read(&f, sizeof(f))) break;
                    src.flags = f;
                } else {
                    src.flags = 0;
                }

                if (version >= 4) {
                    src.expiryRemainingSec = expLike;
                } else {
                    const float nowH = GetGameHours();
                    if (expLike >= 0.f)
                        src.expiryRemainingSec = std::max(0.f, (expLike - nowH) * 3600.f);
                    else
                        src.expiryRemainingSec = -1.f;
                }

                if (version >= 3) {
                    float ftmp;
                    if (!read(&ftmp, sizeof(float))) break;
                    src.ov.maxGloss = ftmp;
                    if (!read(&ftmp, sizeof(float))) break;
                    src.ov.maxSpec = ftmp;
                    if (!read(&ftmp, sizeof(float))) break;
                    src.ov.minGloss = ftmp;
                    if (!read(&ftmp, sizeof(float))) break;
                    src.ov.minSpec = ftmp;
                    if (!read(&ftmp, sizeof(float))) break;
                    src.ov.glossBoost = ftmp;
                    if (!read(&ftmp, sizeof(float))) break;
                    src.ov.specBoost = ftmp;
                    if (!read(&ftmp, sizeof(float))) break;
                    src.ov.skinHairMul = ftmp;
                } else {
                    src.ov = {};
                }

                wd.extSources[key] = std::move(src);
            }

            _wet[newFID] = std::move(wd);
        }

        SKSE::GetTaskInterface()->AddTask([this]() { this->RefreshNow(); });
    }

}