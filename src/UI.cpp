#include "UI.h"

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

    void SaveResetRow(bool showReapply = false) {
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

    if (ImGui::CollapsingHeader("External Wetness (API)")) {
        int mode = Settings::externalBlendMode.load();
        if (ImGui::RadioButton("Max", mode == 0)) mode = 0;
        ImGui::SameLine();
        if (ImGui::RadioButton("Additive (cap 1.0)", mode == 1)) mode = 1;
        ImGui::SameLine();
        if (ImGui::RadioButton("Max + weighted rest", mode == 2)) mode = 2;
        Settings::externalBlendMode.store(mode);

        if (mode == 2) {
            float w = Settings::externalAddWeight.load();
            if (FloatControl("Rest weight", w, 0.f, 1.f, "%.2f")) Settings::externalAddWeight.store(w);
        }
        ImGui::TextDisabled("Other mods can call SWE.SetExternalWetness(actor, key, value[, durationSec]).");
    }

    ImGui::Separator();
    SaveResetRow(true);
}

void __stdcall UI::WetConfig::RenderSources() {
    FontAwesome::PushSolid();
    if (ImGui::CollapsingHeader(sourcesHeader.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        bool rs = Settings::rainSnowEnabled.load();
        if (ImGui::Checkbox("Enable rain/snow wetting", &rs)) Settings::rainSnowEnabled.store(rs);

        bool snow = Settings::affectInSnow.load();
        if (ImGui::Checkbox("Affect in snow", &snow)) Settings::affectInSnow.store(snow);

        bool ig = Settings::ignoreInterior.load();
        if (ImGui::Checkbox("Ignore interiors", &ig)) Settings::ignoreInterior.store(ig);
        HelpMarker("If enabled, interiors disregard rain/snow.");

        ImGui::Separator();
        {
            float w = Settings::secondsToSoakWater.load();
            if (FloatControl("Seconds to fully soak (Water)", w, 0.5f, 120.0f, "%.0f", 1.0f, 5.0f,
                             "How fast you get 0% → 100% wet while in water.")) {
                Settings::secondsToSoakWater.store(w);
            }
        }
        {
            float r = Settings::secondsToSoakRain.load();
            if (FloatControl("Seconds to fully soak (Rain/Snow)", r, 5.0f, 3600.0f, "%.0f", 5.0f, 30.0f,
                             "Time to reach 100% wetness in precipitation (outdoors).")) {
                Settings::secondsToSoakRain.store(r);
            }
        }
        {
            float d = Settings::secondsToDry.load();
            if (FloatControl("Seconds to dry", d, 2.0f, 3600.0f, "%.0f", 5.0f, 30.0f,
                             "Time from 100% → 0% wetness (without fire/heat boost).")) {
                Settings::secondsToDry.store(d);
            }
        }

        ImGui::Separator();
        {
            bool wf = Settings::waterfallEnabled.load();
            if (ImGui::Checkbox("Enable waterfall spray wetting", &wf)) Settings::waterfallEnabled.store(wf);
        }
        {
            float r = Settings::secondsToSoakWaterfall.load();
            if (FloatControl("Seconds to fully soak (Waterfall spray)", r, 1.0f, 3600.0f, "%.0f", 1.0f, 5.0f,
                             "Time to reach 100% wetness when standing in/near a waterfall.")) {
                Settings::secondsToSoakWaterfall.store(r);
            }
        }
        {
            float rad = Settings::nearWaterfallRadius.load();
            if (FloatControl("Waterfall detection radius (units)", rad, 100.0f, 3000.0f, "%.0f", 10.0f, 50.0f,
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

        ImGui::Separator();
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
        bool s = Settings::affectSkin.load();
        if (ImGui::Checkbox("Affect Skin/Face", &s)) Settings::affectSkin.store(s);
        bool h = Settings::affectHair.load();
        if (ImGui::Checkbox("Affect Hair", &h)) Settings::affectHair.store(h);
        bool a = Settings::affectArmor.load();
        if (ImGui::Checkbox("Affect Armor", &a)) Settings::affectArmor.store(a);
        bool w = Settings::affectWeapons.load();
        if (ImGui::Checkbox("Affect Weapons", &w)) Settings::affectWeapons.store(w);

        ImGui::Separator();
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
            if (FloatControl("Skin/Hair response × (gloss & spec)", k, 0.1f, 10.0f, "%.2f", 0.05f, 0.25f,
                             "Extra multiplier applied to both glossiness and specular intensity on skin & hair.")) {
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
                             "Below this it is considered non-glossy (depends on material)")) {
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
            if (FloatControl("Min Specular Strength", smn, 0.0f, 20.0f, "%.2f", 0.05f, 0.5f,
                             "Below this it is considered non-specular.")) {
                if (smn > Settings::maxSpecularStrength.load()) {
                    Settings::maxSpecularStrength.store(smn);
                }
                Settings::minSpecularStrength.store(smn);
            }
        }

        ImGui::Separator();
        {
            float fr = Settings::nearFireRadius.load();
            if (FloatControl("Fireplace radius (units)", fr, 100.0f, 2000.0f, "%.0f", 10.0f, 50.0f,
                             "Range within which heat sources dry you faster.")) {
                Settings::nearFireRadius.store(fr);
            }
        }
        {
            float fm = Settings::dryMultiplierNearFire.load();
            if (FloatControl("Drying speed × near fire", fm, 1.0f, 10.0f, "%.2f", 0.1f, 0.5f,
                             "Multiplier for drying near fire.")) {
                Settings::dryMultiplierNearFire.store(fm);
            }
        }

        ImGui::Separator();
        SaveResetRow(true);
    }
    FontAwesome::Pop();
}

void __stdcall UI::WetConfig::RenderNPCs() {
    FontAwesome::PushSolid();
    if (ImGui::CollapsingHeader(npcsHeader.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        bool en = Settings::affectNPCs.load();
        if (ImGui::Checkbox("Affect NPCs", &en)) Settings::affectNPCs.store(en);

        int r = Settings::npcRadius.load();
        if (IntControl("NPC Radius (0 = All loaded)", r, 0, 16384, "%d", 64, 256,
                       "Actors outside the radius are set dry immediately.")) {
            Settings::npcRadius.store(r);
        }

        ImGui::Separator();
        SaveResetRow();
    }
    FontAwesome::Pop();
}