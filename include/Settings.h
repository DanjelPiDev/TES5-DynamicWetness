#pragma once
#include <atomic>
#include <string>
#include <vector>

namespace Settings {

    struct FormSpec {
        std::string plugin;
        uint32_t id{0};
        float value{0.f};
    };

    extern std::atomic<bool> modEnabled;
    extern std::atomic<bool> affectNPCs;
    extern std::atomic<int> npcRadius;

    extern std::atomic<bool> rainSnowEnabled;
    extern std::atomic<bool> affectInSnow;
    extern std::atomic<bool> ignoreInterior;

    extern std::atomic<bool> affectSkin;
    extern std::atomic<bool> affectHair;
    extern std::atomic<bool> affectArmor;
    extern std::atomic<bool> affectWeapons;

    extern std::atomic<float> secondsToSoakWater;
    extern std::atomic<float> secondsToSoakRain;
    extern std::atomic<float> secondsToDry;
    extern std::atomic<float> minSubmergeToSoak;

    extern std::atomic<float> glossinessBoost;      // added to original glossiness * wetness
    extern std::atomic<float> specularScaleBoost;   // (1 + wet * this) multiplier
    extern std::atomic<float> maxGlossiness;        // hard clamp (e.g. 300)
    extern std::atomic<float> maxSpecularStrength;  // clamp for color channels (0...something)
    extern std::atomic<float> minGlossiness;        // below this is considered non-glossy
    extern std::atomic<float> minSpecularStrength;  // below this is considered non-specular

    extern std::atomic<bool> waterfallEnabled;
    extern std::atomic<float> secondsToSoakWaterfall;
    extern std::atomic<float> nearWaterfallRadius;
    extern std::atomic<float> waterfallWidthPad;  // X
    extern std::atomic<float> waterfallDepthPad;  // Y
    extern std::atomic<float> waterfallZPad;

    extern std::atomic<float> nearFireRadius;
    extern std::atomic<float> dryMultiplierNearFire;

    extern std::atomic<float> skinHairResponseMul;

    extern std::atomic<int> externalBlendMode;  // 0=Max,1=Add,2=MaxPlusWeightedRest
    extern std::atomic<float> externalAddWeight;

    extern std::atomic<int> updateIntervalMs;

    std::string DefaultPath();

    void LoadFromJson(const std::string& path);
    void SaveToJson(const std::string& path);

    void ResetToDefaults();
}
