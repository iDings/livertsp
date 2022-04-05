#pragma once

#include <string>
#include <map>

namespace LiveRTSP {
// type + key-valuea_map
using ParamTypeKeyValMap = std::pair<std::string, std::map<std::string, std::string>>;
}
