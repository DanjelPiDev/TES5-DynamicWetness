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
            if (FloatControl("Skin/Hair response × (gloss & spec)", k, 0.1f, 100.0f, "%.1f", 0.5f, 1.0f,
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
            if (FloatControl("Min Specular Strength", smn, 0.0f, 100.0f, "%.2f", 0.05f, 0.5f,
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

        {
            bool pbr = Settings::pbrFriendlyMode.load();
            if (ImGui::Checkbox("PBR-friendly for Armor/Weapons", &pbr)) Settings::pbrFriendlyMode.store(pbr);
            ImGui::SameLine();
            ImGui::TextDisabled("(Community Shaders PBR / roughness-metalness)");

            if (pbr) {
                ImGui::TextDisabled("Use milder caps on Armor/Weapons and do not force specular on likely PBR sets.");
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
                ImGui::Separator();
            }
        }

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

        bool optin = Settings::npcOptInOnly.load();
        if (ImGui::Checkbox("Opt-in tracking only (use list below)", &optin)) {
            Settings::npcOptInOnly.store(optin);
        }
        ImGui::TextDisabled("When enabled, only NPCs you add to the list will be updated in rain/waterfall/water.");

        ImGui::Separator();

        auto add_override_if_missing = [](std::uint32_t fid) {
            auto& ov = Settings::actorOverrides;
            auto it = std::find_if(ov.begin(), ov.end(), [&](const Settings::FormSpec& fs) { return fs.id == fid; });
            if (it == ov.end()) {
                Settings::FormSpec fs{};
                fs.plugin = "";
                fs.id = fid;
                fs.value = 1.0f;
                fs.enabled = false;
                fs.mask = 0x0F;
                ov.push_back(std::move(fs));
            }
        };

        auto contains_icase = [](const std::string& hay, const std::string& needle) {
            if (needle.empty()) return true;
            auto tolow = [](std::string s) {
                std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                return s;
            };
            std::string H = tolow(hay), N = tolow(needle);
            return H.find(N) != std::string::npos;
        };

        // Tracked NPCs (Opt-in)
        if (ImGui::TreeNodeEx("Tracked NPCs (for Opt-in mode)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("When 'Opt-in tracking only' is enabled, only these actors will be updated.");
            auto& vec = Settings::trackedActors;

            if (ImGui::Button("Add nearby NPCs…")) ImGui::OpenPopup("swe_add_tracked_nearby");
            ImGui::SameLine();
            static char addHex2[16] = "";
            ImGui::SetNextItemWidth(140);
            ImGui::InputTextWithHint("##addTrackedHex", "FormID (hex)", addHex2, IM_ARRAYSIZE(addHex2),
                                     ImGuiInputTextFlags_CharsHexadecimal);
            ImGui::SameLine();
            if (ImGui::Button("Add by FormID##trk")) {
                std::uint32_t id = 0;
                if (std::strlen(addHex2) > 0) {
                    std::string s = addHex2;
                    if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0) s = s.substr(2);
                    auto res = std::from_chars(s.data(), s.data() + s.size(), id, 16);
                    if (res.ec == std::errc()) {
                        Settings::trackedActors.push_back({/*plugin*/ "", id, 1.0f, true});
                        add_override_if_missing(id);
                        SWE::WetController::GetSingleton()->RefreshNow();
                    }
                }
            }

            if (ImGui::BeginPopup("swe_add_tracked_nearby")) {
                static char filter2[48] = "";
                ImGui::InputTextWithHint("##ftrk", "Filter by name…", filter2, IM_ARRAYSIZE(filter2));
                ImGui::Separator();
                if (auto* proc = RE::ProcessLists::GetSingleton()) {
                    RE::Actor* pc = RE::PlayerCharacter::GetSingleton();
                    ImGui::BeginChild("trk_near", ImVec2(0, 240), true);
                    for (auto& h : proc->highActorHandles) {
                        RE::NiPointer<RE::Actor> ap = h.get();
                        RE::Actor* a = ap.get();
                        if (!a || a == pc) continue;
                        auto* ab = a->GetActorBase();
                        if (!ab) continue;
                        const char* nm = ab->GetName();
                        std::string name = nm ? nm : "(no name)";
                        if (filter2[0] && name.find(filter2) == std::string::npos) continue;
                        std::uint32_t fid = ab->GetFormID();
                        ImGui::PushID(static_cast<int>(fid));
                        ImGui::Text("%s  [0x%08X]", name.c_str(), fid);
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Add")) {
                            vec.push_back({"", fid, 1.0f, true});
                            SWE::WetController::GetSingleton()->RefreshNow();
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndChild();
                }
                if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }

            if (ImGui::BeginTable("trk_tbl", 4, ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders)) {
                ImGui::TableSetupColumn("Actor");
                ImGui::TableSetupColumn("FormID");
                ImGui::TableSetupColumn("Enabled");
                ImGui::TableSetupColumn("Remove");
                ImGui::TableHeadersRow();
                for (int i = 0; i < static_cast<int>(vec.size()); ++i) {
                    auto& fs = vec[i];
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    const char* nm = "";
                    if (auto* npc = RE::TESForm::LookupByID<RE::TESNPC>(fs.id)) nm = npc->GetName();
                    ImGui::TextUnformatted(nm && nm[0] ? nm : "(unknown)");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("0x%08X", fs.id);
                    ImGui::TableSetColumnIndex(2);
                    {
                        bool enb = fs.enabled;
                        if (ImGui::Checkbox(("##enT" + std::to_string(i)).c_str(), &enb)) {
                            fs.enabled = enb;
                            SWE::WetController::GetSingleton()->RefreshNow();
                        }
                    }
                    ImGui::TableSetColumnIndex(3);
                    if (ImGui::SmallButton(("Remove##t" + std::to_string(i)).c_str())) {
                        vec.erase(vec.begin() + i);
                        SWE::WetController::GetSingleton()->RefreshNow();
                        --i;
                    }
                }
                ImGui::EndTable();
            }
            ImGui::TreePop();
        }

        ImGui::Separator();

        // Per-Actor Overrides
        if (ImGui::TreeNodeEx("Per-Actor Wetness Overrides", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("Force selected actors to always render at a fixed wetness value (0..1).");
            auto& vec = Settings::actorOverrides;

            // Controls row
            if (ImGui::Button("Add nearby NPCs…")) ImGui::OpenPopup("swe_add_override_nearby");
            ImGui::SameLine();
            static char addHex[16] = "";
            ImGui::SetNextItemWidth(140);
            ImGui::InputTextWithHint("##addOverrideHex", "FormID (hex)", addHex, IM_ARRAYSIZE(addHex),
                                     ImGuiInputTextFlags_CharsHexadecimal);
            ImGui::SameLine();
            if (ImGui::Button("Add by FormID")) {
                std::uint32_t id = 0;
                if (std::strlen(addHex) > 0) {
                    std::string s = addHex;
                    if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0) s = s.substr(2);
                    auto res = std::from_chars(s.data(), s.data() + s.size(), id, 16);
                    if (res.ec == std::errc()) {
                        Settings::actorOverrides.push_back({/*plugin*/ "", id, 1.0f, true});
                        SWE::WetController::GetSingleton()->RefreshNow();
                    }
                }
            }
            ImGui::SameLine();
            static char addName2[64] = "";
            ImGui::SetNextItemWidth(180);
            ImGui::InputTextWithHint("##addTrackedName", "Name contains...", addName2, IM_ARRAYSIZE(addName2));
            ImGui::SameLine();
            if (ImGui::Button("Find by Name##trk")) {
                ImGui::OpenPopup("swe_add_tracked_by_name");
            }

            if (ImGui::BeginPopup("swe_add_tracked_by_name")) {
                int added = 0;
                if (auto* proc = RE::ProcessLists::GetSingleton()) {
                    RE::Actor* pc = RE::PlayerCharacter::GetSingleton();
                    ImGui::BeginChild("trk_by_name", ImVec2(0, 240), true);
                    for (auto& h : proc->highActorHandles) {
                        RE::NiPointer<RE::Actor> ap = h.get();
                        RE::Actor* a = ap.get();
                        if (!a || a == pc) continue;
                        auto* ab = a->GetActorBase();
                        if (!ab) continue;
                        const char* nm = ab->GetName();
                        std::string name = nm ? nm : "(no name)";
                        if (!contains_icase(name, addName2)) continue;
                        std::uint32_t fid = ab->GetFormID();

                        ImGui::PushID(static_cast<int>(fid));
                        ImGui::Text("%s  [0x%08X]", name.c_str(), fid);
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Add")) {
                            // tracked
                            Settings::trackedActors.push_back({"", fid, 1.0f, true});
                            add_override_if_missing(fid);
                            SWE::WetController::GetSingleton()->RefreshNow();
                            ++added;
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndChild();
                }
                if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }

            // Nearby picker popup
            if (ImGui::BeginPopup("swe_add_override_nearby")) {
                static char filter[48] = "";
                ImGui::InputTextWithHint("##fovr", "Filter by name…", filter, IM_ARRAYSIZE(filter));
                ImGui::Separator();
                if (auto* proc = RE::ProcessLists::GetSingleton()) {
                    RE::Actor* pc = RE::PlayerCharacter::GetSingleton();
                    ImGui::BeginChild("ovr_near", ImVec2(0, 240), true);
                    for (auto& h : proc->highActorHandles) {
                        RE::NiPointer<RE::Actor> ap = h.get();
                        RE::Actor* a = ap.get();
                        if (!a || a == pc) continue;
                        auto* ab = a->GetActorBase();
                        if (!ab) continue;
                        const char* nm = ab->GetName();
                        std::string name = nm ? nm : "(no name)";
                        if (filter[0] && name.find(filter) == std::string::npos) continue;
                        std::uint32_t fid = ab->GetFormID();
                        ImGui::PushID(static_cast<int>(fid));
                        ImGui::Text("%s  [0x%08X]", name.c_str(), fid);
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Add")) {
                            vec.push_back({"", fid, 1.0f, true});
                            add_override_if_missing(fid);
                            SWE::WetController::GetSingleton()->RefreshNow();
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndChild();
                }
                if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }

            // Table
            if (ImGui::BeginTable("ovr_tbl", 5, ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders)) {
                ImGui::TableSetupColumn("Actor");
                ImGui::TableSetupColumn("Wetness (0..1)");
                ImGui::TableSetupColumn("Categories");
                ImGui::TableSetupColumn("Enabled");
                ImGui::TableSetupColumn("Remove");
                ImGui::TableHeadersRow();

                for (int i = 0; i < static_cast<int>(vec.size()); ++i) {
                    auto& fs = vec[i];
                    ImGui::TableNextRow();

                    // Actor name
                    ImGui::TableSetColumnIndex(0);
                    const char* nm = "";
                    if (auto* npc = RE::TESForm::LookupByID<RE::TESNPC>(fs.id)) nm = npc->GetName();
                    ImGui::TextUnformatted(nm && nm[0] ? nm : "(unknown)");

                    // Wetness
                    ImGui::TableSetColumnIndex(1);
                    float v = fs.value;
                    if (ImGui::SliderFloat(("##w" + std::to_string(i)).c_str(), &v, 0.0f, 1.0f, "%.2f",
                                           ImGuiSliderFlags_AlwaysClamp)) {
                        fs.value = v;
                        SWE::WetController::GetSingleton()->RefreshNow();
                    }

                    // Categories (Mask)  bit0=Skin,1=Hair,2=Armor,3=Weapon
                    ImGui::TableSetColumnIndex(2);
                    bool mSkin = (fs.mask & 0x01) != 0;
                    bool mHair = (fs.mask & 0x02) != 0;
                    bool mArmor = (fs.mask & 0x04) != 0;
                    bool mWeapon = (fs.mask & 0x08) != 0;

                    ImGui::Checkbox(("Skin##" + std::to_string(i)).c_str(), &mSkin);
                    ImGui::SameLine();
                    ImGui::Checkbox(("Hair##" + std::to_string(i)).c_str(), &mHair);
                    ImGui::SameLine();
                    ImGui::Checkbox(("Armor##" + std::to_string(i)).c_str(), &mArmor);
                    ImGui::SameLine();
                    ImGui::Checkbox(("Weap##" + std::to_string(i)).c_str(), &mWeapon);

                    std::uint8_t newMask = (mSkin ? 1 : 0) | (mHair ? 2 : 0) | (mArmor ? 4 : 0) | (mWeapon ? 8 : 0);
                    if (newMask != fs.mask) {
                        fs.mask = newMask;
                        if (newMask == 0) fs.enabled = false;
                        SWE::WetController::GetSingleton()->RefreshNow();
                    }

                    ImGui::TableSetColumnIndex(3);
                    bool enb = fs.enabled;
                    if (ImGui::Checkbox(("##en" + std::to_string(i)).c_str(), &enb)) {
                        fs.enabled = enb;
                        SWE::WetController::GetSingleton()->RefreshNow();
                    }

                    ImGui::TableSetColumnIndex(4);
                    if (ImGui::SmallButton(("Remove##" + std::to_string(i)).c_str())) {
                        vec.erase(vec.begin() + i);
                        SWE::WetController::GetSingleton()->RefreshNow();
                        --i;
                    }
                }
                ImGui::EndTable();
            }

            ImGui::TreePop();
        }

        ImGui::Separator();
        SaveResetRow();
    }
    FontAwesome::Pop();
}