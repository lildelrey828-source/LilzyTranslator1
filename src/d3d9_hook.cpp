#include "d3d9_hook.hpp"
#include "imgui_ui.hpp"
#include "incoming_hook.hpp"
#include <windows.h>
#include <d3d9.h>
#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx9.h>
#include <atomic>
#include <cstring>
#include <cstdint>

#pragma comment(lib, "d3d9.lib")
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
namespace {
using EndSceneFn = HRESULT (WINAPI*)(IDirect3DDevice9*);
using ResetFn = HRESULT (WINAPI*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
EndSceneFn g_endScene = nullptr; ResetFn g_reset = nullptr;
BYTE g_endOriginal[5]{}; BYTE g_resetOriginal[5]{};
void* g_endTrampoline = nullptr; void* g_resetTrampoline = nullptr;
WNDPROC g_oldProc = nullptr; HWND g_hwnd = nullptr;
IDirect3DDevice9* g_device = nullptr;
Config* g_config = nullptr; std::string* g_targetStatus = nullptr;
bool g_imguiReady = false; std::atomic<bool> g_installing{false};

bool Patch(void* target, void* detour, BYTE original[5], void** trampoline) {
    if (!target || !detour) return false;
    DWORD old{}; if (!VirtualProtect(target, 5, PAGE_EXECUTE_READWRITE, &old)) return false;
    std::memcpy(original, target, 5);
    BYTE* gate = (BYTE*)VirtualAlloc(nullptr, 10, MEM_COMMIT|MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!gate) { VirtualProtect(target,5,old,&old); return false; }
    std::memcpy(gate, original, 5); gate[5]=0xE9; *reinterpret_cast<int32_t*>(gate+6)=(BYTE*)target+5-(gate+10);
    BYTE jump[5]{0xE9}; *reinterpret_cast<int32_t*>(jump+1)=(BYTE*)detour-(BYTE*)target-5;
    std::memcpy(target,jump,5); FlushInstructionCache(GetCurrentProcess(),target,5); VirtualProtect(target,5,old,&old);
    *trampoline=gate; return true;
}
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (LilzyUiIsVisible() && g_imguiReady) {
        ImGui_ImplWin32_WndProcHandler(hwnd,msg,wp,lp);
        ImGuiIO& io=ImGui::GetIO();
        if (io.WantCaptureMouse || io.WantCaptureKeyboard) return 1;
    }
    return CallWindowProc(g_oldProc,hwnd,msg,wp,lp);
}
HRESULT WINAPI HookReset(IDirect3DDevice9* dev,D3DPRESENT_PARAMETERS* pp) {
    if (g_imguiReady) ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr=g_reset(dev,pp);
    if (SUCCEEDED(hr) && g_imguiReady) ImGui_ImplDX9_CreateDeviceObjects();
    return hr;
}
HRESULT WINAPI HookEndScene(IDirect3DDevice9* dev) {
    if (!g_imguiReady) {
        D3DDEVICE_CREATION_PARAMETERS cp{};
        if (SUCCEEDED(dev->GetCreationParameters(&cp)) && cp.hFocusWindow) {
            g_device=dev; g_hwnd=cp.hFocusWindow;
            IMGUI_CHECKVERSION(); ImGui::CreateContext();
            ImGuiIO& io=ImGui::GetIO(); io.IniFilename=nullptr;
            ImGui::StyleColorsDark();
            ImGuiStyle& st=ImGui::GetStyle(); st.WindowRounding=10.f; st.FrameRounding=7.f; st.ScrollbarRounding=8.f;
            st.Colors[ImGuiCol_WindowBg]=ImVec4(.02f,.035f,.03f,.96f);
            st.Colors[ImGuiCol_Border]=ImVec4(.08f,.34f,.23f,.8f);
            if (ImGui_ImplWin32_Init(g_hwnd) && ImGui_ImplDX9_Init(dev)) {
                g_oldProc=(WNDPROC)SetWindowLongPtr(g_hwnd,GWLP_WNDPROC,(LONG_PTR)WindowProc);
                g_imguiReady=true;
            } else { ImGui::DestroyContext(); }
        }
    }
    if (g_imguiReady && g_config && g_targetStatus) {
        ImGui_ImplDX9_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
        ImGui::GetIO().MouseDrawCursor=LilzyUiIsVisible();
        IncomingConfigLock();
        LilzyUiRender(*g_config,*g_targetStatus);
        IncomingConfigUnlock();
        ImGui::EndFrame(); ImGui::Render(); ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    }
    return g_endScene(dev);
}
}

bool InstallD3D9Overlay(Config* config, std::string* targetStatus) {
    if (g_endTrampoline) return true;
    if (g_installing.exchange(true)) return false;
    g_config=config; g_targetStatus=targetStatus;
    HWND hwnd=GetForegroundWindow(); if(!hwnd)hwnd=GetDesktopWindow();
    IDirect3D9* d3d=Direct3DCreate9(D3D_SDK_VERSION); if(!d3d){g_installing=false;return false;}
    D3DPRESENT_PARAMETERS pp{}; pp.Windowed=TRUE; pp.SwapEffect=D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow=hwnd; pp.BackBufferFormat=D3DFMT_UNKNOWN;
    IDirect3DDevice9* dummy=nullptr;
    HRESULT hr=d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,hwnd,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dummy);
    if(FAILED(hr)){hr=d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_REF,hwnd,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dummy);}
    if(FAILED(hr)||!dummy){d3d->Release();g_installing=false;return false;}
    void** vt=*reinterpret_cast<void***>(dummy);
    bool a=Patch(vt[42],(void*)&HookEndScene,g_endOriginal,(void**)&g_endTrampoline);
    bool b=Patch(vt[16],(void*)&HookReset,g_resetOriginal,(void**)&g_resetTrampoline);
    if(a)g_endScene=(EndSceneFn)g_endTrampoline; if(b)g_reset=(ResetFn)g_resetTrampoline;
    dummy->Release(); d3d->Release(); g_installing=false;
    return a&&b;
}
