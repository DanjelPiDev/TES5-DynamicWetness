#include "OverlayMgr.h"

#include <algorithm>
#include <cctype>

#if defined(SWE_USE_DIRECTX_TEX)
    #include <DirectXTex.h>
    using namespace DirectX;
#endif

#include "RE/B/BSLightingShaderMaterialBase.h"
#include "RE/B/BSTextureSet.h"

#include "Settings.h"

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
                        if (auto* mat = l->material ? static_cast<RE::BSLightingShaderMaterialBase*>(l->material) : nullptr) {
                            auto* sp = static_cast<RE::BSShaderProperty*>(l);
                            sp->flags.set(RE::BSShaderProperty::EShaderPropertyFlag::kSpecular);
                            sp->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kSpecular, true);

                            mat->specularColor = {1.0f, 1.0f, 1.0f};
                            mat->specularColorScale = std::max(mat->specularColorScale, 100.0f);
                            mat->specularPower = std::max(mat->specularPower, 1000.0f);

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

    void OverlayMgr::StartMergeWorker() {
        bool expected = false;
        if (!_mergeAlive.compare_exchange_strong(expected, true)) return;

        _mergeThread = std::thread([this]() {
            while (_mergeAlive.load()) {
                MergeJob job;
                {
                    std::unique_lock lk(_jobMtx);
                    _jobCv.wait(lk, [&]() { return !_mergeAlive.load() || !_jobQ.empty(); });
                    if (!_mergeAlive.load()) break;
                    job = _jobQ.front();
                    _jobQ.pop_front();
                }

                std::string outGame = BuildMergedSpecSync(job.key, job.baseSpec, job.wetSpec, job.bucket);

                if (!outGame.empty() && job.actor != 0) {
                    SKSE::GetTaskInterface()->AddTask([this, job]() {
                        ApplyMergedIfStillRelevant(job.actor, job.baseSpec, job.wetSpec, job.bucket);
                    });
                }

                {
                    std::lock_guard lk(_jobMtx);
                    _inflightKeys.erase(job.key);
                }
            }
        });
    }

    void OverlayMgr::StopMergeWorker() {
        _mergeAlive.store(false);
        _jobCv.notify_all();
        if (_mergeThread.joinable()) _mergeThread.join();
        {
            std::lock_guard lk(_jobMtx);
            _jobQ.clear();
            _inflightKeys.clear();
        }
    }

    void OverlayMgr::EnqueueMerge(MergeJob j) {
        StartMergeWorker();

        std::lock_guard lk(_jobMtx);
        if (_inflightKeys.insert(j.key).second) {
            _jobQ.push_back(std::move(j));
            _jobCv.notify_one();
        }
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

        const DXGI_FORMAT kFmt = DXGI_FORMAT_R8G8B8A8_UNORM;
        DirectX::ScratchImage baseRGBA, wetRGBA;

        const auto& mb = baseSrc->GetMetadata();
        const auto& mw = wetSrc->GetMetadata();

        DirectX::TEX_FILTER_FLAGS baseFilter = DirectX::TEX_FILTER_DEFAULT;
        DirectX::TEX_FILTER_FLAGS wetFilter = DirectX::TEX_FILTER_DEFAULT;

        if (DirectX::IsSRGB(mb.format) || DirectX::IsSRGB(kFmt))
            baseFilter = static_cast<DirectX::TEX_FILTER_FLAGS>(baseFilter | DirectX::TEX_FILTER_SRGB);
        if (DirectX::IsSRGB(mw.format) || DirectX::IsSRGB(kFmt))
            wetFilter = static_cast<DirectX::TEX_FILTER_FLAGS>(wetFilter | DirectX::TEX_FILTER_SRGB);

        auto save_wetonly_expanded = [&](const char* why) -> std::string {
            spdlog::warn("[SWE] Merge: fallback ({} -> expanding wet alpha->RGB)", why ? why : "unknown");

            DirectX::ScratchImage wetOnlyRGBA;
            if (FAILED(DirectX::Convert(wetSrc->GetImages(), wetSrc->GetImageCount(), mw, kFmt, wetFilter,
                                        DirectX::TEX_THRESHOLD_DEFAULT, wetOnlyRGBA))) {
                return fallbackWet("Convert(wet) failed in expanded fallback");
            }

            const DirectX::Image* wi = wetOnlyRGBA.GetImage(0, 0, 0);
            for (size_t y = 0; y < wi->height; ++y) {
                uint8_t* row = wi->pixels + y * wi->rowPitch;
                for (size_t x = 0; x < wi->width; ++x) {
                    uint8_t* px = row + 4 * x;
                    const uint8_t a = px[3];
                    const bool rgbDark = (px[0] | px[1] | px[2]) < 5;
                    if (rgbDark) px[0] = px[1] = px[2] = a;
                    if (px[3] < 12) px[3] = 12;
                }
            }

            DirectX::ScratchImage outBC;
            const bool compressOK = SUCCEEDED(DirectX::Compress(*wetOnlyRGBA.GetImages(), DXGI_FORMAT_BC7_UNORM,
                                                                DirectX::TEX_COMPRESS_DEFAULT, 1.0f, outBC));
            const DirectX::ScratchImage& toSave = compressOK ? outBC : wetOnlyRGBA;

            auto writeExpandedWetOnly = [&](const std::string& wetSpec, const std::string& key) -> std::string {
                auto toAbs = [](const std::string& gamePath) {
                    std::filesystem::path p = "Data";
                    p /= (gamePath.rfind("textures/", 0) == 0 ? gamePath : ("textures/" + gamePath));
                    return p;
                };

                DirectX::ScratchImage imgWet;
                if (FAILED(DirectX::LoadFromDDSFile(
                        toAbs(wetSpec).c_str(), DirectX::DDS_FLAGS_LEGACY_DWORD | DirectX::DDS_FLAGS_ALLOW_LARGE_FILES,
                        nullptr, imgWet))) {
                    return fallbackWet("LoadFromDDS(wet) failed");
                }

                const auto metaW = imgWet.GetMetadata();
                const DirectX::ScratchImage* wetSrc = &imgWet;
                DirectX::ScratchImage wetDecomp;

                if (DirectX::IsCompressed(metaW.format)) {
                    if (FAILED(DirectX::Decompress(imgWet.GetImages(), imgWet.GetImageCount(), metaW,
                                                   DXGI_FORMAT_R8G8B8A8_UNORM, wetDecomp))) {
                        return fallbackWet("Decompress(wet) failed");
                    }
                    wetSrc = &wetDecomp;
                }

                const DirectX::Image* w = wetSrc->GetImage(0, 0, 0);
                if (!w) return fallbackWet("GetImage(wet) failed");

                DirectX::ScratchImage outRGBA;
                if (FAILED(outRGBA.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, w->width, w->height, 1, 1))) {
                    return fallbackWet("Initialize2D(expand) failed");
                }

                auto* outImg = outRGBA.GetImages();
                for (size_t y = 0; y < w->height; ++y) {
                    const uint8_t* pw = w->pixels + y * w->rowPitch;
                    uint8_t* po = outImg->pixels + y * outImg->rowPitch;

                    switch (wetSrc->GetMetadata().format) {
                        case DXGI_FORMAT_A8_UNORM:
                        case DXGI_FORMAT_R8_UNORM: {
                            for (size_t x = 0; x < w->width; ++x) {
                                uint8_t m = pw[x];
                                po[4 * x + 0] = m;
                                po[4 * x + 1] = m;
                                po[4 * x + 2] = m;
                                po[4 * x + 3] = m;
                            }
                            break;
                        }
                        case DXGI_FORMAT_R8G8B8A8_UNORM:
                        case DXGI_FORMAT_B8G8R8A8_UNORM: {
                            for (size_t x = 0; x < w->width; ++x) {
                                const uint8_t r = pw[4 * x + 0], g = pw[4 * x + 1], b = pw[4 * x + 2],
                                              a = pw[4 * x + 3];
                                const uint8_t rr = std::max(r, a);
                                const uint8_t gg = std::max(g, a);
                                const uint8_t bb = std::max(b, a);
                                const uint8_t aa = std::max<uint8_t>(std::max(rr, std::max(gg, bb)), a);
                                po[4 * x + 0] = rr;
                                po[4 * x + 1] = gg;
                                po[4 * x + 2] = bb;
                                po[4 * x + 3] = aa;
                            }
                            break;
                        }
                        default: {
                            const size_t step = 1;
                            for (size_t x = 0; x < w->width; ++x) {
                                uint8_t m = pw[x * step];
                                po[4 * x + 0] = m;
                                po[4 * x + 1] = m;
                                po[4 * x + 2] = m;
                                po[4 * x + 3] = m;
                            }
                            break;
                        }
                    }
                }

                DirectX::ScratchImage outBC;
                const bool compressOK = SUCCEEDED(DirectX::Compress(*outRGBA.GetImages(), DXGI_FORMAT_BC7_UNORM,
                                                                    DirectX::TEX_COMPRESS_DEFAULT, 1.0f, outBC));
                const auto& toSave = compressOK ? outBC : outRGBA;

                auto dst = outDir / ("spec_wetexpanded_" + std::to_string(std::hash<std::string>{}(key)) + ".dds");
                if (FAILED(DirectX::SaveToDDSFile(toSave.GetImages(), toSave.GetImageCount(), toSave.GetMetadata(),
                                                  DirectX::DDS_FLAGS_NONE, dst.c_str()))) {
                    return fallbackWet("SaveToDDS(expanded) failed");
                }

                std::string gameRel = dst.generic_string();
                std::transform(gameRel.begin(), gameRel.end(), gameRel.begin(), ::tolower);
                if (auto pos = gameRel.find("data/"); pos != std::string::npos) gameRel.erase(0, pos + 5);

                {
                    std::lock_guard lk(_mergeMtx);
                    _mergeCache[key] = gameRel;
                }
                spdlog::info("[SWE] Merge: wrote expanded wet-only {}", gameRel);
                return gameRel;
            };

            auto dst = outDir / ("spec_wetonly_" + std::to_string(std::hash<std::string>{}(key)) + ".dds");
            auto hr = DirectX::SaveToDDSFile(toSave.GetImages(), toSave.GetImageCount(), toSave.GetMetadata(),
                                             DirectX::DDS_FLAGS_NONE, dst.c_str());
            if (FAILED(hr)) {
                spdlog::error("[SWE] Merge: SaveToDDS(wetonly expanded) failed: 0x{:08X}", (uint32_t)hr);
                return fallbackWet("SaveToDDS(wetonly expanded) failed");
            }

            std::string gameRel = dst.generic_string();
            std::transform(gameRel.begin(), gameRel.end(), gameRel.begin(), ::tolower);
            if (auto pos = gameRel.find("data/"); pos != std::string::npos) gameRel.erase(0, pos + 5);
            std::lock_guard lk(_mergeMtx);
            _mergeCache[key] = gameRel;
            return gameRel;
        };

        if (FAILED(DirectX::Convert(baseSrc->GetImages(), baseSrc->GetImageCount(), mb, kFmt, baseFilter,
                                    DirectX::TEX_THRESHOLD_DEFAULT, baseRGBA))) {
            return save_wetonly_expanded("Convert(base) failed");
        }

        if (FAILED(DirectX::Convert(wetSrc->GetImages(), wetSrc->GetImageCount(), mw, kFmt, wetFilter,
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
        const bool compressOK = SUCCEEDED(
            DirectX::Compress(*outRGBA.GetImages(), DXGI_FORMAT_BC7_UNORM, DirectX::TEX_COMPRESS_DEFAULT, 1.0f, outBC));
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

    void SWE::OverlayMgr::ClearCache(bool removeDir) {
        namespace fs = std::filesystem;
        const fs::path dir = fs::path("Data") / "Textures" / "DynamicWetness" / "_cache";

        {
            std::lock_guard lk(_mergeMtx);
            _mergeCache.clear();
        }

        std::error_code ec;

        if (fs::exists(dir, ec)) {
            fs::remove_all(dir, ec);
            if (ec) {
                spdlog::warn("[SWE] ClearCache: remove_all failed: {}", ec.message());
            }
        }

        if (!removeDir) {
            ec.clear();
            fs::create_directories(dir, ec);
            if (ec) {
                spdlog::warn("[SWE] ClearCache: create_directories failed: {}", ec.message());
            }
        }

        spdlog::info("[SWE] ClearCache: cache folder reset ({})", removeDir ? "removed" : "emptied");
    }

    void SWE::OverlayMgr::ApplyMergedIfStillRelevant(RE::FormID actorFID, const std::string& baseSpecGame,
                                                     const std::string& wetSpecGame, int wetBucket) {
        if (!actorFID) return;

        const std::string key = baseSpecGame + "|" + wetSpecGame + "|" + std::to_string(wetBucket);
        std::string mergedGame;
        {
            std::lock_guard lk(_mergeMtx);
            auto it = _mergeCache.find(key);
            if (it != _mergeCache.end()) mergedGame = it->second;
        }
        if (mergedGame.empty()) return;

        RE::Actor* a = RE::TESForm::LookupByID<RE::Actor>(actorFID);
        if (!a) return;

        bool shouldApply = false;
        {
            std::lock_guard lk(_mtx);
            auto it = _actors.find(actorFID);
            if (it == _actors.end()) return;

            const ActorState& st = it->second;
            if (!st.active) return;

            const std::string currentWet = ToGameTexPath(st.chosenBody);
            const bool baseOK = st.baseSpecBody.empty() || (st.baseSpecBody == baseSpecGame);
            const bool wetOK = (currentWet == wetSpecGame);
            const bool buckOK = (st.lastWetBucket == wetBucket);
            const bool newPath = (st.lastAppliedSpecBody != mergedGame);

            shouldApply = baseOK && wetOK && buckOK && newPath;
        }
        if (!shouldApply) return;

        auto applyOnTree = [&](RE::NiAVObject* root) -> int {
            int changed = 0;
            if (!root) return 0;

            forEachSkinGeom(root, [&](RE::BSGeometry* g) {
                auto& rd = g->GetGeometryRuntimeData();
                for (auto& p : rd.properties) {
                    if (!p) continue;
                    if (auto* l = skyrim_cast<RE::BSLightingShaderProperty*>(p.get())) {
                        auto* mat = l->material ? static_cast<RE::BSLightingShaderMaterialBase*>(l->material) : nullptr;
                        auto* ts = mat ? mat->textureSet.get() : nullptr;
                        if (!ts) continue;

                        const char* cur7 = ts->GetTexturePath(RE::BSTextureSet::Texture::kSpecular);
                        std::string cur = lc_norm_path(cur7);
                        if (cur == mergedGame) continue;

                        ts->SetTexturePath(RE::BSTextureSet::Texture::kSpecular, mergedGame.c_str());
                        ts->SetTexturePath(RE::BSTextureSet::Texture::kBacklightMask, mergedGame.c_str());

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
        if (auto* third = a->Get3D()) total += applyOnTree(third);
        if (a->IsPlayerRef()) {
            if (auto* pc = a->As<RE::PlayerCharacter>())
                if (auto* first = pc->Get3D(true)) total += applyOnTree(first);
        }

        if (total > 0) {
            {
                std::lock_guard lk(_mtx);
                auto it = _actors.find(actorFID);
                if (it != _actors.end()) it->second.lastAppliedSpecBody = mergedGame;
            }
            if (_aum) {
                _aum->AddOverlayUpdate(actorFID);
                _aum->Flush();
            }
            spdlog::debug("[SWE] Applied async merged spec '{}' (bucket={}) to {} geoms (actor={:08X})", mergedGame,
                          wetBucket, total, actorFID);
        }
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

            if (auto* third = a->Get3D()) {
                EnableSpecularOnSkinTree(third);
            }
            if (a->IsPlayerRef())
                if (auto* pc = a->As<RE::PlayerCharacter>())
                    if (auto* first = pc->Get3D(true)) {
                        EnableSpecularOnSkinTree(first);
                    }
        } else if (!active && st.active) {
            st.active = false;
        }
        if (!active) return;

        if (st.baseSpecBody.empty()) {
            if (auto* third = a->Get3D()) st.baseSpecBody = GetFirstSkinSpecPath(third);
            if (st.baseSpecBody.empty() && a->IsPlayerRef()) {
                if (auto* pc = a->As<RE::PlayerCharacter>()) {
                    if (auto* first = pc->Get3D(true)) st.baseSpecBody = GetFirstSkinSpecPath(first);
                }
            }
            if (st.baseSpecBody.empty())
                spdlog::debug("[SWE] No base spec snapshot found; will continue with wet only.");
            else
                spdlog::debug("[SWE] Base spec snapshot = '{}'", st.baseSpecBody);
        }

        const int wetBucket = QuantizeWet(skinWet01);
        const bool bucketChanged = (wetBucket != st.lastWetBucket);

        if (bucketChanged) st.lastWetBucket = wetBucket;

        const std::string chosen = st.chosenBody;
        const std::string wetSpecGame = ToGameTexPath(chosen);
        const std::string baseSpecGame = st.baseSpecBody;

        std::string mergedGame = wetSpecGame;
        if (!baseSpecGame.empty())
            mergedGame = GetOrBuildMergedSpecAsyncForActor(a, baseSpecGame, wetSpecGame, wetBucket);

        if (mergedGame != st.lastAppliedSpecBody && !mergedGame.empty()) {
            auto setOnTree = [&](RE::NiAVObject* root) -> int {
                int changed = 0;
                if (!root) return 0;
                std::function<void(RE::NiAVObject*)> dfs = [&](RE::NiAVObject* o) {
                    if (auto* g = o->AsGeometry()) {
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

                                    l->SetMaterial(mat, true);
                                    l->DoClearRenderPasses();
                                    (void)l->SetupGeometry(g);
                                    (void)l->FinishSetupGeometry(g);
                                    ++changed;
                                }
                            }
                        }
                    }
                    if (auto* n = o->AsNode())
                        for (auto& ch : n->GetChildren())
                            if (ch) dfs(ch.get());
                };
                dfs(root);
                return changed;
            };

            int total = 0;
            if (auto* third = a->Get3D()) total += setOnTree(third);
            if (a->IsPlayerRef())
                if (auto* pc = a->As<RE::PlayerCharacter>())
                    if (auto* first = pc->Get3D(true)) total += setOnTree(first);

            if (total > 0) {
                st.lastAppliedSpecBody = mergedGame;
                spdlog::debug("[SWE] Applied spec '{}' (bucket={}) to {} geoms", mergedGame, wetBucket, total);
            }
        }

        const float gloss = std::clamp(60.0f + skinWet01 * 200.0f, Settings::minGlossiness.load(), Settings::maxGlossiness.load());
        const float spec = std::clamp(0.90f + skinWet01 * Settings::specularScaleBoost.load(), Settings::minSpecularStrength.load(), Settings::maxSpecularStrength.load());

        auto setPBR = [&](RE::NiAVObject* root) {
            if (!root) return;
            std::function<void(RE::NiAVObject*)> dfs = [&](RE::NiAVObject* o) {
                if (auto* g = o->AsGeometry()) {
                    for (auto& p : g->GetGeometryRuntimeData().properties) {
                        if (!p) continue;
                        if (auto* l = skyrim_cast<RE::BSLightingShaderProperty*>(p.get())) {
                            auto* mat =
                                l->material ? static_cast<RE::BSLightingShaderMaterialBase*>(l->material) : nullptr;
                            if (!mat) continue;

                            auto* sp = static_cast<RE::BSShaderProperty*>(l);
                            sp->flags.set(RE::BSShaderProperty::EShaderPropertyFlag::kSpecular);
                            mat->specularPower = std::max(mat->specularPower, gloss);
                            mat->specularColorScale = std::max(mat->specularColorScale, spec);
                            l->SetMaterial(mat, true);
                        }
                    }
                }
                if (auto* n = o->AsNode())
                    for (auto& ch : n->GetChildren())
                        if (ch) dfs(ch.get());
            };
            dfs(root);
        };
        if (auto* third = a->Get3D()) setPBR(third);
        if (a->IsPlayerRef())
            if (auto* pc = a->As<RE::PlayerCharacter>())
                if (auto* first = pc->Get3D(true)) setPBR(first);
    }

    std::string OverlayMgr::RequestMergedOrQueue(const std::string& baseSpecGame, const std::string& wetSpecGame,
                                                 int wetBucket, RE::FormID actorFID) {
        if (baseSpecGame.empty() || wetSpecGame.empty()) return wetSpecGame;

        const std::string key = baseSpecGame + "|" + wetSpecGame + "|" + std::to_string(wetBucket);

        {
            std::lock_guard lk(_mergeMtx);
            if (auto it = _mergeCache.find(key); it != _mergeCache.end()) return it->second;
        }

        std::error_code ec;
        const fs::path outDir = fs::path("Data/Textures/DynamicWetness/_cache");
        fs::create_directories(outDir, ec);

        auto wetOnlyPath = [&]() -> std::string {
            fs::path src = "Data";
            src /= (wetSpecGame.rfind("textures/", 0) == 0 ? wetSpecGame : ("textures/" + wetSpecGame));
            auto dst = outDir / ("spec_wetonly_" + std::to_string(std::hash<std::string>{}(key)) + ".dds");

            std::error_code cec;
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing, cec);
            std::string gameRel;
            if (!cec) {
                gameRel = dst.generic_string();
                std::transform(gameRel.begin(), gameRel.end(), gameRel.begin(), ::tolower);
                if (auto pos = gameRel.find("data/"); pos != std::string::npos) gameRel.erase(0, pos + 5);
            } else {
                gameRel = (fs::path("Data") /
                           (wetSpecGame.rfind("textures/", 0) == 0 ? wetSpecGame : "textures/" + wetSpecGame))
                              .generic_string();
                std::transform(gameRel.begin(), gameRel.end(), gameRel.begin(), ::tolower);
                if (auto pos = gameRel.find("data/"); pos != std::string::npos) gameRel.erase(0, pos + 5);
            }

            {
                std::lock_guard lk2(_mergeMtx);
                if (_mergeCache.find(key) == _mergeCache.end()) _mergeCache[key] = gameRel;
            }
            return gameRel;
        }();

        EnqueueMerge(MergeJob{key, baseSpecGame, wetSpecGame, wetBucket, actorFID});
        return wetOnlyPath;
    }

    std::string OverlayMgr::GetOrBuildMergedSpecAsyncForActor(RE::Actor* a, const std::string& baseSpecGame,
                                                              const std::string& wetSpecGame, int wetBucket) {
        const RE::FormID fid = a ? a->GetFormID() : 0;
        return RequestMergedOrQueue(baseSpecGame, wetSpecGame, wetBucket, fid);
    }

    std::string OverlayMgr::BuildMergedSpecSync(const std::string& key, const std::string& baseSpecGame,
                                                const std::string& wetSpecGame, int wetBucket) {
        std::error_code ec;
        const fs::path outDir = fs::path("Data/Textures/DynamicWetness/_cache");
        fs::create_directories(outDir, ec);

        auto fallbackWet = [&](const char* why) -> std::string {
            spdlog::warn("[SWE] Merge: fallback ({}) -> using wet-only copy", why ? why : "unknown");
            fs::path src = "Data";
            src /= (wetSpecGame.rfind("textures/", 0) == 0 ? wetSpecGame : ("textures/" + wetSpecGame));
            auto dst = outDir / ("spec_wetonly_" + std::to_string(std::hash<std::string>{}(key)) + ".dds");

            std::error_code copyEC;
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing, copyEC);

            std::string gameRel =
                (copyEC ? (fs::path("Data") /
                           (wetSpecGame.rfind("textures/", 0) == 0 ? wetSpecGame : "textures/" + wetSpecGame))
                        : dst)
                    .generic_string();

            std::transform(gameRel.begin(), gameRel.end(), gameRel.begin(), ::tolower);
            if (auto pos = gameRel.find("data/"); pos != std::string::npos) gameRel.erase(0, pos + 5);

            {
                std::lock_guard lk(_mergeMtx);
                _mergeCache[key] = gameRel;
            }
            return gameRel;
        };

#if defined(SWE_USE_DIRECTX_TEX)
        const fs::path outPath = outDir / ("spec_" + std::to_string(std::hash<std::string>{}(key)) + ".dds");

        auto toAbs = [](const std::string& gamePath) {
            fs::path p = "Data";
            p /= (gamePath.rfind("textures/", 0) == 0 ? gamePath : ("textures/" + gamePath));
            return p;
        };

        ScratchImage imgBase, imgWet;
        TexMetadata metaB{}, metaW{};
        if (FAILED(LoadFromDDSFile(toAbs(baseSpecGame).c_str(), DDS_FLAGS_NONE, &metaB, imgBase)))
            return fallbackWet("Load(base) failed");
        if (FAILED(LoadFromDDSFile(toAbs(wetSpecGame).c_str(), DDS_FLAGS_NONE, &metaW, imgWet)))
            return fallbackWet("Load(wet) failed");

        ScratchImage baseLinear, wetLinear;
        const ScratchImage* baseSrc = &imgBase;
        const ScratchImage* wetSrc = &imgWet;

        if (IsCompressed(metaB.format)) {
            if (FAILED(Decompress(imgBase.GetImages(), imgBase.GetImageCount(), metaB, DXGI_FORMAT_R8G8B8A8_UNORM,
                                  baseLinear)))
                return fallbackWet("Decompress(base) failed");
            baseSrc = &baseLinear;
        }
        if (IsCompressed(metaW.format)) {
            if (FAILED(Decompress(imgWet.GetImages(), imgWet.GetImageCount(), metaW, DXGI_FORMAT_R8G8B8A8_UNORM,
                                  wetLinear)))
                return fallbackWet("Decompress(wet) failed");
            wetSrc = &wetLinear;
        }

        const DXGI_FORMAT kFmt = DXGI_FORMAT_R8G8B8A8_UNORM;
        ScratchImage baseRGBA, wetRGBA;

        const auto& mb = baseSrc->GetMetadata();
        const auto& mw = wetSrc->GetMetadata();

        TEX_FILTER_FLAGS baseFilter = TEX_FILTER_DEFAULT;
        TEX_FILTER_FLAGS wetFilter = TEX_FILTER_DEFAULT;

        if (IsSRGB(mb.format) || IsSRGB(kFmt)) baseFilter = static_cast<TEX_FILTER_FLAGS>(baseFilter | TEX_FILTER_SRGB);
        if (IsSRGB(mw.format) || IsSRGB(kFmt)) wetFilter = static_cast<TEX_FILTER_FLAGS>(wetFilter | TEX_FILTER_SRGB);

        auto save_wetonly_expanded = [&](const char* why) -> std::string {
            spdlog::warn("[SWE] Merge: fallback ({} -> expanding wet alpha->RGB)", why ? why : "unknown");

            ScratchImage wetOnlyRGBA;
            if (FAILED(Convert(wetSrc->GetImages(), wetSrc->GetImageCount(), mw, kFmt, wetFilter, TEX_THRESHOLD_DEFAULT,
                               wetOnlyRGBA))) {
                return fallbackWet("Convert(wet) failed in expanded fallback");
            }

            const Image* wi = wetOnlyRGBA.GetImage(0, 0, 0);
            for (size_t y = 0; y < wi->height; ++y) {
                uint8_t* row = wi->pixels + y * wi->rowPitch;
                for (size_t x = 0; x < wi->width; ++x) {
                    uint8_t* px = row + 4 * x;
                    const uint8_t a = px[3];
                    const bool rgbDark = (px[0] | px[1] | px[2]) < 5;
                    if (rgbDark) {
                        px[0] = a;
                        px[1] = a;
                        px[2] = a;
                    }
                    px[3] = std::max<uint8_t>(px[3], a);
                }
            }

            std::error_code makeDirEC;
            fs::create_directories(outDir, makeDirEC);
            fs::path dst = outDir / ("spec_wetonly_exp_" + std::to_string(std::hash<std::string>{}(key)) + ".dds");

            HRESULT hr = E_FAIL;
            {
                ScratchImage bc7;
                hr = Compress(wetOnlyRGBA.GetImages(), wetOnlyRGBA.GetImageCount(), wetOnlyRGBA.GetMetadata(),
                              DXGI_FORMAT_BC7_UNORM, TEX_COMPRESS_DEFAULT, 0.5f, bc7);
                if (SUCCEEDED(hr)) {
                    hr = SaveToDDSFile(bc7.GetImages(), bc7.GetImageCount(), bc7.GetMetadata(),
                                       DDS_FLAGS_FORCE_DX10_EXT, dst.c_str());
                }
            }
            if (FAILED(hr)) {
                hr = SaveToDDSFile(wetOnlyRGBA.GetImages(), wetOnlyRGBA.GetImageCount(), wetOnlyRGBA.GetMetadata(),
                                   DDS_FLAGS_FORCE_DX10_EXT, dst.c_str());
                if (FAILED(hr)) {
                    return fallbackWet("Save(expanded wet) failed");
                }
            }

            std::string gameRel = dst.generic_string();
            std::transform(gameRel.begin(), gameRel.end(), gameRel.begin(), ::tolower);
            if (auto pos = gameRel.find("data/"); pos != std::string::npos) gameRel.erase(0, pos + 5);

            {
                std::lock_guard lk(_mergeMtx);
                _mergeCache[key] = gameRel;
            }
            return gameRel;
        };

        if (FAILED(Convert(baseSrc->GetImages(), baseSrc->GetImageCount(), mb, kFmt, baseFilter, TEX_THRESHOLD_DEFAULT,
                           baseRGBA)))
            return fallbackWet("Convert(base) failed");
        if (FAILED(Convert(wetSrc->GetImages(), wetSrc->GetImageCount(), mw, kFmt, wetFilter, TEX_THRESHOLD_DEFAULT,
                           wetRGBA)))
            return fallbackWet("Convert(wet) failed");

        const Image* bi = baseRGBA.GetImage(0, 0, 0);
        const Image* wi = wetRGBA.GetImage(0, 0, 0);

        ScratchImage wetResized;
        const Image* wImg = wi;
        if (wi->width != bi->width || wi->height != bi->height) {
            if (FAILED(Resize(wetRGBA.GetImages(), wetRGBA.GetImageCount(), wetRGBA.GetMetadata(), bi->width,
                              bi->height, TEX_FILTER_DEFAULT, wetResized))) {
                return save_wetonly_expanded("Resize(wet->base) failed");
            }
            wImg = wetResized.GetImage(0, 0, 0);
        }

        {
            size_t darkCount = 0, sample = 0;
            const size_t maxProbe = std::min<size_t>(wImg->width * wImg->height, 4096);
            for (size_t i = 0; i < maxProbe; ++i) {
                size_t idx = (i * 7919) % (wImg->width * wImg->height);
                size_t y = idx / wImg->width, x = idx % wImg->width;
                const uint8_t* px = wImg->pixels + y * wImg->rowPitch + x * 4;
                if ((px[0] | px[1] | px[2]) < 5 && px[3] > 5) ++darkCount;
                ++sample;
            }
            if (sample > 0 && darkCount > (sample * 3) / 4) {
                return save_wetonly_expanded("Wet RGB mostly black, alpha carries data");
            }
        }

        ScratchImage outImg;
        if (FAILED(outImg.Initialize2D(kFmt, bi->width, bi->height, 1, 1)))
            return fallbackWet("Initialize2D(out) failed");

        static constexpr float kBucketScale[] = {0.35f, 0.55f, 0.75f, 0.90f, 1.0f};
        const float s = (wetBucket >= 0 && wetBucket < (int)std::size(kBucketScale)) ? kBucketScale[wetBucket] : 0.75f;

        for (size_t y = 0; y < bi->height; ++y) {
            const uint8_t* brow = bi->pixels + y * bi->rowPitch;
            const uint8_t* wrow = wImg->pixels + y * wImg->rowPitch;
            uint8_t* orow = outImg.GetImage(0, 0, 0)->pixels + y * outImg.GetImage(0, 0, 0)->rowPitch;

            for (size_t x = 0; x < bi->width; ++x) {
                const uint8_t* bpx = brow + 4 * x;
                const uint8_t* wpx = wrow + 4 * x;
                uint8_t* opx = orow + 4 * x;

                const float br = bpx[0] / 255.0f, bg = bpx[1] / 255.0f, bb = bpx[2] / 255.0f, ba = bpx[3] / 255.0f;
                const float wr = wpx[0] / 255.0f, wg = wpx[1] / 255.0f, wb = wpx[2] / 255.0f, wa = wpx[3] / 255.0f;

                const float mask = std::clamp(wa * s, 0.0f, 1.0f);

                float wetLuma = (wr + wg + wb) / 3.0f;
                const float wetStrength = (wr + wg + wb > 0.01f) ? wetLuma : wa;

                const float t = std::clamp(mask, 0.0f, 1.0f);
                float or_ = std::lerp(br, std::max(wr, wetStrength), t);
                float og_ = std::lerp(bg, std::max(wg, wetStrength), t);
                float ob_ = std::lerp(bb, std::max(wb, wetStrength), t);

                float oa_ = std::clamp(ba * (1.0f - t) + std::max(ba, wa) * t, 0.0f, 1.0f);

                opx[0] = static_cast<uint8_t>(std::clamp(or_, 0.0f, 1.0f) * 255.0f + 0.5f);
                opx[1] = static_cast<uint8_t>(std::clamp(og_, 0.0f, 1.0f) * 255.0f + 0.5f);
                opx[2] = static_cast<uint8_t>(std::clamp(ob_, 0.0f, 1.0f) * 255.0f + 0.5f);
                opx[3] = static_cast<uint8_t>(oa_ * 255.0f + 0.5f);
            }
        }

        HRESULT hr = E_FAIL;
        {
            ScratchImage bc7;
            hr = Compress(outImg.GetImages(), outImg.GetImageCount(), outImg.GetMetadata(), DXGI_FORMAT_BC7_UNORM,
                          TEX_COMPRESS_DEFAULT, 0.5f, bc7);
            if (SUCCEEDED(hr)) {
                hr = SaveToDDSFile(bc7.GetImages(), bc7.GetImageCount(), bc7.GetMetadata(), DDS_FLAGS_FORCE_DX10_EXT,
                                   outPath.c_str());
            }
        }
        if (FAILED(hr)) {
            hr = SaveToDDSFile(outImg.GetImages(), outImg.GetImageCount(), outImg.GetMetadata(),
                               DDS_FLAGS_FORCE_DX10_EXT, outPath.c_str());
            if (FAILED(hr)) {
                return fallbackWet("Save(out) failed");
            }
        }

        std::string gameRel = outPath.generic_string();
        std::transform(gameRel.begin(), gameRel.end(), gameRel.begin(), ::tolower);
        if (auto pos = gameRel.find("data/"); pos != std::string::npos) gameRel.erase(0, pos + 5);

        {
            std::lock_guard lk(_mergeMtx);
            _mergeCache[key] = gameRel;
        }
        return gameRel;
#else
        return fallbackWet("SWE_USE_DIRECTX_TEX not defined");
#endif
    }
}
