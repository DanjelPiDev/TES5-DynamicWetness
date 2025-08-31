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
    std::atomic<bool> affectNPCs{true};
    std::atomic<int> npcRadius{4096};

    std::atomic<bool> rainSnowEnabled{true};
    std::atomic<bool> affectInSnow{true};
    std::atomic<bool> ignoreInterior{true};

    std::atomic<bool> affectSkin{true};
    std::atomic<bool> affectHair{true};
    std::atomic<bool> affectArmor{true};
    std::atomic<bool> affectWeapons{true};

    std::atomic<float> secondsToSoakWater{4.0f};
    std::atomic<float> secondsToSoakRain{18.0f};
    std::atomic<float> secondsToDry{25.0f};

    std::atomic<float> glossinessBoost{120.0f};
    std::atomic<float> specularScaleBoost{2.0f};
    std::atomic<float> maxGlossiness{300.0f};
    std::atomic<float> maxSpecularStrength{3.5f};

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

            apply_if(j, "glossinessBoost", glossinessBoost);
            apply_if(j, "specularScaleBoost", specularScaleBoost);
            apply_if(j, "maxGlossiness", maxGlossiness);
            apply_if(j, "maxSpecularStrength", maxSpecularStrength);

            apply_if(j, "updateIntervalMs", updateIntervalMs);
        } catch (...) {
            // ignore
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

                      {"glossinessBoost", glossinessBoost.load()},
                      {"specularScaleBoost", specularScaleBoost.load()},
                      {"maxGlossiness", maxGlossiness.load()},
                      {"maxSpecularStrength", maxSpecularStrength.load()},

                      {"updateIntervalMs", updateIntervalMs.load()}};
            std::ofstream o(path, std::ios::trunc);
            o << j.dump(2);
        } catch (...) {
        }
    }
}
