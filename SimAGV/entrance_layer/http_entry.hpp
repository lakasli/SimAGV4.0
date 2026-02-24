#pragma once

#include "../coordination_layer/l2_utils.hpp"
#include "../coordination_layer/ports.hpp"

#include "l1_types.hpp"

#include <cstdint>
#include <string>

namespace simagv::l1 {

class HttpEntry final {
public:
    explicit HttpEntry(simagv::l2::ICommandSubmitter& submitter);

    EntryAck handleControlRequest(const std::string& path, const std::string& jsonBody);

private:
    simagv::l2::ICommandSubmitter& submitter_;
};

} // namespace simagv::l1

