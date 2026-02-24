#pragma once

#include <string>

namespace simagv::l2 {

inline bool isMapIdCompatible(const std::string& ownMapId, const std::string& otherMapId)
{
    if (ownMapId.empty() || otherMapId.empty()) {
        return false;
    }
    return ownMapId == otherMapId;
}

} // namespace simagv::l2
