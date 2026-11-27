#include "samp_client.hpp"
#include <cstring>

namespace {
    constexpr unsigned char kDlR1Probe[10] = {
        0x52, 0x8D, 0x44, 0x24, 0x0C, 0x50, 0x8D, 0x7E, 0x09, 0xE8
    };

    bool IsReadable(const void* ptr, SIZE_T bytes) {
        if (!ptr || bytes == 0) return false;
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(ptr, &mbi, sizeof(mbi))) return false;
        if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
        const auto begin = reinterpret_cast<std::uintptr_t>(ptr);
        const auto end = begin + bytes;
        const auto regionEnd = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        return end >= begin && end <= regionEnd;
    }

    bool IsExecutable(const void* ptr) {
        if (!ptr) return false;
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(ptr, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return false;
        const DWORD p = mbi.Protect & 0xFF;
        return p == PAGE_EXECUTE || p == PAGE_EXECUTE_READ ||
               p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
    }

    DWORD ReadImageSize(HMODULE module) {
        if (!IsReadable(module, sizeof(IMAGE_DOS_HEADER))) return 0;
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x100000) return 0;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(reinterpret_cast<const BYTE*>(module) + dos->e_lfanew);
        if (!IsReadable(nt, sizeof(*nt)) || nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return 0;
        return nt->OptionalHeader.SizeOfImage;
    }
}

void* SampClient::Address(std::uintptr_t rva) const {
    if (!module || !imageSize || rva >= imageSize) return nullptr;
    return reinterpret_cast<BYTE*>(module) + rva;
}

bool SampClient::Detect(std::string& reason) {
    module = GetModuleHandleA("samp.dll");
    if (!module) { reason = "samp.dll not loaded"; return false; }

    imageSize = ReadImageSize(module);
    if (!imageSize) { reason = "Invalid or unreadable 32-bit PE image"; return false; }

    const auto* probe = static_cast<const BYTE*>(Address(SampDlR1Addresses::kVersionProbe));
    if (!probe || !IsReadable(probe, sizeof(kDlR1Probe)) ||
        std::memcmp(probe, kDlR1Probe, sizeof(kDlR1Probe)) != 0) {
        reason = "samp.dll is not recognized as SA-MP 0.3.DL-R1";
        return false;
    }

    version = SampClientVersion::DlR1;
    auto readPointer = [this](std::uintptr_t rva) -> void* {
        const auto slot = Address(rva);
        if (!IsReadable(slot, sizeof(void*))) return nullptr;
        return *reinterpret_cast<void* const*>(slot);
    };

    chatInfo = readPointer(SampDlR1Addresses::kChatInfoPtr);
    inputInfo = readPointer(SampDlR1Addresses::kInputInfoPtr);
    sampInfo = readPointer(SampDlR1Addresses::kSampInfoPtr);
    gameInfo = readPointer(SampDlR1Addresses::kGameInfoPtr);

    if (!chatInfo || !inputInfo || !sampInfo || !gameInfo) {
        reason = "DL-R1 signature matched, but one or more SA-MP interface pointers are not initialized";
        return false;
    }

    const std::uintptr_t functions[] = {
        SampDlR1Addresses::kAddChatCommand,
        SampDlR1Addresses::kAddChatMessage,
        SampDlR1Addresses::kSendCommand,
        SampDlR1Addresses::kSay
    };
    for (const auto rva : functions) {
        if (!IsExecutable(Address(rva))) {
            reason = "A known DL-R1 function RVA is outside executable memory";
            return false;
        }
    }

    reason = "SA-MP 0.3.DL-R1 recognized; interface pointers and function regions validated";
    return true;
}

bool SampClient::IsReady() const {
    return version == SampClientVersion::DlR1 && module && chatInfo && inputInfo && sampInfo && gameInfo;
}


bool SampClient::RegisterCommand(const char* name, void (__cdecl* callback)(const char*)) {
    if (!IsReady() || !name || !callback || !*name) return false;
    using AddCommandFn = void (__thiscall*)(void*, const char*, void (__cdecl*)(const char*));
    auto fn = reinterpret_cast<AddCommandFn>(Address(SampDlR1Addresses::kAddChatCommand));
    if (!fn) return false;
    fn(inputInfo, name, callback);
    return true;
}

void SampClient::AddChatMessage(const char* message, unsigned long textColor) {
    if (!IsReady() || !message) return;
    using AddChatMessageFn = void (__thiscall*)(void*, unsigned int, const char*, const char*, unsigned long, unsigned long);
    auto fn = reinterpret_cast<AddChatMessageFn>(Address(SampDlR1Addresses::kAddChatMessage));
    if (!fn) return;
    // SA-MP chat entry type 4 is a text-only server/client message.
    fn(chatInfo, 4u, message, "", textColor, textColor);
}
