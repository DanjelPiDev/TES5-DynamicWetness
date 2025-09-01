#pragma once
#include "SKSEMenuFramework.h"
#include "Settings.h"
#include "WetController.h"

namespace UI {
    void Register();
    void ResetRegistration();

    namespace WetConfig {
        void __stdcall RenderGeneral();
        void __stdcall RenderSources();
        void __stdcall RenderMaterials();
        void __stdcall RenderNPCs();

        inline std::string saveIcon = FontAwesome::UnicodeToUtf8(0xf0c7) + " Save Settings";
        inline std::string resetIcon = FontAwesome::UnicodeToUtf8(0xf0e2) + " Reset to Defaults";

        inline std::string generalHeader = FontAwesome::UnicodeToUtf8(0xf013) + " General";
        inline std::string sourcesHeader = FontAwesome::UnicodeToUtf8(0xf743) + " Sources & Timings";
        inline std::string materialsHeader = FontAwesome::UnicodeToUtf8(0xf5fd) + " Material Response";
        inline std::string npcsHeader = FontAwesome::UnicodeToUtf8(0xf544) + " NPC Settings";
    }
}
