#pragma once
#include "config.hpp"
#include <string>

bool InstallIncomingRpcHook(Config* config, std::string* status);
void IncomingConfigLock();
void IncomingConfigUnlock();
