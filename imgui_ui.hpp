#pragma once
#include "config.hpp"
#include <string>

void LilzyUiSetVisible(bool visible);
bool LilzyUiIsVisible();
void LilzyUiRender(Config& config, std::string& targetStatus);

void LilzyHudPushTranslation(int playerId, const char* playerName, const char* original, const char* translated);
