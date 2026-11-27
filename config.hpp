#pragma once
#include <array>
#include <string>
struct Config {
    bool enabled = true;
    bool autoCopy = true;
    bool incoming = true;
    std::string provider = "free"; // free | groq
    std::string apiKey;
    std::string model = "openai/gpt-oss-20b";
    std::string preset = "casual";
    std::string mood = "neutral";
    std::array<std::string, 3> locked{}; // legacy name whitelist; retained for config compatibility
    bool playerFilterEnabled = false;
    int targetPlayerId = -1;
};
std::string ConfigFile();
bool LoadConfig(Config& c);
bool SaveConfig(const Config& c);
