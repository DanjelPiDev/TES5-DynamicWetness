#pragma once
#include <filesystem>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "IPluginInterface.h"
#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

namespace SWE {

    struct ActorState {
        bool overlaysAdded{false};
        bool active{false};
        std::string chosenBody;
        std::string chosenHand;
        bool female{false};
        std::uint32_t lastGeomStamp{0};
        std::string baseSpecBody;
        std::string baseSpecHand;
        int lastWetBucket{-1};
    };

    class OverlayMgr {
    public:
        static OverlayMgr* Get() {
            static OverlayMgr g;
            return &g;
        }

        static bool IsSpecPath(const std::string& p);
        static std::string ToGameTexPath(std::string p);

        void OnInterfaceMap(IInterfaceMap* map);

        void OnWetnessUpdate(RE::Actor* a, float skinWet01);
        void RevertActor(RE::Actor* a, bool resetDiffuse = true);

        void SetEnabled(bool en) { _enabled = en; }
        void SetThreshold(float t01) { _threshold = std::clamp(t01, 0.0f, 1.0f); }
        void SetUseBody(bool b) { _useBody = b; }
        void SetUseHand(bool h) { _useHand = h; }

        bool HasInterfaces() const noexcept { return _ovl != nullptr; }
        bool HasOverlayInterface() const noexcept { return _ovl != nullptr; }
        bool HasActorUpdateIF() const noexcept { return _aum != nullptr; }
        bool HasNiOverride() const { return _ni != nullptr; }

    private:
        OverlayMgr() = default;

        IOverlayInterface* _ovl{nullptr};
        IActorUpdateManager* _aum{nullptr};
        IOverrideInterface* _ni = nullptr;

        bool _enabled{true};
        float _threshold{0.65f};
        bool _useBody{true};
        bool _useHand{true};

        std::mutex _mtx;
        std::unordered_map<RE::FormID, ActorState> _actors;

        bool _cbRegistered{false};

        std::unordered_map<std::string, std::string> _mergeCache;
        std::mutex _mergeMtx;

        static std::string GetFirstSkinSpecPath(RE::NiAVObject* root);
        std::string GetOrBuildMergedSpec(const std::string& baseSpec, const std::string& wetSpec, int wetBucket);
        static int QuantizeWet(float wet01) { return std::clamp((int)std::round(wet01 * 10.f), 0, 10); }

        void ensureInterfaces();
        void ensureOverlays(RE::Actor* a, ActorState& st);
        void pickTexturesIfNeeded(ActorState& st);
        void applyToActor(RE::Actor* a, const ActorState& st);
        void clearActor(RE::Actor* a, ActorState& st, bool resetDiffuse);

        static bool isFemale(const RE::Actor* a);
        static std::vector<std::string> list_dds(const std::filesystem::path& dir);
        static void setDiffuseOnGeometry(RE::BSGeometry* g, const std::string& ddsPath);
        static void forEachOverlayGeom(RE::NiAVObject* root,
                                       const std::function<void(RE::BSGeometry* g, bool isHand)>& fn);

        static bool isOverlayNodeName(std::string_view n);

        static void OverlayInstalledCB(TESObjectREFR* ref, NiAVObject* node);
    };

}
