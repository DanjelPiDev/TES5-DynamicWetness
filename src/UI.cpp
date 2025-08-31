#include "UI.h"

#ifndef IM_ARRAYSIZE
    #define IM_ARRAYSIZE(_ARR) ((int)(sizeof(_ARR) / sizeof(*(_ARR))))
#endif

namespace {
    static std::atomic<bool> g_registered{false};
}

void UI::ResetRegistration() { g_registered.store(false); }

void UI::Register() {
    if (g_registered.load()) return;
    if (!SKSEMenuFramework::IsInstalled()) return;

    SKSEMenuFramework::SetSection("Skyrim Wet Effect");
    SKSEMenuFramework::AddSectionItem("General", UI::WetConfig::RenderGeneral);
    SKSEMenuFramework::AddSectionItem("Sources & Timings", UI::WetConfig::RenderSources);
    SKSEMenuFramework::AddSectionItem("Materials", UI::WetConfig::RenderMaterials);
    SKSEMenuFramework::AddSectionItem("NPCs", UI::WetConfig::RenderNPCs);

    g_registered.store(true);
}

void __stdcall UI::WetConfig::RenderGeneral() {
    FontAwesome::PushSolid();
    if (ImGui::CollapsingHeader(generalHeader.c_str())) {
        bool en = Settings::modEnabled.load();
        if (ImGui::Checkbox("Enable Mod", &en)) Settings::modEnabled.store(en);

        int upd = Settings::updateIntervalMs.load();
        if (ImGui::SliderInt("Update Interval (ms)", &upd, 10, 1000)) {
            Settings::updateIntervalMs.store(upd);
        }

        ImGui::Separator();
        if (ImGui::Button("Reapply Now")) {
            SWE::WetController::GetSingleton()->RefreshNow();
        }

        ImGui::Separator();
        FontAwesome::PushSolid();
        if (ImGui::Button(saveIcon.c_str())) {
            Settings::SaveToJson(Settings::DefaultPath());
            SWE::WetController::GetSingleton()->RefreshNow();
        }
        FontAwesome::Pop();
    }
    FontAwesome::Pop();
}

void __stdcall UI::WetConfig::RenderSources() {
    FontAwesome::PushSolid();
    if (ImGui::CollapsingHeader(sourcesHeader.c_str())) {
        bool rs = Settings::rainSnowEnabled.load();
        if (ImGui::Checkbox("Enable rain/snow wetting", &rs)) Settings::rainSnowEnabled.store(rs);

        bool snow = Settings::affectInSnow.load();
        if (ImGui::Checkbox("Affect in snow", &snow)) Settings::affectInSnow.store(snow);

        bool ig = Settings::ignoreInterior.load();
        if (ImGui::Checkbox("Ignore interiors", &ig)) Settings::ignoreInterior.store(ig);

        ImGui::Separator();
        float w = Settings::secondsToSoakWater.load();
        if (ImGui::SliderFloat("Seconds to fully soak (Water)", &w, 0.5f, 120.0f, "%.1f")) {
            Settings::secondsToSoakWater.store(w);
        }
        float r = Settings::secondsToSoakRain.load();
        if (ImGui::SliderFloat("Seconds to fully soak (Rain/Snow)", &r, 5.0f, 3600.0f, "%.1f")) {
            Settings::secondsToSoakRain.store(r);
        }
        float d = Settings::secondsToDry.load();
        if (ImGui::SliderFloat("Seconds to dry", &d, 2.0f, 3600.0f, "%.1f")) {
            Settings::secondsToDry.store(d);
        }

        ImGui::Separator();
        FontAwesome::PushSolid();
        if (ImGui::Button(saveIcon.c_str())) {
            Settings::SaveToJson(Settings::DefaultPath());
        }
        FontAwesome::Pop();
    }
    FontAwesome::Pop();
}

void __stdcall UI::WetConfig::RenderMaterials() {
    FontAwesome::PushSolid();
    if (ImGui::CollapsingHeader(materialsHeader.c_str())) {
        bool s = Settings::affectSkin.load();
        if (ImGui::Checkbox("Affect Skin/Face", &s)) Settings::affectSkin.store(s);
        bool h = Settings::affectHair.load();
        if (ImGui::Checkbox("Affect Hair", &h)) Settings::affectHair.store(h);
        bool a = Settings::affectArmor.load();
        if (ImGui::Checkbox("Affect Armor", &a)) Settings::affectArmor.store(a);
        bool w = Settings::affectWeapons.load();
        if (ImGui::Checkbox("Affect Weapons", &w)) Settings::affectWeapons.store(w);

        ImGui::Separator();
        float gb = Settings::glossinessBoost.load();
        if (ImGui::SliderFloat("Glossiness Boost (at 100% wet)", &gb, 0.0f, 10000.0f, "%.0f")) {
            Settings::glossinessBoost.store(gb);
        }
        float sb = Settings::specularScaleBoost.load();
        if (ImGui::SliderFloat("Specular Scale Boost (at 100% wet)", &sb, 0.0f, 100.0f, "%.2f")) {
            Settings::specularScaleBoost.store(sb);
        }
        float gmx = Settings::maxGlossiness.load();
        if (ImGui::SliderFloat("Max Glossiness", &gmx, 1.0f, 10000.0f, "%.0f")) {
            Settings::maxGlossiness.store(gmx);
        }
        float smx = Settings::maxSpecularStrength.load();
        if (ImGui::SliderFloat("Max Specular Strength", &smx, 0.1f, 1000.0f, "%.2f")) {
            Settings::maxSpecularStrength.store(smx);
        }

        ImGui::Separator();
        FontAwesome::PushSolid();
        if (ImGui::Button(saveIcon.c_str())) {
            Settings::SaveToJson(Settings::DefaultPath());
            SWE::WetController::GetSingleton()->RefreshNow();
        }
        FontAwesome::Pop();
    }
    FontAwesome::Pop();
}

void __stdcall UI::WetConfig::RenderNPCs() {
    FontAwesome::PushSolid();
    if (ImGui::CollapsingHeader(npcsHeader.c_str())) {
        bool en = Settings::affectNPCs.load();
        if (ImGui::Checkbox("Affect NPCs", &en)) Settings::affectNPCs.store(en);

        int r = Settings::npcRadius.load();
        if (ImGui::SliderInt("NPC Radius (0 = All loaded)", &r, 0, 16384)) {
            Settings::npcRadius.store(r);
        }

        ImGui::Separator();
        FontAwesome::PushSolid();
        if (ImGui::Button(saveIcon.c_str())) {
            Settings::SaveToJson(Settings::DefaultPath());
        }
        FontAwesome::Pop();
    }
    FontAwesome::Pop();
}
