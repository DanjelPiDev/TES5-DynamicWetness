#include "OverlayMgr.h"

#include <algorithm>
#include <cctype>

#include "RE/B/BSLightingShaderMaterialBase.h"
#include "RE/B/BSTextureSet.h"

using std::string;
using std::vector;

namespace fs = std::filesystem;

namespace SWE {
    static constexpr bool kWFR_Mode = true;

    constexpr uint16_t kKey_TextureSet = 9;
    constexpr uint8_t kIdx_Diffuse = 0;
    constexpr uint8_t kIdx_SpecularTex = 7;
    constexpr uint8_t kIdx_BacklightMask = 8;

    constexpr uint16_t kKey_Glossiness = 2;
    constexpr uint16_t kKey_SpecularStr = 3;

    constexpr uint32_t SLOT_BODY = 0x04;
    constexpr uint32_t SLOT_HANDS = 0x08;
    constexpr uint32_t SLOT_FEET = 0x80;

    constexpr uint8_t kNoSubIndex = 0xFF;

    struct SVString : IOverrideInterface::SetVariant {
        std::string val;
        explicit SVString(std::string v) : val(std::move(v)) {}
        Type GetType() override { return Type::String; }
        const char* String() override { return val.c_str(); }
    };

    struct SVFloat : IOverrideInterface::SetVariant {
        float val;
        explicit SVFloat(float v) : val(v) {}
        Type GetType() override { return Type::Float; }
        float Float() override { return val; }
    };

    struct GVString : IOverrideInterface::GetVariant {
        std::string out;
        void Int(skee_i32) override {}
        void Float(float) override {}
        void String(const char* s) override { out = s ? s : ""; }
        void Bool(bool) override {}
        void TextureSet(const BGSTextureSet*) override {}
    };

    static inline string tolower_copy(string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    }

    static bool IsSpecPath(const std::string& p) {
        std::string s = p;
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return s.size() >= 6 && s.rfind("_s.dds") == s.size() - 6;
    }

    static std::string ToGameTexPath(std::string p) {
        std::transform(p.begin(), p.end(), p.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        for (auto& ch : p)
            if (ch == '\\') ch = '/';

        if (p.rfind("data/", 0) == 0) p.erase(0, 5);

        if (p.rfind("textures/", 0) != 0) {
            spdlog::warn("[SWE] OverlayMgr: unexpected path '{}' (expected to start with textures/)", p);
        }
        return p;
    }

    static int EnableSpecularOnSkinTree(RE::NiAVObject* root) {
        if (!root) return 0;
        int count = 0;
        std::function<void(RE::NiAVObject*)> rec = [&](RE::NiAVObject* o) {
            if (auto* g = o->AsGeometry()) {
                auto& rd = g->GetGeometryRuntimeData();
                for (auto& prop : rd.properties) {
                    if (!prop) continue;
                    if (auto* l = skyrim_cast<RE::BSLightingShaderProperty*>(prop.get())) {
                        if (auto* mat =
                                l->material ? static_cast<RE::BSLightingShaderMaterialBase*>(l->material) : nullptr) {
                            auto* sp = static_cast<RE::BSShaderProperty*>(l);
                            sp->flags.set(RE::BSShaderProperty::EShaderPropertyFlag::kSpecular);
                            sp->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kSpecular, true);

                            mat->specularColor = {1.0f, 1.0f, 1.0f};
                            mat->specularColorScale = std::max(mat->specularColorScale, 10.0f);
                            mat->specularPower = std::max(mat->specularPower, 800.0f);

                            spdlog::debug("[SWE] specColor=({:.2f},{:.2f},{:.2f}) scale={:.1f} power={:.1f}",
                                          mat->specularColor.red, mat->specularColor.green, mat->specularColor.blue,
                                          mat->specularColorScale, mat->specularPower);

                            l->SetMaterial(mat, true);
                            l->DoClearRenderPasses();
                            (void)l->SetupGeometry(g);
                            (void)l->FinishSetupGeometry(g);
                            ++count;
                        }
                    }
                }
            }
            if (auto* n = o->AsNode()) {
                for (auto& ch : n->GetChildren())
                    if (ch) rec(ch.get());
            }
        };
        rec(root);
        return count;
    }

    static std::string ToOverlayDiffusePathOrEmpty(std::string in) {
        std::string p = ToGameTexPath(std::move(in));

        if (IsSpecPath(p)) {
            std::string q = p;
            q.replace(q.size() - 6, 6, "_d.dds");

            std::error_code ec;
            if (std::filesystem::exists(std::filesystem::path("data") / q, ec)) {
                spdlog::debug("[SWE] OverlayMgr: remapped spec '{}' -> diffuse '{}'", p, q);
                return q;
            } else {
                spdlog::warn(
                    "[SWE] OverlayMgr: '{}' looks like spec; matching diffuse '{}' not found -> skipping overlay", p,
                    q);
                return {};
            }
        }

        return p;
    }

    static void DebugDumpSkinOverrides(RE::Actor* a, bool female, IOverrideInterface* ni) {
        if (!a || !ni) return;
        auto* refr = reinterpret_cast<TESObjectREFR*>(a);

        auto probe = [&](bool firstPerson, const char* tag) {
            GVString gv;
            ni->GetSkinOverride(refr, female, firstPerson, SLOT_BODY, kKey_TextureSet, kIdx_SpecularTex, gv);
            spdlog::info("[SWE] {} BODY spec idx7 = '{}'", tag, gv.out);
            gv.out.clear();
            ni->GetSkinOverride(refr, female, firstPerson, SLOT_HANDS, kKey_TextureSet, kIdx_SpecularTex, gv);
            spdlog::info("[SWE] {} HAND spec idx7 = '{}'", tag, gv.out);
        };
        probe(false, "3rd");
        if (a->IsPlayerRef()) probe(true, "1st");
    }

    static void forEachSkinGeom(RE::NiAVObject* root, const std::function<void(RE::BSGeometry* g)>& fn) {
        if (!root) return;
        std::function<void(RE::NiAVObject*)> dfs = [&](RE::NiAVObject* o) {
            if (auto* g = o->AsGeometry()) {
                for (auto& prop : g->GetGeometryRuntimeData().properties) {
                    if (!prop) continue;
                    if (auto* l = skyrim_cast<RE::BSLightingShaderProperty*>(prop.get())) {
                        auto* mat = l->material ? static_cast<RE::BSLightingShaderMaterialBase*>(l->material) : nullptr;
                        auto* ts = mat ? mat->textureSet.get() : nullptr;
                        if (!ts) continue;

                        const char* d = ts->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse);
                        std::string dp = d ? d : "";
                        std::transform(dp.begin(), dp.end(), dp.begin(), ::tolower);
                        const bool looksLikeSkin =
                            dp.find("actors/character/") != std::string::npos ||
                            dp.find("female") != std::string::npos || dp.find("male") != std::string::npos ||
                            dp.find("skin") != std::string::npos || dp.find("body") != std::string::npos ||
                            dp.find("hands") != std::string::npos || dp.find("feet") != std::string::npos;

                        if (looksLikeSkin) fn(g);
                    }
                }
            }
            if (auto* n = o->AsNode())
                for (auto& ch : n->GetChildren())
                    if (ch) dfs(ch.get());
        };
        dfs(root);
    }

    static void ApplyWetViaNiOverride(RE::Actor* a, bool female, const std::string& bodySpecPath,
                                      const std::string& handSpecPath, float glossiness, float specularStrength,
                                      IOverrideInterface* ov, IActorUpdateManager* aum) {
        if (!a || !ov) return;
        auto* refr = reinterpret_cast<TESObjectREFR*>(a);

        const std::string bodySpec = bodySpecPath.empty() ? "" : ToGameTexPath(bodySpecPath);
        const std::string handSpec = handSpecPath.empty() ? "" : ToGameTexPath(handSpecPath);

        if (!bodySpec.empty()) {
            SVString s(bodySpec);
            ov->AddSkinOverride(refr, female, false, SLOT_BODY, kKey_TextureSet, kIdx_SpecularTex, s);
            ov->AddSkinOverride(refr, female, false, SLOT_BODY, kKey_TextureSet, kIdx_BacklightMask, s);
            if (a->IsPlayerRef()) {
                ov->AddSkinOverride(refr, female, true, SLOT_BODY, kKey_TextureSet, kIdx_SpecularTex, s);
                ov->AddSkinOverride(refr, female, true, SLOT_BODY, kKey_TextureSet, kIdx_BacklightMask, s);
            }
        }
        if (!handSpec.empty()) {
            SVString s(handSpec);
            ov->AddSkinOverride(refr, female, false, SLOT_HANDS, kKey_TextureSet, kIdx_SpecularTex, s);
            if (a->IsPlayerRef())
                ov->AddSkinOverride(refr, female, true, SLOT_HANDS, kKey_TextureSet, kIdx_SpecularTex, s);
        }

        {
            SVFloat g(glossiness);
            ov->AddSkinOverride(refr, female, false, (SLOT_BODY | SLOT_HANDS | SLOT_FEET), kKey_Glossiness, kNoSubIndex,
                                g);
            if (a->IsPlayerRef())
                ov->AddSkinOverride(refr, female, true, (SLOT_BODY | SLOT_HANDS | SLOT_FEET), kKey_Glossiness,
                                    kNoSubIndex, g);
        }
        {
            SVFloat s(specularStrength);
            ov->AddSkinOverride(refr, female, false, (SLOT_BODY | SLOT_HANDS | SLOT_FEET), kKey_SpecularStr,
                                kNoSubIndex, s);
            if (a->IsPlayerRef())
                ov->AddSkinOverride(refr, female, true, (SLOT_BODY | SLOT_HANDS | SLOT_FEET), kKey_SpecularStr,
                                    kNoSubIndex, s);
        }

        ov->SetSkinProperties(refr, true);
        if (aum) {
            aum->AddSkinOverrideUpdate(a->GetFormID());
            aum->Flush();
        }
    }

    static int ApplyWetDirectToSkin(RE::Actor* a, const std::string& bodySpecPath, const std::string& handSpecPath,
                                    float glossiness, float specularStrength, bool forceWhiteDebug = false) {
        if (!a) return 0;
        auto setOnTree = [&](RE::NiAVObject* root) -> int {
            int changed = 0;
            forEachSkinGeom(root, [&](RE::BSGeometry* g) {
                for (auto& p : g->GetGeometryRuntimeData().properties) {
                    if (!p) continue;
                    if (auto* l = skyrim_cast<RE::BSLightingShaderProperty*>(p.get())) {
                        auto* mat = l->material ? static_cast<RE::BSLightingShaderMaterialBase*>(l->material) : nullptr;
                        auto* ts = mat ? mat->textureSet.get() : nullptr;
                        if (!ts) continue;

                        std::string specPath =
                            forceWhiteDebug ? "textures/effects/fxwhite.dds" : ToGameTexPath(bodySpecPath);
                        if (!specPath.empty()) {
                            ts->SetTexturePath(RE::BSTextureSet::Texture::kSpecular, specPath.c_str());
                            ts->SetTexturePath(RE::BSTextureSet::Texture::kBacklightMask, specPath.c_str());
                            const char* p7 = ts->GetTexturePath(RE::BSTextureSet::Texture::kSpecular);
                            const char* p8 = ts->GetTexturePath(RE::BSTextureSet::Texture::kBacklightMask);
                            spdlog::debug("[SWE] geom spec(7)='{}' backlight(8)='{}'", p7 ? p7 : "", p8 ? p8 : "");
                        }

                        auto* sp = static_cast<RE::BSShaderProperty*>(l);
                        sp->flags.set(RE::BSShaderProperty::EShaderPropertyFlag::kSpecular);

                        mat->specularPower = std::max(mat->specularPower, glossiness);
                        mat->specularColorScale = std::max(mat->specularColorScale, specularStrength);

                        l->SetMaterial(mat, true);
                        l->DoClearRenderPasses();
                        (void)l->SetupGeometry(g);
                        (void)l->FinishSetupGeometry(g);
                        ++changed;
                    }
                }
            });
            return changed;
        };

        int total = 0;
        if (auto* third = a->Get3D()) total += setOnTree(third);
        if (a->IsPlayerRef())
            if (auto* pc = a->As<RE::PlayerCharacter>())
                if (auto* first = pc->Get3D(true)) total += setOnTree(first);

        spdlog::debug("[SWE] ApplyWetDirectToSkin patched {} geoms (forceWhiteDebug={})", total, forceWhiteDebug);
        return total;
    }

    void OverlayMgr::OnInterfaceMap(IInterfaceMap* map) {
        if (!map) return;

        _ovl = static_cast<IOverlayInterface*>(map->QueryInterface("NiOverride-Overlay"));
        if (!_ovl) _ovl = static_cast<IOverlayInterface*>(map->QueryInterface("OVERLAY"));

        _aum = static_cast<IActorUpdateManager*>(map->QueryInterface("NiOverride-ActorUpdate"));
        if (!_aum) _aum = static_cast<IActorUpdateManager*>(map->QueryInterface("ACTORUPDATE"));

        _ni = static_cast<IOverrideInterface*>(map->QueryInterface("NiOverride"));
        if (!_ni) _ni = static_cast<IOverrideInterface*>(map->QueryInterface("SKEE"));

        if (_ovl && !_cbRegistered) {
            _ovl->RegisterInstallCallback("SWE", &OverlayMgr::OverlayInstalledCB);
            _cbRegistered = true;
        }

        spdlog::info("[SWE] IF: ovl={}, aum={}, ni={}", (void*)_ovl, (void*)_aum, (void*)_ni);
    }

    void OverlayMgr::ensureInterfaces() {
    }

    bool OverlayMgr::isFemale(const RE::Actor* a) {
        if (!a) return false;
        auto* base = a->GetActorBase();
        if (!base) return false;
        return base->GetSex() == RE::SEX::kFemale;
    }

    vector<string> OverlayMgr::list_dds(const fs::path& dir) {
        vector<string> out;
        std::error_code ec;
        if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return out;
        for (auto& de : fs::directory_iterator(dir, ec)) {
            if (de.is_regular_file()) {
                auto p = de.path();
                auto ext = tolower_copy(p.extension().string());
                if (ext == ".dds") {
                    string s = p.generic_string();
                    spdlog::info("Texture: '{}'", s);
                    std::transform(s.begin(), s.end(), s.begin(),
                                   [](unsigned char c) { return (char)std::tolower(c); });
                    out.push_back(s);
                }
            }
        }
        return out;
    }

    bool OverlayMgr::isOverlayNodeName(std::string_view n) {
        std::string s(n);
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return s.find("ovl") != std::string::npos || s.find("overlay") != std::string::npos;
    }

    void OverlayMgr::forEachOverlayGeom(RE::NiAVObject* root,
                                        const std::function<void(RE::BSGeometry* g, bool isHand)>& fn) {
        if (!root) return;
        std::function<void(RE::NiAVObject*, bool, bool)> rec = [&](RE::NiAVObject* o, bool inOverlay, bool isHand) {
            if (!o) return;
            std::string name = tolower_copy(o->name.c_str());
            bool nowOverlay = inOverlay || isOverlayNodeName(name);
            bool nowIsHand = isHand || (name.find("hand") != std::string::npos);

            if (auto* g = o->AsGeometry()) {
                if (nowOverlay) fn(g, nowIsHand);
            }
            if (auto* n = o->AsNode()) {
                for (auto& ch : n->GetChildren()) {
                    if (ch) rec(ch.get(), nowOverlay, nowIsHand);
                }
            }
        };
        rec(root, false, false);
    }

    void OverlayMgr::setDiffuseOnGeometry(RE::BSGeometry* g, const std::string& ddsPath) {
        if (!g) return;

        const std::string gamePath = ToOverlayDiffusePathOrEmpty(ddsPath);
        if (gamePath.empty()) {
            spdlog::warn("[SWE] OverlayMgr: skipping overlay '{}'", ddsPath);
            return;
        }

        auto& rdata = g->GetGeometryRuntimeData();
        for (auto& p : rdata.properties) {
            if (!p) continue;
            if (auto* l = skyrim_cast<RE::BSLightingShaderProperty*>(p.get())) {
                auto* mat = l->material ? static_cast<RE::BSLightingShaderMaterialBase*>(l->material) : nullptr;
                if (!mat) continue;
                auto* ts = mat->textureSet.get();
                if (!ts) continue;

                ts->SetTexturePath(RE::BSTextureSet::Texture::kDiffuse, gamePath.c_str());

                l->SetMaterial(mat, true);
                l->DoClearRenderPasses();
                (void)l->SetupGeometry(g);
                (void)l->FinishSetupGeometry(g);
            }
        }
    }

    void OverlayMgr::OverlayInstalledCB(::TESObjectREFR* ref, ::NiAVObject* node) {
        if (kWFR_Mode) return;
        auto* refRE = reinterpret_cast<RE::TESObjectREFR*>(ref);
        auto* a = refRE ? refRE->As<RE::Actor>() : nullptr;
        if (!a || !node) return;

        auto* mgr = OverlayMgr::Get();
        std::lock_guard lk(mgr->_mtx);

        auto it = mgr->_actors.find(a->GetFormID());
        if (it == mgr->_actors.end()) return;

        const auto& st = it->second;
        if (!st.active) return;

        int applied = 0;
        mgr->forEachOverlayGeom(reinterpret_cast<RE::NiAVObject*>(node), [&](RE::BSGeometry* g, bool isHand) {
            const std::string& pick = isHand ? st.chosenHand : st.chosenBody;
            if (!pick.empty()) {
                setDiffuseOnGeometry(g, pick);
                ++applied;
            }
        });

        if (applied > 0 && mgr->_aum) {
            mgr->_aum->AddOverlayUpdate(a->GetFormID());
            mgr->_aum->Flush();
        }
        spdlog::debug("[SWE] OverlayInstalledCB: applied={} (actor={:08X})", applied, a->GetFormID());
    }

    void OverlayMgr::ensureOverlays(RE::Actor* a, ActorState& st) {
        if (kWFR_Mode) return;
        if (!_ovl || !a) return;
        if (!st.overlaysAdded) {
            _ovl->AddOverlays(reinterpret_cast<::TESObjectREFR*>(a), true);
            st.overlaysAdded = true;
            if (_aum) {
                _aum->AddOverlayUpdate(a->GetFormID());
                _aum->Flush();
            }
        }
    }

    void OverlayMgr::pickTexturesIfNeeded(ActorState& st) {
        if (!st.chosenBody.empty() || !st.chosenHand.empty()) return;

        const char* sexDir = st.female ? "female" : "male";
        fs::path base = fs::path("data") / "textures" / "dynamicwetness" / sexDir;

        auto pick_random = [](std::vector<std::string>& v) -> std::string {
            if (v.empty()) return {};
            std::mt19937 rng{std::random_device{}()};
            return v[std::uniform_int_distribution<int>(0, (int)v.size() - 1)(rng)];
        };

        if (_useBody) {
            auto bodyAll = list_dds(base / "body");
            std::vector<std::string> bodySpec;
            for (auto& s : bodyAll)
                if (IsSpecPath(s)) bodySpec.push_back(s);
            st.chosenBody = pick_random(bodySpec);
            if (st.chosenBody.empty()) spdlog::warn("[SWE] No *_s.dds for BODY found in {}", (base / "body").string());
        }
        if (_useHand) {
            auto handAll = list_dds(base / "hand");
            std::vector<std::string> handSpec;
            for (auto& s : handAll)
                if (IsSpecPath(s)) handSpec.push_back(s);
            st.chosenHand = pick_random(handSpec);
            if (st.chosenHand.empty()) spdlog::warn("[SWE] No *_s.dds for HAND found in {}", (base / "hand").string());
        }
    }

    static void RevertWetViaNiOverride(RE::Actor* a, bool female, IOverrideInterface* ov, IActorUpdateManager* aum) {
        if (!a || !ov) return;
        auto* refr = reinterpret_cast<TESObjectREFR*>(a);
        const skee_u32 mask = (SLOT_BODY | SLOT_HANDS | SLOT_FEET);

        ov->RemoveSkinOverride(refr, female, false, mask, kKey_TextureSet, kIdx_SpecularTex);
        ov->RemoveSkinOverride(refr, female, true, mask, kKey_TextureSet, kIdx_SpecularTex);

        ov->RemoveSkinOverride(refr, female, false, mask, kKey_Glossiness, kNoSubIndex);
        ov->RemoveSkinOverride(refr, female, true, mask, kKey_Glossiness, kNoSubIndex);

        ov->RemoveSkinOverride(refr, female, false, mask, kKey_SpecularStr, kNoSubIndex);
        ov->RemoveSkinOverride(refr, female, true, mask, kKey_SpecularStr, kNoSubIndex);

        ov->SetSkinProperties(refr, true);
        if (aum) {
            aum->AddSkinOverrideUpdate(a->GetFormID());
            aum->Flush();
        }
    }

    void OverlayMgr::applyToActor(RE::Actor* a, const ActorState& st) {
        if (!a) return;

        RE::NiAVObject* third = a->Get3D();
        RE::NiAVObject* first = nullptr;

        if (a->IsPlayerRef()) {
            if (auto* pc = a->As<RE::PlayerCharacter>()) {
                first = pc->Get3D(true);
            }
            if (!first && third) {
                first = third->GetObjectByName("1st Person");
                if (!first) first = third->GetObjectByName("1stPerson");
            }
        }

        auto applyRoot = [&](RE::NiAVObject* root) -> int {
            if (!root) return 0;
            int applied = 0;
            forEachOverlayGeom(root, [&](RE::BSGeometry* g, bool isHand) {
                const std::string& pick = isHand ? st.chosenHand : st.chosenBody;
                if (!pick.empty()) {
                    setDiffuseOnGeometry(g, pick);
                    ++applied;
                }
            });
            return applied;
        };

        const int c3 = applyRoot(third);
        const int c1 = applyRoot(first);

        if ((c3 + c1) > 0 && _aum) {
            _aum->AddOverlayUpdate(a->GetFormID());
            _aum->Flush();
        }
        spdlog::debug("[SWE] applyToActor: applied third={}, first={} (actor={:08X})", c3, c1, a->GetFormID());
    }

    void OverlayMgr::clearActor(RE::Actor* a, ActorState& st, bool resetDiffuse) {
        if (_ovl) {
            _ovl->RevertOverlays(reinterpret_cast<TESObjectREFR*>(a), resetDiffuse, true);
            if (_aum) {
                _aum->AddOverlayUpdate(a->GetFormID());
                _aum->Flush();
            }
        }
        st.active = false;
        st.chosenBody.clear();
        st.chosenHand.clear();
    }

    void OverlayMgr::RevertActor(RE::Actor* a, bool resetDiffuse) {
        if (!a) return;
        std::lock_guard lk(_mtx);
        auto it = _actors.find(a->GetFormID());
        if (it == _actors.end()) return;
        clearActor(a, it->second, resetDiffuse);
    }

    void OverlayMgr::OnWetnessUpdate(RE::Actor* a, float skinWet01) {
        if (!_enabled || !a) return;

        std::lock_guard lk(_mtx);
        auto& st = _actors[a->GetFormID()];
        st.female = isFemale(a);

        const bool active = (skinWet01 >= _threshold);

        if (active && !st.active) {
            pickTexturesIfNeeded(st);
            st.active = true;
        } else if (!active && st.active) {
            st.active = false;
        }

        if (!active) return;

        const float gloss = std::clamp(60.0f + skinWet01 * 200.0f, 0.f, 400.f);
        const float spec = std::clamp(2.5f + skinWet01 * 7.5f, 0.f, 100.f);

        int patched = 0;
        if (_ni) {
            ApplyWetViaNiOverride(a, st.female, st.chosenBody, st.chosenHand, gloss, spec, _ni, _aum);
        } else {
            patched = ApplyWetDirectToSkin(a, st.chosenBody, st.chosenHand, gloss, spec, false);
            if (patched == 0) {
                patched = ApplyWetDirectToSkin(a, st.chosenBody, st.chosenHand, gloss, spec, true);
            }
        }
    }
}
