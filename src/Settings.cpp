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

    std::atomic<bool> rainSnowEnabled{true};
    std::atomic<bool> affectInSnow{false};
    std::atomic<bool> ignoreInterior{true};

    std::atomic<bool> affectSkin{true};
    std::atomic<bool> affectHair{true};
    std::atomic<bool> affectArmor{true};
    std::atomic<bool> affectWeapons{true};

    std::atomic<float> secondsToSoakWater{2.0f};
    std::atomic<float> secondsToSoakRain{36.0f};
    std::atomic<float> secondsToDry{25.0f};
    std::atomic<float> minSubmergeToSoak{0.5f};

    std::atomic<float> glossinessBoost{120.0f};
    std::atomic<float> specularScaleBoost{8.0f};
    std::atomic<float> maxGlossiness{800.0f};
    std::atomic<float> maxSpecularStrength{10.0f};
    std::atomic<float> minGlossiness{0.0f};
    std::atomic<float> minSpecularStrength{0.0f};

    std::atomic<bool> waterfallEnabled{true};
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

            apply_if(j, "rainSnowEnabled", rainSnowEnabled);
            apply_if(j, "affectInSnow", affectInSnow);
            apply_if(j, "ignoreInterior", ignoreInterior);

            apply_if(j, "affectSkin", affectSkin);
            apply_if(j, "affectHair", affectHair);
            apply_if(j, "affectArmor", affectArmor);
            apply_if(j, "affectWeapons", affectWeapons);

            apply_if(j, "secondsToSoakWater", secondsToSoakWater);
            apply_if(j, "secondsToSoakRain", secondsToSoakRain);
            apply_if(j, "secondsToDry", secondsToDry);
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
        } catch (...) {
        }
    }

    void SaveToJson(const std::string& path) {
        try {
            std::filesystem::create_directories(std::filesystem::path(path).parent_path());
            json j = {{"modEnabled", modEnabled.load()},
                      {"affectNPCs", affectNPCs.load()},
                      {"npcRadius", npcRadius.load()},

                      {"rainSnowEnabled", rainSnowEnabled.load()},
                      {"affectInSnow", affectInSnow.load()},
                      {"ignoreInterior", ignoreInterior.load()},

                      {"affectSkin", affectSkin.load()},
                      {"affectHair", affectHair.load()},
                      {"affectArmor", affectArmor.load()},
                      {"affectWeapons", affectWeapons.load()},

                      {"secondsToSoakWater", secondsToSoakWater.load()},
                      {"secondsToSoakRain", secondsToSoakRain.load()},
                      {"secondsToDry", secondsToDry.load()},
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

                      {"updateIntervalMs", updateIntervalMs.load()}};
            std::ofstream o(path, std::ios::trunc);
            o << j.dump(2);
        } catch (...) {
        }
    }

    void ResetToDefaults() {
        modEnabled.store(true);
        affectNPCs.store(false);
        npcRadius.store(4096);

        rainSnowEnabled.store(false);
        affectInSnow.store(false);
        ignoreInterior.store(true);

        affectSkin.store(true);
        affectHair.store(true);
        affectArmor.store(true);
        affectWeapons.store(true);

        secondsToSoakWater.store(2.0f);
        secondsToSoakRain.store(36.0f);
        secondsToDry.store(40.0f);
        minSubmergeToSoak.store(0.5f);

        glossinessBoost.store(120.0f);
        specularScaleBoost.store(8.0f);
        maxGlossiness.store(800.0f);
        maxSpecularStrength.store(10.0f);
        minGlossiness.store(0.0f);
        minSpecularStrength.store(0.0f);

        waterfallEnabled.store(true);
        secondsToSoakWaterfall.store(8.0f);
        nearWaterfallRadius.store(640.0f);
        waterfallWidthPad.store(45.0f);
        waterfallDepthPad.store(64.0f);
        waterfallZPad.store(51.0f);

        nearFireRadius.store(512.0f);
        dryMultiplierNearFire.store(3.0f);

        externalBlendMode.store(0);
        externalAddWeight.store(0.5f);

        skinHairResponseMul.store(5.0f);

        updateIntervalMs.store(50);
    }
}
