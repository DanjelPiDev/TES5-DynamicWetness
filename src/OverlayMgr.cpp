#include "OverlayMgr.h"

#include <algorithm>
#include <cctype>

#include "RE/B/BSLightingShaderMaterialBase.h"
#include "RE/B/BSTextureSet.h"

using std::string;
using std::vector;

namespace fs = std::filesystem;

namespace SWE {

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

    void OverlayMgr::OnInterfaceMap(IInterfaceMap* map) {
        if (!map) return;
        _ovl = static_cast<IOverlayInterface*>(map->QueryInterface("NiOverride-Overlay"));
        if (!_ovl) _ovl = static_cast<IOverlayInterface*>(map->QueryInterface("OVERLAY"));
        _aum = static_cast<IActorUpdateManager*>(map->QueryInterface("NiOverride-ActorUpdate"));
        if (!_aum) _aum = static_cast<IActorUpdateManager*>(map->QueryInterface("ACTORUPDATE"));

        if (_ovl && !_cbRegistered) {
            _ovl->RegisterInstallCallback("SWE", &OverlayMgr::OverlayInstalledCB);
            _cbRegistered = true;
        }
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

        std::function<void(RE::NiAVObject*, bool inOverlay, bool isHand)> rec = [&](RE::NiAVObject* o, bool inOverlay,
                                                                                    bool isHand) {
            if (!o) return;

            std::string name = tolower_copy(o->name.c_str());
            const bool nowOverlay = inOverlay || isOverlayNodeName(name);
            const bool nowIsHand = isHand || (name.find("hand") != std::string::npos);

            if (auto* g = o->AsGeometry()) {
                if (nowOverlay) fn(g, nowIsHand);
            }

            if (auto* node = o->AsNode()) {
                for (auto& ch : node->GetChildren()) {
                    if (ch) rec(ch.get(), nowOverlay, nowIsHand);
                }
            }
        };

        rec(root, false, false);
    }

    void OverlayMgr::setDiffuseOnGeometry(RE::BSGeometry* g, const std::string& ddsPath) {
        if (!g) return;
        auto& rdata = g->GetGeometryRuntimeData();
        for (auto& p : rdata.properties) {
            if (!p) continue;
            if (auto* l = skyrim_cast<RE::BSLightingShaderProperty*>(p.get())) {
                auto* mat = l->material ? static_cast<RE::BSLightingShaderMaterialBase*>(l->material) : nullptr;
                if (!mat) continue;
                auto* ts = mat->textureSet.get();
                if (!ts) continue;

                const std::string gamePath = ToGameTexPath(ddsPath);
                if (IsSpecPath(gamePath)) {
                    ts->SetTexturePath(RE::BSTextureSet::Texture::kSpecular, gamePath.c_str());
                } else {
                    ts->SetTexturePath(RE::BSTextureSet::Texture::kDiffuse, gamePath.c_str());
                }

                if (auto* sp = static_cast<RE::BSShaderProperty*>(l)) {
                    sp->flags.set(RE::BSShaderProperty::EShaderPropertyFlag::kSpecular);
                }
                mat->specularColorScale = std::max(mat->specularColorScale, 1.25f);
                mat->specularPower = std::max(mat->specularPower, 80.0f);

                l->SetMaterial(mat, true);
                l->DoClearRenderPasses();
                (void)l->SetupGeometry(g);
                (void)l->FinishSetupGeometry(g);
            }
        }
    }

    void OverlayMgr::OverlayInstalledCB(::TESObjectREFR* ref, ::NiAVObject* node) {
        auto* refRE = reinterpret_cast<RE::TESObjectREFR*>(ref);
        auto* a = refRE ? refRE->As<RE::Actor>() : nullptr;
        if (!a || !node) return;

        auto* mgr = OverlayMgr::Get();
        std::lock_guard lk(mgr->_mtx);

        auto it = mgr->_actors.find(a->GetFormID());
        if (it == mgr->_actors.end()) return;
        const auto& st = it->second;
        if (!st.active) return;

        mgr->forEachOverlayGeom(reinterpret_cast<RE::NiAVObject*>(node), [&](RE::BSGeometry* g, bool isHand) {
            const std::string& pick = isHand ? st.chosenHand : st.chosenBody;
            if (!pick.empty()) {
                setDiffuseOnGeometry(g, pick);
            }
        });
    }

    void OverlayMgr::ensureOverlays(RE::Actor* a, ActorState& st) {
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

        if (_useBody) {
            auto bodyList = list_dds(base / "body");
            if (!bodyList.empty()) {
                std::mt19937 rng{std::random_device{}()};
                st.chosenBody = bodyList[std::uniform_int_distribution<int>(0, (int)bodyList.size() - 1)(rng)];
            }
        }
        if (_useHand) {
            auto handList = list_dds(base / "hand");
            if (!handList.empty()) {
                std::mt19937 rng{std::random_device{}()};
                st.chosenHand = handList[std::uniform_int_distribution<int>(0, (int)handList.size() - 1)(rng)];
            }
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

        auto applyRoot = [&](RE::NiAVObject* root) {
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

        int c3 = applyRoot(third);
        int c1 = applyRoot(first);

        if (_aum) {
            _aum->AddOverlayUpdate(a->GetFormID());
            _aum->Flush();
        }
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
        if (!_ovl) {
            return;
        }

        std::lock_guard lk(_mtx);
        auto& st = _actors[a->GetFormID()];
        st.female = isFemale(a);

        const bool shouldBeActive = (skinWet01 >= _threshold);

        if (shouldBeActive && !st.active) {
            ensureOverlays(a, st);
            pickTexturesIfNeeded(st);
            applyToActor(a, st);
            st.active = true;
        } else if (shouldBeActive && st.active) {
            applyToActor(a, st);
        } else if (!shouldBeActive && st.active) {
            clearActor(a, st, true);
        }
    }
}
