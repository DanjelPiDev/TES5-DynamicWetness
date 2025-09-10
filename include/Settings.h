#pragma once
#include <atomic>
#include <string>
#include <vector>

namespace Settings {

    struct FormSpec {
        std::string plugin;
        std::uint32_t id{0};
        float value{0.f};
        bool enabled{true};
        std::uint8_t mask{0x0F};
        bool autoWet{true};
    };

    extern std::atomic<bool> modEnabled;
    extern std::atomic<bool> affectNPCs;
    extern std::atomic<int> npcRadius;
    extern std::atomic<bool> npcOptInOnly;

    extern std::atomic<bool> rainEnabled;
    extern std::atomic<bool> snowEnabled;
    extern std::atomic<bool> affectInSnow;
    extern std::atomic<bool> ignoreInterior;

    extern std::atomic<bool> affectSkin;
    extern std::atomic<bool> affectHair;
    extern std::atomic<bool> affectArmor;
    extern std::atomic<bool> affectWeapons;

    extern std::atomic<float> secondsToSoakWater;
    extern std::atomic<float> secondsToSoakRain;
    extern std::atomic<float> secondsToSoakSnow;
    extern std::atomic<float> secondsToDrySkin;
    extern std::atomic<float> secondsToDryHair;
    extern std::atomic<float> secondsToDryArmor;
    extern std::atomic<float> secondsToDryWeapon;
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

    extern std::atomic<bool> pbrFriendlyMode;
    extern std::atomic<float> pbrArmorWeapMul;
    extern std::atomic<float> pbrMaxGlossArmor;
    extern std::atomic<float> pbrMaxSpecArmor;

    extern std::atomic<bool> pbrClearcoatOnWet;   // default false
    extern std::atomic<float> pbrClearcoatScale;  // default 0.35f
    extern std::atomic<float> pbrClearcoatSpec;

    // Activity-based wetness (sweat) – optional
    extern std::atomic<bool> activityWetEnabled;
    extern std::atomic<bool> activityTriggerRunning;
    extern std::atomic<bool> activityTriggerSneaking;
    extern std::atomic<bool> activityTriggerWorking;

    // 4-bit cat mask (default: Skin only = 0x01)
    extern std::atomic<int> activityCatMask;

    // Soak/Dry times for activity wetness (independent von Rain/Water)
    extern std::atomic<float> secondsToSoakActivity;
    extern std::atomic<float> secondsToDryActivity;

    extern std::atomic<bool> overlayEnabled;
    extern std::atomic<float> overlayThreshold;

    extern std::vector<FormSpec> actorOverrides;
    extern std::vector<FormSpec> trackedActors;
    extern std::shared_mutex actorsMutex;
    std::vector<FormSpec> SnapshotTrackedActors();
    std::vector<FormSpec> SnapshotActorOverrides();

    std::string DefaultPath();

    void LoadFromJson(const std::string& path);
    void SaveToJson(const std::string& path);

    void ResetToDefaults();
}
