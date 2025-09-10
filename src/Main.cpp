#include "Main.h"
#include "PapyrusAPI.h"
#include "OverlayMgr.h"
#include "utils/Utils.h"

using namespace SKSE;

namespace SWE {

    void OnSave(SKSE::SerializationInterface* intfc) {
        if (auto* wc = WetController::GetSingleton()) {
            if (intfc->OpenRecord(kSerID, kSerVersion)) {
                wc->Serialize(intfc);
            }
        }
    }

    void OnLoad(SKSE::SerializationInterface* intfc) {
        std::uint32_t type, version, length;
        while (intfc->GetNextRecordInfo(type, version, length)) {
            if (type != kSerID) {
                std::vector<char> skip(length);
                if (length > 0) intfc->ReadRecordData(skip.data(), static_cast<std::uint32_t>(skip.size()));
                continue;
            }
            if (auto* wc = WetController::GetSingleton()) {
                wc->Deserialize(intfc, version, length);
            } else {
                std::vector<char> skip(length);
                if (length > 0) intfc->ReadRecordData(skip.data(), static_cast<std::uint32_t>(skip.size()));
            }
        }
    }
}

static void SetupLog() {
    try {
        if (auto path = logger::log_directory()) {
            *path /= "SkyrimWetEffect.log";
            auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), /*truncate=*/true);
            auto logger = std::make_shared<spdlog::logger>("SWE", sink);
            spdlog::set_default_logger(logger);
            spdlog::set_level(spdlog::level::trace);
            spdlog::flush_on(spdlog::level::info);
            spdlog::info("SkyrimWetEffect logging initialized.");
            return;
        }
    } catch (...) {
    }

    auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("Data/SKSE/Plugins/SkyrimWetEffect.log", true);
    auto logger = std::make_shared<spdlog::logger>("SWE", sink);
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::trace);
    spdlog::flush_on(spdlog::level::info);
    spdlog::info("SkyrimWetEffect logging initialized (fallback path).");
}

static void RequestRMInterfaces() {
    auto* mi = SKSE::GetMessagingInterface();
    if (!mi) return;

    InterfaceExchangeMessage ex{};
    ex.interfaceMap = nullptr;

    const char* receivers[] = {"skee", "SKEE", "NiOverride", "RaceMenu", nullptr };

    for (auto* r : receivers) {
        mi->Dispatch(InterfaceExchangeMessage::kMessage_ExchangeInterface, &ex, sizeof(ex), r);
        if (ex.interfaceMap) break;
    }

    if (ex.interfaceMap) {
        SWE::OverlayMgr::Get()->OnInterfaceMap(ex.interfaceMap);
        spdlog::info("[SWE] RaceMenu interfaces acquired via InterfaceExchange (receiver ok)");
    } else {
        spdlog::warn("[SWE] OverlayMgr: No overlays interface available (InterfaceExchange returned null)");
    }
}

static void OnSKSEMessage(SKSE::MessagingInterface::Message* msg) {
    if (!msg || !msg->sender) return;

    // RaceMenu/SKEE senden das Interface-Map-Event
    const bool fromNiOverride = std::strcmp(msg->sender, "NiOverride") == 0 || std::strcmp(msg->sender, "SKEE") == 0 ||
                                std::strcmp(msg->sender, "RaceMenu") == 0;

    if (fromNiOverride && msg->type == InterfaceExchangeMessage::kMessage_ExchangeInterface && msg->data) {
        auto* ex = static_cast<InterfaceExchangeMessage*>(msg->data);
        SWE::OverlayMgr::Get()->OnInterfaceMap(ex->interfaceMap);
    }
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }

extern "C" {
    __declspec(dllexport) float SWE_GetFinalWetness(RE::Actor* a) {
        if (auto* wc = SWE::WetController::GetSingleton()) return wc->GetFinalWetnessForActor(a);
        return 0.0f;
    }
    __declspec(dllexport) float SWE_GetExternalWetness(RE::Actor* a, const char* key) {
        if (!a || !key) return 0.0f;
        if (auto* wc = SWE::WetController::GetSingleton()) return wc->GetExternalWetness(a, key);
        return 0.0f;
    }
    __declspec(dllexport) float SWE_GetBaseWetness(RE::Actor* a) {
        if (auto* wc = SWE::WetController::GetSingleton()) return wc->GetBaseWetnessForActor(a);
        return 0.0f;
    }
    __declspec(dllexport) void SWE_SetExternalWetness(RE::Actor* a, const char* key, float value, float durationSec) {
        if (!a || !key) return;
        SWE::WetController::GetSingleton()->SetExternalWetness(a, key, value, durationSec);
    }
    __declspec(dllexport) void SWE_ClearExternalWetness(RE::Actor* a, const char* key) {
        if (!a || !key) return;
        SWE::WetController::GetSingleton()->ClearExternalWetness(a, key);
    }
    __declspec(dllexport) void SWE_SetExternalWetnessMask(RE::Actor* a, const char* key, float value, float durationSec,
                                                          unsigned int catMask) {
        if (!a || !key) return;

        const std::uint32_t raw = catMask;
        const std::uint8_t catBits = static_cast<std::uint8_t>(raw & 0x0Fu);
        const std::uint32_t flags = (raw & ~0x0Fu);

        const float i = std::clamp(value, 0.0f, 1.0f);

        if (auto* wc = SWE::WetController::GetSingleton()) {
            wc->SetExternalWetnessMask(a, key, i, durationSec, catBits, flags);
        }
    }

    __declspec(dllexport) void SWE_SetExternalWetnessEx(RE::Actor* a, const char* key, float value, float durationSec,
                                                        unsigned int catMask, float maxGloss, float maxSpec,
                                                        float minGloss, float minSpec, float glossBoost,
                                                        float specBoost, float skinHairMul) {
        if (!a || !key) return;
        SWE::WetController::OverrideParams ov{};
        ov.maxGloss = maxGloss;
        ov.maxSpec = maxSpec;
        ov.minGloss = minGloss;
        ov.minSpec = minSpec;
        ov.glossBoost = glossBoost;
        ov.specBoost = specBoost;
        ov.skinHairMul = skinHairMul;
        SWE::WetController::GetSingleton()->SetExternalWetnessEx(a, key, value, durationSec,
                                                                 static_cast<std::uint8_t>(catMask & 0x0F), ov);
    }

    __declspec(dllexport) float SWE_GetActorSubmergeLevel(RE::Actor* a) {
        if (auto* wc = SWE::WetController::GetSingleton()) {
            return wc->GetSubmergedLevel(a);
        }
        return 0.0f;
    }

    __declspec(dllexport) bool SWE_IsActorInWater(RE::Actor* a) {
        if (auto* wc = SWE::WetController::GetSingleton()) {
            return wc->IsActorWetByWater(a);
        }
        return false;
    }

    __declspec(dllexport) bool SWE_IsWetWeatherAround(RE::Actor* a) {
        if (auto* wc = SWE::WetController::GetSingleton()) {
            return wc->IsWetWeatherAround(a);
        }
        return false;
    }

    __declspec(dllexport) bool SWE_IsNearHeatSource(RE::Actor* a, float radius) {
        if (auto* wc = SWE::WetController::GetSingleton()) {
            return wc->IsNearHeatSource(a, radius);
        }
        return false;
    }

    __declspec(dllexport) bool SWE_IsUnderRoof(RE::Actor* a) {
        if (auto* wc = SWE::WetController::GetSingleton()) {
            return wc->IsUnderRoof(a);
        }
        return false;
    }

    __declspec(dllexport) bool SWE_IsActorInExteriorWet(RE::Actor* a) {
        if (auto* wc = SWE::WetController::GetSingleton()) {
            return wc->IsActorInExteriorWet(a);
        }
        return false;
    }

    __declspec(dllexport) unsigned SWE_GetEnvMask(RE::Actor* a) {
        unsigned m = 0;
        auto* wc = SWE::WetController::GetSingleton();
        if (!a || !wc) return m;

        if (wc->IsActorWetByWater(a)) {
            m |= SWE::Util::SWE_ENV_WATER;
        }

        const bool wetWorld = wc->IsWetWeatherAround(a);
        if (wetWorld) {
            m |= SWE::Util::SWE_ENV_WET_WEATHER;
        }

        const float heatR = std::max(50.0f, Settings::nearFireRadius.load());
        if (wc->IsNearHeatSource(a, heatR)) {
            m |= SWE::Util::SWE_ENV_NEAR_HEAT;
        }

        const bool underRoof = wc->IsUnderRoof(a);
        if (underRoof) {
            m |= SWE::Util::SWE_ENV_UNDER_ROOF;
        }

        if (wc->IsActorInExteriorWet(a) && !underRoof) {
            m |= SWE::Util::SWE_ENV_EXTERIOR_OPEN;
        }

        return m;
    }

}


extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const SKSE::LoadInterface* skse) {
    SetupLog();
    SKSE::Init(skse);

    Settings::LoadFromJson(Settings::DefaultPath());

    if (auto* ser = SKSE::GetSerializationInterface()) {
        ser->SetUniqueID(SWE::kSerID);
        ser->SetSaveCallback(SWE::OnSave);
        ser->SetLoadCallback(SWE::OnLoad);
    }

    SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* msg) {
        if (!msg) return;

        if (msg->sender && (std::strcmp(msg->sender, "NiOverride") == 0 || std::strcmp(msg->sender, "SKEE") == 0 ||
                            std::strcmp(msg->sender, "RaceMenu") == 0)) {
            if (msg->type == InterfaceExchangeMessage::kMessage_ExchangeInterface && msg->data) {
                auto* ex = static_cast<InterfaceExchangeMessage*>(msg->data);
                SWE::OverlayMgr::Get()->OnInterfaceMap(ex->interfaceMap);
            }
            return;
        }

        auto* wc = SWE::WetController::GetSingleton();
        switch (msg->type) {
            case SKSE::MessagingInterface::kDataLoaded:
                wc->Install();
                if (const auto pap = SKSE::GetPapyrusInterface(); pap) {
                    pap->Register(SWE::Papyrus::Register);
                }
                SKSE::GetTaskInterface()->AddTask([]() { UI::Register(); });
                break;

            case SKSE::MessagingInterface::kPreLoadGame:
                wc->Stop();
                wc->OnPreLoadGame();
                UI::ResetRegistration();
                SKSE::GetTaskInterface()->AddTask([]() { UI::Register(); });
                break;

            case SKSE::MessagingInterface::kNewGame:
                wc->OnPostLoadGame();
                wc->Start();
                break;

            case SKSE::MessagingInterface::kPostLoadGame:
                wc->OnPostLoadGame();
                wc->Start();
                if (!SWE::OverlayMgr::Get()->HasInterfaces()) RequestRMInterfaces();
                break;
            default:
                break;
        }
    });

    return true;
}
