#include "player_filter.hpp"
#include <algorithm>
#include <cctype>
static std::string lower(std::string s){std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return (char)std::tolower(c);});return s;}
bool LockPlayer(Config& c,const std::string& n){if(n.empty()||n.size()>24)return false;for(auto& x:c.locked)if(lower(x)==lower(n))return true;for(auto& x:c.locked)if(x.empty()){x=n;SaveConfig(c);return true;}return false;}
void ClearLocks(Config& c){for(auto& x:c.locked)x.clear();SaveConfig(c);}
bool IsLocked(const Config& c,const std::string& n){for(const auto& x:c.locked)if(!x.empty()&&lower(x)==lower(n))return true;return false;}
