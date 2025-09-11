#include "OverlayMgr.h"

#include <algorithm>
#include <cctype>

#if defined(SWE_USE_DIRECTX_TEX)
    #include <DirectXTex.h>
#endif

#include "RE/B/BSLightingShaderMaterialBase.h"
#include "RE/B/BSTextureSet.h"

using std::string;
using std::vector;

namespace fs = std::filesystem;

using namespace DirectX;


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

    static inline std::string lc_norm_path(const char* p) {
        if (!p || !p[0]) return {};
        std::string s(p);
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        std::replace(s.begin(), s.end(), '\\', '/');
        if (s.rfind("data/", 0) == 0) s.erase(0, 5);
        return s;
    }

    static inline string tolower_copy(string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return s;
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
        std::string p = OverlayMgr::ToGameTexPath(std::move(in));

        if (OverlayMgr::IsSpecPath(p)) {
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

        const std::string bodySpec = bodySpecPath.empty() ? "" : OverlayMgr::ToGameTexPath(bodySpecPath);
        const std::string handSpec = handSpecPath.empty() ? "" : OverlayMgr::ToGameTexPath(handSpecPath);

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
                            forceWhiteDebug ? "textures/effects/fxwhite.dds" : OverlayMgr::ToGameTexPath(bodySpecPath);
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

    std::string SWE::OverlayMgr::GetFirstSkinSpecPath(RE::NiAVObject* root) {
        std::string found;
        if (!root) return found;
        std::function<void(RE::NiAVObject*)> dfs = [&](RE::NiAVObject* o) {
            if (found.size()) return;
            if (auto* g = o->AsGeometry()) {
                for (auto& p : g->GetGeometryRuntimeData().properties) {
                    if (!p) continue;
                    if (auto* l = skyrim_cast<RE::BSLightingShaderProperty*>(p.get())) {
                        auto* mat = l->material ? static_cast<RE::BSLightingShaderMaterialBase*>(l->material) : nullptr;
                        auto* ts = mat ? mat->textureSet.get() : nullptr;
                        if (!ts) continue;
                        auto* s7 = ts->GetTexturePath(RE::BSTextureSet::Texture::kSpecular);
                        std::string sp = lc_norm_path(s7);
                        if (!sp.empty() && IsSpecPath(sp)) {
                            found = sp;
                            return;
                        }
                    }
                }
            }
            if (auto* n = o->AsNode())
                for (auto& ch : n->GetChildren())
                    if (ch) dfs(ch.get());
        };
        dfs(root);
        return found;
    }

    std::string SWE::OverlayMgr::GetOrBuildMergedSpec(const std::string& baseSpec, const std::string& wetSpec,
                                                      int wetBucket) {
        if (baseSpec.empty() || wetSpec.empty()) return wetSpec;

        const std::string key = baseSpec + "|" + wetSpec + "|" + std::to_string(wetBucket);

        std::error_code ec;
        const std::filesystem::path outDir = std::filesystem::path("Data/Textures/DynamicWetness/_cache");
        std::filesystem::create_directories(outDir, ec);

        {
            std::lock_guard lk(_mergeMtx);
            if (auto it = _mergeCache.find(key); it != _mergeCache.end()) return it->second;
        }

        auto fallbackWet = [&](const char* why) -> std::string {
            spdlog::warn("[SWE] Merge: fallback ({}) -> using wet-only copy", why ? why : "unknown");
            std::filesystem::path src = "Data";
            src /= (wetSpec.rfind("textures/", 0) == 0 ? wetSpec : ("textures/" + wetSpec));

            auto dst = outDir / ("spec_wetonly_" + std::to_string(std::hash<std::string>{}(key)) + ".dds");

            std::error_code copyEC;
            std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, copyEC);
            if (copyEC) {
                spdlog::warn("[SWE] Fallback copy failed: {}", copyEC.message());
                std::string gameRel = (std::filesystem::path("Data") /
                                       (wetSpec.rfind("textures/", 0) == 0 ? wetSpec : "textures/" + wetSpec))
                                          .generic_string();
                std::transform(gameRel.begin(), gameRel.end(), gameRel.begin(), ::tolower);
                if (auto pos = gameRel.find("data/"); pos != std::string::npos) gameRel.erase(0, pos + 5);
                std::lock_guard lk(_mergeMtx);
                _mergeCache[key] = gameRel;
                return gameRel;
            }

            std::string gameRel = dst.generic_string();
            std::transform(gameRel.begin(), gameRel.end(), gameRel.begin(), ::tolower);
            if (auto pos = gameRel.find("data/"); pos != std::string::npos) gameRel.erase(0, pos + 5);
            std::lock_guard lk(_mergeMtx);
            _mergeCache[key] = gameRel;
            return gameRel;
        };

#if defined(SWE_USE_DIRECTX_TEX)
        
        const std::filesystem::path outPath =
            outDir / ("spec_" + std::to_string(std::hash<std::string>{}(key)) + ".dds");

        spdlog::info("[SWE] Merging spec '{}' + '{}' (bucket {})", baseSpec, wetSpec, wetBucket);

        auto toAbs = [](const std::string& gamePath) {
            std::filesystem::path p = "Data";
            p /= (gamePath.rfind("textures/", 0) == 0 ? gamePath : ("textures/" + gamePath));
            return p;
        };

        DirectX::ScratchImage imgBase, imgWet;
        if (FAILED(DirectX::LoadFromDDSFile(toAbs(baseSpec).c_str(), DirectX::DDS_FLAGS_NONE, nullptr, imgBase))) {
            spdlog::warn("[SWE] Merge: failed to load base '{}', fallback to wet only", baseSpec);
            return fallbackWet("LoadFromDDS(base) failed");
        }
        if (FAILED(DirectX::LoadFromDDSFile(toAbs(wetSpec).c_str(), DirectX::DDS_FLAGS_NONE, nullptr, imgWet))) {
            spdlog::warn("[SWE] Merge: failed to load wet '{}', fallback to wet only", wetSpec);
            return fallbackWet("LoadFromDDS(wet) failed");
        }

        const DirectX::TexMetadata metaB = imgBase.GetMetadata();
        const DirectX::TexMetadata metaW = imgWet.GetMetadata();

        DirectX::ScratchImage baseLinear, wetLinear;
        const DirectX::ScratchImage* baseSrc = &imgBase;
        const DirectX::ScratchImage* wetSrc = &imgWet;

        // BC/DXT -> RGBA8
        if (DirectX::IsCompressed(metaB.format)) {
            if (FAILED(DirectX::Decompress(imgBase.GetImages(), imgBase.GetImageCount(), metaB,
                                           DXGI_FORMAT_R8G8B8A8_UNORM, baseLinear))) {
                return fallbackWet("Decompress(base) failed");
            }
            baseSrc = &baseLinear;
        }
        if (DirectX::IsCompressed(metaW.format)) {
            if (FAILED(DirectX::Decompress(imgWet.GetImages(), imgWet.GetImageCount(), metaW,
                                           DXGI_FORMAT_R8G8B8A8_UNORM, wetLinear))) {
                return fallbackWet("Decompress(wet) failed");
            }
            wetSrc = &wetLinear;
        }

        // To RGBA8
        constexpr DXGI_FORMAT kFmt = DXGI_FORMAT_R8G8B8A8_UNORM;
        DirectX::ScratchImage baseRGBA, wetRGBA;

        const auto& mb = baseSrc->GetMetadata();
        const auto& mw = wetSrc->GetMetadata();
        if (FAILED(DirectX::Convert(baseSrc->GetImages(), baseSrc->GetImageCount(), mb, kFmt,
                                    DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, baseRGBA))) {
            return fallbackWet("Convert(base) failed");
        }
        if (FAILED(DirectX::Convert(wetSrc->GetImages(), wetSrc->GetImageCount(), mw, kFmt, DirectX::TEX_FILTER_DEFAULT,
                                    DirectX::TEX_THRESHOLD_DEFAULT, wetRGBA))) {
            return fallbackWet("Convert(wet) failed");
        }

        const DirectX::Image* b = baseRGBA.GetImage(0, 0, 0);
        const DirectX::Image* w = wetRGBA.GetImage(0, 0, 0);
        if (!b || !w) return fallbackWet("GetImage(0,0,0) failed");

        if (b->width != w->width || b->height != w->height) {
            DirectX::ScratchImage wetScaled;
            if (FAILED(DirectX::Resize(*w, b->width, b->height, DirectX::TEX_FILTER_CUBIC, wetScaled)))
                return fallbackWet("Resize failed");
            wetRGBA = std::move(wetScaled);
            w = wetRGBA.GetImage(0, 0, 0);
        }

        DirectX::ScratchImage outRGBA;
        if (FAILED(outRGBA.Initialize2D(kFmt, b->width, b->height, 1, 1))) return fallbackWet("Initialize2D failed");

        const float w01 = std::clamp(wetBucket / 10.0f, 0.0f, 1.0f);
        for (size_t y = 0; y < b->height; ++y) {
            const uint8_t* pb = b->pixels + y * b->rowPitch;
            const uint8_t* pw = w->pixels + y * w->rowPitch;
            uint8_t* po = outRGBA.GetImages()->pixels + y * outRGBA.GetImages()->rowPitch;
            for (size_t x = 0; x < b->width; ++x) {
                const uint8_t br = pb[4 * x + 0], bg = pb[4 * x + 1], bb = pb[4 * x + 2], ba = pb[4 * x + 3];
                const uint8_t wr = pw[4 * x + 0], wg = pw[4 * x + 1], wb = pw[4 * x + 2], wa = pw[4 * x + 3];
                po[4 * x + 0] = static_cast<uint8_t>(std::max<int>(br, (int)std::round(wr * w01)));
                po[4 * x + 1] = static_cast<uint8_t>(std::max<int>(bg, (int)std::round(wg * w01)));
                po[4 * x + 2] = static_cast<uint8_t>(std::max<int>(bb, (int)std::round(wb * w01)));
                po[4 * x + 3] = static_cast<uint8_t>(std::max<int>(ba, (int)std::round(wa * w01)));
            }
        }

        DirectX::ScratchImage outBC;
        const bool compressOK =
            SUCCEEDED(DirectX::Compress(*outRGBA.GetImages(), DXGI_FORMAT_BC7_UNORM, DirectX::TEX_COMPRESS_DEFAULT,
                                        1.0f,
                                        outBC));
        const DirectX::ScratchImage& toSave = compressOK ? outBC : outRGBA;

        auto hr = DirectX::SaveToDDSFile(toSave.GetImages(), toSave.GetImageCount(), toSave.GetMetadata(),
                                         DirectX::DDS_FLAGS_NONE, outPath.c_str());
        if (FAILED(hr)) {
            spdlog::error("[SWE] Merge: SaveToDDSFile failed: 0x{:08X} -> {}", (uint32_t)hr, outPath.string());
            return fallbackWet("SaveToDDSFile failed");
        }
        spdlog::info("[SWE] Merge: wrote {}", outPath.string());

        std::string gameRel = outPath.generic_string();
        std::transform(gameRel.begin(), gameRel.end(), gameRel.begin(), ::tolower);
        if (auto posData = gameRel.find("data/"); posData != std::string::npos) gameRel.erase(0, posData + 5);

        {
            std::lock_guard lk(_mergeMtx);
            _mergeCache[key] = gameRel;
        }
        return gameRel;
#else
        spdlog::warn("[SWE] Merge: DirectXTex not enabled, fallback");
        return fallbackWet();
#endif
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

    bool SWE::OverlayMgr::IsSpecPath(const std::string& p) {
        std::string s = p;
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return s.size() >= 6 && s.rfind("_s.dds") == s.size() - 6;
    }

    std::string SWE::OverlayMgr::ToGameTexPath(std::string p) {
        std::transform(p.begin(), p.end(), p.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        for (auto& ch : p)
            if (ch == '\\') ch = '/';
        if (p.rfind("data/", 0) == 0) p.erase(0, 5);
        if (p.rfind("textures/", 0) != 0)
            spdlog::warn("[SWE] OverlayMgr: unexpected path '{}' (expected to start with textures/)", p);
        return p;
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
            st.lastWetBucket = -1;

            if (auto* third = a->Get3D()) EnableSpecularOnSkinTree(third);
            if (a->IsPlayerRef())
                if (auto* pc = a->As<RE::PlayerCharacter>())
                    if (auto* first = pc->Get3D(true)) EnableSpecularOnSkinTree(first);
        } else if (!active && st.active) {
            st.active = false;
        }
        if (!active) return;

        if (st.baseSpecBody.empty()) {
            if (auto* third = a->Get3D()) st.baseSpecBody = GetFirstSkinSpecPath(third);
            if (st.baseSpecBody.empty() && a->IsPlayerRef())
                if (auto* pc = a->As<RE::PlayerCharacter>()) {
                    if (auto* first = pc->Get3D(true)) st.baseSpecBody = GetFirstSkinSpecPath(first);
                }
            if (st.baseSpecBody.empty()) {
                spdlog::debug("[SWE] No base spec snapshot found; will continue with wet only.");
            } else {
                spdlog::debug("[SWE] Base spec snapshot = '{}'", st.baseSpecBody);
            }
        }

        const int wetBucket = QuantizeWet(skinWet01);
        if (wetBucket == st.lastWetBucket) {
            // Do nothing if bucket unchanged?
        } else {
            st.lastWetBucket = wetBucket;

            const std::string chosen = st.chosenBody;
            const std::string wetSpecGame = ToGameTexPath(chosen);
            const std::string baseSpecGame = st.baseSpecBody;

            std::string mergedGame = wetSpecGame;
            if (!baseSpecGame.empty()) mergedGame = GetOrBuildMergedSpec(baseSpecGame, wetSpecGame, wetBucket);

            auto setOnTree = [&](RE::NiAVObject* root) -> int {
                int changed = 0;
                if (!root) return 0;
                forEachSkinGeom(root, [&](RE::BSGeometry* g) {
                    for (auto& p : g->GetGeometryRuntimeData().properties) {
                        if (!p) continue;
                        if (auto* l = skyrim_cast<RE::BSLightingShaderProperty*>(p.get())) {
                            auto* mat =
                                l->material ? static_cast<RE::BSLightingShaderMaterialBase*>(l->material) : nullptr;
                            auto* ts = mat ? mat->textureSet.get() : nullptr;
                            if (!ts) continue;

                            const char* cur7 = ts->GetTexturePath(RE::BSTextureSet::Texture::kSpecular);
                            std::string curSpec = lc_norm_path(cur7);
                            if (curSpec != mergedGame) {
                                ts->SetTexturePath(RE::BSTextureSet::Texture::kSpecular, mergedGame.c_str());
                                ts->SetTexturePath(RE::BSTextureSet::Texture::kBacklightMask, mergedGame.c_str());

                                const char* d0 = ts->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse);
                                const char* s7 = ts->GetTexturePath(RE::BSTextureSet::Texture::kSpecular);
                                const char* b8 = ts->GetTexturePath(RE::BSTextureSet::Texture::kBacklightMask);
                                spdlog::info("[SWE] GEOM='{}' d0='{}' s7='{}' b8='{}'", g->name.c_str(), d0 ? d0 : "",
                                             s7 ? s7 : "", b8 ? b8 : "");

                                l->SetMaterial(mat, true);
                                l->DoClearRenderPasses();
                                (void)l->SetupGeometry(g);
                                (void)l->FinishSetupGeometry(g);
                                ++changed;
                            }
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

            spdlog::debug("[SWE] Applied merged spec (bucket={}) to {} geoms", wetBucket, total);
        }

        const float gloss = std::clamp(60.0f + skinWet01 * 200.0f, 0.f, 400.f);
        const float spec = std::clamp(2.5f + skinWet01 * 7.5f, 0.f, 100.f);

        auto setCaps = [&](RE::NiAVObject* root) {
            if (!root) return;
            forEachSkinGeom(root, [&](RE::BSGeometry* g) {
                for (auto& p : g->GetGeometryRuntimeData().properties) {
                    if (!p) continue;
                    if (auto* l = skyrim_cast<RE::BSLightingShaderProperty*>(p.get())) {
                        auto* mat = l->material ? static_cast<RE::BSLightingShaderMaterialBase*>(l->material) : nullptr;
                        if (!mat) continue;
                        auto* sp = static_cast<RE::BSShaderProperty*>(l);

                        sp->flags.set(RE::BSShaderProperty::EShaderPropertyFlag::kSpecular);
                        sp->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kSpecular, true);

                        mat->specularColor = {1.0f, 1.0f, 1.0f};
                        mat->specularPower = std::max(mat->specularPower, gloss);
                        mat->specularColorScale = std::max(mat->specularColorScale, spec);

                        l->SetMaterial(mat, true);
                        l->DoClearRenderPasses();
                        (void)l->SetupGeometry(g);
                        (void)l->FinishSetupGeometry(g);
                    }
                }
            });
        };
        if (auto* third = a->Get3D()) setCaps(third);
        if (a->IsPlayerRef())
            if (auto* pc = a->As<RE::PlayerCharacter>())
                if (auto* first = pc->Get3D(true)) setCaps(first);
    }
}
