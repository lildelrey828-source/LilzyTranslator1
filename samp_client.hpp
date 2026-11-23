#pragma once
#include <windows.h>
#include <cstdint>
#include <string>

// Address map is specifically for SA-MP 0.3.DL-R1. These are RVAs,
// not absolute addresses; samp.dll may be rebased by Windows.
struct SampDlR1Addresses {
    static constexpr std::uintptr_t kVersionProbe = 0xBABE;
    static constexpr std::uintptr_t kChatInfoPtr = 0x2ACA10;
    static constexpr std::uintptr_t kInputInfoPtr = 0x2ACA14;
    static constexpr std::uintptr_t kSampInfoPtr = 0x2ACA24;
    static constexpr std::uintptr_t kGameInfoPtr = 0x2ACA3C;
    static constexpr std::uintptr_t kAddChatCommand = 0x0691B0;
    static constexpr std::uintptr_t kAddChatMessage = 0x067650;
    static constexpr std::uintptr_t kSendCommand = 0x069340;
    static constexpr std::uintptr_t kSay = 0x005860;
};

enum class SampClientVersion { Unknown, DlR1 };

struct SampClient {
    HMODULE module = nullptr;
    DWORD imageSize = 0;
    SampClientVersion version = SampClientVersion::Unknown;
    void* chatInfo = nullptr;
    void* inputInfo = nullptr;
    void* sampInfo = nullptr;
    void* gameInfo = nullptr;

    bool Detect(std::string& reason);
    bool IsReady() const;
    void* Address(std::uintptr_t rva) const;
    bool RegisterCommand(const char* name, void (__cdecl* callback)(const char*));
    void AddChatMessage(const char* message, unsigned long textColor = 0xFFFFFFFFUL);
};
