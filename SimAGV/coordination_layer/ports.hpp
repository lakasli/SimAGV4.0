#pragma once

#include "l2_types.hpp"

#include <cstdint>
#include <string>

namespace simagv::l2 {

class ICommandSubmitter {
public:
    virtual ~ICommandSubmitter() = default;
    virtual void submitCommand(CommandIntent intent) = 0;
};

class IMqttDiplomat {
public:
    virtual ~IMqttDiplomat() = default;
    virtual void publish(std::string topic, std::string payload, uint8_t qos, bool retain) = 0;
};

class ISimulatorEngine {
public:
    virtual ~ISimulatorEngine() = default;
    virtual void applyOrder(const simagv::json::Value& order) = 0;
    virtual void applyInstantActions(const simagv::json::Value& instantActions) = 0;
    virtual void applyConfig(const simagv::json::Value& simConfig) = 0;
    virtual void applyPerceptionUpdate(const PerceptionUpdate& update) = 0;
    virtual void updateState(uint32_t tickMs) = 0;
    virtual Snapshot buildSnapshot(uint64_t nowMs) const = 0;
};

} // namespace simagv::l2
