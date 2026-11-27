#include <windows.h>
#include <fstream>
#include <string>
#include <thread>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include "config.hpp"
#include "player_filter.hpp"
#include "translator.hpp"
#include "samp_client.hpp"
#include "imgui_ui.hpp"
#include "d3d9_hook.hpp"
#include "incoming_hook.hpp"

static HMODULE g_module=nullptr;
static Config g_config;
static SampClient g_samp;
static std::string g_targetStatus="Belum ada target ID.";
static void Log(const std::string&s){char p[MAX_PATH]{};GetModuleFileNameA(g_module,p,MAX_PATH);std::string f=p;auto n=f.find_last_of("\\/");f=(n==std::string::npos?".":f.substr(0,n))+"\\lilzy_translator_native.log";std::ofstream o(f,std::ios::app);if(o)o<<s<<"\r\n";}
static std::string Trim(const char*raw){std::string s=raw?raw:"";auto a=s.find_first_not_of(" \t\r\n");if(a==s.npos)return {};auto b=s.find_last_not_of(" \t\r\n");return s.substr(a,b-a+1);}
static std::string Lower(std::string s){std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return (char)std::tolower(c);});return s;}
static void Chat(const std::string&s,DWORD c=0xFF66FF99UL){g_samp.AddChatMessage(s.c_str(),c);}
static void CmdMenu(const char*){LilzyUiSetVisible(!LilzyUiIsVisible());}
static void CmdLockPlayer(const char*p){auto n=Trim(p);if(n.empty()){Chat("[Lilzy] Usage: /lockplayer [ID] atau /lockplayer off",0xFFFFCC66UL);return;}if(Lower(n)=="off"){IncomingConfigLock();g_config.targetPlayerId=-1;g_config.playerFilterEnabled=false;SaveConfig(g_config);IncomingConfigUnlock();g_targetStatus="Lock Player dinonaktifkan.";Chat("[Lilzy] Player filter OFF.");return;}char*e=nullptr;long id=strtol(n.c_str(),&e,10);if(!e||*e||id<0||id>1004){Chat("[Lilzy] ID tidak valid. Gunakan ID 0-1004.",0xFFFF7777UL);return;}IncomingConfigLock();g_config.targetPlayerId=(int)id;g_config.playerFilterEnabled=true;SaveConfig(g_config);IncomingConfigUnlock();g_targetStatus="Target ID "+std::to_string(id)+" tersimpan. Pesan tanpa sender ID akan diabaikan.";Chat("[Lilzy] Target ID tersimpan: "+std::to_string(id));}
static void CmdClearLock(const char*){IncomingConfigLock();g_config.targetPlayerId=-1;g_config.playerFilterEnabled=false;SaveConfig(g_config);IncomingConfigUnlock();g_targetStatus="Lock Player dinonaktifkan.";Chat("[Lilzy] Player filter dinonaktifkan.");}
static void CmdSetKey(const char*p){auto k=Trim(p);if(k.empty()){Chat("[Lilzy] Usage: /setkey gsk_xxxxx",0xFFFFCC66UL);return;}IncomingConfigLock();g_config.apiKey=k;bool saved=SaveConfig(g_config);IncomingConfigUnlock();Chat(saved?"[Lilzy] Groq API key tersimpan.":"[Lilzy] Gagal menyimpan konfigurasi.",saved?0xFF66FF99UL:0xFFFF7777UL);}
static void CmdProvider(const char*p){auto v=Lower(Trim(p));if(v!="free"&&v!="groq"){Chat("[Lilzy] Usage: /provider free atau /provider groq",0xFFFFCC66UL);return;}IncomingConfigLock();g_config.provider=v;SaveConfig(g_config);IncomingConfigUnlock();Chat("[Lilzy] Provider aktif: "+v);}
static void CmdStyle(const char*p){auto v=Lower(Trim(p));static const char*valid[]={"formal","casual","los_angeles","latino","bay_area","chicago","new_york","atlanta","houston","miami","british_urban","asian_american"};for(auto x:valid)if(v==x){IncomingConfigLock();g_config.preset=v;SaveConfig(g_config);IncomingConfigUnlock();Chat("[Lilzy] Style: "+v);return;}Chat("[Lilzy] Style tidak dikenal. Gunakan /menu.",0xFFFFCC66UL);}
static void CmdMood(const char*p){auto v=Lower(Trim(p));static const char*valid[]={"neutral","friendly","aggressive","sarcastic","playful","cold","professional","romantic"};for(auto x:valid)if(v==x){IncomingConfigLock();g_config.mood=v;SaveConfig(g_config);IncomingConfigUnlock();Chat("[Lilzy] Mood: "+v);return;}Chat("[Lilzy] Mood tidak dikenal. Gunakan /menu.",0xFFFFCC66UL);}
static void CmdToggle(const char*p){auto v=Lower(Trim(p));if(v!="on"&&v!="off"){Chat("[Lilzy] Usage: /toggle on|off",0xFFFFCC66UL);return;}IncomingConfigLock();g_config.enabled=v=="on";SaveConfig(g_config);bool enabled=g_config.enabled;IncomingConfigUnlock();Chat(std::string("[Lilzy] Translator ")+(enabled?"ON":"OFF"));}
struct WorkItem{Config cfg;std::string text;};
static DWORD WINAPI TranslateWorker(void*raw){WorkItem*w=(WorkItem*)raw;std::string out,err;bool ok=TranslateText(w->cfg,w->text,out,err);if(ok){if(!CopyUnicodeText(out))Log("Clipboard write failed after successful translation.");else Log("Translation copied to clipboard: "+out);}else Log("Translation failed: "+err);delete w;return 0;}
static void CmdTr(const char*p){auto text=Trim(p);if(text.empty()){Chat("[Lilzy] Usage: /tr [text]",0xFFFFCC66UL);return;}Config snapshot;IncomingConfigLock();snapshot=g_config;IncomingConfigUnlock();if(!snapshot.enabled){Chat("[Lilzy] Translator sedang OFF.",0xFFFF7777UL);return;}if(snapshot.provider=="groq"&&snapshot.apiKey.empty()){Chat("[Lilzy] Set API key dulu: /setkey [key]",0xFFFF7777UL);return;}auto*w=new WorkItem{snapshot,text};HANDLE h=CreateThread(nullptr,0,TranslateWorker,w,0,nullptr);if(h)CloseHandle(h);else{delete w;Chat("[Lilzy] Gagal membuat translation worker.",0xFFFF7777UL);}}
static DWORD WINAPI Initialize(void*){LoadConfig(g_config);Log("[Lilzy] Native ASI R5.1 starting.");std::string reason;for(int i=0;i<600;i++){if(GetModuleHandleA("samp.dll")&&g_samp.Detect(reason)){bool ok=true;ok&=g_samp.RegisterCommand("menu",&CmdMenu);ok&=g_samp.RegisterCommand("translator",&CmdMenu);ok&=g_samp.RegisterCommand("tr",&CmdTr);ok&=g_samp.RegisterCommand("provider",&CmdProvider);ok&=g_samp.RegisterCommand("setkey",&CmdSetKey);ok&=g_samp.RegisterCommand("style",&CmdStyle);ok&=g_samp.RegisterCommand("mood",&CmdMood);ok&=g_samp.RegisterCommand("lockplayer",&CmdLockPlayer);ok&=g_samp.RegisterCommand("clearlock",&CmdClearLock);ok&=g_samp.RegisterCommand("toggle",&CmdToggle);if(ok){Chat("[Lilzy] Native R5 aktif. Ketik /menu.");Log("SA-MP DL-R1 recognized; commands registered.");if(InstallIncomingRpcHook(&g_config,&g_targetStatus))Log("RakHook incoming RPC observer installed.");else Log("RakHook incoming RPC observer installation failed.");if(InstallD3D9Overlay(&g_config,&g_targetStatus))Log("D3D9 ImGui hook installed.");else Log("D3D9 ImGui hook installation failed.");}else Log("One or more command registrations failed.");return 0;}Sleep(100);}Log(std::string("SA-MP detection failed: ")+reason);return 0;}
BOOL APIENTRY DllMain(HMODULE h,DWORD r,LPVOID){if(r==DLL_PROCESS_ATTACH){g_module=h;DisableThreadLibraryCalls(h);HANDLE t=CreateThread(nullptr,0,Initialize,nullptr,0,nullptr);if(t)CloseHandle(t);}return TRUE;}
