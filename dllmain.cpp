#include <windows.h>
#include <fstream>
#include <string>
#include <thread>
#include <algorithm>
#include <cctype>
#include "config.hpp"
#include "player_filter.hpp"
#include "translator.hpp"
#include "samp_client.hpp"

static HMODULE g_module=nullptr;
static Config g_config;
static SampClient g_samp;
static void Log(const std::string&s){char p[MAX_PATH]{};GetModuleFileNameA(g_module,p,MAX_PATH);std::string f=p;auto n=f.find_last_of("\\/");f=(n==std::string::npos?".":f.substr(0,n))+"\\lilzy_translator_native.log";std::ofstream o(f,std::ios::app);if(o)o<<s<<"\r\n";}
static std::string Trim(const char*raw){std::string s=raw?raw:"";auto a=s.find_first_not_of(" \t\r\n");if(a==s.npos)return {};auto b=s.find_last_not_of(" \t\r\n");return s.substr(a,b-a+1);}
static std::string Lower(std::string s){std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return (char)std::tolower(c);});return s;}
static void Chat(const std::string&s,DWORD c=0xFF66FF99UL){g_samp.AddChatMessage(s.c_str(),c);}
static void CmdMenu(const char*){Chat("[Lilzy] Native Translator R3 | /tr [text] | /provider free|groq | /setkey [key]",0xFF66FF99UL);Chat("[Lilzy] /style formal|casual|los_angeles|latino|bay_area|chicago|new_york|atlanta|houston|miami|british_urban|asian_american",0xFFFFFFFFUL);Chat("[Lilzy] /mood neutral|friendly|aggressive|sarcastic|playful|cold|professional|romantic",0xFFFFFFFFUL);Chat("[Lilzy] /lock [player] | /clearlock | /toggle on|off",0xFFFFFFFFUL);}
static void CmdLock(const char*p){auto n=Trim(p);if(n.empty()){Chat("[Lilzy] Usage: /lock Player_Name",0xFFFFCC66UL);return;}if(LockPlayer(g_config,n))Chat("[Lilzy] Target tersimpan: "+n);else Chat("[Lilzy] Gagal: whitelist maksimal 3 player.",0xFFFF7777UL);}
static void CmdClearLock(const char*){ClearLocks(g_config);Chat("[Lilzy] Whitelist dikosongkan.");}
static void CmdSetKey(const char*p){auto k=Trim(p);if(k.empty()){Chat("[Lilzy] Usage: /setkey gsk_xxxxx",0xFFFFCC66UL);return;}g_config.apiKey=k;if(SaveConfig(g_config))Chat("[Lilzy] Groq API key tersimpan.");else Chat("[Lilzy] Gagal menyimpan konfigurasi.",0xFFFF7777UL);}
static void CmdProvider(const char*p){auto v=Lower(Trim(p));if(v!="free"&&v!="groq"){Chat("[Lilzy] Usage: /provider free atau /provider groq",0xFFFFCC66UL);return;}g_config.provider=v;SaveConfig(g_config);Chat("[Lilzy] Provider aktif: "+v);}
static void CmdStyle(const char*p){auto v=Lower(Trim(p));static const char*valid[]={"formal","casual","los_angeles","latino","bay_area","chicago","new_york","atlanta","houston","miami","british_urban","asian_american"};for(auto x:valid)if(v==x){g_config.preset=v;SaveConfig(g_config);Chat("[Lilzy] Style: "+v);return;}Chat("[Lilzy] Style tidak dikenal. Gunakan /menu.",0xFFFFCC66UL);}
static void CmdMood(const char*p){auto v=Lower(Trim(p));static const char*valid[]={"neutral","friendly","aggressive","sarcastic","playful","cold","professional","romantic"};for(auto x:valid)if(v==x){g_config.mood=v;SaveConfig(g_config);Chat("[Lilzy] Mood: "+v);return;}Chat("[Lilzy] Mood tidak dikenal. Gunakan /menu.",0xFFFFCC66UL);}
static void CmdToggle(const char*p){auto v=Lower(Trim(p));if(v!="on"&&v!="off"){Chat("[Lilzy] Usage: /toggle on|off",0xFFFFCC66UL);return;}g_config.enabled=v=="on";SaveConfig(g_config);Chat(std::string("[Lilzy] Translator ")+(g_config.enabled?"ON":"OFF"));}
struct WorkItem{Config cfg;std::string text;};
static DWORD WINAPI TranslateWorker(void*raw){WorkItem*w=(WorkItem*)raw;std::string out,err;bool ok=TranslateText(w->cfg,w->text,out,err);if(ok){if(!CopyUnicodeText(out))Log("Clipboard write failed after successful translation.");else Log("Translation copied to clipboard: "+out);}else Log("Translation failed: "+err);delete w;return 0;}
static void CmdTr(const char*p){auto text=Trim(p);if(text.empty()){Chat("[Lilzy] Usage: /tr [text]",0xFFFFCC66UL);return;}if(!g_config.enabled){Chat("[Lilzy] Translator sedang OFF.",0xFFFF7777UL);return;}if(g_config.provider=="groq"&&g_config.apiKey.empty()){Chat("[Lilzy] Set API key dulu: /setkey [key]",0xFFFF7777UL);return;}auto*w=new WorkItem{g_config,text};HANDLE h=CreateThread(nullptr,0,TranslateWorker,w,0,nullptr);if(h)CloseHandle(h);else{delete w;Chat("[Lilzy] Gagal membuat translation worker.",0xFFFF7777UL);}}
static DWORD WINAPI Initialize(void*){LoadConfig(g_config);Log("[Lilzy] ASI R3 loaded; waiting for samp.dll.");std::string reason;for(int i=0;i<600;i++){if(GetModuleHandleA("samp.dll")&&g_samp.Detect(reason)){bool ok=true;ok&=g_samp.RegisterCommand("menu",&CmdMenu);ok&=g_samp.RegisterCommand("menus",&CmdMenu);ok&=g_samp.RegisterCommand("translator",&CmdMenu);ok&=g_samp.RegisterCommand("tr",&CmdTr);ok&=g_samp.RegisterCommand("provider",&CmdProvider);ok&=g_samp.RegisterCommand("setkey",&CmdSetKey);ok&=g_samp.RegisterCommand("style",&CmdStyle);ok&=g_samp.RegisterCommand("mood",&CmdMood);ok&=g_samp.RegisterCommand("lock",&CmdLock);ok&=g_samp.RegisterCommand("clearlock",&CmdClearLock);ok&=g_samp.RegisterCommand("toggle",&CmdToggle);if(ok){Chat("[Lilzy] Native R3 aktif. Ketik /menu.");Log("SA-MP DL-R1 detected; command callbacks registered.");}else Log("One or more SA-MP command registrations failed.");return 0;}Sleep(100);}Log(std::string("SA-MP detection failed: ")+reason);return 0;}
BOOL APIENTRY DllMain(HMODULE h,DWORD r,LPVOID){if(r==DLL_PROCESS_ATTACH){g_module=h;DisableThreadLibraryCalls(h);HANDLE t=CreateThread(nullptr,0,Initialize,nullptr,0,nullptr);if(t)CloseHandle(t);}return TRUE;}
