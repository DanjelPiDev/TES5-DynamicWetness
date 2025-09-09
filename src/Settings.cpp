#include "Settings.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

using nlohmann::json;

namespace Settings {

    std::atomic<bool> modEnabled{true};
    std::atomic<bool> affectNPCs{false};
    std::atomic<int> npcRadius{4096};
    std::atomic<bool> npcOptInOnly{false};

    std::atomic<bool> rainEnabled{true};
    std::atomic<bool> snowEnabled{true};
    std::atomic<bool> affectInSnow{false};
    std::atomic<bool> ignoreInterior{true};

    std::atomic<bool> affectSkin{true};
    std::atomic<bool> affectHair{true};
    std::atomic<bool> affectArmor{true};
    std::atomic<bool> affectWeapons{true};

    std::atomic<float> secondsToSoakWater{2.0f};
    std::atomic<float> secondsToSoakRain{36.0f};
    std::atomic<float> secondsToSoakSnow{48.0f};
    std::atomic<float> secondsToDrySkin{40.0f};
    std::atomic<float> secondsToDryHair{40.0f};
    std::atomic<float> secondsToDryArmor{40.0f};
    std::atomic<float> secondsToDryWeapon{40.0f};
    std::atomic<float> minSubmergeToSoak{0.5f};

    std::atomic<float> glossinessBoost{120.0f};
    std::atomic<float> specularScaleBoost{8.0f};
    std::atomic<float> maxGlossiness{800.0f};
    std::atomic<float> maxSpecularStrength{10.0f};
    std::atomic<float> minGlossiness{0.0f};
    std::atomic<float> minSpecularStrength{0.0f};

    std::atomic<bool> waterfallEnabled{false};
    std::atomic<float> secondsToSoakWaterfall{8.0f};
    std::atomic<float> nearWaterfallRadius{128.0f};
    std::atomic<float> waterfallWidthPad{45.0f};
    std::atomic<float> waterfallDepthPad{64.0f};
    std::atomic<float> waterfallZPad{51.0f};

    std::atomic<int> externalBlendMode{0};
    std::atomic<float> externalAddWeight{0.5f};

    std::atomic<float> nearFireRadius{512.0f};
    std::atomic<float> dryMultiplierNearFire{3.0f};

    std::atomic<float> skinHairResponseMul{5.0f};

    std::atomic<int> updateIntervalMs{50};

    std::atomic<bool> pbrFriendlyMode{false};
    std::atomic<float> pbrArmorWeapMul{0.5f};
    std::atomic<float> pbrMaxGlossArmor{300.0f};
    std::atomic<float> pbrMaxSpecArmor{5.0f};

    std::atomic<bool> pbrClearcoatOnWet{false};
    std::atomic<float> pbrClearcoatScale{0.35f};
    std::atomic<float> pbrClearcoatSpec{0.25f};

    std::atomic<bool> activityWetEnabled{false};
    std::atomic<bool> activityTriggerRunning{true};
    std::atomic<bool> activityTriggerSneaking{true};
    std::atomic<bool> activityTriggerWorking{true};

    std::atomic<int> activityCatMask{0x01};  // Skin only

    std::atomic<float> secondsToSoakActivity{40.0f};  // 0->100% bei dauerhafter Aktivität
    std::atomic<float> secondsToDryActivity{35.0f};

    std::vector<FormSpec> actorOverrides;
    std::vector<FormSpec> trackedActors;
    std::shared_mutex actorsMutex;

    std::vector<FormSpec> SnapshotTrackedActors() {
        std::shared_lock lk(actorsMutex);
        return trackedActors;
    }
    std::vector<FormSpec> SnapshotActorOverrides() {
        std::shared_lock lk(actorsMutex);
        return actorOverrides;
    }

    static std::uint32_t RebaseFormID(std::uint32_t saved, std::string_view plugin) {
        if (saved == 0 || plugin.empty()) return saved;
        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) return saved;

        const std::uint32_t top = saved >> 24;
        const std::uint32_t local = (top == 0xFEu) ? (saved & 0x00000FFFu) : (saved & 0x00FFFFFFu);

        if (auto light = dh->GetLoadedLightModIndex(plugin)) {
            return 0xFE000000u | (static_cast<std::uint32_t>(*light) << 12) | (local & 0xFFFu);
        }
        if (auto reg = dh->GetLoadedModIndex(plugin)) {
            return (static_cast<std::uint32_t>(*reg) << 24) | (local & 0x00FFFFFFu);
        }
        return saved;
    }

    static std::uint32_t parse_form_id(const nlohmann::json& jv) {
        // Accept either number or hex string "0xXXXXXXXX"/"XXXXXXXX"
        if (jv.is_number_unsigned()) return jv.get<std::uint32_t>();
        if (jv.is_string()) {
            std::string s = jv.get<std::string>();
            // strip 0x if present
            if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0) s = s.substr(2);
            std::uint32_t out = 0;
            auto res = std::from_chars(s.data(), s.data() + s.size(), out, 16);
            if (res.ec == std::errc()) return out;
        }
        return 0;
    }

    static void load_formspec_array(const nlohmann::json& j, const char* key, std::vector<FormSpec>& out) {
        if (!j.contains(key) || !j.at(key).is_array()) return;
        out.clear();
        for (const auto& e : j.at(key)) {
            try {
                FormSpec fs{};
                if (e.contains("plugin")) fs.plugin = e.at("plugin").get<std::string>();
                if (e.contains("id")) fs.id = parse_form_id(e.at("id"));
                if (e.contains("value")) fs.value = std::clamp(e.at("value").get<float>(), 0.0f, 1.0f);
                if (e.contains("enabled")) fs.enabled = e.at("enabled").get<bool>();
                if (e.contains("mask")) {
                    if (e.at("mask").is_number_unsigned()) {
                        fs.mask = static_cast<std::uint8_t>(e.at("mask").get<unsigned>() & 0x0F);
                    } else if (e.at("mask").is_string()) {
                        std::string ms = e.at("mask").get<std::string>();
                        if (ms.rfind("0x", 0) == 0 || ms.rfind("0X", 0) == 0) ms = ms.substr(2);
                        unsigned mv = 0;
                        auto res = std::from_chars(ms.data(), ms.data() + ms.size(), mv, 16);
                        if (res.ec == std::errc()) fs.mask = static_cast<std::uint8_t>(mv & 0x0F);
                    }
                }
                if (e.contains("auto")) fs.autoWet = e.at("auto").get<bool>();
                if (fs.id != 0) out.push_back(std::move(fs));
            } catch (...) {
            }
        }
    }

    static nlohmann::json dump_formspec_array(const std::vector<FormSpec>& v) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& fs : v) {
            char idbuf[11]{};
            std::snprintf(idbuf, sizeof(idbuf), "0x%08X", fs.id);
            char maskbuf[5]{};
            std::snprintf(maskbuf, sizeof(maskbuf), "0x%X", (unsigned)(fs.mask & 0x0F));
            arr.push_back({
                {"plugin", fs.plugin},
                {"id", std::string(idbuf)},
                {"value", std::clamp(fs.value, 0.0f, 1.0f)},
                {"enabled", fs.enabled},
                {"mask", std::string(maskbuf)},
                {"auto", fs.autoWet}
            });
        }
        return arr;
    }

    static void apply_if(json& j, const char* key, auto& ref) {
        if (j.contains(key)) {
            using T = std::decay_t<decltype(ref.load())>;
            try {
                ref.store(j.at(key).get<T>());
            } catch (...) {
            }
        }
    }

    std::string DefaultPath() { return "Data/SKSE/Plugins/SkyrimWetEffect.json"; }

    void LoadFromJson(const std::string& path) {
        try {
            std::ifstream f(path);
            if (!f.good()) return;
            json j;
            f >> j;

            apply_if(j, "modEnabled", modEnabled);
            apply_if(j, "affectNPCs", affectNPCs);
            apply_if(j, "npcRadius", npcRadius);

            apply_if(j, "rainEnabled", rainEnabled);
            apply_if(j, "snowEnabled", snowEnabled);
            apply_if(j, "affectInSnow", affectInSnow);
            apply_if(j, "ignoreInterior", ignoreInterior);

            apply_if(j, "affectSkin", affectSkin);
            apply_if(j, "affectHair", affectHair);
            apply_if(j, "affectArmor", affectArmor);
            apply_if(j, "affectWeapons", affectWeapons);

            apply_if(j, "secondsToSoakWater", secondsToSoakWater);
            apply_if(j, "secondsToSoakRain", secondsToSoakRain);
            apply_if(j, "secondsToSoakSnow", secondsToSoakSnow);

            if (j.contains("secondsToDrySkin") || j.contains("secondsToDryHair") || j.contains("secondsToDryArmor") ||
                j.contains("secondsToDryWeapon")) {
                apply_if(j, "secondsToDrySkin", secondsToDrySkin);
                apply_if(j, "secondsToDryHair", secondsToDryHair);
                apply_if(j, "secondsToDryArmor", secondsToDryArmor);
                apply_if(j, "secondsToDryWeapon", secondsToDryWeapon);
            } else if (j.contains("secondsToDry")) {
                try {
                    float legacy = j.at("secondsToDry").get<float>();
                    secondsToDrySkin.store(legacy);
                    secondsToDryHair.store(legacy);
                    secondsToDryArmor.store(legacy);
                    secondsToDryWeapon.store(legacy);
                } catch (...) {
                }
            }
            apply_if(j, "minSubmergeToSoak", minSubmergeToSoak);

            apply_if(j, "glossinessBoost", glossinessBoost);
            apply_if(j, "specularScaleBoost", specularScaleBoost);
            apply_if(j, "maxGlossiness", maxGlossiness);
            apply_if(j, "maxSpecularStrength", maxSpecularStrength);
            apply_if(j, "minGlossiness", minGlossiness);
            apply_if(j, "minSpecularStrength", minSpecularStrength);

            apply_if(j, "waterfallEnabled", waterfallEnabled);
            apply_if(j, "secondsToSoakWaterfall", secondsToSoakWaterfall);
            apply_if(j, "nearWaterfallRadius", nearWaterfallRadius);
            apply_if(j, "waterfallWidthPad", waterfallWidthPad);
            apply_if(j, "waterfallDepthPad", waterfallDepthPad);
            apply_if(j, "waterfallZPad", waterfallZPad);

            apply_if(j, "nearFireRadius", nearFireRadius);
            apply_if(j, "dryMultiplierNearFire", dryMultiplierNearFire);

            apply_if(j, "externalBlendMode", externalBlendMode);
            apply_if(j, "externalAddWeight", externalAddWeight);

            apply_if(j, "skinHairResponseMul", skinHairResponseMul);

            apply_if(j, "updateIntervalMs", updateIntervalMs);

            apply_if(j, "pbrFriendlyMode", pbrFriendlyMode);
            apply_if(j, "pbrArmorWeapMul", pbrArmorWeapMul);
            apply_if(j, "pbrMaxGlossArmor", pbrMaxGlossArmor);
            apply_if(j, "pbrMaxSpecArmor", pbrMaxSpecArmor);

            apply_if(j, "pbrClearcoatOnWet", pbrClearcoatOnWet);
            apply_if(j, "pbrClearcoatScale", pbrClearcoatScale);
            apply_if(j, "pbrClearcoatSpec", pbrClearcoatSpec);

            apply_if(j, "activityWetEnabled", activityWetEnabled);
            apply_if(j, "activityTriggerRunning", activityTriggerRunning);
            apply_if(j, "activityTriggerSneaking", activityTriggerSneaking);
            apply_if(j, "activityTriggerWorking", activityTriggerWorking);

            if (j.contains("activityCatMask")) {
                try {
                    if (j.at("activityCatMask").is_number_unsigned()) {
                        int mv = static_cast<int>(j.at("activityCatMask").get<unsigned>());
                        activityCatMask.store(mv & 0x0F);
                    } else if (j.at("activityCatMask").is_string()) {
                        std::string ms = j.at("activityCatMask").get<std::string>();
                        if (ms.rfind("0x", 0) == 0 || ms.rfind("0X", 0) == 0) ms = ms.substr(2);
                        unsigned mv = 0;
                        auto res = std::from_chars(ms.data(), ms.data() + ms.size(), mv, 16);
                        if (res.ec == std::errc()) activityCatMask.store(static_cast<int>(mv & 0x0F));
                    }
                } catch (...) {
                }
            }

            apply_if(j, "secondsToSoakActivity", secondsToSoakActivity);
            apply_if(j, "secondsToDryActivity", secondsToDryActivity);

            apply_if(j, "npcOptInOnly", npcOptInOnly);
            std::vector<FormSpec> aoTmp, taTmp;
            load_formspec_array(j, "actorOverrides", aoTmp);
            load_formspec_array(j, "trackedActors", taTmp);
            {
                std::unique_lock lk(actorsMutex);
                actorOverrides = std::move(aoTmp);
                trackedActors = std::move(taTmp);

                auto fixup = [](std::vector<FormSpec>& v) {
                    for (auto& fs : v)
                        if (fs.id && !fs.plugin.empty()) fs.id = RebaseFormID(fs.id, fs.plugin);
                };
                fixup(actorOverrides);
                fixup(trackedActors);
            }
        } catch (...) {
        }
    }

    void SaveToJson(const std::string& path) {
        try {
            std::filesystem::create_directories(std::filesystem::path(path).parent_path());
            json j = {{"modEnabled", modEnabled.load()},
                      {"affectNPCs", affectNPCs.load()},
                      {"npcRadius", npcRadius.load()},

                      {"rainEnabled", rainEnabled.load()},
                      {"snowEnabled", snowEnabled.load()},
                      {"affectInSnow", affectInSnow.load()},
                      {"ignoreInterior", ignoreInterior.load()},

                      {"affectSkin", affectSkin.load()},
                      {"affectHair", affectHair.load()},
                      {"affectArmor", affectArmor.load()},
                      {"affectWeapons", affectWeapons.load()},

                      {"secondsToSoakWater", secondsToSoakWater.load()},
                      {"secondsToSoakRain", secondsToSoakRain.load()},
                      {"secondsToSoakSnow", secondsToSoakRain.load()},
                      {"secondsToDrySkin", secondsToDrySkin.load()},
                      {"secondsToDryHair", secondsToDryHair.load()},
                      {"secondsToDryArmor", secondsToDryArmor.load()},
                      {"secondsToDryWeapon", secondsToDryWeapon.load()},
                      {"minSubmergeToSoak", minSubmergeToSoak.load()},

                      {"glossinessBoost", glossinessBoost.load()},
                      {"specularScaleBoost", specularScaleBoost.load()},
                      {"maxGlossiness", maxGlossiness.load()},
                      {"maxSpecularStrength", maxSpecularStrength.load()},
                      {"minGlossiness", minGlossiness.load()},
                      {"minSpecularStrength", minSpecularStrength.load()},

                      {"waterfallEnabled", waterfallEnabled.load()},
                      {"secondsToSoakWaterfall", secondsToSoakWaterfall.load()},
                      {"nearWaterfallRadius", nearWaterfallRadius.load()},
                      {"waterfallWidthPad", waterfallWidthPad.load()},
                      {"waterfallDepthPad", waterfallDepthPad.load()},
                      {"waterfallZPad", waterfallZPad.load()},

                      {"nearFireRadius", nearFireRadius.load()},
                      {"dryMultiplierNearFire", dryMultiplierNearFire.load()},

                      {"externalBlendMode", externalBlendMode.load()},
                      {"externalAddWeight", externalAddWeight.load()},

                      {"skinHairResponseMul", skinHairResponseMul.load()},

                      {"npcOptInOnly", npcOptInOnly.load()},

                      {"pbrFriendlyMode", pbrFriendlyMode.load()},
                      {"pbrArmorWeapMul", pbrArmorWeapMul.load()},
                      {"pbrMaxGlossArmor", pbrMaxGlossArmor.load()},
                      {"pbrMaxSpecArmor", pbrMaxSpecArmor.load()},

                      {"pbrClearcoatOnWet", pbrClearcoatOnWet.load()},
                      {"pbrClearcoatScale", pbrClearcoatScale.load()},
                      {"pbrClearcoatSpec", pbrClearcoatSpec.load()},

                      {"activityWetEnabled", activityWetEnabled.load()},
                      {"activityTriggerRunning", activityTriggerRunning.load()},
                      {"activityTriggerSneaking", activityTriggerSneaking.load()},
                      {"activityTriggerWorking", activityTriggerWorking.load()},
                      {"activityCatMask", activityCatMask.load()},
                      {"secondsToSoakActivity", secondsToSoakActivity.load()},
                      {"secondsToDryActivity", secondsToDryActivity.load()},


                      {"updateIntervalMs", updateIntervalMs.load()}};
            auto ao = SnapshotActorOverrides();
            auto ta = SnapshotTrackedActors();
            j["actorOverrides"] = dump_formspec_array(ao);
            j["trackedActors"] = dump_formspec_array(ta);
            std::ofstream o(path, std::ios::trunc);
            o << j.dump(2);
        } catch (...) {
        }
    }

    void ResetToDefaults() {
        modEnabled.store(true);
        affectNPCs.store(false);
        npcRadius.store(4096);
        npcOptInOnly.store(false);

        rainEnabled.store(false);
        snowEnabled.store(false);
        affectInSnow.store(false);
        ignoreInterior.store(true);

        affectSkin.store(true);
        affectHair.store(true);
        affectArmor.store(true);
        affectWeapons.store(true);

        secondsToSoakWater.store(6.0f);
        secondsToSoakRain.store(1450.0f);
        secondsToSoakSnow.store(2100.0f);
        secondsToDrySkin.store(1600.0f);
        secondsToDryHair.store(1900.0f);
        secondsToDryArmor.store(2200.0f);
        secondsToDryWeapon.store(2000.0f);
        minSubmergeToSoak.store(0.5f);

        glossinessBoost.store(120.0f);
        specularScaleBoost.store(8.0f);
        maxGlossiness.store(800.0f);
        maxSpecularStrength.store(10.0f);
        minGlossiness.store(0.0f);
        minSpecularStrength.store(0.0f);

        waterfallEnabled.store(false);
        secondsToSoakWaterfall.store(8.0f);
        nearWaterfallRadius.store(512.0f);
        waterfallWidthPad.store(45.0f);
        waterfallDepthPad.store(64.0f);
        waterfallZPad.store(51.0f);

        nearFireRadius.store(512.0f);
        dryMultiplierNearFire.store(8.0f);

        externalBlendMode.store(0);
        externalAddWeight.store(0.5f);

        skinHairResponseMul.store(5.0f);

        updateIntervalMs.store(50);

        pbrFriendlyMode.store(false);
        pbrArmorWeapMul.store(0.5f);
        pbrMaxGlossArmor.store(300.0f);
        pbrMaxSpecArmor.store(5.0f);

        pbrClearcoatOnWet.store(false);
        pbrClearcoatScale.store(0.35f);
        pbrClearcoatSpec.store(0.25f);

        activityWetEnabled.store(false);
        activityTriggerRunning.store(true);
        activityTriggerSneaking.store(true);
        activityTriggerWorking.store(true);
        activityCatMask.store(0x01);
        secondsToSoakActivity.store(40.0f);
        secondsToDryActivity.store(35.0f);


        {
            std::unique_lock lk(actorsMutex);
            actorOverrides.clear();
            trackedActors.clear();
        }
    }
}
