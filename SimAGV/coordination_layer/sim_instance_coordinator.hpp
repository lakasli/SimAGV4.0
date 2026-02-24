#pragma once

#include "l2_types.hpp"
#include "ports.hpp"
#include "trace_ring_buffer.hpp"

#include "../molecular_functions/common/l3_types.hpp"
#include "../molecular_functions/map/map_topology_builder.hpp"
#include "../molecular_functions/map/vehicle_map_topo_atoms.hpp"

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace simagv::l2 {

class SimInstanceCoordinator final : public ICommandSubmitter {
public:
    SimInstanceCoordinator(
        ISimulatorEngine& engine,
        IMqttDiplomat& diplomat,
        std::string mqttBaseTopic,
        std::string protocolVersion,
        std::string manufacturer,
        std::string serialNumber,
        RuntimeConfig initialConfig,
        size_t traceCapacity);

    void start();
    void tickOnce(uint32_t tickMs);

    void submitCommand(CommandIntent intent) override;
    Snapshot getSnapshot() const;
    std::vector<TraceRecord> getTrace() const;

private:
    static constexpr size_t kAcceptedOrderIdentityCacheMax = 50U;

    struct PublishSchedule {
        float stateElapsedMs;
        float visualizationElapsedMs;
        float connectionElapsedMs;
        float factsheetElapsedMs;
    };

    ISimulatorEngine& engine_;
    IMqttDiplomat& diplomat_;
    std::string mqttBaseTopic_;
    std::string protocolVersion_;
    std::string manufacturer_;
    std::string serialNumber_;
    mutable std::mutex mutex_;
    std::deque<CommandIntent> inbox_;
    RuntimeConfig config_;
    PublishSchedule schedule_;
    TraceRingBuffer trace_;
    bool booted_;
    bool powerOn_;
    uint64_t lastInstantHeaderId_;
    uint64_t stateHeaderId_;
    uint64_t visualizationHeaderId_;
    uint64_t connectionHeaderId_;
    uint64_t factsheetHeaderId_;
    std::string loadedMapId_;
    std::shared_ptr<const simagv::l4::VehicleMapTopoPackage> loadedMapTopo_;
    std::shared_ptr<const simagv::l3::MapTopology> loadedTopology_;
    std::unordered_map<std::string, std::shared_ptr<const simagv::l4::VehicleMapTopoPackage>> loadedMapTopoCache_;
    std::unordered_map<std::string, std::shared_ptr<const simagv::l3::MapTopology>> loadedTopologyCache_;
    std::deque<std::string> acceptedOrderIdentities_;
    std::unordered_set<std::string> acceptedOrderIdentitySet_;
    std::string lastAcceptedOrderId_; // 上一条已接受订单号
    uint64_t lastAcceptedOrderUpdateId_{0U}; // 上一条已接受订单更新号
    uint64_t orderTimestampWindowMs_{10000U}; // 订单时间戳允许窗口(ms)

    struct OtherVehicleVisualization {
        std::string manufacturer;
        std::string serialNumber;
        std::string mapId;
        float safetyCenterX;
        float safetyCenterY;
        float safetyLength;
        float safetyWidth;
        float safetyTheta;
        uint64_t receiveTimestampMs;
    };

    std::unordered_map<std::string, OtherVehicleVisualization> otherVisualizationsByKey_;
    uint64_t otherVisualizationStaleMs_{15000U};
    bool disableRadarBlockedOnRotation_{true};
    float disableRadarBlockedOmegaThreshold_{0.01F};
    float radarBlockedMinSpeedThreshold_{0.01F};
    float radarBlockedForwardSpeedThreshold_{0.01F};

    void processInbox();
    void handleOrder(const CommandIntent& intent);
    void handleInstantActions(const CommandIntent& intent);
    void handleSimConfig(const CommandIntent& intent);
    void handleVisualization(const CommandIntent& intent);
    void publishBootOnce(uint64_t nowMsValue);
    void publishBySchedule(uint32_t tickMs, uint64_t nowMsValue);
    void publishTopic(TopicType topicType, const Snapshot& snapshot);
    void updateRuntimeConfigFromSimConfig(const simagv::json::Value& simConfig);
    void preloadMapIfNeeded(const std::string& mapId, uint64_t startTs, const char* flowId, const char* stepId, const CommandIntent& intent);
    void pushTrace(
        uint64_t timestampMs,
        std::string flowId,
        std::string stepId,
        uint64_t serial,
        TopicType topicType,
        uint64_t headerId,
        std::string result,
        uint32_t elapsedMs,
        std::string summary);
};

} // namespace simagv::l2
