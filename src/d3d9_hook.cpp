#include "d3d9_hook.hpp"
#include "imgui_ui.hpp"
#include "incoming_hook.hpp"

#include <windows.h>
#include <d3d9.h>
#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx9.h>
#include <MinHook.h>

#include <atomic>
#include <cstdint>
#include <fstream>
#include <string>

#pragma comment(lib, "d3d9.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {
using EndSceneFn = HRESULT (WINAPI*)(IDirect3DDevice9*);
using ResetFn = HRESULT (WINAPI*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

EndSceneFn g_endScene = nullptr;
ResetFn g_reset = nullptr;
WNDPROC g_oldProc = nullptr;
HWND g_hwnd = nullptr;
Config* g_config = nullptr;
std::string* g_targetStatus = nullptr;

bool g_imguiReady = false;
bool g_win32Ready = false;
bool g_dx9Ready = false;
bool g_mhInitialized = false;
std::atomic<bool> g_installing{false};

void LogHook(const char* message) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&LogHook), &module)) {
        OutputDebugStringA(message);
        OutputDebugStringA("\n");
        return;
    }

    char path[MAX_PATH]{};
    GetModuleFileNameA(module, path, MAX_PATH);
    std::string file(path);
    const auto slash = file.find_last_of("\\/");
    file = (slash == std::string::npos ? "." : file.substr(0, slash));
    file += "\\lilzy_translator_native.log";

    std::ofstream out(file, std::ios::app);
    if (out) out << "[D3D9] " << message << "\r\n";
    OutputDebugStringA(message);
    OutputDebugStringA("\n");
}

const char* MhStatus(MH_STATUS status) {
    switch (status) {
    case MH_OK: return "MH_OK";
    case MH_ERROR_ALREADY_INITIALIZED: return "MH_ERROR_ALREADY_INITIALIZED";
    case MH_ERROR_ALREADY_CREATED: return "MH_ERROR_ALREADY_CREATED";
    case MH_ERROR_ENABLED: return "MH_ERROR_ENABLED";
    case MH_ERROR_DISABLED: return "MH_ERROR_DISABLED";
    case MH_ERROR_NOT_EXECUTABLE: return "MH_ERROR_NOT_EXECUTABLE";
    case MH_ERROR_UNSUPPORTED_FUNCTION: return "MH_ERROR_UNSUPPORTED_FUNCTION";
    case MH_ERROR_MEMORY_ALLOC: return "MH_ERROR_MEMORY_ALLOC";
    default: return "MinHook error";
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_imguiReady && LilzyUiIsVisible()) {
        ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp);
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse || io.WantCaptureKeyboard)
            return 1;
    }

    return g_oldProc ? CallWindowProc(g_oldProc, hwnd, msg, wp, lp)
                     : DefWindowProc(hwnd, msg, wp, lp);
}

HRESULT WINAPI HookReset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp) {
    if (!g_reset) return D3DERR_INVALIDCALL;

    if (g_imguiReady && g_dx9Ready)
        ImGui_ImplDX9_InvalidateDeviceObjects();

    const HRESULT hr = g_reset(dev, pp);

    if (SUCCEEDED(hr) && g_imguiReady && g_dx9Ready)
        ImGui_ImplDX9_CreateDeviceObjects();

    return hr;
}

HRESULT WINAPI HookEndScene(IDirect3DDevice9* dev) {
    if (!g_imguiReady) {
        D3DDEVICE_CREATION_PARAMETERS cp{};
        if (SUCCEEDED(dev->GetCreationParameters(&cp)) && cp.hFocusWindow) {
            g_hwnd = cp.hFocusWindow;

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();

            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            ImGui::StyleColorsDark();

            ImGuiStyle& style = ImGui::GetStyle();
            style.WindowRounding = 10.f;
            style.FrameRounding = 7.f;
            style.ScrollbarRounding = 8.f;
            style.Colors[ImGuiCol_WindowBg] = ImVec4(.02f, .035f, .03f, .96f);
            style.Colors[ImGuiCol_Border] = ImVec4(.08f, .34f, .23f, .8f);

            g_win32Ready = ImGui_ImplWin32_Init(g_hwnd);
            if (g_win32Ready) {
                g_dx9Ready = ImGui_ImplDX9_Init(dev);
            }

            if (g_win32Ready && g_dx9Ready) {
                SetLastError(0);
                const LONG_PTR previous = SetWindowLongPtr(
                    g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WindowProc));

                if (previous != 0 || GetLastError() == 0) {
                    g_oldProc = reinterpret_cast<WNDPROC>(previous);
                    g_imguiReady = true;
                    LogHook("ImGui Win32 + DX9 backends initialized.");
                } else {
                    LogHook("SetWindowLongPtr(GWLP_WNDPROC) failed.");
                }
            }

            if (!g_imguiReady) {
                if (g_dx9Ready) ImGui_ImplDX9_Shutdown();
                if (g_win32Ready) ImGui_ImplWin32_Shutdown();
                g_dx9Ready = false;
                g_win32Ready = false;
                ImGui::DestroyContext();
            }
        }
    }

    if (g_imguiReady && g_config && g_targetStatus) {
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::GetIO().MouseDrawCursor = LilzyUiIsVisible();

        IncomingConfigLock();
        LilzyUiRender(*g_config, *g_targetStatus);
        IncomingConfigUnlock();

        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    }

    return g_endScene ? g_endScene(dev) : D3D_OK;
}
}

bool InstallD3D9Overlay(Config* config, std::string* targetStatus) {
    if (!config || !targetStatus) {
        LogHook("Install rejected: null config/status pointer.");
        return false;
    }

    if (g_endScene && g_reset) return true;
    if (g_installing.exchange(true)) return false;

    g_config = config;
    g_targetStatus = targetStatus;

    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) {
        LogHook("Direct3DCreate9 failed.");
        g_installing = false;
        return false;
    }

    HWND hwnd = GetForegroundWindow();
    if (!hwnd || !IsWindow(hwnd)) hwnd = GetDesktopWindow();

    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = hwnd;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;

    IDirect3DDevice9* dummy = nullptr;
    HRESULT hr = d3d->CreateDevice(
        D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dummy);

    if (FAILED(hr)) {
        LogHook("HAL dummy device creation failed; trying REF device.");
        hr = d3d->CreateDevice(
            D3DADAPTER_DEFAULT, D3DDEVTYPE_REF, hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dummy);
    }

    if (FAILED(hr) || !dummy) {
        LogHook("Could not create a temporary D3D9 device; hook not installed.");
        d3d->Release();
        g_installing = false;
        return false;
    }

    void** vtable = *reinterpret_cast<void***>(dummy);
    void* endSceneAddress = vtable[42];
    void* resetAddress = vtable[16];

    if (!g_mhInitialized) {
        MH_STATUS initStatus = MH_Initialize();
        if (initStatus == MH_OK || initStatus == MH_ERROR_ALREADY_INITIALIZED) {
            g_mhInitialized = true;
        } else {
            std::string msg = "MH_Initialize failed: ";
            msg += MhStatus(initStatus);
            LogHook(msg.c_str());
        }
    }

    bool endCreated = false;
    bool resetCreated = false;
    bool endEnabled = false;
    bool resetEnabled = false;

    if (g_mhInitialized) {
        MH_STATUS status = MH_CreateHook(
            endSceneAddress, reinterpret_cast<void*>(&HookEndScene),
            reinterpret_cast<void**>(&g_endScene));
        endCreated = status == MH_OK;
        if (!endCreated) {
            std::string msg = "MH_CreateHook(EndScene) failed: ";
            msg += MhStatus(status);
            LogHook(msg.c_str());
        }

        status = MH_CreateHook(
            resetAddress, reinterpret_cast<void*>(&HookReset),
            reinterpret_cast<void**>(&g_reset));
        resetCreated = status == MH_OK;
        if (!resetCreated) {
            std::string msg = "MH_CreateHook(Reset) failed: ";
            msg += MhStatus(status);
            LogHook(msg.c_str());
        }

        if (endCreated) {
            status = MH_EnableHook(endSceneAddress);
            endEnabled = status == MH_OK;
            if (!endEnabled) {
                std::string msg = "MH_EnableHook(EndScene) failed: ";
                msg += MhStatus(status);
                LogHook(msg.c_str());
            }
        }

        if (resetCreated) {
            status = MH_EnableHook(resetAddress);
            resetEnabled = status == MH_OK;
            if (!resetEnabled) {
                std::string msg = "MH_EnableHook(Reset) failed: ";
                msg += MhStatus(status);
                LogHook(msg.c_str());
            }
        }
    }

    dummy->Release();
    d3d->Release();
    g_installing = false;

    if (endEnabled && resetEnabled) {
        LogHook("EndScene and Reset hooks installed successfully with MinHook.");
        return true;
    }

    // Roll back partial installation so the game is not left with half a hook.
    if (endEnabled) MH_DisableHook(endSceneAddress);
    if (resetEnabled) MH_DisableHook(resetAddress);
    if (endCreated) MH_RemoveHook(endSceneAddress);
    if (resetCreated) MH_RemoveHook(resetAddress);
    g_endScene = nullptr;
    g_reset = nullptr;

    LogHook("D3D9 hook installation incomplete; partial hooks rolled back.");
    return false;
}
