#pragma once

using namespace std::literals;
#include <spdlog/spdlog.h>

#include <cstdint>

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"
#include "Settings.h"
#include "UI.h"
#include "WetController.h"

namespace SWE {
    static constexpr std::uint32_t kSerVersion = 1;
    static constexpr std::uint32_t kSerID = 'SWE1';

    void OnSave(SKSE::SerializationInterface* intfc);
    void OnLoad(SKSE::SerializationInterface* intfc);
}
