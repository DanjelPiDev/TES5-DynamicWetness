#include "Main.h"

using namespace SKSE;

namespace SWE {

    void OnSave(SKSE::SerializationInterface* intfc) {
        auto* wc = WetController::GetSingleton();
        float playerWet = wc ? wc->GetPlayerWetness() : 0.0f;
        if (intfc->OpenRecord(kSerID, kSerVersion)) {
            intfc->WriteRecordData(&playerWet, sizeof(playerWet));
        }
    }

    void OnLoad(SKSE::SerializationInterface* intfc) {
        std::uint32_t type, version, length;
        while (intfc->GetNextRecordInfo(type, version, length)) {
            if (type != kSerID || version != kSerVersion) {
                std::vector<char> skip(length);
                if (length > 0) intfc->ReadRecordData(skip.data(), static_cast<std::uint32_t>(skip.size()));
                continue;
            }
            float playerWet = 0.0f;
            if (length >= sizeof(float)) {
                intfc->ReadRecordData(&playerWet, sizeof(playerWet));
            } else {
                std::vector<char> skip(length);
                if (length > 0) intfc->ReadRecordData(skip.data(), static_cast<std::uint32_t>(skip.size()));
            }
            if (auto* wc = WetController::GetSingleton()) {
                wc->SetPlayerWetnessSnapshot(playerWet);
                SKSE::GetTaskInterface()->AddTask([]() {
                    if (auto* w = WetController::GetSingleton()) w->RefreshNow();
                });
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
            spdlog::set_level(spdlog::level::trace);  // alles mitnehmen
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

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }

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
        auto* wc = SWE::WetController::GetSingleton();
        switch (msg->type) {
            case SKSE::MessagingInterface::kDataLoaded:
                wc->Install();
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
                break;

            default:
                break;
        }
    });


    return true;
}
