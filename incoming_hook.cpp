#include "incoming_hook.hpp"
#include "translator.hpp"
#include "imgui_ui.hpp"
#include <windows.h>
#include <MinHook.h>
#include <RakHook/rakhook.hpp>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace {
constexpr unsigned char kRpcChat = 101;
constexpr unsigned char kRpcSetPlayerName = 11;
constexpr unsigned char kRpcServerJoin = 137;
constexpr unsigned char kRpcServerQuit = 138;

struct MinHookBackend {
    static bool initialize() {
        const MH_STATUS s = MH_Initialize();
        g_ownsMinHook = (s == MH_OK);
        return s == MH_OK || s == MH_ERROR_ALREADY_INITIALIZED;
    }
    static void uninitialize() {
        if (g_ownsMinHook) { MH_Uninitialize(); g_ownsMinHook = false; }
    }
    template <typename Fn, auto Detour>
    struct hook {
        explicit hook(Fn target) : target_(target) {}
        ~hook() { uninstall(); }
        bool install() {
            if (!target_) return false;
            if (created_) return true;
            if (MH_CreateHook(reinterpret_cast<void*>(target_), reinterpret_cast<void*>(Detour), reinterpret_cast<void**>(&original_)) != MH_OK) return false;
            created_ = true;
            if (MH_EnableHook(reinterpret_cast<void*>(target_)) != MH_OK) {
                MH_RemoveHook(reinterpret_cast<void*>(target_)); created_ = false; original_ = nullptr; return false;
            }
            return true;
        }
        bool uninstall() {
            if (!created_) return true;
            MH_DisableHook(reinterpret_cast<void*>(target_));
            const MH_STATUS s = MH_RemoveHook(reinterpret_cast<void*>(target_));
            created_ = false; original_ = nullptr; return s == MH_OK;
        }
        Fn get_original() { return original_; }
    private:
        Fn target_ = nullptr;
        Fn original_ = nullptr;
        bool created_ = false;
    };
private:
    static inline bool g_ownsMinHook = false;
};
using RakHook = rakhook::hooker<MinHookBackend>;

struct IncomingTask { Config config; unsigned short playerId=0; std::string playerName, text; };
Config* g_config=nullptr;
std::string* g_status=nullptr;
std::mutex g_stateMutex, g_queueMutex;
std::unordered_map<unsigned short,std::string> g_playerNames;
std::condition_variable g_queueCv;
std::deque<IncomingTask> g_queue;
std::atomic<bool> g_workerStarted{false}, g_hookInstalled{false};

bool ReadString8(RakNet::BitStream* bs, std::string& out) {
    if (!bs) return false;
    unsigned char length=0;
    if (!bs->Read(length)) return false;
    if (!length) { out.clear(); return true; }
    if (bs->GetNumberOfUnreadBits() < static_cast<unsigned int>(length)*8u) return false;
    char buffer[256]{};
    if (!bs->Read(buffer,length)) return false;
    out.assign(buffer,length);
    return true;
}
void WorkerLoop() {
    for (;;) {
        IncomingTask task;
        {
            std::unique_lock<std::mutex> lock(g_queueMutex);
            g_queueCv.wait(lock, [] { return !g_queue.empty(); });
            task=std::move(g_queue.front()); g_queue.pop_front();
        }
        std::string translated,error;
        if (TranslateText(task.config,task.text,translated,error,true))
            LilzyHudPushTranslation(task.playerId,task.playerName.c_str(),task.text.c_str(),translated.c_str());
    }
}
void StartWorker() {
    bool expected=false;
    if (g_workerStarted.compare_exchange_strong(expected,true)) std::thread(WorkerLoop).detach();
}
void QueueIncoming(unsigned short id,const std::string& name,const std::string& text,const Config& cfg) {
    if (text.empty()) return;
    { std::lock_guard<std::mutex> lock(g_queueMutex); if(g_queue.size()>=24) g_queue.pop_front(); g_queue.push_back({cfg,id,name,text}); }
    g_queueCv.notify_one();
}

bool OnReceiveRpc(unsigned char& rpcId,RakNet::BitStream* bs) {
    if (!bs) return true;
    // Names are cached only for display. Sender matching is always numeric ID.
    if (rpcId==kRpcServerJoin) {
        unsigned short id=0; int color=0; unsigned char npc=0; std::string name;
        if(bs->Read(id)&&bs->Read(color)&&bs->Read(npc)&&ReadString8(bs,name)) { std::lock_guard<std::mutex> lock(g_stateMutex); g_playerNames[id]=name; }
        return true;
    }
    if (rpcId==kRpcSetPlayerName) {
        unsigned short id=0; unsigned char success=0; std::string name;
        if(bs->Read(id)&&ReadString8(bs,name)&&bs->Read(success)&&success) { std::lock_guard<std::mutex> lock(g_stateMutex); g_playerNames[id]=name; }
        return true;
    }
    if (rpcId==kRpcServerQuit) {
        unsigned short id=0; unsigned char reason=0;
        if(bs->Read(id)&&bs->Read(reason)) {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            g_playerNames.erase(id);
            if(g_config&&g_config->playerFilterEnabled&&g_config->targetPlayerId==id) {
                g_config->targetPlayerId=-1; g_config->playerFilterEnabled=false; SaveConfig(*g_config);
            }
        }
        return true;
    }
    // Only RPC 101 CHAT carries a sender ID. Server messages without one are ignored.
    if(rpcId!=kRpcChat||!g_config) return true;
    Config snapshot;
    { std::lock_guard<std::mutex> lock(g_stateMutex); snapshot=*g_config; }
    if(!snapshot.enabled||!snapshot.incoming||!snapshot.playerFilterEnabled||snapshot.targetPlayerId<0) return true;
    unsigned short senderId=0; std::string message;
    // SAMP.Lua incoming CHAT layout: uint16 playerId + string8 text.
    if(!bs->Read(senderId)||!ReadString8(bs,message)) return true;
    if(senderId>1004||senderId!=static_cast<unsigned short>(snapshot.targetPlayerId)) return true;
    std::string displayName;
    { std::lock_guard<std::mutex> lock(g_stateMutex); auto it=g_playerNames.find(senderId); if(it==g_playerNames.end()) return true; displayName=it->second.empty()?"Player ID "+std::to_string(senderId):it->second; }
    QueueIncoming(senderId,displayName,message,snapshot);
    return true;
}
}

bool InstallIncomingRpcHook(Config* config,std::string* status) {
    if(g_hookInstalled.load()) return true;
    if(!config) return false;
    g_config=config; g_status=status; StartWorker();
    RakHook::on_receive_rpc += &OnReceiveRpc;
    if(!RakHook::initialize()) { if(g_status)*g_status="RakHook gagal diinisialisasi: versi/offset tidak cocok atau hook backend gagal."; return false; }
    g_hookInstalled=true;
    if(g_status)*g_status="Incoming RPC aktif (SA-MP 0.3.DL-R1). Lock Player memakai ID.";
    return true;
}

void IncomingConfigLock() { g_stateMutex.lock(); }
void IncomingConfigUnlock() { g_stateMutex.unlock(); }
