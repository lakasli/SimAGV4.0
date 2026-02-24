#pragma once

#include "../atom_functions/json_min.hpp"

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace simagv::l2 {

enum class TopicType : uint8_t {
    Order,
    InstantActions,
    SimConfig,
    State,
    Visualization,
    Connection,
    Factsheet,
    Unknown
};

struct TraceRecord {
    uint64_t timestampMs;
    std::string flowId;
    std::string stepId;
    uint64_t serial;
    TopicType topicType;
    uint64_t headerId;
    std::string result;
    uint32_t elapsedMs;
    std::string summary;
};

struct CommandIntent {
    TopicType topicType;
    std::string topic;
    simagv::json::Value payload;
    uint64_t receiveTimestampMs;
    uint64_t serial;
    uint64_t headerId;
    std::string mapId;
};

struct PublishLimits {
    float maxPublishHz;
};

struct RuntimeConfig {
    float simTimeScale;
    float stateFrequencyHz;
    float visualizationFrequencyHz;
    float connectionFrequencyHz;
    float factsheetFrequencyHz;
    PublishLimits publishLimits;
};

struct Snapshot {
    uint64_t timestampMs;
    std::string mapId;
    simagv::json::Object state;
    simagv::json::Object visualization;
    simagv::json::Object connection;
    simagv::json::Object factsheet;
};

struct PerceptionContact {
    std::string manufacturer;
    std::string serialNumber;
};

struct PerceptionUpdate {
    bool blocked;
    bool collision;
    std::vector<PerceptionContact> blockedBy;
    std::vector<PerceptionContact> collidedWith;
};

inline TopicType topicTypeFromTopic(std::string_view topic) {
    const size_t slashPos = topic.rfind('/');
    const std::string_view last = (slashPos == std::string_view::npos) ? topic : topic.substr(slashPos + 1);
    if (last == "order") {
        return TopicType::Order;
    }
    if (last == "instantActions") {
        return TopicType::InstantActions;
    }
    if (last == "simConfig") {
        return TopicType::SimConfig;
    }
    if (last == "state") {
        return TopicType::State;
    }
    if (last == "visualization") {
        return TopicType::Visualization;
    }
    if (last == "connection") {
        return TopicType::Connection;
    }
    if (last == "factsheet") {
        return TopicType::Factsheet;
    }
    return TopicType::Unknown;
}

} // namespace simagv::l2
