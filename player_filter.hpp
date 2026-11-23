#pragma once
#include "config.hpp"
bool LockPlayer(Config&, const std::string&);
void ClearLocks(Config&);
bool IsLocked(const Config&, const std::string&);
