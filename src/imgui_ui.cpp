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


void SyncFromConfig(const Config& c) {
    std::snprintf(g_apiKey, sizeof(g_apiKey), "%s", c.apiKey.c_str());
    std::snprintf(g_targetId, sizeof(g_targetId), "%d", c.targetPlayerId);
    g_synced = true;
}
void Accent() { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.04f,.38f,.25f,1)); ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.06f,.52f,.34f,1)); ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.02f,.30f,.20f,1)); }
void AccentEnd() { ImGui::PopStyleColor(3); }
bool Toggle(const char* label, bool* v) { return ImGui::Checkbox(label, v); }
void Section(const char* label) {
    ImGui::Spacing(); ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(.18f,.86f,.58f,1));
    ImGui::Text("|  %s", label); ImGui::PopStyleColor();
    ImGui::Separator(); ImGui::Spacing();
}
void NavButton(const char* label, int page) {
    if (g_page == page) Accent();
    if (ImGui::Button(label, ImVec2(0, 36))) g_page = page;
    if (g_page == page) AccentEnd();
}
}

void LilzyUiSetVisible(bool visible) { g_visible = visible; }
void LilzyHudPushTranslation(int playerId, const char* playerName, const char* original, const char* translated) {
    std::lock_guard<std::mutex> lock(g_hudMutex);
    g_hudMessages.push_back({playerId, playerName ? playerName : "Unknown", original ? original : "", translated ? translated : ""});
    while (g_hudMessages.size() > 8) g_hudMessages.pop_front();
    if (g_hudAtBottom) g_hudForceBottom = true;
}
bool LilzyUiIsVisible() { return g_visible; }

void LilzyUiRender(Config& c, std::string& targetStatus) {
    if (!g_synced) SyncFromConfig(c);
    std::vector<HudMessage> hudSnapshot;
    bool forceHudBottom=false;
    { std::lock_guard<std::mutex> lock(g_hudMutex); hudSnapshot.assign(g_hudMessages.begin(),g_hudMessages.end()); forceHudBottom=g_hudForceBottom; g_hudForceBottom=false; }
    ImGuiIO& io = ImGui::GetIO();
    const float h = io.DisplaySize.y * .88f;
    const float w = h * .735f;
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x-w)*.5f, (io.DisplaySize.y-h)*.5f), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 9.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16,14));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(.025f,.040f,.034f,.97f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(.045f,.065f,.055f,.92f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(.10f,.28f,.20f,.85f));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(.10f,.88f,.57f,1));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(.10f,.78f,.50f,1));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(.15f,.95f,.62f,1));

    if (g_visible) {
    if (ImGui::Begin("LILZY TRANSLATOR  |  Native ASI", &g_visible, ImGuiWindowFlags_NoCollapse)) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(.15f,.88f,.58f,1));
        ImGui::TextUnformatted("LILZY TRANSLATOR"); ImGui::PopStyleColor();
        ImGui::SameLine(); ImGui::TextDisabled("Native Translator R5");
        ImGui::Separator();
        const char* names[] = {"General", "Provider", "Language", "Mood", "Lock Player", "ChatLog"};
        for (int i=0;i<6;i++) { ImGui::SameLine(i?0:0); if (g_page==i) Accent(); if (ImGui::Button(names[i], ImVec2(0,34))) g_page=i; if(g_page==i)AccentEnd(); if(i<5) ImGui::SameLine(); }
        ImGui::Spacing();
        if (g_page==0) {
            Section("General Settings");
            Toggle("Enable Translator", &c.enabled);
            Toggle("Auto Copy", &c.autoCopy);
            Toggle("Incoming Translation", &c.incoming);
            Section("Player Filter");
            Toggle("Player Filter", &c.playerFilterEnabled);
            ImGui::TextDisabled("Only process incoming messages from the selected Player ID.");
            ImGui::Text("Target Player ID"); ImGui::SetNextItemWidth(-1); ImGui::InputText("##targetid", g_targetId, sizeof(g_targetId), ImGuiInputTextFlags_CharsDecimal);
            ImGui::TextDisabled("Target: %s", g_targetName);
            if (ImGui::Button("Apply Target ID", ImVec2(160,34))) {
                int id = -1; if (std::sscanf(g_targetId, "%d", &id)==1 && id>=0 && id<=1004) { c.targetPlayerId=id; targetStatus="ID tersimpan di konfigurasi; validasi Player Pool menunggu integrasi hook SA-MP."; }
                else { c.targetPlayerId=-1; targetStatus="ID tidak valid. Gunakan 0-1004."; }
            }
            ImGui::TextWrapped("%s", targetStatus.c_str());
            Section("Maintenance");
            if (ImGui::Button("Save Settings", ImVec2(-1,40))) { c.apiKey=g_apiKey; SaveConfig(c); }
        } else if (g_page==1) {
            Section("API Provider");
            const char* providers[]={"Free (MyMemory)","Groq"}; int p=c.provider=="groq"?1:0;
            if(ImGui::BeginCombo("Provider",providers[p])) { if(ImGui::Selectable(providers[0],p==0))c.provider="free"; if(ImGui::Selectable(providers[1],p==1))c.provider="groq"; ImGui::EndCombo(); }
            ImGui::Text("Model"); ImGui::SetNextItemWidth(-1); char model[128]{}; std::snprintf(model,sizeof(model),"%s",c.model.c_str()); if(ImGui::InputText("##model",model,sizeof(model)))c.model=model;
            ImGui::Text("API Key"); ImGui::SetNextItemWidth(-1); ImGuiInputTextFlags flags=g_showKey?0:ImGuiInputTextFlags_Password; if(ImGui::InputText("##apikey",g_apiKey,sizeof(g_apiKey),flags))c.apiKey=g_apiKey;
            ImGui::SameLine(); if(ImGui::SmallButton(g_showKey?"Hide":"Show"))g_showKey=!g_showKey;
            ImGui::TextDisabled("API Key disimpan di lilzy_translator.ini.");
            if(ImGui::Button("Save Provider Settings",ImVec2(-1,38))){c.apiKey=g_apiKey;SaveConfig(c);}
        } else if (g_page==2) {
            Section("Language Style");
            const char* styles[]={"formal","casual","los_angeles","latino","bay_area","chicago","new_york","atlanta","houston","miami","british_urban","asian_american"};
            if(ImGui::BeginCombo("Preset",c.preset.c_str())){for(auto s:styles)if(ImGui::Selectable(s,c.preset==s))c.preset=s;ImGui::EndCombo();}
            ImGui::TextWrapped("Style ini menggunakan preset prompt yang sudah ada di translator engine.");
            if(ImGui::Button("Save Language",ImVec2(-1,38)))SaveConfig(c);
        } else if (g_page==3) {
            Section("Translation Mood");
            const char* moods[]={"neutral","friendly","aggressive","sarcastic","playful","cold","professional","romantic"};
            if(ImGui::BeginCombo("Mood",c.mood.c_str())){for(auto s:moods)if(ImGui::Selectable(s,c.mood==s))c.mood=s;ImGui::EndCombo();}
            if(ImGui::Button("Save Mood",ImVec2(-1,38)))SaveConfig(c);
        } else if (g_page==4) {
            Section("Lock Player");
            ImGui::TextWrapped("Kunci berdasarkan Player ID, bukan nama.");
            ImGui::SetNextItemWidth(-1); ImGui::InputText("Player ID",g_targetId,sizeof(g_targetId),ImGuiInputTextFlags_CharsDecimal);
            if(ImGui::Button("Set Lock Player",ImVec2(-1,38))){int id=-1;if(std::sscanf(g_targetId,"%d",&id)==1&&id>=0&&id<=1004){c.targetPlayerId=id;c.playerFilterEnabled=true;targetStatus="Target ID disimpan. Incoming akan diproses hanya jika sender ID cocok.";SaveConfig(c);}else targetStatus="ID tidak valid.";}
            if(ImGui::Button("Unlock Player",ImVec2(-1,34))){c.targetPlayerId=-1;c.playerFilterEnabled=false;targetStatus="Lock dinonaktifkan.";SaveConfig(c);}
            ImGui::TextWrapped("%s",targetStatus.c_str());
        } else {
            Section("ChatLog");
            if(hudSnapshot.empty()) ImGui::TextDisabled("Belum ada hasil terjemahan incoming.");
            ImGui::BeginChild("##chatloghistory",ImVec2(0,300),true);
            for(const auto& m:hudSnapshot){ImGui::TextColored(ImVec4(.20f,.85f,.58f,1),"[%d] %s",m.id,m.name.c_str());ImGui::TextWrapped("%s",m.original.c_str());ImGui::TextColored(ImVec4(.72f,.91f,.81f,1),"%s",m.translated.c_str());ImGui::Separator();}
            ImGui::EndChild();
        }
    }
    ImGui::End();
    }

    // Compact transparent incoming-translation HUD; fed by the future SA-MP chat hook.
    if (!hudSnapshot.empty()) {
        ImGui::SetNextWindowPos(ImVec2(22.f, io.DisplaySize.y - 225.f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(.48f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 7.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(9.f,7.f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(.015f,.025f,.022f,.48f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(.08f,.42f,.28f,.55f));
        if (ImGui::Begin("Lilzy Incoming##hud", nullptr, ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoFocusOnAppearing|ImGuiWindowFlags_NoNav|ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(.18f,.88f,.58f,1));
            ImGui::TextUnformatted("LILZY TRANSLATOR  /  INCOMING"); ImGui::PopStyleColor();
            ImGui::BeginChild("##messages", ImVec2(330.f, 174.f), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
            for (size_t i=0;i<hudSnapshot.size();++i) {
                const auto& m=hudSnapshot[i];
                ImGui::PushID((int)i);
                ImGui::TextColored(ImVec4(.20f,.85f,.58f,1), "[%d] %s", m.id, m.name.c_str());
                ImGui::TextWrapped("%s",m.original.c_str());
                ImGui::TextColored(ImVec4(.72f,.91f,.81f,1), "%s",m.translated.c_str());
                ImGui::Separator();
                ImGui::PopID();
            }
            if(forceHudBottom) ImGui::SetScrollHereY(1.f);
            { std::lock_guard<std::mutex> lock(g_hudMutex); g_hudAtBottom=ImGui::GetScrollY()>=ImGui::GetScrollMaxY()-4.f; }
            if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel > 0.f) g_hudAtBottom=false;
            ImGui::EndChild();
        }
        ImGui::End();
        ImGui::PopStyleColor(2); ImGui::PopStyleVar(2);
    }
    ImGui::PopStyleColor(6); ImGui::PopStyleVar(4);
}
