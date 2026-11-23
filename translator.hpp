#pragma once
#include "config.hpp"
#include <string>
bool TranslateText(const Config& cfg, const std::string& text, std::string& output, std::string& error);
bool CopyUnicodeText(const std::string& utf8);
