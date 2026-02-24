#pragma once

#include "../atom_functions/json_min.hpp"

#include <string>

namespace simagv::l1 {

bool tryLoadHotSimConfig(const std::string& filePath, simagv::json::Object& outSimConfig, std::string& outError);

} // namespace simagv::l1

