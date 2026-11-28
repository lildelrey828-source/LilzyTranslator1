#include "imgui_ui.hpp"
#include <imgui.h>
#include <cstring>
#include <cstdio>
#include <deque>
#include <mutex>
#include <vector>

namespace {
bool g_visible = false;
int g_page = 0;
char g_apiKey[512]{};
char g_targetId[16] = "-1";
char g_targetName[64] = "Belum dipilih";
bool g_showKey = false;
bool g_synced = false;
struct HudMessage { int id; std::string name, original, translated; };
std::deque<HudMessage> g_hudMessages;
std::mutex g_hudMutex;
bool g_hudAtBottom = true;
bool g_hudForceBottom = false;

const ImVec4 C_BG       = ImVec4(.025f, .040f, .034f, .985f);
const ImVec4 C_PANEL    = ImVec4(.045f, .065f, .055f, 1.00f);
const ImVec4 C_ROW      = ImVec4(.055f, .075f, .065f, 1.00f);
const ImVec4 C_EMERALD  = ImVec4(.04f, .78f, .48f, 1.00f);
const ImVec4 C_MUTED    = ImVec4(.58f, .65f, .61f, 1.00f);

void SyncFromConfig(const Config& c) {
    std::snprintf(g_apiKey, sizeof(g_apiKey), "%s", c.apiKey.c_str());
    std::snprintf(g_targetId, sizeof(g_targetId), "%d", c.targetPlayerId);
    g_synced = true;
}
void AccentPush() {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.035f,.34f,.23f,1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.045f,.48f,.31f,1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.025f,.27f,.18f,1));
}
void AccentPop() { ImGui::PopStyleColor(3); }
void Section(const char* label) {
    ImGui::Spacing(); ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, C_EMERALD);
    ImGui::Text("◆  %s", label);
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(.04f,.48f,.30f,.95f));
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::Spacing();
}
void SettingRow(const char* label, const char* description, bool* value) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, C_ROW);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.f);
    ImGui::BeginChild(label, ImVec2(0, description ? 53.f : 43.f), true,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::SetCursorPosY(description ? 7.f : 11.f);
    ImGui::TextUnformatted(label);
    if (description) {
        ImGui::PushStyleColor(ImGuiCol_Text, C_MUTED);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1.f);
        ImGui::TextUnformatted(description);
        ImGui::PopStyleColor();
    }
    ImGui::SameLine(ImGui::GetWindowWidth() - 54.f);
    ImGui::SetCursorPosY(description ? 15.f : 9.f);
    ImGui::Checkbox((std::string("##toggle_") + label).c_str(), value);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::Spacing();
}
void NavButton(const char* label, int page, float width) {
    if (g_page == page) AccentPush();
    if (ImGui::Button(label, ImVec2(width, 37.f))) g_page = page;
    if (g_page == page) AccentPop();
}
void Save(Config& c) { c.apiKey = g_apiKey; SaveConfig(c); }
void DrawProviderFields(Config& c, bool compact) {
    const char* providers[] = {"Free (MyMemory)", "Groq AI"};
    int provider = c.provider == "groq" ? 1 : 0;
    ImGui::Text("Provider");
    ImGui::SameLine(compact ? 145.f : 165.f);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##provider", providers[provider])) {
        if (ImGui::Selectable(providers[0], provider == 0)) c.provider = "free";
        if (ImGui::Selectable(providers[1], provider == 1)) c.provider = "groq";
        ImGui::EndCombo();
    }
    ImGui::Spacing();
    const char* presets[] = {"Formal", "Casual", "Los Angeles", "Latino", "Bay Area", "Chicago", "New York", "Atlanta", "Houston", "Miami", "British Urban", "Asian-American"};
    const char* presetIds[] = {"formal", "casual", "los_angeles", "latino", "bay_area", "chicago", "new_york", "atlanta", "houston", "miami", "british_urban", "asian_american"};
    int selected = 1;
    for (int i=0;i<12;i++) if (c.preset == presetIds[i]) selected = i;
    ImGui::Text("Preset"); ImGui::SameLine(compact ? 145.f : 165.f); ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##preset", presets[selected])) {
        for (int i=0;i<12;i++) if (ImGui::Selectable(presets[i], selected == i)) c.preset = presetIds[i];
        ImGui::EndCombo();
    }
    ImGui::Spacing();
    ImGui::Text("API Key"); ImGui::SameLine(compact ? 145.f : 165.f);
    ImGui::SetNextItemWidth(-72.f);
    ImGuiInputTextFlags flags = g_showKey ? 0 : ImGuiInputTextFlags_Password;
    if (ImGui::InputText("##apikey", g_apiKey, sizeof(g_apiKey), flags)) c.apiKey = g_apiKey;
    ImGui::SameLine();
    if (ImGui::Button(g_showKey ? "Hide" : "Show", ImVec2(58.f, 0.f))) g_showKey = !g_showKey;
    ImGui::Spacing();
    if (c.provider == "free") ImGui::TextDisabled("MyMemory does not require an API key.");
    else ImGui::TextDisabled("Groq API key is stored locally in lilzy_translator.ini.");
}
}

void LilzyUiSetVisible(bool visible) { g_visible = visible; }
bool LilzyUiIsVisible() { return g_visible; }
void LilzyHudPushTranslation(int playerId, const char* playerName, const char* original, const char* translated) {
    std::lock_guard<std::mutex> lock(g_hudMutex);
    g_hudMessages.push_back({playerId, playerName ? playerName : "Unknown", original ? original : "", translated ? translated : ""});
    while (g_hudMessages.size() > 8) g_hudMessages.pop_front();
    if (g_hudAtBottom) g_hudForceBottom = true;
}

void LilzyUiRender(Config& c, std::string& targetStatus) {
    if (!g_synced) SyncFromConfig(c);
    std::vector<HudMessage> hudSnapshot;
    bool forceHudBottom = false;
    {
        std::lock_guard<std::mutex> lock(g_hudMutex);
        hudSnapshot.assign(g_hudMessages.begin(), g_hudMessages.end());
        forceHudBottom = g_hudForceBottom;
        g_hudForceBottom = false;
    }
    ImGuiIO& io = ImGui::GetIO();
    const float h = io.DisplaySize.y * .88f;
    const float w = (h * .75f < io.DisplaySize.x * .92f) ? h * .75f : io.DisplaySize.x * .92f;
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x-w)*.5f, (io.DisplaySize.y-h)*.5f), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(15.f, 12.f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, C_BG);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, C_PANEL);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(.08f,.30f,.21f,.90f));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, C_EMERALD);
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, C_EMERALD);
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(.18f,.95f,.62f,1));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(.065f,.085f,.075f,1));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(.085f,.15f,.115f,1));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.07f,.095f,.085f,1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.10f,.20f,.15f,1));

    if (g_visible) {
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
        if (ImGui::Begin("##lilzy_main", &g_visible, flags)) {
            // Header
            ImGui::BeginChild("##header", ImVec2(0, 54.f), false, ImGuiWindowFlags_NoScrollbar);
            ImGui::PushStyleColor(ImGuiCol_Text, C_EMERALD);
            ImGui::SetWindowFontScale(1.25f); ImGui::TextUnformatted("◢"); ImGui::SetWindowFontScale(1.f);
            ImGui::PopStyleColor(); ImGui::SameLine(48.f);
            ImGui::SetCursorPosY(4.f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(.94f,.97f,.95f,1));
            ImGui::Text("LILZY TRANSLATOR"); ImGui::PopStyleColor();
            ImGui::SameLine(); ImGui::PushStyleColor(ImGuiCol_Text, C_EMERALD); ImGui::Text("  Native Translator R5.1"); ImGui::PopStyleColor();
            ImGui::EndChild();
            ImGui::Separator(); ImGui::Spacing();

            // Navigation
            const char* nav[] = {"General", "Provider", "Language", "Mood", "Lock Player", "ChatLog"};
            const float navW = (ImGui::GetContentRegionAvail().x - 5.f * 5.f) / 6.f;
            for (int i=0;i<6;i++) { if (i) ImGui::SameLine(); NavButton(nav[i], i, navW); }
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            if (g_page == 0) {
                Section("General Settings");
                SettingRow("Enable Translator", nullptr, &c.enabled);
                SettingRow("Auto Copy", "Copy translated text to clipboard automatically.", &c.autoCopy);
                SettingRow("Incoming Translation", "Translate incoming chat messages.", &c.incoming);

                Section("Player Filter");
                SettingRow("Player Filter", "Only translate messages from the selected player.", &c.playerFilterEnabled);
                ImGui::PushStyleColor(ImGuiCol_ChildBg, C_ROW);
                ImGui::BeginChild("##target_card", ImVec2(0, 145.f), true);
                ImGui::PushStyleColor(ImGuiCol_Text, C_EMERALD); ImGui::TextUnformatted("◎  Target Player"); ImGui::PopStyleColor();
                ImGui::TextDisabled("Player ID"); ImGui::SameLine(95.f);
                ImGui::SetNextItemWidth(-1.f); ImGui::InputText("##targetid_general", g_targetId, sizeof(g_targetId), ImGuiInputTextFlags_CharsDecimal);
                ImGui::TextDisabled("Target: %s", g_targetName);
                ImGui::Spacing();
                if (ImGui::Button("Apply Target ID", ImVec2(150.f, 32.f))) {
                    int id = -1;
                    if (std::sscanf(g_targetId, "%d", &id) == 1 && id >= 0 && id <= 1004) {
                        c.targetPlayerId = id;
                        c.playerFilterEnabled = true;
                        targetStatus = "Target ID tersimpan. Validasi Player Pool tetap ditangani hook SA-MP.";
                    } else { targetStatus = "ID tidak valid. Gunakan angka 0-1004."; }
                }
                ImGui::SameLine(); ImGui::TextDisabled("%s", targetStatus.c_str());
                ImGui::EndChild(); ImGui::PopStyleColor();

                Section("API Provider");
                DrawProviderFields(c, false);
                Section("Maintenance");
                if (ImGui::Button("Reset to Default", ImVec2((ImGui::GetContentRegionAvail().x - 8.f) * .5f, 38.f))) {
                    c.enabled = true; c.autoCopy = true; c.incoming = true; c.provider = "free";
                    c.preset = "casual"; c.mood = "neutral"; c.playerFilterEnabled = false; c.targetPlayerId = -1;
                    std::snprintf(g_targetId, sizeof(g_targetId), "-1");
                    targetStatus = "Settings dikembalikan ke default.";
                }
                ImGui::SameLine();
                AccentPush();
                if (ImGui::Button("Save Settings", ImVec2(-1.f, 38.f))) Save(c);
                AccentPop();
            } else if (g_page == 1) {
                Section("API Provider Configuration");
                DrawProviderFields(c, false);
                ImGui::Spacing();
                AccentPush(); if (ImGui::Button("Save Provider Settings", ImVec2(-1.f, 40.f))) Save(c); AccentPop();
            } else if (g_page == 2) {
                Section("Language Style");
                const char* styles[] = {"formal","casual","los_angeles","latino","bay_area","chicago","new_york","atlanta","houston","miami","british_urban","asian_american"};
                if (ImGui::BeginCombo("Preset", c.preset.c_str())) { for (const char* s : styles) if (ImGui::Selectable(s, c.preset == s)) c.preset = s; ImGui::EndCombo(); }
                AccentPush(); if (ImGui::Button("Save Language", ImVec2(-1.f, 40.f))) Save(c); AccentPop();
            } else if (g_page == 3) {
                Section("Translation Mood");
                const char* moods[] = {"neutral","friendly","aggressive","sarcastic","playful","cold","professional","romantic"};
                if (ImGui::BeginCombo("Mood", c.mood.c_str())) { for (const char* s : moods) if (ImGui::Selectable(s, c.mood == s)) c.mood = s; ImGui::EndCombo(); }
                AccentPush(); if (ImGui::Button("Save Mood", ImVec2(-1.f, 40.f))) Save(c); AccentPop();
            } else if (g_page == 4) {
                Section("Lock Player");
                ImGui::TextWrapped("Lock berdasarkan Player ID, bukan nama pemain.");
                ImGui::SetNextItemWidth(-1.f); ImGui::InputText("Player ID", g_targetId, sizeof(g_targetId), ImGuiInputTextFlags_CharsDecimal);
                AccentPush();
                if (ImGui::Button("Set Lock Player", ImVec2(-1.f, 38.f))) {
                    int id=-1;
                    if (std::sscanf(g_targetId, "%d", &id)==1 && id>=0 && id<=1004) { c.targetPlayerId=id; c.playerFilterEnabled=true; targetStatus="Target ID tersimpan."; Save(c); }
                    else targetStatus="ID tidak valid.";
                }
                AccentPop();
                if (ImGui::Button("Unlock Player", ImVec2(-1.f, 36.f))) { c.targetPlayerId=-1; c.playerFilterEnabled=false; std::snprintf(g_targetId,sizeof(g_targetId),"-1"); targetStatus="Lock dinonaktifkan."; Save(c); }
                ImGui::TextWrapped("%s", targetStatus.c_str());
            } else {
                Section("ChatLog History");
                if (hudSnapshot.empty()) ImGui::TextDisabled("Belum ada hasil terjemahan incoming.");
                ImGui::BeginChild("##chatloghistory", ImVec2(0, ImGui::GetContentRegionAvail().y), true);
                for (const auto& m : hudSnapshot) {
                    ImGui::PushStyleColor(ImGuiCol_Text, C_EMERALD); ImGui::Text("[%d] %s", m.id, m.name.c_str()); ImGui::PopStyleColor();
                    ImGui::TextWrapped("%s", m.original.c_str());
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(.72f,.91f,.81f,1)); ImGui::TextWrapped("%s", m.translated.c_str()); ImGui::PopStyleColor();
                    ImGui::Separator();
                }
                ImGui::EndChild();
            }
            ImGui::Spacing(); ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, C_MUTED);
            ImGui::Text("Lilzy Translator R5.1  |  Developed by Lilzy");
            ImGui::PopStyleColor();
        }
        ImGui::End();
    }

    // Compact incoming HUD, separate from the main settings window.
    if (!hudSnapshot.empty()) {
        ImGui::SetNextWindowPos(ImVec2(22.f, io.DisplaySize.y - 225.f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(.48f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 7.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(9.f,7.f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(.015f,.025f,.022f,.48f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(.08f,.42f,.28f,.55f));
        if (ImGui::Begin("Lilzy Incoming##hud", nullptr, ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoFocusOnAppearing|ImGuiWindowFlags_NoNav|ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::PushStyleColor(ImGuiCol_Text, C_EMERALD); ImGui::TextUnformatted("LILZY TRANSLATOR  /  INCOMING"); ImGui::PopStyleColor();
            ImGui::BeginChild("##messages", ImVec2(330.f,174.f), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
            for (size_t i=0;i<hudSnapshot.size();++i) {
                const auto& m=hudSnapshot[i]; ImGui::PushID((int)i);
                ImGui::TextColored(C_EMERALD,"[%d] %s",m.id,m.name.c_str());
                ImGui::TextWrapped("%s",m.original.c_str());
                ImGui::TextColored(ImVec4(.72f,.91f,.81f,1),"%s",m.translated.c_str());
                ImGui::Separator(); ImGui::PopID();
            }
            if(forceHudBottom) ImGui::SetScrollHereY(1.f);
            { std::lock_guard<std::mutex> lock(g_hudMutex); g_hudAtBottom=ImGui::GetScrollY()>=ImGui::GetScrollMaxY()-4.f; }
            if(ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel>0.f) g_hudAtBottom=false;
            ImGui::EndChild();
        }
        ImGui::End(); ImGui::PopStyleColor(2); ImGui::PopStyleVar(2);
    }
    ImGui::PopStyleColor(10);
    ImGui::PopStyleVar(4);
}
