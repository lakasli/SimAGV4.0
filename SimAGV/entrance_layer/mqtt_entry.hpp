#pragma once

#include "../coordination_layer/l2_utils.hpp"
#include "../coordination_layer/ports.hpp"

#include "l1_types.hpp"

#include <cstdint>
#include <mutex>
#include <string>

namespace simagv::l1 {

class MqttEntry final {
public:
    explicit MqttEntry(simagv::l2::ICommandSubmitter& submitter);

    EntryAck handleMessage(const std::string& topic, const std::string& payload);

private:
    simagv::l2::ICommandSubmitter& submitter_;
    std::mutex mutex_;
    uint64_t lastInstantHeaderId_;

    simagv::l2::CommandIntent buildIntent(const std::string& topic, const std::string& payload, uint64_t nowMsValue);
};

} // namespace simagv::l1

