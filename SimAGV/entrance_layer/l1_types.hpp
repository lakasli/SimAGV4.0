#pragma once

#include "../coordination_layer/l2_types.hpp"

#include <cstdint>
#include <string>

namespace simagv::l1 {

struct EntryAck {
    bool ok;
    std::string message;
    uint64_t headerId;
    simagv::l2::TopicType topicType;
};

} // namespace simagv::l1

