#include "UI.h"

#include "utils/Utils.h"

#ifndef IM_ARRAYSIZE
    #define IM_ARRAYSIZE(_ARR) ((int)(sizeof(_ARR) / sizeof(*(_ARR))))
#endif

namespace {
    static std::atomic<bool> g_registered{false};

    inline float clampf(float v, float lo, float hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }
    inline int clampi(int v, int lo, int hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }

    static void HelpMarker(const char* text) {
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }
    static void SubHeader(const char* label) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.85f, 0.92f, 1.0f, 1.0f), "%s", label);
    }
    static void BadgeBoolFA(std::uint32_t iconOn, const char* textOn, std::uint32_t iconOff, const char* textOff,
                            bool on) {
        const ImVec4 c = on ? ImVec4(0.38f, 0.84f, 0.52f, 1.0f) : ImVec4(0.60f, 0.60f, 0.60f, 1.0f);

        const std::string icon = FontAwesome::UnicodeToUtf8(on ? iconOn : iconOff);
        const char* labelText = on ? textOn : textOff;

        ImGui::SameLine(0, 8);
        FontAwesome::PushSolid();
        ImGui::TextColored(c, "%s %s", icon.c_str(), labelText);
        FontAwesome::Pop();
    }
    static void BadgeBool(const char* onLabel, const char* offLabel, bool on) {
        ImVec4 c = on ? ImVec4(0.38f, 0.84f, 0.52f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
        ImGui::SameLine(0, 8);
        ImGui::TextColored(c, "%s", on ? onLabel : offLabel);
    }
    static void BadgeBool(const char8_t* onLabel, const char8_t* offLabel, bool on) {
        BadgeBool(reinterpret_cast<const char*>(onLabel), reinterpret_cast<const char*>(offLabel), on);
    }

    static float DefaultInputWidthForFormat(const char* fmt) {
        const char* sample = (strstr(fmt, ".2f") || strstr(fmt, "%.2f"))   ? "-00000.00"
                             : (strstr(fmt, ".1f") || strstr(fmt, "%.1f")) ? "-00000.0"
                             : (strstr(fmt, "%d"))                         ? "-999999"
                                                                           : "-000000.000";
        ImVec2 sz{};
        ImGui::CalcTextSize(&sz, sample, nullptr, false, 0.0f);
        const float padX = ImGui::GetStyle()->FramePadding.x;
        return sz.x + padX * 4.0f;
    }

    static bool FloatControl(const char* label, float& v, float minV, float maxV, const char* fmt = "%.2f",
                             float step = 0.1f, float stepFast = 1.0f, const char* tooltip = nullptr,
                             ImGuiSliderFlags sflags = ImGuiSliderFlags_AlwaysClamp) {
        bool changed = false;
        ImGui::PushID(label);

        ImVec2 avail{};
        ImGui::GetContentRegionAvail(&avail);
        float fullWidth = avail.x;

        const ImGuiStyle* st = ImGui::GetStyle();

        const float inputW = DefaultInputWidthForFormat(fmt) * 1.25f;
        const float btnW = ImGui::GetFrameHeight() * 1.20f;
        const float innerX = st->ItemInnerSpacing.x;

        float sliderW = fullWidth - inputW - btnW * 2.0f - innerX * 3.0f;
        if (sliderW < 100.0f) sliderW = 100.0f;

        const ImVec2 padSlider = ImVec2(st->FramePadding.x, st->FramePadding.y * 0.60f);
        const float grabMin = st->GrabMinSize * 0.85f;
        const ImVec2 padBig = ImVec2(st->FramePadding.x, st->FramePadding.y * 1.25f);

        ImGui::BeginGroup();

        ImGui::TextWrapped("%s", label);
        if (tooltip) HelpMarker(tooltip);

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, padSlider);
        ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, grabMin);
        ImGui::PushItemWidth(sliderW);
        float tmp = v;
        if (ImGui::SliderFloat("##slider", &tmp, minV, maxV, fmt, sflags)) {
            v = clampf(tmp, minV, maxV);
            changed = true;
        }
        ImGui::PopItemWidth();
        ImGui::PopStyleVar(2);

        ImGui::SameLine();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, padBig);
        ImGui::PushItemWidth(inputW);
        if (ImGui::InputFloat("##input", &v, step, stepFast, fmt,
                              ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsScientific)) {
            v = clampf(v, minV, maxV);
            changed = true;
        }
        ImGui::PopItemWidth();
        ImGui::PopStyleVar();

        ImGui::EndGroup();
        ImGui::PopID();
        return changed;
    }

    static bool IntControl(const char* label, int& v, int minV, int maxV, const char* fmt = "%d", int step = 1,
                           int stepFast = 10, const char* tooltip = nullptr,
                           ImGuiSliderFlags sflags = ImGuiSliderFlags_AlwaysClamp) {
        bool changed = false;
        ImGui::PushID(label);

        ImVec2 avail{};
        ImGui::GetContentRegionAvail(&avail);
        float fullWidth = avail.x;

        const ImGuiStyle* st = ImGui::GetStyle();

        const float inputW = DefaultInputWidthForFormat(fmt) * 2.0f;
        const float btnW = ImGui::GetFrameHeight() * 1.20f;
        const float innerX = st->ItemInnerSpacing.x;

        float sliderW = fullWidth - inputW - btnW * 2.0f - innerX * 3.0f;
        if (sliderW < 100.0f) sliderW = 100.0f;

        const ImVec2 padSlider = ImVec2(st->FramePadding.x, st->FramePadding.y * 0.60f);
        const float grabMin = st->GrabMinSize * 0.85f;
        const ImVec2 padBig = ImVec2(st->FramePadding.x, st->FramePadding.y * 1.25f);

        ImGui::BeginGroup();

        ImGui::TextWrapped("%s", label);
        if (tooltip) HelpMarker(tooltip);

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, padSlider);
        ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, grabMin);
        ImGui::PushItemWidth(sliderW);
        int tmp = v;
        if (ImGui::SliderInt("##slider", &tmp, minV, maxV, fmt, sflags)) {
            v = clampi(tmp, minV, maxV);
            changed = true;
        }
        ImGui::PopItemWidth();
        ImGui::PopStyleVar(2);

        ImGui::SameLine();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, padBig);
        ImGui::PushItemWidth(inputW);
        if (ImGui::InputInt("##input", &v, step, stepFast, ImGuiInputTextFlags_EnterReturnsTrue)) {
            v = clampi(v, minV, maxV);
            changed = true;
        }
        ImGui::PopItemWidth();
        ImGui::PopStyleVar();

        ImGui::EndGroup();
        ImGui::PopID();
        return changed;
    }

    static void SaveResetRow(bool showReapply = false) {
        FontAwesome::PushSolid();
        if (ImGui::Button(UI::WetConfig::saveIcon.c_str())) {
            Settings::SaveToJson(Settings::DefaultPath());
        }
        ImGui::SameLine();
        if (ImGui::Button(UI::WetConfig::resetIcon.c_str())) {
            Settings::ResetToDefaults();
            SWE::WetController::GetSingleton()->RefreshNow();
        }
        if (showReapply) {
            ImGui::SameLine();
            if (ImGui::Button("Reapply Now")) {
                SWE::WetController::GetSingleton()->RefreshNow();
            }
        }
        FontAwesome::Pop();
    }

    static void ApplyPreset_Subtle() {
        Settings::glossinessBoost.store(60.0f);
        Settings::specularScaleBoost.store(3.0f);
        Settings::skinHairResponseMul.store(2.0f);
        Settings::maxGlossiness.store(400.0f);
        Settings::maxSpecularStrength.store(5.0f);
        Settings::secondsToSoakRain.store(60.0f);
        Settings::secondsToSoakSnow.store(std::round(Settings::secondsToSoakRain.load() * 1.25f));
        Settings::secondsToDry.store(45.0f);
        Settings::pbrFriendlyMode.store(true);
        Settings::pbrArmorWeapMul.store(0.35f);
        Settings::pbrMaxGlossArmor.store(260.0f);
        Settings::pbrMaxSpecArmor.store(4.0f);
    }
    static void ApplyPreset_Balanced() {
        Settings::glossinessBoost.store(120.0f);
        Settings::specularScaleBoost.store(8.0f);
        Settings::skinHairResponseMul.store(5.0f);
        Settings::maxGlossiness.store(800.0f);
        Settings::maxSpecularStrength.store(10.0f);
        Settings::secondsToSoakRain.store(36.0f);
        Settings::secondsToSoakSnow.store(std::round(Settings::secondsToSoakRain.load() * 1.25f));
        Settings::secondsToDry.store(40.0f);
        Settings::pbrFriendlyMode.store(false);
        Settings::pbrArmorWeapMul.store(0.5f);
        Settings::pbrMaxGlossArmor.store(300.0f);
        Settings::pbrMaxSpecArmor.store(5.0f);
    }
    static void ApplyPreset_GlossyENB() {
        Settings::glossinessBoost.store(200.0f);
        Settings::specularScaleBoost.store(10.0f);
        Settings::skinHairResponseMul.store(6.0f);
        Settings::maxGlossiness.store(1200.0f);
        Settings::maxSpecularStrength.store(15.0f);
        Settings::pbrFriendlyMode.store(false);
    }
    static void ApplyPreset_PBRFriendly() {
        Settings::pbrFriendlyMode.store(true);
        Settings::pbrArmorWeapMul.store(0.5f);
        Settings::pbrMaxGlossArmor.store(300.0f);
        Settings::pbrMaxSpecArmor.store(5.0f);

        Settings::specularScaleBoost.store(std::min(6.0f, Settings::specularScaleBoost.load()));
        Settings::glossinessBoost.store(std::min(120.0f, Settings::glossinessBoost.load()));
    }

}

void UI::ResetRegistration() { g_registered.store(false); }

void UI::Register() {
    if (g_registered.load()) return;
    if (!SKSEMenuFramework::IsInstalled()) return;

    SKSEMenuFramework::SetSection("Dynamic Wetness");
    SKSEMenuFramework::AddSectionItem("General", UI::WetConfig::RenderGeneral);
    SKSEMenuFramework::AddSectionItem("Sources & Timings", UI::WetConfig::RenderSources);
    SKSEMenuFramework::AddSectionItem("Materials", UI::WetConfig::RenderMaterials);
    SKSEMenuFramework::AddSectionItem("NPCs", UI::WetConfig::RenderNPCs);

    g_registered.store(true);
}

void __stdcall UI::WetConfig::RenderGeneral() {
    if (ImGui::CollapsingHeader("Overview & Presets", ImGuiTreeNodeFlags_DefaultOpen)) {
        SubHeader("Player Status");
        if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
            const float wet = SWE::Util::GetFinalWetness(pc);  // 0..1
            ImGui::Text("Wetness: %.0f%%", wet * 100.0f);
            ImGui::ProgressBar(wet, ImVec2(-FLT_MIN, 0.0f), "Wetness");

            auto env = SWE::Util::QueryEnvironment(pc);
            ImGui::TextDisabled("Environment:");
            BadgeBoolFA(0xf773, "In Water", 0xf773, "Not in Water", env.inWater);
            BadgeBoolFA(0xf740, "Wet Weather", 0xf740, "Dry Weather", env.wetWeather);
            BadgeBoolFA(0xf6d9, "Under Roof", 0xf6d9, "Open Sky", env.underRoof);
            BadgeBoolFA(0xf06d, "Near Heat", 0xf06d, "No Heat", env.nearHeat);
            BadgeBoolFA(0xf185, "Exterior", 0xf015, "Interior", env.exteriorOpen);
        } else {
            ImGui::TextDisabled("Player not available.");
        }

        // Presets
        SubHeader("Quick Presets");
        ImGui::TextDisabled("Click to apply a curated set of values. You can still tweak everything below.");
        if (ImGui::Button("Subtle")) {
            ApplyPreset_Subtle();
            SWE::WetController::GetSingleton()->RefreshNow();
        }
        ImGui::SameLine();
        if (ImGui::Button("Balanced")) {
            ApplyPreset_Balanced();
            SWE::WetController::GetSingleton()->RefreshNow();
        }
        ImGui::SameLine();
        if (ImGui::Button("Glossy ENB")) {
            ApplyPreset_GlossyENB();
            SWE::WetController::GetSingleton()->RefreshNow();
        }
        ImGui::SameLine();
        if (ImGui::Button("PBR-friendly")) {
            ApplyPreset_PBRFriendly();
            SWE::WetController::GetSingleton()->RefreshNow();
        }
    }

    FontAwesome::PushSolid();
    if (ImGui::CollapsingHeader(generalHeader.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        bool en = Settings::modEnabled.load();
        if (ImGui::Checkbox("Enable Mod", &en)) Settings::modEnabled.store(en);

        int upd = Settings::updateIntervalMs.load();
        if (IntControl("Update Interval (ms)", upd, 10, 1000, "%d", 5, 25,
                       "How often the logic runs. Higher = less frequent.")) {
            Settings::updateIntervalMs.store(upd);
        }
    }
    FontAwesome::Pop();

    if (ImGui::CollapsingHeader("Integration (API)")) {
        int mode = Settings::externalBlendMode.load();
        ImGui::Text("External Wetness Blend");
        ImGui::RadioButton("Max", &mode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Additive (cap 1.0)", &mode, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Max + weighted rest", &mode, 2);
        Settings::externalBlendMode.store(mode);

        if (mode == 2) {
            float w = Settings::externalAddWeight.load();
            if (FloatControl("Rest weight", w, 0.f, 1.f, "%.2f", 0.01f, 0.05f,
                             "Weight for the non-maximum external contributions when using 'Max + weighted rest'")) {
                Settings::externalAddWeight.store(w);
            }
        }
    }

    ImGui::Separator();
    SaveResetRow(true);
}

void __stdcall UI::WetConfig::RenderSources() {
    FontAwesome::PushSolid();
    if (ImGui::CollapsingHeader(sourcesHeader.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        // Precipitation
        SubHeader("Precipitation");
        bool rain = Settings::rainEnabled.load();
        bool snow = Settings::snowEnabled.load();
        if (ImGui::Checkbox("Enable rain wetting", &rain)) Settings::rainEnabled.store(rain);
        ImGui::SameLine();
        if (ImGui::Checkbox("Enable snow wetting", &snow)) Settings::snowEnabled.store(snow);

        bool ig = Settings::ignoreInterior.load();
        if (ImGui::Checkbox("Ignore interiors", &ig)) Settings::ignoreInterior.store(ig);
        HelpMarker("If enabled, interiors disregard rain/snow.");

        // Timings
        SubHeader("Timings");
        {
            float w = Settings::secondsToSoakWater.load();
            if (FloatControl("Seconds to fully soak (Water)", w, 0.5f, 120.0f, "%.0f", 1.0f, 5.0f,
                             "Time from 0% -> 100% wetness while in water.")) {
                Settings::secondsToSoakWater.store(w);
            }
        }
        {
            float rr = Settings::secondsToSoakRain.load();
            if (FloatControl("Seconds to fully soak (Rain)", rr, 5.0f, 3600.0f, "%.0f", 5.0f, 30.0f)) {
                Settings::secondsToSoakRain.store(rr);
            }
        }
        {
            float rs = Settings::secondsToSoakSnow.load();
            if (FloatControl("Seconds to fully soak (Snow)", rs, 5.0f, 3600.0f, "%.0f", 5.0f, 30.0f)) {
                Settings::secondsToSoakSnow.store(rs);
            }
        }
        {
            float d = Settings::secondsToDry.load();
            if (FloatControl("Seconds to dry", d, 2.0f, 7200.0f, "%.0f", 5.0f, 30.0f,
                             "Time from 100% -> 0% wetness (without fire/heat boost).")) {
                Settings::secondsToDry.store(d);
            }
        }

        // Waterfalls
        SubHeader("Waterfalls");
        if (Settings::waterfallEnabled.load()) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.6f, 1.0f),
                               "Waterfall wetting is experimental. Disable if you see odd effects.");
        }
        {
            bool wf = Settings::waterfallEnabled.load();
            if (ImGui::Checkbox("Enable waterfall spray wetting", &wf)) Settings::waterfallEnabled.store(wf);
        }
        {
            float r = Settings::secondsToSoakWaterfall.load();
            if (FloatControl("Seconds to fully soak (Waterfall spray)", r, 1.0f, 3600.0f, "%.0f", 1.0f, 5.0f,
                             "Time to reach 100% wetness when inside waterfall FX volume.")) {
                Settings::secondsToSoakWaterfall.store(r);
            }
        }
        {
            float rad = Settings::nearWaterfallRadius.load();
            if (FloatControl("Waterfall detection radius (units)", rad, 5.0f, 3000.0f, "%.0f", 10.0f, 50.0f,
                             "How far to scan for waterfall FX around the actor.")) {
                Settings::nearWaterfallRadius.store(rad);
            }
        }
        ImGui::TextDisabled("FX bounds padding:");
        {
            float px = Settings::waterfallWidthPad.load();
            if (FloatControl("Width pad (X)", px, 0.f, 300.f, "%.0f")) Settings::waterfallWidthPad.store(px);
        }
        {
            float py = Settings::waterfallDepthPad.load();
            if (FloatControl("Depth pad (Y)", py, 0.f, 400.f, "%.0f")) Settings::waterfallDepthPad.store(py);
        }
        {
            float pz = Settings::waterfallZPad.load();
            if (FloatControl("Height pad (Z)", pz, 0.f, 400.f, "%.0f")) Settings::waterfallZPad.store(pz);
        }

        // Threshold
        SubHeader("Water Depth Threshold");
        {
            float ms = Settings::minSubmergeToSoak.load();
            if (FloatControl("Min water depth to start soaking (0..1)", ms, 0.0f, 0.8f, "%.2f", 0.01f, 0.05f,
                             "Relative depth at which water starts soaking you.")) {
                Settings::minSubmergeToSoak.store(ms);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("[%.0f%%]", Settings::minSubmergeToSoak.load() * 100.0f);
        }

        ImGui::Separator();
        SaveResetRow();
    }
    FontAwesome::Pop();
}

void __stdcall UI::WetConfig::RenderMaterials() {
    FontAwesome::PushSolid();
    if (ImGui::CollapsingHeader(materialsHeader.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        SubHeader("Categories Affected");
        bool s = Settings::affectSkin.load();
        bool h = Settings::affectHair.load();
        bool a = Settings::affectArmor.load();
        bool w = Settings::affectWeapons.load();

        // compact row
        ImGui::Checkbox("Skin/Face", &s);
        ImGui::SameLine();
        ImGui::Checkbox("Hair", &h);
        ImGui::SameLine();
        ImGui::Checkbox("Armor", &a);
        ImGui::SameLine();
        ImGui::Checkbox("Weapons", &w);

        if (s != Settings::affectSkin.load()) Settings::affectSkin.store(s);
        if (h != Settings::affectHair.load()) Settings::affectHair.store(h);
        if (a != Settings::affectArmor.load()) Settings::affectArmor.store(a);
        if (w != Settings::affectWeapons.load()) Settings::affectWeapons.store(w);

        SubHeader("Response & Limits");
        {
            float gb = Settings::glossinessBoost.load();
            if (FloatControl("Glossiness Boost (at 100% wet)", gb, 0.0f, 10000.0f, "%.0f", 5.0f, 50.0f,
                             "How much gloss is added at 100% wetness.")) {
                Settings::glossinessBoost.store(gb);
            }
        }
        {
            float sb = Settings::specularScaleBoost.load();
            if (FloatControl("Specular Scale Boost (at 100% wet)", sb, 0.0f, 100.0f, "%.2f", 0.1f, 1.0f,
                             "Factor by which specular intensity increases at 100% wetness.")) {
                Settings::specularScaleBoost.store(sb);
            }
        }
        {
            float k = Settings::skinHairResponseMul.load();
            if (FloatControl("Skin/Hair response × (gloss & spec)", k, 0.1f, 100.0f, "%.1f", 0.5f, 1.0f,
                             "Extra multiplier on skin & hair only (keeps armor/weapons unchanged).")) {
                Settings::skinHairResponseMul.store(k);
            }
        }
        {
            float gmx = Settings::maxGlossiness.load();
            float gmn = Settings::minGlossiness.load();
            if (FloatControl("Max Glossiness", gmx, 1.0f, 10000.0f, "%.0f", 10.0f, 100.0f,
                             "Hard clamp for glossiness.")) {
                if (gmn > gmx) gmn = gmx;
                Settings::maxGlossiness.store(gmx);
                Settings::minGlossiness.store(gmn);
            }
            if (FloatControl("Min Glossiness", gmn, 0.0f, 300.0f, "%.1f", 0.5f, 5.0f,
                             "Below this it is considered non-glossy (depends on material).")) {
                if (gmn > Settings::maxGlossiness.load()) {
                    Settings::maxGlossiness.store(gmn);
                }
                Settings::minGlossiness.store(gmn);
            }
        }
        {
            float smx = Settings::maxSpecularStrength.load();
            float smn = Settings::minSpecularStrength.load();
            if (FloatControl("Max Specular Strength", smx, 0.1f, 1000.0f, "%.2f", 0.1f, 1.0f,
                             "Clamp for specular color channels (RGB).")) {
                if (smn > smx) smn = smx;
                Settings::maxSpecularStrength.store(smx);
                Settings::minSpecularStrength.store(smn);
            }
            if (FloatControl("Min Specular Strength", smn, 0.0f, 100.0f, "%.2f", 0.05f, 0.5f,
                             "Below this it is considered non-specular.")) {
                if (smn > Settings::maxSpecularStrength.load()) {
                    Settings::maxSpecularStrength.store(smn);
                }
                Settings::minSpecularStrength.store(smn);
            }
        }

        SubHeader("Drying Boost Near Heat");
        {
            float fr = Settings::nearFireRadius.load();
            if (FloatControl("Fireplace radius (units)", fr, 100.0f, 2000.0f, "%.0f", 10.0f, 50.0f,
                             "Range within which heat sources dry you faster.")) {
                Settings::nearFireRadius.store(fr);
            }
        }
        {
            float fm = Settings::dryMultiplierNearFire.load();
            if (FloatControl("Drying speed × near fire", fm, 1.0f, 100.0f, "%.2f", 0.1f, 0.5f,
                             "Multiplier for drying near fire.")) {
                Settings::dryMultiplierNearFire.store(fm);
            }
        }

        // PBR area
        SubHeader("PBR (Armor/Weapons)");
        {
            bool pbr = Settings::pbrFriendlyMode.load();
            if (ImGui::Checkbox("PBR-friendly for Armor/Weapons", &pbr)) Settings::pbrFriendlyMode.store(pbr);
            ImGui::SameLine();
            ImGui::TextDisabled("(Community Shaders PBR / roughness-metalness)");
            if (pbr) {
                ImGui::TextDisabled("Use milder caps on Armor/Weapons and avoid forcing spec on likely PBR sets.");
                float mul = Settings::pbrArmorWeapMul.load();
                if (FloatControl("Armor/Weapons response × (PBR)", mul, 0.0f, 1.0f, "%.2f", 0.01f, 0.1f,
                                 "Scales gloss/spec boosts for armor/weapons when PBR mode is enabled.")) {
                    Settings::pbrArmorWeapMul.store(mul);
                }
                {
                    float gmx = Settings::pbrMaxGlossArmor.load();
                    if (FloatControl("PBR Max Glossiness (Armor/Weapons)", gmx, 1.0f, 10000.0f, "%.0f", 10.0f, 50.0f,
                                     "Extra clamp applied on armor/weapons in PBR mode.")) {
                        Settings::pbrMaxGlossArmor.store(gmx);
                    }
                }
                {
                    float smx = Settings::pbrMaxSpecArmor.load();
                    if (FloatControl("PBR Max Specular Strength (Armor/Weapons)", smx, 0.1f, 1000.0f, "%.2f", 0.1f,
                                     1.0f, "Extra clamp applied on armor/weapons in PBR mode.")) {
                        Settings::pbrMaxSpecArmor.store(smx);
                    }
                }
            }
        }

        SaveResetRow(true);
    }
    FontAwesome::Pop();
}

void __stdcall UI::WetConfig::RenderNPCs() {
    FontAwesome::PushSolid();
    if (ImGui::CollapsingHeader(npcsHeader.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        // Top toggles
        bool en = Settings::affectNPCs.load();
        if (ImGui::Checkbox("Affect NPCs", &en)) Settings::affectNPCs.store(en);

        int r = Settings::npcRadius.load();
        if (IntControl("NPC Radius (0 = All loaded)", r, 0, 16384, "%d", 64, 256,
                       "Actors outside the radius are set dry immediately.")) {
            Settings::npcRadius.store(r);
        }

        bool optin = Settings::npcOptInOnly.load();
        if (ImGui::Checkbox("Opt-in tracking only (use list below)", &optin)) {
            Settings::npcOptInOnly.store(optin);
        }
        ImGui::TextDisabled("When enabled, only NPCs you add to the list will be updated in rain/waterfall/water.");

        ImGui::Separator();

        // Helpers to edit lists
        auto find_by_id = [](std::vector<Settings::FormSpec>& v, std::uint32_t fid) -> Settings::FormSpec* {
            auto it = std::find_if(v.begin(), v.end(), [&](const Settings::FormSpec& fs) { return fs.id == fid; });
            return (it == v.end()) ? nullptr : &(*it);
        };
        auto find_by_id_const = [](const std::vector<Settings::FormSpec>& v,
                                   std::uint32_t fid) -> const Settings::FormSpec* {
            auto it = std::find_if(v.begin(), v.end(), [&](const Settings::FormSpec& fs) { return fs.id == fid; });
            return (it == v.end()) ? nullptr : &(*it);
        };
        auto remove_from = [](std::vector<Settings::FormSpec>& v, std::uint32_t fid) {
            v.erase(std::remove_if(v.begin(), v.end(), [&](const Settings::FormSpec& fs) { return fs.id == fid; }),
                    v.end());
        };

        // ----- Add NPCs toolbar -----
        if (ImGui::TreeNodeEx("Add NPCs", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Button("Add nearby NPCs…")) ImGui::OpenPopup("swe_add_npc_nearby_unified");
            if (ImGui::BeginPopup("swe_add_npc_nearby_unified")) {
                static char filterN[48] = "";
                ImGui::InputTextWithHint("##fnear", "Filter by name…", filterN, IM_ARRAYSIZE(filterN));
                ImGui::Separator();
                if (auto* proc = RE::ProcessLists::GetSingleton()) {
                    RE::Actor* pc = RE::PlayerCharacter::GetSingleton();
                    ImGui::BeginChild("near_list", ImVec2(0, 240), true);
                    for (auto& h : proc->highActorHandles) {
                        RE::Actor* a = h.get().get();
                        if (!a || a == pc) continue;
                        auto* ab = a->GetActorBase();
                        if (!ab) continue;
                        const char* nm = ab->GetName();
                        std::string name = nm ? nm : "(no name)";
                        if (filterN[0] && name.find(filterN) == std::string::npos) continue;

                        const std::uint32_t fid = ab->GetFormID();
                        ImGui::PushID(static_cast<int>(fid));
                        ImGui::Text("%s  [0x%08X]", name.c_str(), fid);
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Add")) {
                            {
                                std::unique_lock lk(Settings::actorsMutex);
                                if (!find_by_id(Settings::trackedActors, fid)) {
                                    std::string plugin;
                                    if (auto* f = ab->GetFile(0)) plugin = f->GetFilename();
                                    Settings::trackedActors.push_back({plugin, fid, 1.0f, true, 0x0F});
                                }
                            }
                            SWE::WetController::GetSingleton()->RefreshNow();
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndChild();
                }
                if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            ImGui::SameLine();

            // Add by FormID
            {
                static char addHexAll[16] = "";
                static char addHexPlugin[64] = "";
                ImGui::SetNextItemWidth(140);
                ImGui::InputTextWithHint("##addHexAll", "FormID (hex)", addHexAll, IM_ARRAYSIZE(addHexAll),
                                         ImGuiInputTextFlags_CharsHexadecimal);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(220);
                ImGui::InputTextWithHint("##addHexPlugin", "Plugin name (optional)", addHexPlugin,
                                         IM_ARRAYSIZE(addHexPlugin));
                ImGui::SameLine();
                if (ImGui::Button("Add by FormID")) {
                    std::uint32_t id = 0;
                    if (std::strlen(addHexAll) > 0) {
                        std::string s = addHexAll;
                        if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0) s = s.substr(2);
                        auto res = std::from_chars(s.data(), s.data() + s.size(), id, 16);
                        if (res.ec == std::errc()) {
                            std::string plugin = addHexPlugin;
                            {
                                std::unique_lock lk(Settings::actorsMutex);
                                if (!find_by_id(Settings::trackedActors, id))
                                    Settings::trackedActors.push_back({plugin, id, 1.0f, true, 0x0F});
                                if (!find_by_id(Settings::actorOverrides, id))
                                    Settings::actorOverrides.push_back({plugin, id, 1.0f, true, 0x0F});
                            }
                            SWE::WetController::GetSingleton()->RefreshNow();
                        }
                    }
                }
            }
            ImGui::SameLine();

            // Find by Name
            {
                static char addNameAll[64] = "";
                ImGui::SetNextItemWidth(180);
                ImGui::InputTextWithHint("##addNameAll", "Name contains...", addNameAll, IM_ARRAYSIZE(addNameAll));
                ImGui::SameLine();
                if (ImGui::Button("Find by Name")) ImGui::OpenPopup("swe_add_npc_by_name_unified");
                if (ImGui::BeginPopup("swe_add_npc_by_name_unified")) {
                    if (auto* proc = RE::ProcessLists::GetSingleton()) {
                        RE::Actor* pc = RE::PlayerCharacter::GetSingleton();
                        ImGui::BeginChild("by_name", ImVec2(0, 240), true);
                        for (auto& h : proc->highActorHandles) {
                            RE::Actor* a = h.get().get();
                            if (!a || a == pc) continue;
                            auto* ab = a->GetActorBase();
                            if (!ab) continue;
                            const char* nm = ab->GetName();
                            std::string name = nm ? nm : "(no name)";
                            std::string N = name, F = addNameAll;
                            std::transform(N.begin(), N.end(), N.begin(), ::tolower);
                            std::transform(F.begin(), F.end(), F.begin(), ::tolower);
                            if (!F.empty() && N.find(F) == std::string::npos) continue;

                            const std::uint32_t fid = ab->GetFormID();
                            ImGui::PushID(static_cast<int>(fid));
                            ImGui::Text("%s  [0x%08X]", name.c_str(), fid);
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Add")) {
                                {
                                    std::unique_lock lk(Settings::actorsMutex);
                                    if (!find_by_id(Settings::trackedActors, fid))
                                        Settings::trackedActors.push_back({"", fid, 1.0f, true, 0x0F});
                                }
                                SWE::WetController::GetSingleton()->RefreshNow();
                            }
                            ImGui::PopID();
                        }
                        ImGui::EndChild();
                    }
                    if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
            }

            // Clear All
            ImGui::SameLine();
            if (ImGui::Button("Clear all…")) ImGui::OpenPopup("swe_clear_all_lists");
            if (ImGui::BeginPopupModal("swe_clear_all_lists", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextWrapped("This will remove all tracked actors and manual overrides. Continue?");
                if (ImGui::Button("Yes, clear")) {
                    {
                        std::unique_lock lk(Settings::actorsMutex);
                        Settings::actorOverrides.clear();
                        Settings::trackedActors.clear();
                    }
                    SWE::WetController::GetSingleton()->RefreshNow();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }

            ImGui::TreePop();
        }

        // Snapshots
        auto trackedSnap = Settings::SnapshotTrackedActors();
        auto overridesSnap = Settings::SnapshotActorOverrides();

        std::vector<std::uint32_t> ids;
        ids.reserve(trackedSnap.size() + overridesSnap.size());
        for (const auto& fs : trackedSnap)
            if (std::find(ids.begin(), ids.end(), fs.id) == ids.end()) ids.push_back(fs.id);
        for (const auto& fs : overridesSnap)
            if (std::find(ids.begin(), ids.end(), fs.id) == ids.end()) ids.push_back(fs.id);

        // Filter bar for the table
        static char tblFilter[64] = "";
        ImGui::SetNextItemWidth(240);
        ImGui::InputTextWithHint("##tblFilter", "Filter table by name…", tblFilter, IM_ARRAYSIZE(tblFilter));

        if (ImGui::BeginTable("npc_mgr_tbl", 6,
                              ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Actor");
            ImGui::TableSetupColumn("Mode");
            ImGui::TableSetupColumn("Wetness (0..1)");
            ImGui::TableSetupColumn("Categories");
            ImGui::TableSetupColumn("Enabled");
            ImGui::TableSetupColumn("Remove");
            ImGui::TableHeadersRow();

            for (int i = 0; i < static_cast<int>(ids.size()); ++i) {
                const std::uint32_t fid = ids[i];
                const Settings::FormSpec* trkS = find_by_id_const(trackedSnap, fid);
                const Settings::FormSpec* ovrS = find_by_id_const(overridesSnap, fid);

                // Actor Name
                const char* nm = "";
                if (auto* npc = RE::TESForm::LookupByID<RE::TESNPC>(fid)) nm = npc->GetName();
                std::string disp = nm && nm[0] ? nm : "(unknown)";

                // Table filter
                if (tblFilter[0]) {
                    std::string lowN = disp, lowF = tblFilter;
                    std::transform(lowN.begin(), lowN.end(), lowN.begin(), ::tolower);
                    std::transform(lowF.begin(), lowF.end(), lowF.begin(), ::tolower);
                    if (lowN.find(lowF) == std::string::npos) continue;
                }

                ImGui::PushID(static_cast<int>(fid));
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(disp.c_str());

                ImGui::TableSetColumnIndex(1);
                bool autoWet = trkS ? trkS->autoWet : true;
                int modeIdx = autoWet ? 0 : 1;
                if (ImGui::Combo("##mode", &modeIdx, "Automatic\0Manual\0")) {
                    autoWet = (modeIdx == 0);
                    std::unique_lock lk(Settings::actorsMutex);
                    // ensure tracked exists
                    auto it = std::find_if(Settings::trackedActors.begin(), Settings::trackedActors.end(),
                                           [&](const Settings::FormSpec& fs) { return fs.id == fid; });
                    std::string pluginName;
                    if (auto* npc = RE::TESForm::LookupByID<RE::TESNPC>(fid)) {
                        if (auto* f = npc->GetFile(0)) pluginName = f->GetFilename();
                    }
                    if (it == Settings::trackedActors.end()) {
                        Settings::trackedActors.push_back(
                            {pluginName, fid, /*value*/ 1.0f, /*enabled*/ true, 0x0F /*mask*/});
                    } else {
                        it->autoWet = autoWet;
                    }
                    if (!autoWet) {
                        auto o = std::find_if(Settings::actorOverrides.begin(), Settings::actorOverrides.end(),
                                              [&](const Settings::FormSpec& fs) { return fs.id == fid; });
                        if (o == Settings::actorOverrides.end())
                            Settings::actorOverrides.push_back(
                                {pluginName, fid, /*value*/ 1.0f, /*enabled*/ true, 0x0F /*mask*/});
                    }
                    SWE::WetController::GetSingleton()->RefreshNow();
                }

                const bool manual = !autoWet;

                // Wetness Slider (Manual only)
                ImGui::TableSetColumnIndex(2);
                float v = ovrS ? ovrS->value : 1.0f;
                if (!manual) ImGui::BeginDisabled();
                if (ImGui::SliderFloat("##w", &v, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
                    std::unique_lock lk(Settings::actorsMutex);
                    auto* o = [&]() {
                        auto it = std::find_if(Settings::actorOverrides.begin(), Settings::actorOverrides.end(),
                                               [&](const Settings::FormSpec& fs) { return fs.id == fid; });
                        if (it == Settings::actorOverrides.end()) {
                            Settings::actorOverrides.push_back({"", fid, v, true, 0x0F});
                            return &Settings::actorOverrides.back();
                        }
                        return &(*it);
                    }();
                    o->value = v;
                    SWE::WetController::GetSingleton()->RefreshNow();
                }
                if (!manual) ImGui::EndDisabled();

                // Category Mask
                ImGui::TableSetColumnIndex(3);
                std::uint8_t maskCur = ovrS ? ovrS->mask : 0x0F;
                bool mSkin = (maskCur & 0x01) != 0;
                bool mHair = (maskCur & 0x02) != 0;
                bool mArmor = (maskCur & 0x04) != 0;
                bool mWeapon = (maskCur & 0x08) != 0;
                ImGui::Checkbox("Skin##m", &mSkin);
                ImGui::SameLine();
                ImGui::Checkbox("Hair##m", &mHair);
                ImGui::SameLine();
                ImGui::Checkbox("Armor##m", &mArmor);
                ImGui::SameLine();
                ImGui::Checkbox("Weap##m", &mWeapon);
                std::uint8_t newMask = (mSkin ? 1 : 0) | (mHair ? 2 : 0) | (mArmor ? 4 : 0) | (mWeapon ? 8 : 0);
                if (newMask != maskCur) {
                    {
                        std::unique_lock lk(Settings::actorsMutex);
                        auto* o = find_by_id(Settings::actorOverrides, fid);
                        if (!o)
                            Settings::actorOverrides.push_back({"", fid, 1.0f, (newMask != 0), newMask});
                        else {
                            o->mask = newMask;
                            o->enabled = (newMask != 0);
                        }
                        auto* t = find_by_id(Settings::trackedActors, fid);
                        if (!t)
                            Settings::trackedActors.push_back({"", fid, 1.0f, (newMask != 0), 0x0F});
                        else
                            t->enabled = (newMask != 0);
                    }
                    SWE::WetController::GetSingleton()->RefreshNow();
                }

                // Enabled
                ImGui::TableSetColumnIndex(4);
                bool bothEn = ((ovrS && ovrS->enabled) || (trkS && trkS->enabled));
                if (ImGui::Checkbox("##en", &bothEn)) {
                    {
                        std::unique_lock lk(Settings::actorsMutex);
                        if (auto* o = find_by_id(Settings::actorOverrides, fid)) {
                            o->enabled = bothEn;
                        }
                        auto* t = find_by_id(Settings::trackedActors, fid);
                        if (!t)
                            Settings::trackedActors.push_back({"", fid, 1.0f, bothEn, 0x0F});
                        else
                            t->enabled = bothEn;
                    }
                    SWE::WetController::GetSingleton()->RefreshNow();
                }

                // Remove
                ImGui::TableSetColumnIndex(5);
                if (ImGui::SmallButton("X")) {
                    {
                        std::unique_lock lk(Settings::actorsMutex);
                        remove_from(Settings::actorOverrides, fid);
                        remove_from(Settings::trackedActors, fid);
                    }
                    SWE::WetController::GetSingleton()->RefreshNow();
                    ImGui::PopID();
                    continue;
                }

                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        ImGui::Separator();
        SaveResetRow();
    }
    FontAwesome::Pop();
}
