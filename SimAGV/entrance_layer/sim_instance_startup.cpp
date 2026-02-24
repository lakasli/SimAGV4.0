#include "http_entry.hpp"
#include "hot_config_loader.hpp"
#include "mqtt_entry.hpp"
#include "mqtt_broker_diplomat.hpp"
#include "yaml_config_loader.hpp"

#include "../coordination_layer/l2_utils.hpp"
#include "../coordination_layer/sim_instance_coordinator.hpp"
#include "../atom_functions/console_log_atoms.hpp"
#include "../atom_functions/error_management_atoms.hpp"
#include "../atom_functions/map_atoms.hpp"
#include "../atom_functions/path_planning_atoms.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

struct RuntimeCliConfig final {
    uint32_t tickMs;
    size_t traceCapacity;
    std::string mqttBaseTopic;
    std::string configPath;
    bool stdoutMqtt;
    float simTimeScale;
    float stateFrequencyHz;
    float visualizationFrequencyHz;
    float maxPublishHz;
};

bool tryParseUint32(const std::string& text, uint32_t& outValue)
{
    try {
        const unsigned long parsed = std::stoul(text);
        if (parsed > 0xFFFFFFFFUL) {
            return false;
        }
        outValue = static_cast<uint32_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool tryParseSizeT(const std::string& text, size_t& outValue)
{
    try {
        outValue = static_cast<size_t>(std::stoull(text));
        return true;
    } catch (...) {
        return false;
    }
}

bool tryParseFloat(const std::string& text, float& outValue)
{
    try {
        outValue = std::stof(text);
        return true;
    } catch (...) {
        return false;
    }
}

RuntimeCliConfig makeDefaultConfig()
{
    RuntimeCliConfig config;
    config.tickMs = 50;
    config.traceCapacity = 2048;
    config.mqttBaseTopic = "";
    config.configPath = "./config.yaml";
    config.stdoutMqtt = false;
    config.simTimeScale = 1.0F;
    config.stateFrequencyHz = 10.0F;
    config.visualizationFrequencyHz = 5.0F;
    config.maxPublishHz = 30.0F;
    return config;
}

RuntimeCliConfig parseArgs(int argc, char** argv)
{
    RuntimeCliConfig config = makeDefaultConfig();
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        auto readNext = [&](std::string& outValue) -> bool {
            if (i + 1 >= argc) {
                return false;
            }
            outValue = argv[++i];
            return true;
        };

        if (arg == "--tick-ms") {
            std::string value;
            if (!readNext(value) || !tryParseUint32(value, config.tickMs)) {
                throw std::runtime_error("invalid --tick-ms");
            }
            continue;
        }
        if (arg == "--trace-capacity") {
            std::string value;
            if (!readNext(value) || !tryParseSizeT(value, config.traceCapacity)) {
                throw std::runtime_error("invalid --trace-capacity");
            }
            continue;
        }
        if (arg == "--mqtt-base-topic") {
            if (!readNext(config.mqttBaseTopic) || config.mqttBaseTopic.empty()) {
                throw std::runtime_error("invalid --mqtt-base-topic");
            }
            continue;
        }
        if (arg == "--config") {
            if (!readNext(config.configPath) || config.configPath.empty()) {
                throw std::runtime_error("invalid --config");
            }
            continue;
        }
        if (arg == "--stdout-mqtt") {
            config.stdoutMqtt = true;
            continue;
        }
        if (arg == "--sim-time-scale") {
            std::string value;
            if (!readNext(value) || !tryParseFloat(value, config.simTimeScale)) {
                throw std::runtime_error("invalid --sim-time-scale");
            }
            continue;
        }
        if (arg == "--state-hz") {
            std::string value;
            if (!readNext(value) || !tryParseFloat(value, config.stateFrequencyHz)) {
                throw std::runtime_error("invalid --state-hz");
            }
            continue;
        }
        if (arg == "--vis-hz") {
            std::string value;
            if (!readNext(value) || !tryParseFloat(value, config.visualizationFrequencyHz)) {
                throw std::runtime_error("invalid --vis-hz");
            }
            continue;
        }
        if (arg == "--max-publish-hz") {
            std::string value;
            if (!readNext(value) || !tryParseFloat(value, config.maxPublishHz)) {
                throw std::runtime_error("invalid --max-publish-hz");
            }
            continue;
        }
        if (arg == "--help") {
            std::ostringstream oss;
            oss << "Usage:\n"
                << "  simagv_runtime [options]\n"
                << "Options:\n"
                << "  --tick-ms <u32>\n"
                << "  --trace-capacity <size>\n"
                << "  --mqtt-base-topic <string>\n"
                << "  --config <path>\n"
                << "  --stdout-mqtt\n"
                << "  --sim-time-scale <float>\n"
                << "  --state-hz <float>\n"
                << "  --vis-hz <float>\n"
                << "  --max-publish-hz <float>\n"
                << "Input commands (stdin):\n"
                << "  mqtt <topic> <json>\n"
                << "  http <path> <json>\n"
                << "  quit\n";
            std::cout << oss.str();
            std::exit(0);
        }

        throw std::runtime_error("unknown_arg: " + arg);
    }
    return config;
}

std::string resolveReadableFilePath(const std::string& rawPath)
{
    if (rawPath.empty()) {
        return "";
    }
    const std::filesystem::path inputPath(rawPath);
    auto isReadableFile = [](const std::filesystem::path& p) -> bool {
        std::error_code ec;
        return std::filesystem::exists(p, ec) && std::filesystem::is_regular_file(p, ec);
    };

    if (inputPath.is_absolute() && isReadableFile(inputPath)) {
        return inputPath.string();
    }

    const std::filesystem::path cwd = std::filesystem::current_path();
    const std::vector<std::filesystem::path> candidates{
        inputPath,
        cwd / inputPath,
        cwd.parent_path() / inputPath,
        cwd.parent_path().parent_path() / inputPath,
        cwd.parent_path().parent_path().parent_path() / inputPath,
        cwd.parent_path().parent_path().parent_path().parent_path() / inputPath,
    };

    for (const auto& cand : candidates) {
        if (isReadableFile(cand)) {
            std::error_code ec;
            return std::filesystem::weakly_canonical(cand, ec).string();
        }
    }
    return rawPath;
}

std::string readRemainder(std::istringstream& iss)
{
    std::string out;
    std::getline(iss, out);
    while (!out.empty() && std::isspace(static_cast<unsigned char>(out.front()))) {
        out.erase(out.begin());
    }
    return out;
}

simagv::l2::CommandIntent buildFileSimConfigIntent(const std::string& filePath, simagv::json::Object simConfigObj)
{
    simagv::l2::CommandIntent intent;
    intent.topicType = simagv::l2::TopicType::SimConfig;
    intent.topic = std::string("file:") + filePath;
    intent.payload = simagv::json::Value{std::move(simConfigObj)};
    intent.receiveTimestampMs = simagv::l2::nowMs();
    intent.serial = 0;
    intent.headerId = 0;
    intent.mapId = "";
    return intent;
}

class StdoutMqttDiplomat final : public simagv::l2::IMqttDiplomat {
public:
    void publish(std::string topic, std::string payload, uint8_t qos, bool retain) override
    {
        std::ostringstream oss;
        oss << "mqtt_stdout_pub qos=" << static_cast<int>(qos) << " retain=" << (retain ? "true" : "false") << " topic=" << topic
            << " payload=" << payload;
        simagv::l4::logInfo(oss.str());
    }
};

std::string isoNow()
{
    const uint64_t epochMs = simagv::l2::nowMs();
    const std::time_t sec = static_cast<std::time_t>(epochMs / 1000U);
    const uint32_t ms = static_cast<uint32_t>(epochMs % 1000U);
    std::tm tmUtc;
    ::gmtime_r(&sec, &tmUtc);
    std::ostringstream oss;
    oss << std::put_time(&tmUtc, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << ms << 'Z';
    return oss.str();
}

class StubSimulatorEngine final : public simagv::l2::ISimulatorEngine {
public:
    StubSimulatorEngine(std::string mapId, simagv::l1::SimInstanceConfig config)
        : mapId_(std::move(mapId)), tickCount_(0), config_(std::move(config))
    {
        batteryConfigDefault_ = 100.0F;
        batteryIdleDrainPerMin_ = 1.0F;
        batteryMoveEmptyMultiplier_ = 1.5F;
        batteryMoveLoadedMultiplier_ = 2.5F;
        batteryChargePerMin_ = 10.0F;
        simTimeScale_ = 1.0F;
        batteryChargeLevel_ = batteryConfigDefault_;
        poseX_ = 1.271F;
        poseY_ = -8.2F;
        poseTheta_ = 1.574980402796971F;
        if (config_.initialPose.hasPose) {
            poseX_ = static_cast<float>(config_.initialPose.x);
            poseY_ = static_cast<float>(config_.initialPose.y);
            poseTheta_ = static_cast<float>(config_.initialPose.theta);
        }

        const std::string loadModelPath = resolveReadableFilePath("SimAGV/shelf/BD1.shelf");
        try {
            loadModel_ = simagv::l4::parseLoadModelFile(loadModelPath);
            hasLoadModel_ = true;
        } catch (const std::exception& e) {
            hasLoadModel_ = false;
            std::ostringstream oss;
            oss << "load_model_init_failed path=" << loadModelPath << " reason=" << e.what();
            simagv::l4::logWarn(oss.str());
        }
    }

private:
    static bool isChargingPointNodeId(std::string_view nodeId);
    bool isStoppedAtNodeLocked() const;
    bool isStoppedAtChargingPointLocked() const;
    void refreshChargingStateLocked();

public:
    void applyOrder(const simagv::json::Value& order) override
    {
        const std::string orderText = simagv::l2::toJsonString(order);
        std::lock_guard<std::mutex> lock(mutex_);
        lastOrder_ = orderText;
        if (!order.isObject()) {
            clearCurrentOrderLocked();
            simagv::l4::logInfo("engine_order_cleared reason=payload_not_object");
            return;
        }

        const simagv::json::Object orderObj = order.asObject();
        const std::string newOrderId = simagv::l2::readStringOr(orderObj, "order_id", "orderId", "");
        const uint64_t newOrderUpdateId = simagv::l2::readUintOr(orderObj, "order_update_id", "orderUpdateId", 0U);
        if (newOrderId.empty()) {
            driving_ = false;
            simagv::l4::logWarn("engine_order_reject reason=missing_order_id");
            return;
        }
        if (const auto itNodes = orderObj.find("nodes"); itNodes != orderObj.end() && itNodes->second.isArray() && !itNodes->second.asArray().empty()) {
            const simagv::json::Value& firstNodeValue = itNodes->second.asArray().front();
            if (firstNodeValue.isObject()) {
                const simagv::json::Object& firstNodeObj = firstNodeValue.asObject();
                const auto itPos = firstNodeObj.find("nodePosition");
                if (itPos != firstNodeObj.end() && itPos->second.isObject()) {
                    const simagv::json::Object& posObj = itPos->second.asObject();
                    const std::string rawMapId = simagv::l2::readStringOr(posObj, "map_id", "mapId", "");
                    const std::string normalizedMapId = simagv::l2::canonicalizeMapId(rawMapId);
                    if (!normalizedMapId.empty()) {
                        mapId_ = normalizedMapId;
                    }
                }
            }
        }
        resetOpenLoopLocked();
        if (!currentOrderId_.empty() && newOrderId == currentOrderId_) {
            if (newOrderUpdateId <= currentOrderUpdateId_) {
                throw std::runtime_error("order_update_id_not_increment");
            }
            appendNavigationPlanLocked(orderObj);
            currentOrderUpdateId_ = newOrderUpdateId;
            if (navigationBlocked_ && blockedEdgeIndex_ < plannedEdges_.size() && plannedEdges_[blockedEdgeIndex_].released) {
                navigationBlocked_ = false;
                blockedEdgeIndex_ = std::numeric_limits<size_t>::max();
            }
            std::ostringstream oss;
            oss << "nav_plan_appended orderId=" << currentOrderId_ << " orderUpdateId=" << currentOrderUpdateId_
                << " mode=" << ((navigationMode_ == NavigationMode::Station) ? "station" : "path")
                << " nodes=" << plannedNodes_.size() << " edges=" << plannedEdges_.size()
                << " activeIndex=" << activeItemIndex_ << " blocked=" << (navigationBlocked_ ? "true" : "false");
            simagv::l4::logInfo(oss.str());
            return;
        }

        resetNavigationLocked();
        currentOrderId_ = newOrderId;
        currentOrderUpdateId_ = newOrderUpdateId;
        buildNavigationPlanLocked(orderObj);
        driving_ = hasPendingNavigationLocked();
        std::ostringstream oss;
        oss << "nav_plan_built orderId=" << currentOrderId_ << " orderUpdateId=" << currentOrderUpdateId_
            << " mode=" << ((navigationMode_ == NavigationMode::Station) ? "station" : "path")
            << " nodes=" << plannedNodes_.size() << " edges=" << plannedEdges_.size()
            << " driving=" << (driving_ ? "true" : "false");
        simagv::l4::logInfo(oss.str());
    }

    void applyInstantActions(const simagv::json::Value& instantActions) override
    {
        const std::string instantText = simagv::l2::toJsonString(instantActions);
        std::lock_guard<std::mutex> lock(mutex_);
        lastInstantActions_ = instantText;

        if (!instantActions.isObject()) {
            simagv::l4::logWarn("engine_instant_ignored reason=payload_not_object");
            return;
        }
        const simagv::json::Object obj = instantActions.asObject();
        const auto itActions = obj.find("actions");
        if (itActions == obj.end() || !itActions->second.isArray()) {
            simagv::l4::logWarn("engine_instant_ignored reason=missing_actions");
            return;
        }
        const std::string beforeMapId = mapId_;
        const std::string beforeMode = operatingMode_;
        const bool beforePaused = paused_;
        size_t actionCount = 0U;
        for (const simagv::json::Value& actionValue : itActions->second.asArray()) {
            if (!actionValue.isObject()) {
                continue;
            }
            actionCount += 1U;
            applyActionLocked(actionValue.asObject());
        }
        {
            std::ostringstream oss;
            oss << "engine_instant_applied actions=" << actionCount << " mapId=" << beforeMapId << "->" << mapId_
                << " operatingMode=" << beforeMode << "->" << operatingMode_ << " paused=" << (beforePaused ? "true" : "false") << "->"
                << (paused_ ? "true" : "false");
            simagv::l4::logInfo(oss.str());
        }
    }

    void applyConfig(const simagv::json::Value& simConfig) override
    {
        std::string mapIdValue = mapId_;
        float batteryDefault = batteryConfigDefault_;
        float batteryIdleDrainPerMin = batteryIdleDrainPerMin_;
        float batteryMoveEmptyMultiplier = batteryMoveEmptyMultiplier_;
        float batteryMoveLoadedMultiplier = batteryMoveLoadedMultiplier_;
        float batteryChargePerMin = batteryChargePerMin_;
        bool automaticCharging = automaticCharging_;
        float simTimeScale = simTimeScale_;
        float radarFovDeg = radarFovDeg_;
        float radarRadiusM = radarRadiusM_;
        float safetyScale = safetyScale_;
        bool hasBatteryDefault = false;
        try {
            const simagv::json::Object obj = simagv::l2::asObjectOrThrow(simConfig);
            if (const auto* v = simagv::l2::tryGetSnakeOrCamel(obj, "map_id", "mapId"); v != nullptr && v->isString()) {
                const std::string raw = v->asString();
                const auto itNonSpace = std::find_if(raw.begin(), raw.end(), [](unsigned char ch) { return !std::isspace(ch); });
                if (itNonSpace != raw.end()) {
                    mapIdValue = simagv::l2::canonicalizeMapId(raw);
                }
            }
            if (simagv::l2::tryGetSnakeOrCamel(obj, "battery_default", "batteryDefault") != nullptr) {
                hasBatteryDefault = true;
            }
            batteryDefault = simagv::l2::readFloatOr(obj, "battery_default", "batteryDefault", batteryDefault);
            batteryIdleDrainPerMin = simagv::l2::readFloatOr(obj, "battery_idle_drain_per_min", "batteryIdleDrainPerMin", batteryIdleDrainPerMin);
            batteryMoveEmptyMultiplier = simagv::l2::readFloatOr(obj, "battery_move_empty_multiplier", "batteryMoveEmptyMultiplier", batteryMoveEmptyMultiplier);
            batteryMoveLoadedMultiplier = simagv::l2::readFloatOr(obj, "battery_move_loaded_multiplier", "batteryMoveLoadedMultiplier", batteryMoveLoadedMultiplier);
            batteryChargePerMin = simagv::l2::readFloatOr(obj, "battery_charge_per_min", "batteryChargePerMin", batteryChargePerMin);
            automaticCharging = simagv::l2::readBoolOr(obj, "automatic_charging", "automaticCharging", automaticCharging);
            simTimeScale = simagv::l2::readFloatOr(obj, "sim_time_scale", "simTimeScale", simTimeScale);
            radarFovDeg = simagv::l2::readFloatOr(obj, "radar_fov_deg", "radarFovDeg", radarFovDeg);
            radarRadiusM = simagv::l2::readFloatOr(obj, "radar_radius_m", "radarRadiusM", radarRadiusM);
            safetyScale = simagv::l2::readFloatOr(obj, "safety_scale", "safetyScale", safetyScale);
        } catch (...) {
            mapIdValue = "default";
        }

        std::lock_guard<std::mutex> lock(mutex_);
        mapId_ = std::move(mapIdValue);
        batteryConfigDefault_ = simagv::l2::clampRange(batteryDefault, 0.0001F, 100.0F);
        batteryIdleDrainPerMin_ = simagv::l2::clampRange(batteryIdleDrainPerMin, 0.0F, 100.0F);
        batteryMoveEmptyMultiplier_ = simagv::l2::clampRange(batteryMoveEmptyMultiplier, 0.0F, 100.0F);
        batteryMoveLoadedMultiplier_ = simagv::l2::clampRange(batteryMoveLoadedMultiplier, 0.0F, 100.0F);
        batteryChargePerMin_ = simagv::l2::clampRange(batteryChargePerMin, 0.0F, 100.0F);
        automaticCharging_ = automaticCharging;
        simTimeScale_ = simagv::l2::clampRange(simTimeScale, 0.0001F, 1000.0F);
        radarFovDeg_ = simagv::l2::clampRange(radarFovDeg, 1.0F, 360.0F);
        radarRadiusM_ = simagv::l2::clampRange(radarRadiusM, 0.01F, 10.0F);
        safetyScale_ = simagv::l2::clampRange(safetyScale, 1.0F, 5.0F);
        if (hasBatteryDefault) {
            batteryChargeLevel_ = batteryConfigDefault_;
        }
        refreshChargingStateLocked();
    }

    void applyPerceptionUpdate(const simagv::l2::PerceptionUpdate& update) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        perceptionBlocked_ = update.blocked;
        perceptionCollision_ = update.collision;
        perceptionBlockedBy_ = update.blockedBy;
        perceptionCollidedWith_ = update.collidedWith;
    }

    void updateState(uint32_t tickMs) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tickCount_ += 1;
        lastTickMs_ = tickMs;
        refreshChargingStateLocked();
        updateBatteryLocked(tickMs);
        updateMotionLocked(tickMs);
        updateLiftLocked(tickMs);
        updateActionStateLocked(tickMs);
        errorManager_.updateBattery(batteryChargeLevel_);
        errorManager_.updateMovementBlocked(false);
        if (perceptionBlocked_) {
            std::ostringstream desc;
            desc << "radar blocked by other vehicle serial_number=";
            const size_t n = perceptionBlockedBy_.size();
            const size_t take = std::min(n, static_cast<size_t>(3));
            for (size_t i = 0; i < take; ++i) {
                if (i > 0) {
                    desc << ",";
                }
                desc << perceptionBlockedBy_[i].serialNumber;
            }
            if (n > take) {
                desc << "...";
            }
            errorManager_.setErrorCustom("Navigation", "WARNING", "54330", desc.str());
        } else {
            errorManager_.clearErrorByCode(54330);
        }
        if (perceptionCollision_) {
            std::ostringstream desc;
            desc << "collision detected with other vehicle serial_number=";
            const size_t n = perceptionCollidedWith_.size();
            const size_t take = std::min(n, static_cast<size_t>(3));
            for (size_t i = 0; i < take; ++i) {
                if (i > 0) {
                    desc << ",";
                }
                desc << perceptionCollidedWith_[i].serialNumber;
            }
            if (n > take) {
                desc << "...";
            }
            errorManager_.setErrorCustom("Collision", "FATAL", "54331", desc.str());
        } else {
            errorManager_.clearErrorByCode(54331);
        }
    }

    simagv::l2::Snapshot buildSnapshot(uint64_t nowMs) const override
    {
        simagv::l2::Snapshot snapshot;
        snapshot.timestampMs = nowMs;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot.mapId = mapId_;

            snapshot.state = makeStateObjectLocked();
            snapshot.visualization = makeVisualizationObjectLocked();
            snapshot.connection = makeConnectionObjectLocked();
            snapshot.factsheet = makeFactsheetObjectLocked();
        }
        return snapshot;
    }

private:
    enum class NavigationMode : uint8_t { Station, Path };
    enum class OpenLoopMode : uint8_t { None, Velocity, Translate, Turn };
    enum class ActivePathKind : uint8_t { None, StationNode, PathApproachStartNode, PathTraverseEdge };

    struct PlannedNode final {
        std::string nodeId;
        uint32_t sequenceId;
        bool released;
        float x;
        float y;
        float theta;
        float allowedDeviationXY;
        float allowedDeviationTheta;
        std::vector<simagv::json::Object> actions;
    };

    struct PlannedEdge final {
        std::string edgeId;
        uint32_t sequenceId;
        bool released;
        std::string startNodeId;
        std::string endNodeId;
        size_t startNodeIndex;
        size_t endNodeIndex;
        bool rotationAllowed;
        std::optional<float> maxSpeed;
        std::optional<float> maxRotationSpeed;
        std::optional<float> orientation;
        std::string direction;
        std::optional<simagv::l4::Trajectory> trajectory;
        std::vector<simagv::json::Object> actions;
    };

    mutable std::mutex mutex_;
    std::string mapId_;
    std::string operatingMode_{"AUTOMATIC"};
    uint64_t tickCount_;
    uint32_t lastTickMs_{0};
    std::string lastOrder_;
    std::string lastInstantActions_;
    std::string currentOrderId_;      // 当前订单号
    uint64_t currentOrderUpdateId_{0}; // 当前订单更新号
    simagv::l1::SimInstanceConfig config_;
    float batteryConfigDefault_{100.0F};
    float batteryIdleDrainPerMin_{1.0F};
    float batteryMoveEmptyMultiplier_{1.5F};
    float batteryMoveLoadedMultiplier_{2.5F};
    float batteryChargePerMin_{10.0F};
    float simTimeScale_{1.0F};
    float batteryChargeLevel_{100.0F};
    simagv::l4::SimErrorManager errorManager_;
    float poseX_{0.0F};
    float poseY_{0.0F};
    float poseTheta_{0.0F};
    float velVx_{0.0F};
    float velVy_{0.0F};
    float velOmega_{0.0F};
    bool batteryCharging_{false};
    bool chargingCommanded_{false};
    bool automaticCharging_{true};
    bool driving_{false};
    bool hasLoad_{false};
    bool paused_{false};
    float radarFovDeg_{60.0F};
    float radarRadiusM_{0.5F};
    float safetyScale_{1.1F};
    bool perceptionBlocked_{false};
    bool perceptionCollision_{false};
    std::vector<simagv::l2::PerceptionContact> perceptionBlockedBy_;
    std::vector<simagv::l2::PerceptionContact> perceptionCollidedWith_;
    float loadTheta_{0.0F};
    simagv::l4::LoadModel loadModel_{};
    bool hasLoadModel_{false};
    std::unordered_map<std::string, simagv::l4::LoadModel> preloadedLoadModels_;
    float forkHeightM_{0.0F};
    float liftStartHeightM_{0.0F};
    float liftTargetHeightM_{0.0F};
    uint32_t liftElapsedMs_{0U};
    uint32_t liftTotalMs_{0U};
    std::string activeActionId_;
    std::string activeActionStatus_;
    std::string activeActionType_;
    std::string activeActionDescription_;
    std::string activeResultDescription_;
    uint32_t activeActionElapsedMs_{0U};
    uint32_t activeActionTotalMs_{0U};
    bool hasActiveAction_{false};
    bool waitingForHardAction_{false};
    NavigationMode navigationMode_{NavigationMode::Station};
    OpenLoopMode openLoopMode_{OpenLoopMode::None};
    float openLoopVxRobot_{0.0F};
    float openLoopVyRobot_{0.0F};
    float openLoopOmega_{0.0F};
    uint32_t openLoopRemainingMs_{0U};
    float openLoopTranslateRemainingM_{0.0F};
    float openLoopTranslateDirX_{1.0F};
    float openLoopTranslateDirY_{0.0F};
    float openLoopTranslateSpeed_{0.0F};
    float openLoopTurnRemainingRad_{0.0F};
    float openLoopTurnOmega_{0.0F};
    std::vector<PlannedNode> plannedNodes_;
    std::vector<PlannedEdge> plannedEdges_;
    size_t activeItemIndex_{0U};
    std::vector<simagv::l4::PosePoint> activePath_;
    size_t activePathIndex_{0U};
    ActivePathKind activePathKind_{ActivePathKind::None};
    size_t activeApproachNodeIndex_{std::numeric_limits<size_t>::max()};
    float activeMaxSpeed_{0.0F};
    float activeMaxRotationSpeed_{1.0F};
    bool activeRotationAllowed_{true};
    bool navigationBlocked_{false};
    size_t blockedEdgeIndex_{std::numeric_limits<size_t>::max()};
    size_t lastAppliedEdgeActionsIndex_{std::numeric_limits<size_t>::max()};
    size_t lastAppliedNodeActionsIndex_{std::numeric_limits<size_t>::max()};
    std::string lastNodeId_;
    uint32_t lastNodeSequenceId_{0U};
    static constexpr float kStationNearDistanceM = 0.1F;

    static std::string normalizeOperatingMode(std::string_view operatingMode)
    {
        std::string upper;
        upper.reserve(operatingMode.size());
        for (char ch : operatingMode) {
            upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
        }
        if (upper == "AUTOMATIC") {
            return "AUTOMATIC";
        }
        if (upper == "SERVICE" || upper == "MANUAL") {
            return "SERVICE";
        }
        return "";
    }

    static std::string readActionParamString(const simagv::json::Object& actionObj, std::string_view expectedKey)
    {
        const auto* p_ParamsValue = simagv::l2::tryGetSnakeOrCamel(actionObj, "action_parameters", "actionParameters");
        if (p_ParamsValue == nullptr || !p_ParamsValue->isArray()) {
            return "";
        }
        for (const simagv::json::Value& paramValue : p_ParamsValue->asArray()) {
            if (!paramValue.isObject()) {
                continue;
            }
            const simagv::json::Object& paramObj = paramValue.asObject();
            const std::string key = simagv::l2::readStringOr(paramObj, "key", "key", "");
            if (key != expectedKey) {
                continue;
            }
            const auto* p_Value = simagv::l2::tryGetSnakeOrCamel(paramObj, "value", "value");
            if (p_Value == nullptr || !p_Value->isString()) {
                return "";
            }
            return p_Value->asString();
        }
        return "";
    }

    static std::optional<double> readActionParamNumberAnyKey(const simagv::json::Object& actionObj, const std::vector<std::string>& keys)
    {
        const auto* p_ParamsValue = simagv::l2::tryGetSnakeOrCamel(actionObj, "action_parameters", "actionParameters");
        if (p_ParamsValue == nullptr || !p_ParamsValue->isArray()) {
            return std::nullopt;
        }
        for (const simagv::json::Value& paramValue : p_ParamsValue->asArray()) {
            if (!paramValue.isObject()) {
                continue;
            }
            const simagv::json::Object& paramObj = paramValue.asObject();
            const std::string key = simagv::l2::readStringOr(paramObj, "key", "key", "");
            if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
                continue;
            }
            const auto* p_Value = simagv::l2::tryGetSnakeOrCamel(paramObj, "value", "value");
            if (p_Value == nullptr || !p_Value->isNumber()) {
                return std::nullopt;
            }
            return p_Value->asNumber();
        }
        return std::nullopt;
    }

    static std::string readActionParamStringAnyKey(const simagv::json::Object& actionObj, const std::vector<std::string>& keys)
    {
        const auto* p_ParamsValue = simagv::l2::tryGetSnakeOrCamel(actionObj, "action_parameters", "actionParameters");
        if (p_ParamsValue == nullptr || !p_ParamsValue->isArray()) {
            return "";
        }
        for (const simagv::json::Value& paramValue : p_ParamsValue->asArray()) {
            if (!paramValue.isObject()) {
                continue;
            }
            const simagv::json::Object& paramObj = paramValue.asObject();
            const std::string key = simagv::l2::readStringOr(paramObj, "key", "key", "");
            if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
                continue;
            }
            const auto* p_Value = simagv::l2::tryGetSnakeOrCamel(paramObj, "value", "value");
            if (p_Value == nullptr || !p_Value->isString()) {
                return "";
            }
            return p_Value->asString();
        }
        return "";
    }

    static std::string readActionIdAnyKey(const simagv::json::Object& actionObj)
    {
        const std::string actionId = simagv::l2::readStringOr(actionObj, "action_id", "actionId", "");
        if (!actionId.empty()) {
            return actionId;
        }
        std::ostringstream oss;
        oss << "act-" << simagv::l2::nowMs();
        return oss.str();
    }

    static bool readActionBlockingHardAnyKey(const simagv::json::Object& actionObj)
    {
        const std::string blockingType = simagv::l2::readStringOr(actionObj, "blocking_type", "blockingType", "HARD");
        std::string upper;
        upper.reserve(blockingType.size());
        for (char ch : blockingType) {
            upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
        }
        return upper != "SOFT";
    }

    static std::string resolveLoadModelPathAny(const std::string& recFile)
    {
        if (recFile.empty()) {
            return "";
        }
        const std::filesystem::path inputPath{recFile};
        if (std::filesystem::exists(inputPath)) {
            return std::filesystem::absolute(inputPath).string();
        }
        const std::filesystem::path cwd = std::filesystem::current_path();
        const std::filesystem::path direct = cwd / inputPath;
        if (std::filesystem::exists(direct)) {
            return std::filesystem::absolute(direct).string();
        }
        const std::filesystem::path underSimAgv = cwd / "SimAGV" / inputPath;
        if (std::filesystem::exists(underSimAgv)) {
            return std::filesystem::absolute(underSimAgv).string();
        }
        return "";
    }

    void preloadNodeActionsLocked()
    {
        for (const PlannedNode& node : plannedNodes_) {
            for (const simagv::json::Object& actionObj : node.actions) {
                const std::string actionType = simagv::l2::readStringOr(actionObj, "action_type", "actionType", "");
                if (actionType != "JackLoad" && actionType != "pick") {
                    continue;
                }
                const std::string recFile = readActionParamStringAnyKey(actionObj, {"recfile", "recFile"});
                if (recFile.empty()) {
                    continue;
                }
                const std::string modelPath = resolveLoadModelPathAny(recFile);
                if (modelPath.empty()) {
                    continue;
                }
                if (preloadedLoadModels_.find(modelPath) != preloadedLoadModels_.end()) {
                    continue;
                }
                try {
                    preloadedLoadModels_.emplace(modelPath, simagv::l4::parseLoadModelFile(modelPath));
                } catch (...) {
                }
            }
        }
    }

    void loadModelFromRecfilePreloadedFirstLocked(const simagv::json::Object& actionObj)
    {
        const std::string recFile = readActionParamStringAnyKey(actionObj, {"recfile", "recFile"});
        if (!recFile.empty()) {
            const std::string modelPath = resolveLoadModelPathAny(recFile);
            if (!modelPath.empty()) {
                const auto it = preloadedLoadModels_.find(modelPath);
                if (it != preloadedLoadModels_.end()) {
                    loadModel_ = it->second;
                    hasLoadModel_ = true;
                    return;
                }
            }
        }
        loadModelFromRecfileLocked(actionObj);
    }

    void loadModelFromRecfileLocked(const simagv::json::Object& actionObj)
    {
        const std::string recFile = readActionParamStringAnyKey(actionObj, {"recfile", "recFile"});
        if (recFile.empty()) {
            hasLoadModel_ = false;
            return;
        }
        const std::string modelPath = resolveLoadModelPathAny(recFile);
        if (modelPath.empty()) {
            hasLoadModel_ = false;
            return;
        }
        try {
            loadModel_ = simagv::l4::parseLoadModelFile(modelPath);
            hasLoadModel_ = true;
        } catch (...) {
            hasLoadModel_ = false;
        }
    }

    void startActionStateLocked(const simagv::json::Object& actionObj, const std::string& actionType, uint32_t durationMs)
    {
        activeActionId_ = readActionIdAnyKey(actionObj);
        activeActionStatus_ = "RUNNING";
        activeActionType_ = actionType;
        const std::string receivedAt = isoNow();
        const std::string inputDesc = simagv::l2::readStringOr(actionObj, "action_description", "actionDescription", "");
        if (!inputDesc.empty()) {
            activeActionDescription_ = std::string("收到时间: ") + receivedAt + ", 动作: " + actionType + ", 描述: " + inputDesc;
        } else {
            activeActionDescription_ = std::string("收到时间: ") + receivedAt + ", 动作: " + actionType;
        }
        activeResultDescription_ = "待执行";
        activeActionElapsedMs_ = 0U;
        activeActionTotalMs_ = std::max<uint32_t>(durationMs, 200U);
        hasActiveAction_ = true;
    }

    void updateActionStateLocked(uint32_t tickMs)
    {
        if (!hasActiveAction_ || activeActionStatus_ != "RUNNING") {
            return;
        }
        if (activeActionElapsedMs_ >= activeActionTotalMs_) {
            activeActionStatus_ = "FINISHED";
            activeResultDescription_ = "执行完成";
            return;
        }
        const uint32_t stepMs = std::min(tickMs, activeActionTotalMs_ - activeActionElapsedMs_);
        activeActionElapsedMs_ += stepMs;
        if (activeActionElapsedMs_ >= activeActionTotalMs_) {
            activeActionStatus_ = "FINISHED";
            activeResultDescription_ = "执行完成";
            return;
        }
        if (activeActionElapsedMs_ > 0U) {
            activeResultDescription_ = "执行中";
        }
    }

    void startLiftLocked(float targetHeightM, float speedMps)
    {
        const float speed = std::max(0.001F, speedMps);
        const float delta = std::abs(targetHeightM - forkHeightM_);
        const float timeSec = delta / speed;
        const uint32_t execMs = static_cast<uint32_t>(std::min(600000.0F, timeSec * 1000.0F));
        liftStartHeightM_ = forkHeightM_;
        liftTargetHeightM_ = std::max(0.0F, targetHeightM);
        liftElapsedMs_ = 0U;
        liftTotalMs_ = std::max<uint32_t>(execMs, 1U);
    }

    void updateLiftLocked(uint32_t tickMs)
    {
        if (liftTotalMs_ == 0U) {
            return;
        }
        if (liftElapsedMs_ >= liftTotalMs_) {
            forkHeightM_ = liftTargetHeightM_;
            liftTotalMs_ = 0U;
            liftElapsedMs_ = 0U;
            return;
        }
        const uint32_t stepMs = std::min(tickMs, liftTotalMs_ - liftElapsedMs_);
        liftElapsedMs_ += stepMs;
        const float ratio = std::min(1.0F, static_cast<float>(liftElapsedMs_) / static_cast<float>(liftTotalMs_));
        forkHeightM_ = liftStartHeightM_ + (liftTargetHeightM_ - liftStartHeightM_) * ratio;
        if (liftElapsedMs_ >= liftTotalMs_) {
            forkHeightM_ = liftTargetHeightM_;
            liftTotalMs_ = 0U;
            liftElapsedMs_ = 0U;
        }
    }

    static float normalizeAngle(float angleRad)
    {
        constexpr float kPi = 3.14159265358979323846f;
        float t = angleRad;
        while (t <= -kPi) {
            t += 2.0f * kPi;
        }
        while (t > kPi) {
            t -= 2.0f * kPi;
        }
        return t;
    }

    static float hypot2(float dx, float dy) { return std::sqrt(dx * dx + dy * dy); }

    static float clampAbs(float value, float maxAbs)
    {
        if (value > maxAbs) {
            return maxAbs;
        }
        if (value < -maxAbs) {
            return -maxAbs;
        }
        return value;
    }

    void resetNavigationLocked()
    {
        plannedNodes_.clear();
        plannedEdges_.clear();
        preloadedLoadModels_.clear();
        activeItemIndex_ = 0U;
        activePath_.clear();
        activePathIndex_ = 0U;
        activePathKind_ = ActivePathKind::None;
        activeApproachNodeIndex_ = std::numeric_limits<size_t>::max();
        activeMaxSpeed_ = 0.0F;
        activeMaxRotationSpeed_ = 1.0F;
        activeRotationAllowed_ = true;
        navigationBlocked_ = false;
        blockedEdgeIndex_ = std::numeric_limits<size_t>::max();
        lastAppliedEdgeActionsIndex_ = std::numeric_limits<size_t>::max();
        lastAppliedNodeActionsIndex_ = std::numeric_limits<size_t>::max();
        waitingForHardAction_ = false;
        velVx_ = 0.0F;
        velVy_ = 0.0F;
        velOmega_ = 0.0F;
        lastNodeId_.clear();
        lastNodeSequenceId_ = 0U;
    }

    void resetOpenLoopLocked()
    {
        openLoopMode_ = OpenLoopMode::None;
        openLoopVxRobot_ = 0.0F;
        openLoopVyRobot_ = 0.0F;
        openLoopOmega_ = 0.0F;
        openLoopRemainingMs_ = 0U;
        openLoopTranslateRemainingM_ = 0.0F;
        openLoopTranslateDirX_ = 1.0F;
        openLoopTranslateDirY_ = 0.0F;
        openLoopTranslateSpeed_ = 0.0F;
        openLoopTurnRemainingRad_ = 0.0F;
        openLoopTurnOmega_ = 0.0F;
    }

    void clearCurrentOrderLocked()
    {
        currentOrderId_.clear();
        currentOrderUpdateId_ = 0U;
        driving_ = false;
        resetNavigationLocked();
        resetOpenLoopLocked();
    }

    void startManualGotoLocked(float x, float y, float theta)
    {
        clearCurrentOrderLocked();
        PlannedNode n{};
        n.nodeId = "initPosition";
        n.sequenceId = 0U;
        n.released = true;
        n.x = x;
        n.y = y;
        n.theta = theta;
        n.allowedDeviationXY = 0.01F;
        n.allowedDeviationTheta = 0.01F;
        plannedNodes_.push_back(std::move(n));
        navigationMode_ = NavigationMode::Station;
        driving_ = true;
    }

    static std::vector<simagv::json::Object> readActionObjects(const simagv::json::Object& hostObj)
    {
        std::vector<simagv::json::Object> out;
        const auto itActions = hostObj.find("actions");
        if (itActions == hostObj.end() || !itActions->second.isArray()) {
            return out;
        }
        for (const simagv::json::Value& actionValue : itActions->second.asArray()) {
            if (!actionValue.isObject()) {
                continue;
            }
            out.push_back(actionValue.asObject());
        }
        return out;
    }

    void applyActionLocked(const simagv::json::Object& actionObj)
    {
        const std::string actionType = simagv::l2::readStringOr(actionObj, "action_type", "actionType", "");
        if (actionType.empty()) {
            return;
        }
        if (actionType == "JackLoad") {
            constexpr float kJackSpeedMps = 0.025F;
            constexpr float kJackMaxHeightM = 0.1F;
            hasLoad_ = true;
            loadModelFromRecfilePreloadedFirstLocked(actionObj);
            startLiftLocked(kJackMaxHeightM, kJackSpeedMps);
            startActionStateLocked(actionObj, actionType, liftTotalMs_);
            if (readActionBlockingHardAnyKey(actionObj)) {
                waitingForHardAction_ = true;
            }
            return;
        }
        if (actionType == "JackUnload") {
            constexpr float kJackSpeedMps = 0.025F;
            constexpr float kJackMinHeightM = 0.0F;
            hasLoad_ = false;
            hasLoadModel_ = false;
            loadModel_ = simagv::l4::LoadModel{};
            startLiftLocked(kJackMinHeightM, kJackSpeedMps);
            startActionStateLocked(actionObj, actionType, liftTotalMs_);
            if (readActionBlockingHardAnyKey(actionObj)) {
                waitingForHardAction_ = true;
            }
            return;
        }
        if (actionType.rfind("LIFT_", 0) == 0) {
            const float targetHeightMm = static_cast<float>(readActionParamNumberAnyKey(actionObj, {"targetHeightMm", "target_height_mm"}).value_or(0.0));
            const float speedMmS = static_cast<float>(readActionParamNumberAnyKey(actionObj, {"actionSpeedMmS", "action_speed_mm_s"}).value_or(50.0));
            const float targetHeightM = (actionType == "LIFT_HOME" || actionType == "LIFT_STOP") ? 0.0F : (targetHeightMm / 1000.0F);
            const float speedMps = std::max(0.001F, speedMmS / 1000.0F);
            startLiftLocked(targetHeightM, speedMps);
            startActionStateLocked(actionObj, actionType, liftTotalMs_);
            if (readActionBlockingHardAnyKey(actionObj)) {
                waitingForHardAction_ = true;
            }
            return;
        }
        if (actionType == "stopPause") {
            startActionStateLocked(actionObj, actionType, 200U);
            paused_ = true;
            if (readActionBlockingHardAnyKey(actionObj)) {
                waitingForHardAction_ = true;
            }
            return;
        }
        if (actionType == "startPause") {
            startActionStateLocked(actionObj, actionType, 200U);
            paused_ = false;
            if (readActionBlockingHardAnyKey(actionObj)) {
                waitingForHardAction_ = true;
            }
            return;
        }
        if (actionType == "cancelOrder") {
            startActionStateLocked(actionObj, actionType, 200U);
            clearCurrentOrderLocked();
            if (readActionBlockingHardAnyKey(actionObj)) {
                waitingForHardAction_ = true;
            }
            return;
        }
        if (actionType == "startCharging") {
            startActionStateLocked(actionObj, actionType, 500U);
            chargingCommanded_ = true;
            refreshChargingStateLocked();
            if (readActionBlockingHardAnyKey(actionObj)) {
                waitingForHardAction_ = true;
            }
            return;
        }
        if (actionType == "stopCharging") {
            startActionStateLocked(actionObj, actionType, 500U);
            chargingCommanded_ = false;
            refreshChargingStateLocked();
            if (readActionBlockingHardAnyKey(actionObj)) {
                waitingForHardAction_ = true;
            }
            return;
        }
        if (actionType == "pick") {
            startActionStateLocked(actionObj, actionType, 1000U);
            hasLoad_ = true;
            loadModelFromRecfilePreloadedFirstLocked(actionObj);
            if (readActionBlockingHardAnyKey(actionObj)) {
                waitingForHardAction_ = true;
            }
            return;
        }
        if (actionType == "drop") {
            startActionStateLocked(actionObj, actionType, 1000U);
            hasLoad_ = false;
            hasLoadModel_ = false;
            loadModel_ = simagv::l4::LoadModel{};
            if (readActionBlockingHardAnyKey(actionObj)) {
                waitingForHardAction_ = true;
            }
            return;
        }
        if (actionType == "rotateLoad") {
            startActionStateLocked(actionObj, actionType, 500U);
            const auto angleOpt = readActionParamNumberAnyKey(actionObj, {"angle"});
            if (angleOpt.has_value()) {
                loadTheta_ = normalizeAngle(loadTheta_ + static_cast<float>(angleOpt.value()));
            }
            if (readActionBlockingHardAnyKey(actionObj)) {
                waitingForHardAction_ = true;
            }
            return;
        }
        if (actionType == "rotateAgv" || actionType == "rotateAgy") {
            startActionStateLocked(actionObj, actionType, 500U);
            const auto angleOpt = readActionParamNumberAnyKey(actionObj, {"angle"});
            if (angleOpt.has_value()) {
                poseTheta_ = normalizeAngle(poseTheta_ + static_cast<float>(angleOpt.value()));
            }
            if (readActionBlockingHardAnyKey(actionObj)) {
                waitingForHardAction_ = true;
            }
            return;
        }
        if (actionType == "switchMap") {
            startActionStateLocked(actionObj, actionType, 500U);
            const std::string mapIdRaw = readActionParamStringAnyKey(actionObj, {"map", "mapId", "map_id"});
            if (!mapIdRaw.empty()) {
                const std::string newMapId = simagv::l2::canonicalizeMapId(mapIdRaw);
                if (!newMapId.empty() && newMapId != mapId_) {
                    mapId_ = newMapId;
                    clearCurrentOrderLocked();
                }
            }
            const auto xOpt = readActionParamNumberAnyKey(actionObj, {"center_x", "centerX", "x"});
            const auto yOpt = readActionParamNumberAnyKey(actionObj, {"center_y", "centerY", "y"});
            const auto thetaOpt = readActionParamNumberAnyKey(actionObj, {"initiate_angle", "initiateAngle", "theta"});
            if (xOpt.has_value() && yOpt.has_value()) {
                poseX_ = static_cast<float>(xOpt.value());
                poseY_ = static_cast<float>(yOpt.value());
                if (thetaOpt.has_value()) {
                    poseTheta_ = normalizeAngle(static_cast<float>(thetaOpt.value()));
                }
            }
            if (readActionBlockingHardAnyKey(actionObj)) {
                waitingForHardAction_ = true;
            }
            return;
        }
        if (actionType == "changeControl") {
            startActionStateLocked(actionObj, actionType, 200U);
            const std::string operatingModeRaw = readActionParamStringAnyKey(actionObj, {"control"});
            if (!operatingModeRaw.empty()) {
                const std::string normalizedMode = normalizeOperatingMode(operatingModeRaw);
                if (!normalizedMode.empty()) {
                    operatingMode_ = normalizedMode;
                }
            }
            if (readActionBlockingHardAnyKey(actionObj)) {
                waitingForHardAction_ = true;
            }
            return;
        }
        if (actionType == "initPosition") {
            startActionStateLocked(actionObj, actionType, 500U);
            const auto xOpt = readActionParamNumberAnyKey(actionObj, {"x"});
            const auto yOpt = readActionParamNumberAnyKey(actionObj, {"y"});
            const auto thetaOpt = readActionParamNumberAnyKey(actionObj, {"theta"});
            if (xOpt.has_value() && yOpt.has_value()) {
                const std::string mapIdRaw = readActionParamStringAnyKey(actionObj, {"map", "mapId", "map_id"});
                if (!mapIdRaw.empty()) {
                    const std::string newMapId = simagv::l2::canonicalizeMapId(mapIdRaw);
                    if (!newMapId.empty()) {
                        mapId_ = newMapId;
                    }
                }
                clearCurrentOrderLocked();
                poseX_ = static_cast<float>(xOpt.value());
                poseY_ = static_cast<float>(yOpt.value());
                if (thetaOpt.has_value()) {
                    poseTheta_ = normalizeAngle(static_cast<float>(thetaOpt.value()));
                }
                const std::string lastNodeId = readActionParamStringAnyKey(actionObj, {"lastNodeId", "last_node_id", "last_nodeId"});
                if (!lastNodeId.empty()) {
                    lastNodeId_ = lastNodeId;
                }
            }
            if (readActionBlockingHardAnyKey(actionObj)) {
                waitingForHardAction_ = true;
            }
            return;
        }
        if (actionType == "motion") {
            const float vx = static_cast<float>(readActionParamNumberAnyKey(actionObj, {"vx", "vX"}).value_or(0.0));
            const float vy = static_cast<float>(readActionParamNumberAnyKey(actionObj, {"vy", "vY"}).value_or(0.0));
            const float w = static_cast<float>(readActionParamNumberAnyKey(actionObj, {"w", "omega"}).value_or(0.0));
            const uint32_t duration = static_cast<uint32_t>(std::max(0.0, readActionParamNumberAnyKey(actionObj, {"duration"}).value_or(0.0)));
            startActionStateLocked(actionObj, actionType, duration);
            clearCurrentOrderLocked();
            openLoopMode_ = OpenLoopMode::Velocity;
            openLoopVxRobot_ = vx;
            openLoopVyRobot_ = vy;
            openLoopOmega_ = w;
            openLoopRemainingMs_ = duration;
            if (readActionBlockingHardAnyKey(actionObj)) {
                waitingForHardAction_ = true;
            }
            return;
        }
        if (actionType == "translate") {
            const float dist = static_cast<float>(std::abs(readActionParamNumberAnyKey(actionObj, {"dist"}).value_or(0.0)));
            const float vx = static_cast<float>(readActionParamNumberAnyKey(actionObj, {"vx", "vX"}).value_or(0.1));
            const float vy = static_cast<float>(readActionParamNumberAnyKey(actionObj, {"vy", "vY"}).value_or(0.0));
            const float speed = std::max(1e-4F, hypot2(vx, vy));
            const float dirRobotX = vx / speed;
            const float dirRobotY = vy / speed;
            const float dirWorldX = std::cos(poseTheta_) * dirRobotX - std::sin(poseTheta_) * dirRobotY;
            const float dirWorldY = std::sin(poseTheta_) * dirRobotX + std::cos(poseTheta_) * dirRobotY;
            clearCurrentOrderLocked();
            openLoopMode_ = OpenLoopMode::Translate;
            openLoopTranslateRemainingM_ = dist;
            openLoopTranslateDirX_ = dirWorldX;
            openLoopTranslateDirY_ = dirWorldY;
            openLoopTranslateSpeed_ = speed;
            const uint32_t duration = static_cast<uint32_t>(std::max(1.0F, (dist / speed) * 1000.0F));
            startActionStateLocked(actionObj, actionType, duration);
            if (readActionBlockingHardAnyKey(actionObj)) {
                waitingForHardAction_ = true;
            }
            return;
        }
        if (actionType == "turn") {
            const float angle = static_cast<float>(std::abs(readActionParamNumberAnyKey(actionObj, {"angle"}).value_or(0.0)));
            const float vw = static_cast<float>(readActionParamNumberAnyKey(actionObj, {"vw", "w"}).value_or(0.5));
            if (angle <= 0.0F) {
                return;
            }
            clearCurrentOrderLocked();
            openLoopMode_ = OpenLoopMode::Turn;
            openLoopTurnRemainingRad_ = angle;
            openLoopTurnOmega_ = (std::abs(vw) <= 1e-6F) ? 0.5F : vw;
            const float omega = std::max(0.001F, std::abs(openLoopTurnOmega_));
            const uint32_t duration = static_cast<uint32_t>(std::max(1.0F, (angle / omega) * 1000.0F));
            startActionStateLocked(actionObj, actionType, duration);
            if (readActionBlockingHardAnyKey(actionObj)) {
                waitingForHardAction_ = true;
            }
            return;
        }
        if (actionType == "clearErrors") {
            startActionStateLocked(actionObj, actionType, 200U);
            if (readActionBlockingHardAnyKey(actionObj)) {
                waitingForHardAction_ = true;
            }
            return;
        }
    }

    bool hasPendingNavigationLocked() const
    {
        if (navigationMode_ == NavigationMode::Station) {
            return !plannedNodes_.empty();
        }
        return !plannedEdges_.empty();
    }

    std::optional<size_t> findNextPlannedNodeIndexLocked(const std::string& nodeId, size_t fromIndex) const
    {
        for (size_t i = fromIndex; i < plannedNodes_.size(); ++i) {
            if (plannedNodes_[i].nodeId == nodeId) {
                return i;
            }
        }
        return std::nullopt;
    }

    void resolvePlannedEdgeNodeIndicesLocked()
    {
        size_t cursorIndex = 0U;
        for (auto& edge : plannedEdges_) {
            edge.startNodeIndex = std::numeric_limits<size_t>::max();
            edge.endNodeIndex = std::numeric_limits<size_t>::max();
            auto startIndexOpt = findNextPlannedNodeIndexLocked(edge.startNodeId, cursorIndex);
            if (!startIndexOpt.has_value() && cursorIndex > 0U) {
                startIndexOpt = findNextPlannedNodeIndexLocked(edge.startNodeId, 0U);
            }
            if (!startIndexOpt.has_value()) {
                continue;
            }
            auto endIndexOpt = findNextPlannedNodeIndexLocked(edge.endNodeId, startIndexOpt.value() + 1U);
            if (!endIndexOpt.has_value()) {
                continue;
            }
            edge.startNodeIndex = startIndexOpt.value();
            edge.endNodeIndex = endIndexOpt.value();
            cursorIndex = edge.endNodeIndex;
        }
    }

    static bool readReleasedDefaultTrue(const simagv::json::Object& obj)
    {
        const auto* releasedValue = simagv::l2::tryGetSnakeOrCamel(obj, "released", "released");
        return (releasedValue != nullptr && releasedValue->isBool()) ? releasedValue->asBool() : true;
    }

    std::vector<PlannedNode> parsePlannedNodesFromOrderLocked(const simagv::json::Object& orderObj) const
    {
        std::vector<PlannedNode> out;
        const auto itNodes = orderObj.find("nodes");
        if (itNodes == orderObj.end() || !itNodes->second.isArray()) {
            return out;
        }
        const simagv::json::Array& nodes = itNodes->second.asArray();
        out.reserve(nodes.size());
        for (const simagv::json::Value& nodeValue : nodes) {
            if (!nodeValue.isObject()) {
                continue;
            }
            const simagv::json::Object& nodeObj = nodeValue.asObject();
            const auto itPos = nodeObj.find("nodePosition");
            if (itPos == nodeObj.end() || !itPos->second.isObject()) {
                continue;
            }
            const simagv::json::Object& posObj = itPos->second.asObject();

            PlannedNode n{};
            n.nodeId = simagv::l2::readStringOr(nodeObj, "node_id", "nodeId", "");
            n.sequenceId = simagv::l2::readUintOr(nodeObj, "sequence_id", "sequenceId", 0U);
            n.released = readReleasedDefaultTrue(nodeObj);
            n.x = simagv::l2::readFloatOr(posObj, "x", "x", 0.0F);
            n.y = simagv::l2::readFloatOr(posObj, "y", "y", 0.0F);
            n.theta = simagv::l2::readFloatOr(posObj, "theta", "theta", 0.0F);
            n.allowedDeviationXY = simagv::l2::readFloatOr(posObj, "allowed_deviation_xy", "allowedDeviationXY", 0.5F);
            n.allowedDeviationTheta = simagv::l2::readFloatOr(posObj, "allowed_deviation_theta", "allowedDeviationTheta", 0.5F);
            n.actions = readActionObjects(nodeObj);
            if (!n.nodeId.empty()) {
                out.push_back(std::move(n));
            }
        }
        std::sort(out.begin(), out.end(), [](const PlannedNode& a, const PlannedNode& b) { return a.sequenceId < b.sequenceId; });
        return out;
    }

    std::vector<PlannedEdge> parsePlannedEdgesFromOrderLocked(const simagv::json::Object& orderObj) const
    {
        std::vector<PlannedEdge> out;
        const auto itEdges = orderObj.find("edges");
        if (itEdges == orderObj.end() || !itEdges->second.isArray()) {
            return out;
        }
        const simagv::json::Array& edges = itEdges->second.asArray();
        out.reserve(edges.size());
        for (const simagv::json::Value& edgeValue : edges) {
            if (!edgeValue.isObject()) {
                continue;
            }
            const simagv::json::Object& edgeObj = edgeValue.asObject();
            PlannedEdge e{};
            e.edgeId = simagv::l2::readStringOr(edgeObj, "edge_id", "edgeId", "");
            e.sequenceId = simagv::l2::readUintOr(edgeObj, "sequence_id", "sequenceId", 0U);
            e.released = readReleasedDefaultTrue(edgeObj);
            e.startNodeId = simagv::l2::readStringOr(edgeObj, "start_node_id", "startNodeId", "");
            e.endNodeId = simagv::l2::readStringOr(edgeObj, "end_node_id", "endNodeId", "");
            e.startNodeIndex = std::numeric_limits<size_t>::max();
            e.endNodeIndex = std::numeric_limits<size_t>::max();
            const auto* rot = simagv::l2::tryGetSnakeOrCamel(edgeObj, "rotation_allowed", "rotationAllowed");
            e.rotationAllowed = (rot != nullptr && rot->isBool()) ? rot->asBool() : true;

            const auto* maxSpeedValue = simagv::l2::tryGetSnakeOrCamel(edgeObj, "max_speed", "maxSpeed");
            if (maxSpeedValue != nullptr && maxSpeedValue->isNumber()) {
                e.maxSpeed = static_cast<float>(maxSpeedValue->asNumber());
            }
            const auto* maxRotValue = simagv::l2::tryGetSnakeOrCamel(edgeObj, "max_rotation_speed", "maxRotationSpeed");
            if (maxRotValue != nullptr && maxRotValue->isNumber()) {
                e.maxRotationSpeed = static_cast<float>(maxRotValue->asNumber());
            }
            const auto* oriValue = simagv::l2::tryGetSnakeOrCamel(edgeObj, "orientation", "orientation");
            if (oriValue != nullptr && oriValue->isNumber()) {
                e.orientation = static_cast<float>(oriValue->asNumber());
            }
            e.direction = simagv::l2::readStringOr(edgeObj, "direction", "direction", "");
            e.actions = readActionObjects(edgeObj);

            const auto itTraj = edgeObj.find("trajectory");
            if (itTraj != edgeObj.end() && itTraj->second.isObject()) {
                const simagv::json::Object trajObj = itTraj->second.asObject();
                const std::string type = simagv::l2::readStringOr(trajObj, "type", "type", "");
                if (type == "CubicBezier" || type == "CUBIC_BEZIER") {
                    simagv::l4::Trajectory traj{};
                    traj.type = simagv::l4::TrajectoryType::CUBIC_BEZIER;
                    traj.degree = simagv::l2::readUintOr(trajObj, "degree", "degree", 3U);
                    const auto itCps = trajObj.find("controlPoints");
                    if (itCps != trajObj.end() && itCps->second.isArray()) {
                        for (const simagv::json::Value& cpValue : itCps->second.asArray()) {
                            if (!cpValue.isObject()) {
                                continue;
                            }
                            const simagv::json::Object& cpObj = cpValue.asObject();
                            simagv::l4::TrajectoryControlPoint cp{};
                            cp.x = simagv::l2::readFloatOr(cpObj, "x", "x", 0.0F);
                            cp.y = simagv::l2::readFloatOr(cpObj, "y", "y", 0.0F);
                            cp.weight = simagv::l2::readFloatOr(cpObj, "weight", "weight", 1.0F);
                            cp.orientation = std::numeric_limits<float>::quiet_NaN();
                            traj.controlPoints.push_back(cp);
                        }
                    }
                    if (!traj.controlPoints.empty()) {
                        e.trajectory = std::move(traj);
                    }
                }
            }

            if (!e.edgeId.empty()) {
                out.push_back(std::move(e));
            }
        }
        std::sort(out.begin(), out.end(), [](const PlannedEdge& a, const PlannedEdge& b) { return a.sequenceId < b.sequenceId; });
        return out;
    }

    std::optional<size_t> findNodeIndexInVector(const std::vector<PlannedNode>& nodes, const std::string& nodeId, size_t fromIndex) const
    {
        for (size_t i = fromIndex; i < nodes.size(); ++i) {
            if (nodes[i].nodeId == nodeId) {
                return i;
            }
        }
        return std::nullopt;
    }

    void appendNavigationPlanLocked(const simagv::json::Object& orderObj)
    {
        if (navigationMode_ != NavigationMode::Path || plannedEdges_.empty()) {
            throw std::runtime_error("append_failed_no_existing_path");
        }

        const std::vector<PlannedNode> incomingNodes = parsePlannedNodesFromOrderLocked(orderObj);
        const std::vector<PlannedEdge> incomingEdges = parsePlannedEdgesFromOrderLocked(orderObj);
        if (incomingEdges.empty()) {
            throw std::runtime_error("append_failed_missing_edges");
        }
        if (incomingNodes.empty()) {
            throw std::runtime_error("append_failed_missing_nodes");
        }

        const auto lockedIt = std::find_if(plannedEdges_.begin(), plannedEdges_.end(), [](const PlannedEdge& e) { return !e.released; });
        if (lockedIt != plannedEdges_.end()) {
            const size_t lockedIndex = static_cast<size_t>(std::distance(plannedEdges_.begin(), lockedIt));
            const PlannedEdge lockedEdge = plannedEdges_[lockedIndex];

            const PlannedEdge& firstEdge = incomingEdges.front();
            if (firstEdge.edgeId != lockedEdge.edgeId || firstEdge.startNodeId != lockedEdge.startNodeId || firstEdge.endNodeId != lockedEdge.endNodeId) {
                throw std::runtime_error("append_failed_locked_edge_not_match");
            }
            if (!firstEdge.released) {
                throw std::runtime_error("append_failed_locked_edge_not_released");
            }

            const auto incomingStartIdxOpt = findNodeIndexInVector(incomingNodes, lockedEdge.startNodeId, 0U);
            if (!incomingStartIdxOpt.has_value()) {
                throw std::runtime_error("append_failed_missing_start_node");
            }
            const auto existingStartIdxOpt = findNextPlannedNodeIndexLocked(lockedEdge.startNodeId, 0U);
            if (!existingStartIdxOpt.has_value()) {
                throw std::runtime_error("append_failed_missing_start_node");
            }
            size_t lockedStartNodeIndex = existingStartIdxOpt.value();
            if (lockedEdge.startNodeIndex < plannedNodes_.size()) {
                lockedStartNodeIndex = lockedEdge.startNodeIndex;
            }
            size_t keepNodeMaxIndex = lockedStartNodeIndex;
            for (size_t i = 0U; i < lockedIndex; ++i) {
                const PlannedEdge& e = plannedEdges_[i];
                if (e.startNodeIndex < plannedNodes_.size()) {
                    keepNodeMaxIndex = std::max(keepNodeMaxIndex, e.startNodeIndex);
                }
                if (e.endNodeIndex < plannedNodes_.size()) {
                    keepNodeMaxIndex = std::max(keepNodeMaxIndex, e.endNodeIndex);
                }
            }

            uint32_t maxEdgeSequenceId = 0U;
            if (lockedIndex > 0U) {
                maxEdgeSequenceId = plannedEdges_[lockedIndex - 1U].sequenceId;
            }
            for (size_t i = 0U; i < incomingEdges.size(); ++i) {
                if (incomingEdges[i].sequenceId <= maxEdgeSequenceId) {
                    throw std::runtime_error("append_failed_edge_sequence_not_increment");
                }
                maxEdgeSequenceId = incomingEdges[i].sequenceId;
            }

            plannedEdges_.resize(lockedIndex);
            plannedEdges_.insert(plannedEdges_.end(), incomingEdges.begin(), incomingEdges.end());

            if (keepNodeMaxIndex >= plannedNodes_.size()) {
                throw std::runtime_error("append_failed_missing_start_node");
            }
            plannedNodes_.resize(keepNodeMaxIndex + 1U);
            if (lockedStartNodeIndex >= plannedNodes_.size()) {
                throw std::runtime_error("append_failed_missing_start_node");
            }
            plannedNodes_[lockedStartNodeIndex].released = incomingNodes[incomingStartIdxOpt.value()].released;

            uint32_t maxNodeSequenceId = plannedNodes_.empty() ? 0U : plannedNodes_.back().sequenceId;
            for (size_t i = incomingStartIdxOpt.value() + 1U; i < incomingNodes.size(); ++i) {
                if (incomingNodes[i].sequenceId <= maxNodeSequenceId) {
                    throw std::runtime_error("append_failed_node_sequence_not_increment");
                }
                plannedNodes_.push_back(incomingNodes[i]);
                maxNodeSequenceId = incomingNodes[i].sequenceId;
            }

            resolvePlannedEdgeNodeIndicesLocked();
            preloadNodeActionsLocked();
            driving_ = hasPendingNavigationLocked();
            return;
        }

        const PlannedEdge& lastEdge = plannedEdges_.back();
        const PlannedEdge& firstEdge = incomingEdges.front();
        if (lastEdge.edgeId == firstEdge.edgeId && lastEdge.startNodeId == firstEdge.startNodeId && lastEdge.endNodeId == firstEdge.endNodeId) {
            if (firstEdge.sequenceId <= lastEdge.sequenceId) {
                throw std::runtime_error("append_failed_edge_sequence_not_increment");
            }

            const auto startIdxOpt = findNodeIndexInVector(incomingNodes, firstEdge.startNodeId, 0U);
            if (!startIdxOpt.has_value()) {
                throw std::runtime_error("append_failed_missing_start_node");
            }
            const auto endIdxOpt = findNodeIndexInVector(incomingNodes, firstEdge.endNodeId, startIdxOpt.value() + 1U);
            if (!endIdxOpt.has_value()) {
                throw std::runtime_error("append_failed_missing_end_node");
            }

            PlannedEdge updatedEdge = firstEdge;
            updatedEdge.startNodeIndex = lastEdge.startNodeIndex;
            updatedEdge.endNodeIndex = lastEdge.endNodeIndex;
            plannedEdges_.back() = std::move(updatedEdge);
            if (lastEdge.startNodeIndex < plannedNodes_.size()) {
                plannedNodes_[lastEdge.startNodeIndex].released = incomingNodes[startIdxOpt.value()].released;
            }
            if (lastEdge.endNodeIndex < plannedNodes_.size()) {
                plannedNodes_[lastEdge.endNodeIndex].released = incomingNodes[endIdxOpt.value()].released;
            }

            uint32_t maxNodeSequenceId = plannedNodes_.empty() ? 0U : plannedNodes_.back().sequenceId;
            for (size_t i = endIdxOpt.value() + 1U; i < incomingNodes.size(); ++i) {
                if (incomingNodes[i].sequenceId <= maxNodeSequenceId) {
                    throw std::runtime_error("append_failed_node_sequence_not_increment");
                }
                plannedNodes_.push_back(incomingNodes[i]);
                maxNodeSequenceId = incomingNodes[i].sequenceId;
            }

            uint32_t maxEdgeSequenceId = plannedEdges_.empty() ? 0U : plannedEdges_.back().sequenceId;
            for (size_t i = 1U; i < incomingEdges.size(); ++i) {
                if (incomingEdges[i].sequenceId <= maxEdgeSequenceId) {
                    throw std::runtime_error("append_failed_edge_sequence_not_increment");
                }
                plannedEdges_.push_back(incomingEdges[i]);
                maxEdgeSequenceId = incomingEdges[i].sequenceId;
            }

            resolvePlannedEdgeNodeIndicesLocked();
            preloadNodeActionsLocked();
            driving_ = hasPendingNavigationLocked();
            return;
        }

        if (firstEdge.startNodeId != lastEdge.endNodeId) {
            throw std::runtime_error("append_failed_start_node_not_match");
        }
        if (firstEdge.sequenceId <= lastEdge.sequenceId) {
            throw std::runtime_error("append_failed_edge_sequence_not_increment");
        }

        const auto startIdxOpt = findNodeIndexInVector(incomingNodes, firstEdge.startNodeId, 0U);
        if (!startIdxOpt.has_value()) {
            throw std::runtime_error("append_failed_missing_start_node");
        }
        if (!plannedNodes_.empty() && plannedNodes_.back().nodeId == firstEdge.startNodeId) {
            plannedNodes_.back().released = incomingNodes[startIdxOpt.value()].released;
        } else if (const auto existingStartIdxOpt = findNextPlannedNodeIndexLocked(firstEdge.startNodeId, 0U); existingStartIdxOpt.has_value()) {
            plannedNodes_[existingStartIdxOpt.value()].released = incomingNodes[startIdxOpt.value()].released;
        }

        uint32_t maxNodeSequenceId = plannedNodes_.empty() ? 0U : plannedNodes_.back().sequenceId;
        for (size_t i = startIdxOpt.value() + 1U; i < incomingNodes.size(); ++i) {
            if (incomingNodes[i].sequenceId <= maxNodeSequenceId) {
                throw std::runtime_error("append_failed_node_sequence_not_increment");
            }
            plannedNodes_.push_back(incomingNodes[i]);
            maxNodeSequenceId = incomingNodes[i].sequenceId;
        }

        uint32_t maxEdgeSequenceId = plannedEdges_.empty() ? 0U : plannedEdges_.back().sequenceId;
        for (size_t i = 0U; i < incomingEdges.size(); ++i) {
            if (incomingEdges[i].sequenceId <= maxEdgeSequenceId) {
                throw std::runtime_error("append_failed_edge_sequence_not_increment");
            }
            plannedEdges_.push_back(incomingEdges[i]);
            maxEdgeSequenceId = incomingEdges[i].sequenceId;
        }

        resolvePlannedEdgeNodeIndicesLocked();
        preloadNodeActionsLocked();
        driving_ = hasPendingNavigationLocked();
    }

    void buildNavigationPlanLocked(const simagv::json::Object& orderObj)
    {
        plannedNodes_.clear();
        plannedEdges_.clear();
        activeItemIndex_ = 0U;
        activePath_.clear();
        activePathIndex_ = 0U;

        const auto itNodes = orderObj.find("nodes");
        if (itNodes == orderObj.end() || !itNodes->second.isArray()) {
            return;
        }
        const simagv::json::Array& nodes = itNodes->second.asArray();
        plannedNodes_.reserve(nodes.size());
        for (const simagv::json::Value& nodeValue : nodes) {
            if (!nodeValue.isObject()) {
                continue;
            }
            const simagv::json::Object& nodeObj = nodeValue.asObject();
            const auto itPos = nodeObj.find("nodePosition");
            if (itPos == nodeObj.end() || !itPos->second.isObject()) {
                continue;
            }
            const simagv::json::Object& posObj = itPos->second.asObject();

            PlannedNode n{};
            n.nodeId = simagv::l2::readStringOr(nodeObj, "node_id", "nodeId", "");
            n.sequenceId = simagv::l2::readUintOr(nodeObj, "sequence_id", "sequenceId", 0U);
            {
                const auto* releasedValue = simagv::l2::tryGetSnakeOrCamel(nodeObj, "released", "released");
                n.released = (releasedValue != nullptr && releasedValue->isBool()) ? releasedValue->asBool() : true;
            }
            n.x = simagv::l2::readFloatOr(posObj, "x", "x", 0.0F);
            n.y = simagv::l2::readFloatOr(posObj, "y", "y", 0.0F);
            n.theta = simagv::l2::readFloatOr(posObj, "theta", "theta", 0.0F);
            n.allowedDeviationXY = simagv::l2::readFloatOr(posObj, "allowed_deviation_xy", "allowedDeviationXY", 0.5F);
            n.allowedDeviationTheta = simagv::l2::readFloatOr(posObj, "allowed_deviation_theta", "allowedDeviationTheta", 0.5F);
            n.actions = readActionObjects(nodeObj);
            if (!n.nodeId.empty()) {
                plannedNodes_.push_back(std::move(n));
            }
        }
        std::sort(plannedNodes_.begin(), plannedNodes_.end(), [](const PlannedNode& a, const PlannedNode& b) { return a.sequenceId < b.sequenceId; });

        const auto itEdges = orderObj.find("edges");
        if (itEdges == orderObj.end() || !itEdges->second.isArray() || itEdges->second.asArray().empty()) {
            navigationMode_ = NavigationMode::Station;
            return;
        }
        navigationMode_ = NavigationMode::Path;

        const simagv::json::Array& edges = itEdges->second.asArray();
        plannedEdges_.reserve(edges.size());
        for (const simagv::json::Value& edgeValue : edges) {
            if (!edgeValue.isObject()) {
                continue;
            }
            const simagv::json::Object& edgeObj = edgeValue.asObject();
            PlannedEdge e{};
            e.edgeId = simagv::l2::readStringOr(edgeObj, "edge_id", "edgeId", "");
            e.sequenceId = simagv::l2::readUintOr(edgeObj, "sequence_id", "sequenceId", 0U);
            {
                const auto* releasedValue = simagv::l2::tryGetSnakeOrCamel(edgeObj, "released", "released");
                e.released = (releasedValue != nullptr && releasedValue->isBool()) ? releasedValue->asBool() : true;
            }
            e.startNodeId = simagv::l2::readStringOr(edgeObj, "start_node_id", "startNodeId", "");
            e.endNodeId = simagv::l2::readStringOr(edgeObj, "end_node_id", "endNodeId", "");
            e.startNodeIndex = std::numeric_limits<size_t>::max();
            e.endNodeIndex = std::numeric_limits<size_t>::max();
            const auto* rot = simagv::l2::tryGetSnakeOrCamel(edgeObj, "rotation_allowed", "rotationAllowed");
            e.rotationAllowed = (rot != nullptr && rot->isBool()) ? rot->asBool() : true;

            const auto* maxSpeedValue = simagv::l2::tryGetSnakeOrCamel(edgeObj, "max_speed", "maxSpeed");
            if (maxSpeedValue != nullptr && maxSpeedValue->isNumber()) {
                e.maxSpeed = static_cast<float>(maxSpeedValue->asNumber());
            }
            const auto* maxRotValue = simagv::l2::tryGetSnakeOrCamel(edgeObj, "max_rotation_speed", "maxRotationSpeed");
            if (maxRotValue != nullptr && maxRotValue->isNumber()) {
                e.maxRotationSpeed = static_cast<float>(maxRotValue->asNumber());
            }
            const auto* oriValue = simagv::l2::tryGetSnakeOrCamel(edgeObj, "orientation", "orientation");
            if (oriValue != nullptr && oriValue->isNumber()) {
                e.orientation = static_cast<float>(oriValue->asNumber());
            }
            e.direction = simagv::l2::readStringOr(edgeObj, "direction", "direction", "");
            e.actions = readActionObjects(edgeObj);

            const auto itTraj = edgeObj.find("trajectory");
            if (itTraj != edgeObj.end() && itTraj->second.isObject()) {
                const simagv::json::Object trajObj = itTraj->second.asObject();
                const std::string type = simagv::l2::readStringOr(trajObj, "type", "type", "");
                if (type == "CubicBezier" || type == "CUBIC_BEZIER") {
                    simagv::l4::Trajectory traj{};
                    traj.type = simagv::l4::TrajectoryType::CUBIC_BEZIER;
                    traj.degree = simagv::l2::readUintOr(trajObj, "degree", "degree", 3U);
                    const auto itCps = trajObj.find("controlPoints");
                    if (itCps != trajObj.end() && itCps->second.isArray()) {
                        for (const simagv::json::Value& cpValue : itCps->second.asArray()) {
                            if (!cpValue.isObject()) {
                                continue;
                            }
                            const simagv::json::Object& cpObj = cpValue.asObject();
                            simagv::l4::TrajectoryControlPoint cp{};
                            cp.x = simagv::l2::readFloatOr(cpObj, "x", "x", 0.0F);
                            cp.y = simagv::l2::readFloatOr(cpObj, "y", "y", 0.0F);
                            cp.weight = simagv::l2::readFloatOr(cpObj, "weight", "weight", 1.0F);
                            cp.orientation = std::numeric_limits<float>::quiet_NaN();
                            traj.controlPoints.push_back(cp);
                        }
                    }
                    if (!traj.controlPoints.empty()) {
                        e.trajectory = std::move(traj);
                    }
                }
            }

            if (!e.edgeId.empty()) {
                plannedEdges_.push_back(std::move(e));
            }
        }
        std::sort(plannedEdges_.begin(), plannedEdges_.end(), [](const PlannedEdge& a, const PlannedEdge& b) { return a.sequenceId < b.sequenceId; });
        resolvePlannedEdgeNodeIndicesLocked();
        preloadNodeActionsLocked();
    }

    void updateMotionLocked(uint32_t tickMs)
    {
        const float dtSec = (static_cast<float>(tickMs) / 1000.0F) * simTimeScale_;
        if (dtSec <= 0.0F) {
            velVx_ = 0.0F;
            velVy_ = 0.0F;
            velOmega_ = 0.0F;
            return;
        }

        const float prevX = poseX_;
        const float prevY = poseY_;
        const float prevTheta = poseTheta_;

        if (paused_) {
            velVx_ = 0.0F;
            velVy_ = 0.0F;
            velOmega_ = 0.0F;
            return;
        }

        if (perceptionBlocked_ || perceptionCollision_) {
            velVx_ = 0.0F;
            velVy_ = 0.0F;
            velOmega_ = 0.0F;
            return;
        }

        if (openLoopMode_ != OpenLoopMode::None) {
            updateOpenLoopLocked(tickMs, dtSec);
            velVx_ = (poseX_ - prevX) / dtSec;
            velVy_ = (poseY_ - prevY) / dtSec;
            velOmega_ = normalizeAngle(poseTheta_ - prevTheta) / dtSec;
            return;
        }

        updateNavigationLocked(tickMs);
    }

    void updateOpenLoopLocked(uint32_t tickMs, float dtSec)
    {
        if (openLoopMode_ == OpenLoopMode::Velocity) {
            const float worldVx = std::cos(poseTheta_) * openLoopVxRobot_ - std::sin(poseTheta_) * openLoopVyRobot_;
            const float worldVy = std::sin(poseTheta_) * openLoopVxRobot_ + std::cos(poseTheta_) * openLoopVyRobot_;
            poseX_ += worldVx * dtSec;
            poseY_ += worldVy * dtSec;
            poseTheta_ = normalizeAngle(poseTheta_ + openLoopOmega_ * dtSec);
            if (openLoopRemainingMs_ > 0U) {
                const uint32_t dec = std::min(openLoopRemainingMs_, tickMs);
                openLoopRemainingMs_ -= dec;
                if (openLoopRemainingMs_ == 0U) {
                    resetOpenLoopLocked();
                }
            }
            return;
        }
        if (openLoopMode_ == OpenLoopMode::Translate) {
            const float step = openLoopTranslateSpeed_ * dtSec;
            const float dist = std::min(openLoopTranslateRemainingM_, std::max(0.0F, step));
            poseX_ += openLoopTranslateDirX_ * dist;
            poseY_ += openLoopTranslateDirY_ * dist;
            openLoopTranslateRemainingM_ -= dist;
            if (openLoopTranslateRemainingM_ <= 1e-5F) {
                resetOpenLoopLocked();
            }
            return;
        }
        if (openLoopMode_ == OpenLoopMode::Turn) {
            const float step = std::abs(openLoopTurnOmega_) * dtSec;
            const float angle = std::min(openLoopTurnRemainingRad_, std::max(0.0F, step));
            const float sign = (openLoopTurnOmega_ >= 0.0F) ? 1.0F : -1.0F;
            poseTheta_ = normalizeAngle(poseTheta_ + sign * angle);
            openLoopTurnRemainingRad_ -= angle;
            if (openLoopTurnRemainingRad_ <= 1e-5F) {
                resetOpenLoopLocked();
            }
            return;
        }
    }

    void updateNavigationLocked(uint32_t tickMs)
    {
        const float dtSec = (static_cast<float>(tickMs) / 1000.0F) * simTimeScale_;
        if (!driving_ || dtSec <= 0.0F) {
            velVx_ = 0.0F;
            velVy_ = 0.0F;
            velOmega_ = 0.0F;
            return;
        }

        if (waitingForHardAction_) {
            if (!hasActiveAction_ || activeActionStatus_ == "FINISHED") {
                waitingForHardAction_ = false;
                activePath_.clear();
                activePathIndex_ = 0U;
                activePathKind_ = ActivePathKind::None;
                activeApproachNodeIndex_ = std::numeric_limits<size_t>::max();
            } else {
                velVx_ = 0.0F;
                velVy_ = 0.0F;
                velOmega_ = 0.0F;
                return;
            }
        }

        if (navigationBlocked_) {
            if (blockedEdgeIndex_ >= plannedEdges_.size()) {
                navigationBlocked_ = false;
                blockedEdgeIndex_ = std::numeric_limits<size_t>::max();
            } else if (plannedEdges_[blockedEdgeIndex_].released) {
                const PlannedEdge& edge = plannedEdges_[blockedEdgeIndex_];
                navigationBlocked_ = false;
                blockedEdgeIndex_ = std::numeric_limits<size_t>::max();
                std::ostringstream oss;
                oss << "nav_edge_unblocked orderId=" << currentOrderId_ << " edgeId=" << edge.edgeId << " sequenceId=" << edge.sequenceId;
                simagv::l4::logInfo(oss.str());
            } else {
                velVx_ = 0.0F;
                velVy_ = 0.0F;
                velOmega_ = 0.0F;
                return;
            }
        }

        const float prevX = poseX_;
        const float prevY = poseY_;
        const float prevTheta = poseTheta_;

        if (activePathIndex_ >= activePath_.size()) {
            activePath_.clear();
            activePathIndex_ = 0U;
            activePathKind_ = ActivePathKind::None;
            activeApproachNodeIndex_ = std::numeric_limits<size_t>::max();
        }
        if (activePath_.empty()) {
            buildNextActivePathLocked();
        }
        if (!driving_ || activePath_.empty()) {
            velVx_ = 0.0F;
            velVy_ = 0.0F;
            velOmega_ = 0.0F;
            return;
        }

        float distanceBudget = std::max(0.0F, activeMaxSpeed_) * dtSec;
        while (distanceBudget > 1e-6F && activePathIndex_ < activePath_.size()) {
            const simagv::l4::PosePoint& target = activePath_[activePathIndex_];
            const float dx = target.x - poseX_;
            const float dy = target.y - poseY_;
            const float dist = hypot2(dx, dy);
            const float thetaError = normalizeAngle(target.theta - poseTheta_);
            const float thetaErrorAbs = std::abs(thetaError);
            const bool canRotate = activeMaxRotationSpeed_ > 1e-6F;
            if (dist <= 1e-4F) {
                poseX_ = target.x;
                poseY_ = target.y;
                if (canRotate && thetaErrorAbs > 0.02F) {
                    poseTheta_ = applyThetaStepLocked(poseTheta_, target.theta, dtSec);
                    distanceBudget = 0.0F;
                    break;
                }
                activePathIndex_ += 1U;
                continue;
            }
            if (canRotate && thetaErrorAbs > 0.35F) {
                poseTheta_ = applyThetaStepLocked(poseTheta_, target.theta, dtSec);
                distanceBudget = 0.0F;
                break;
            }
            if (dist <= distanceBudget) {
                poseX_ = target.x;
                poseY_ = target.y;
                poseTheta_ = applyThetaStepLocked(poseTheta_, target.theta, dtSec);
                distanceBudget -= dist;
                activePathIndex_ += 1U;
                continue;
            }
            const float ratio = distanceBudget / dist;
            poseX_ += dx * ratio;
            poseY_ += dy * ratio;
            poseTheta_ = applyThetaStepLocked(poseTheta_, target.theta, dtSec);
            distanceBudget = 0.0F;
        }

        if (activePathIndex_ >= activePath_.size()) {
            onActivePathCompleteLocked();
        }

        velVx_ = (poseX_ - prevX) / dtSec;
        velVy_ = (poseY_ - prevY) / dtSec;
        velOmega_ = normalizeAngle(poseTheta_ - prevTheta) / dtSec;
    }

    float applyThetaStepLocked(float currentTheta, float desiredTheta, float dtSec) const
    {
        const float maxOmega = std::max(0.0F, activeMaxRotationSpeed_);
        const float maxStep = maxOmega * dtSec;
        if (maxStep <= 0.0F) {
            return currentTheta;
        }
        const float delta = normalizeAngle(desiredTheta - currentTheta);
        const float applied = clampAbs(delta, maxStep);
        return normalizeAngle(currentTheta + applied);
    }

    void buildNextActivePathLocked()
    {
        if (navigationMode_ == NavigationMode::Station) {
            if (activeItemIndex_ >= plannedNodes_.size()) {
                driving_ = false;
                return;
            }
            const PlannedNode& targetNode = plannedNodes_[activeItemIndex_];
            simagv::l4::Trajectory traj{};
            traj.type = simagv::l4::TrajectoryType::STRAIGHT;
            traj.degree = 0U;
            traj.controlPoints.clear();
            traj.knotVector.clear();

            simagv::l4::TrajectoryPolylineConfig cfg{};
            cfg.steps = 50U;
            cfg.orientation = targetNode.theta;
            cfg.direction = "";

            const simagv::l4::Position startPos{poseX_, poseY_, 0.0F};
            const simagv::l4::Position endPos{targetNode.x, targetNode.y, 0.0F};
            activePath_ = simagv::l4::trajectoryPolyline(traj, startPos, endPos, cfg);
            activePathIndex_ = 0U;
            activePathKind_ = ActivePathKind::StationNode;
            activeApproachNodeIndex_ = std::numeric_limits<size_t>::max();
            const float speedMax = static_cast<float>(config_.physicalParameters.speedMax);
            activeMaxSpeed_ = std::max(0.1F, speedMax);
            activeMaxRotationSpeed_ = 1.0F;
            activeRotationAllowed_ = true;
            {
                std::ostringstream oss;
                oss << "nav_segment_start mode=station orderId=" << currentOrderId_ << " nodeId=" << targetNode.nodeId
                    << " sequenceId=" << targetNode.sequenceId << " startX=" << poseX_ << " startY=" << poseY_
                    << " endX=" << targetNode.x << " endY=" << targetNode.y << " speed=" << activeMaxSpeed_
                    << " pathPoints=" << activePath_.size();
                simagv::l4::logInfo(oss.str());
            }
            return;
        }

        if (activeItemIndex_ >= plannedEdges_.size()) {
            driving_ = false;
            return;
        }
        const PlannedEdge& edge = plannedEdges_[activeItemIndex_];
        if (edge.startNodeIndex >= plannedNodes_.size() || edge.endNodeIndex >= plannedNodes_.size()) {
            driving_ = false;
            return;
        }
        const PlannedNode& startNode = plannedNodes_[edge.startNodeIndex];
        const PlannedNode& endNode = plannedNodes_[edge.endNodeIndex];

        const float startDx = startNode.x - poseX_;
        const float startDy = startNode.y - poseY_;
        const float distToStart = hypot2(startDx, startDy);
        if (distToStart > kStationNearDistanceM) {
            simagv::l4::Trajectory traj{};
            traj.type = simagv::l4::TrajectoryType::STRAIGHT;
            traj.degree = 0U;
            traj.controlPoints.clear();
            traj.knotVector.clear();

            simagv::l4::TrajectoryPolylineConfig cfg{};
            cfg.steps = 50U;
            cfg.orientation = startNode.theta;
            cfg.direction = "";

            const simagv::l4::Position startPos{poseX_, poseY_, 0.0F};
            const simagv::l4::Position endPos{startNode.x, startNode.y, 0.0F};
            activePath_ = simagv::l4::trajectoryPolyline(traj, startPos, endPos, cfg);
            activePathIndex_ = 0U;
            activePathKind_ = ActivePathKind::PathApproachStartNode;
            activeApproachNodeIndex_ = edge.startNodeIndex;
            const float speedMax = static_cast<float>(config_.physicalParameters.speedMax);
            activeMaxSpeed_ = std::max(0.1F, speedMax);
            activeMaxRotationSpeed_ = 1.0F;
            activeRotationAllowed_ = true;
            {
                std::ostringstream oss;
                oss << "nav_segment_start mode=path_approach_start orderId=" << currentOrderId_ << " targetNodeId=" << startNode.nodeId
                    << " sequenceId=" << startNode.sequenceId << " startX=" << poseX_ << " startY=" << poseY_
                    << " endX=" << startNode.x << " endY=" << startNode.y << " speed=" << activeMaxSpeed_
                    << " pathPoints=" << activePath_.size();
                simagv::l4::logInfo(oss.str());
            }
            return;
        }

        lastNodeId_ = startNode.nodeId;
        lastNodeSequenceId_ = startNode.sequenceId;
        if (!edge.released) {
            if (!navigationBlocked_ || blockedEdgeIndex_ != activeItemIndex_) {
                navigationBlocked_ = true;
                blockedEdgeIndex_ = activeItemIndex_;
                std::ostringstream oss;
                oss << "nav_edge_locked_wait orderId=" << currentOrderId_ << " orderUpdateId=" << currentOrderUpdateId_
                    << " edgeId=" << edge.edgeId << " sequenceId=" << edge.sequenceId << " startNodeId=" << edge.startNodeId
                    << " endNodeId=" << edge.endNodeId;
                simagv::l4::logInfo(oss.str());
            }
            return;
        }
        if (navigationBlocked_ && blockedEdgeIndex_ == activeItemIndex_) {
            navigationBlocked_ = false;
            blockedEdgeIndex_ = std::numeric_limits<size_t>::max();
        }
        if (!edge.actions.empty() && lastAppliedEdgeActionsIndex_ != activeItemIndex_) {
            for (const auto& actionObj : edge.actions) {
                applyActionLocked(actionObj);
            }
            lastAppliedEdgeActionsIndex_ = activeItemIndex_;
            if (waitingForHardAction_) {
                return;
            }
        }

        simagv::l4::Trajectory traj{};
        if (edge.trajectory.has_value()) {
            traj = edge.trajectory.value();
        } else {
            traj.type = simagv::l4::TrajectoryType::STRAIGHT;
            traj.degree = 0U;
            traj.controlPoints.clear();
            traj.knotVector.clear();
        }

        simagv::l4::TrajectoryPolylineConfig cfg{};
        cfg.steps = 80U;
        cfg.orientation = edge.orientation.has_value() ? edge.orientation.value() : std::numeric_limits<float>::quiet_NaN();
        cfg.direction = edge.direction;

        const simagv::l4::Position startPos{poseX_, poseY_, 0.0F};
        const simagv::l4::Position endPos{endNode.x, endNode.y, 0.0F};
        activePath_ = simagv::l4::trajectoryPolyline(traj, startPos, endPos, cfg);
        activePathIndex_ = 0U;
        activePathKind_ = ActivePathKind::PathTraverseEdge;
        activeApproachNodeIndex_ = std::numeric_limits<size_t>::max();

        const float speedMax = static_cast<float>(config_.physicalParameters.speedMax);
        activeMaxSpeed_ = edge.maxSpeed.has_value() ? edge.maxSpeed.value() : speedMax;
        activeMaxSpeed_ = std::max(0.1F, std::min(activeMaxSpeed_, speedMax));
        activeMaxRotationSpeed_ = edge.maxRotationSpeed.has_value() ? std::max(0.0F, edge.maxRotationSpeed.value()) : 1.0F;
        activeRotationAllowed_ = edge.rotationAllowed;
        {
            std::ostringstream oss;
            oss << "nav_segment_start mode=path orderId=" << currentOrderId_ << " edgeId=" << edge.edgeId << " sequenceId=" << edge.sequenceId
                << " startNodeId=" << edge.startNodeId << " endNodeId=" << edge.endNodeId << " startX=" << poseX_ << " startY=" << poseY_
                << " endX=" << endNode.x << " endY=" << endNode.y << " speed=" << activeMaxSpeed_
                << " rotationAllowed=" << (activeRotationAllowed_ ? "true" : "false") << " pathPoints=" << activePath_.size();
            simagv::l4::logInfo(oss.str());
        }
    }

    void onActivePathCompleteLocked()
    {
        if (navigationMode_ == NavigationMode::Station) {
            if (activeItemIndex_ < plannedNodes_.size()) {
                if (!plannedNodes_[activeItemIndex_].actions.empty() && lastAppliedNodeActionsIndex_ != activeItemIndex_) {
                    for (const auto& actionObj : plannedNodes_[activeItemIndex_].actions) {
                        applyActionLocked(actionObj);
                    }
                    lastAppliedNodeActionsIndex_ = activeItemIndex_;
                }
                lastNodeId_ = plannedNodes_[activeItemIndex_].nodeId;
                lastNodeSequenceId_ = plannedNodes_[activeItemIndex_].sequenceId;
                {
                    std::ostringstream oss;
                    oss << "nav_segment_done mode=station orderId=" << currentOrderId_ << " nodeId=" << lastNodeId_
                        << " sequenceId=" << lastNodeSequenceId_;
                    simagv::l4::logInfo(oss.str());
                }
            }
            activePath_.clear();
            activePathIndex_ = 0U;
            activePathKind_ = ActivePathKind::None;
            activeApproachNodeIndex_ = std::numeric_limits<size_t>::max();
            if (waitingForHardAction_) {
                return;
            }
            activeItemIndex_ += 1U;
            if (activeItemIndex_ >= plannedNodes_.size()) {
                driving_ = false;
                std::ostringstream oss;
                oss << "nav_order_done orderId=" << currentOrderId_ << " orderUpdateId=" << currentOrderUpdateId_ << " mode=station";
                simagv::l4::logInfo(oss.str());
            }
            return;
        }

        if (activePathKind_ == ActivePathKind::PathApproachStartNode) {
            if (activeApproachNodeIndex_ < plannedNodes_.size()) {
                const PlannedNode& node = plannedNodes_[activeApproachNodeIndex_];
                lastNodeId_ = node.nodeId;
                lastNodeSequenceId_ = node.sequenceId;
            }
            {
                std::ostringstream oss;
                oss << "nav_segment_done mode=path_approach_start orderId=" << currentOrderId_ << " nodeId=" << lastNodeId_
                    << " sequenceId=" << lastNodeSequenceId_;
                simagv::l4::logInfo(oss.str());
            }
            activePathKind_ = ActivePathKind::None;
            activeApproachNodeIndex_ = std::numeric_limits<size_t>::max();
            activePath_.clear();
            activePathIndex_ = 0U;
            return;
        }

        if (activeItemIndex_ < plannedEdges_.size()) {
            const PlannedEdge& edge = plannedEdges_[activeItemIndex_];
            size_t endNodeIndex = edge.endNodeIndex;
            if (endNodeIndex >= plannedNodes_.size()) {
                const size_t fromIndex = (edge.startNodeIndex < plannedNodes_.size()) ? (edge.startNodeIndex + 1U) : 0U;
                const auto endIndexOpt = findNextPlannedNodeIndexLocked(edge.endNodeId, fromIndex);
                if (endIndexOpt.has_value()) {
                    endNodeIndex = endIndexOpt.value();
                }
            }
            if (endNodeIndex < plannedNodes_.size()) {
                const PlannedNode& endNode = plannedNodes_[endNodeIndex];
                lastNodeId_ = endNode.nodeId;
                lastNodeSequenceId_ = endNode.sequenceId;
                if (!endNode.actions.empty() && lastAppliedNodeActionsIndex_ != endNodeIndex) {
                    for (const auto& actionObj : endNode.actions) {
                        applyActionLocked(actionObj);
                    }
                    lastAppliedNodeActionsIndex_ = endNodeIndex;
                }
            } else {
                lastNodeId_ = edge.endNodeId;
            }
            {
                std::ostringstream oss;
                oss << "nav_segment_done mode=path orderId=" << currentOrderId_ << " edgeId=" << edge.edgeId << " sequenceId=" << edge.sequenceId
                    << " endNodeId=" << lastNodeId_;
                simagv::l4::logInfo(oss.str());
            }
        }
        activeItemIndex_ += 1U;
        activePath_.clear();
        activePathIndex_ = 0U;
        activePathKind_ = ActivePathKind::None;
        activeApproachNodeIndex_ = std::numeric_limits<size_t>::max();
        if (activeItemIndex_ >= plannedEdges_.size() && !waitingForHardAction_) {
            driving_ = false;
            std::ostringstream oss;
            oss << "nav_order_done orderId=" << currentOrderId_ << " orderUpdateId=" << currentOrderUpdateId_ << " mode=path";
            simagv::l4::logInfo(oss.str());
        }
    }

    void updateBatteryLocked(uint32_t tickMs)
    {
        const float tickMin = (static_cast<float>(tickMs) / 60000.0F) * simTimeScale_;
        if (tickMin <= 0.0F) {
            return;
        }

        if (batteryCharging_) {
            batteryChargeLevel_ += batteryChargePerMin_ * tickMin;
            batteryChargeLevel_ = simagv::l2::clampRange(batteryChargeLevel_, 0.0F, 100.0F);
            return;
        }

        float drainPerMin = batteryIdleDrainPerMin_;
        if ((openLoopMode_ != OpenLoopMode::None) || (driving_ && !paused_)) {
            const float multiplier = hasLoad_ ? batteryMoveLoadedMultiplier_ : batteryMoveEmptyMultiplier_;
            drainPerMin *= multiplier;
        }
        batteryChargeLevel_ -= drainPerMin * tickMin;
        batteryChargeLevel_ = simagv::l2::clampRange(batteryChargeLevel_, 0.0F, 100.0F);
    }

    simagv::json::Object makeNodePositionObjectLocked(const PlannedNode& node) const
    {
        simagv::json::Object nodePos;
        nodePos.emplace("x", simagv::json::Value{node.x});
        nodePos.emplace("y", simagv::json::Value{node.y});
        nodePos.emplace("theta", simagv::json::Value{node.theta});
        nodePos.emplace("allowedDeviationXy", simagv::json::Value{node.allowedDeviationXY});
        nodePos.emplace("allowedDeviationTheta", simagv::json::Value{node.allowedDeviationTheta});
        nodePos.emplace("mapId", simagv::json::Value{mapId_});
        nodePos.emplace("mapDescription", simagv::json::Value{std::string("")});
        return nodePos;
    }

    simagv::json::Object makeNodeStateObjectLocked(const PlannedNode& node) const
    {
        simagv::json::Object nodeState;
        nodeState.emplace("nodeId", simagv::json::Value{node.nodeId});
        nodeState.emplace("sequenceId", simagv::json::Value{static_cast<double>(node.sequenceId)});
        nodeState.emplace("released", simagv::json::Value{node.released});
        nodeState.emplace("nodeDescription", simagv::json::Value{std::string("")});
        nodeState.emplace("nodePosition", simagv::json::Value{makeNodePositionObjectLocked(node)});
        nodeState.emplace("rotationAllowed", simagv::json::Value{false});
        simagv::json::Array actions;
        actions.reserve(node.actions.size());
        for (const simagv::json::Object& actionObj : node.actions) {
            actions.emplace_back(actionObj);
        }
        nodeState.emplace("actions", simagv::json::Value{std::move(actions)});
        return nodeState;
    }

    simagv::json::Object makeEdgeStateObjectLocked(const PlannedEdge& edge) const
    {
        simagv::json::Object edgeState;
        edgeState.emplace("edgeId", simagv::json::Value{edge.edgeId});
        edgeState.emplace("sequenceId", simagv::json::Value{static_cast<double>(edge.sequenceId)});
        edgeState.emplace("released", simagv::json::Value{edge.released});
        edgeState.emplace("edgeDescription", simagv::json::Value{std::string("")});
        edgeState.emplace("trajectory", simagv::json::Value{std::string("")});
        if (edge.maxSpeed.has_value()) {
            edgeState.emplace("maxSpeed", simagv::json::Value{static_cast<double>(edge.maxSpeed.value())});
        } else {
            edgeState.emplace("maxSpeed", simagv::json::Value{std::string("")});
        }
        edgeState.emplace("rotationAllowed", simagv::json::Value{edge.rotationAllowed});
        if (edge.maxRotationSpeed.has_value()) {
            edgeState.emplace("maxRotationSpeed", simagv::json::Value{static_cast<double>(edge.maxRotationSpeed.value())});
        } else {
            edgeState.emplace("maxRotationSpeed", simagv::json::Value{std::string("")});
        }
        edgeState.emplace("maxHeight", simagv::json::Value{std::string("")});
        edgeState.emplace("minHeight", simagv::json::Value{std::string("")});
        return edgeState;
    }

    simagv::json::Object makeStateObjectLocked() const
    {
        simagv::json::Object out;
        out.emplace(
            "driving",
            simagv::json::Value{(!paused_) && (!perceptionBlocked_) && (!perceptionCollision_) &&
                                ((openLoopMode_ != OpenLoopMode::None) || (driving_ && !navigationBlocked_))});
        out.emplace("operatingMode", simagv::json::Value{operatingMode_});
        simagv::json::Array nodeStates;
        simagv::json::Array edgeStates;
        if (navigationMode_ == NavigationMode::Station) {
            for (size_t i = activeItemIndex_; i < plannedNodes_.size(); ++i) {
                nodeStates.emplace_back(makeNodeStateObjectLocked(plannedNodes_[i]));
            }
        } else {
            for (size_t i = activeItemIndex_; i < plannedEdges_.size(); ++i) {
                const PlannedEdge& edge = plannedEdges_[i];
                edgeStates.emplace_back(makeEdgeStateObjectLocked(edge));
                if (i == activeItemIndex_) {
                    if (edge.startNodeIndex < plannedNodes_.size()) {
                        nodeStates.emplace_back(makeNodeStateObjectLocked(plannedNodes_[edge.startNodeIndex]));
                    }
                }
                if (edge.endNodeIndex < plannedNodes_.size()) {
                    nodeStates.emplace_back(makeNodeStateObjectLocked(plannedNodes_[edge.endNodeIndex]));
                }
            }
        }
        out.emplace("nodeStates", simagv::json::Value{std::move(nodeStates)});
        out.emplace("edgeStates", simagv::json::Value{std::move(edgeStates)});
        out.emplace("lastNodeId", simagv::json::Value{lastNodeId_});
        out.emplace("orderId", simagv::json::Value{currentOrderId_});
        out.emplace("orderUpdateId", simagv::json::Value{static_cast<double>(currentOrderUpdateId_)});
        out.emplace("lastNodeSequenceId", simagv::json::Value{static_cast<double>(lastNodeSequenceId_)});
        simagv::json::Array actionStates;
        if (hasActiveAction_) {
            simagv::json::Object actionState;
            actionState.emplace("actionId", simagv::json::Value{activeActionId_});
            actionState.emplace("actionStatus", simagv::json::Value{activeActionStatus_});
            actionState.emplace("actionType", simagv::json::Value{activeActionType_});
            actionState.emplace("actionDescription", simagv::json::Value{activeActionDescription_});
            actionState.emplace("resultDescription", simagv::json::Value{activeResultDescription_});
            actionStates.emplace_back(std::move(actionState));
        }
        out.emplace("actionStates", simagv::json::Value{std::move(actionStates)});

        simagv::json::Array information;
        if (!perceptionBlockedBy_.empty() || !perceptionCollidedWith_.empty()) {
            auto join = [](const std::vector<simagv::l2::PerceptionContact>& items, size_t limit) -> std::string {
                std::ostringstream oss;
                const size_t n = items.size();
                const size_t take = std::min(n, limit);
                for (size_t i = 0; i < take; ++i) {
                    if (i > 0) {
                        oss << ",";
                    }
                    oss << items[i].manufacturer << "/" << items[i].serialNumber;
                }
                if (n > take) {
                    oss << "...";
                }
                return oss.str();
            };
            std::ostringstream desc;
            desc << "perception blockedBy=" << join(perceptionBlockedBy_, 3) << " collidedWith=" << join(perceptionCollidedWith_, 3);
            simagv::json::Object info;
            info.emplace("infoType", simagv::json::Value{std::string("INFO")});
            info.emplace("infoDescription", simagv::json::Value{desc.str()});
            info.emplace("infoLevel", simagv::json::Value{std::string("INFO")});
            info.emplace("infoReferences", simagv::json::Value{simagv::json::Array{}});
            information.emplace_back(std::move(info));
        }
        if (forkHeightM_ > 1e-6F && activeActionType_ != "JackUnload") {
            simagv::json::Object di;
            di.emplace("infoType", simagv::json::Value{std::string("DI")});
            simagv::json::Array diRefs;
            simagv::json::Object diRef;
            diRef.emplace("referenceKey", simagv::json::Value{std::string("DI")});
            simagv::json::Array diValues;
            simagv::json::Object diValue;
            diValue.emplace("id", simagv::json::Value{0.0});
            diValue.emplace("source", simagv::json::Value{std::string("normal")});
            diValue.emplace("status", simagv::json::Value{forkHeightM_ > 1e-6F});
            diValue.emplace("valid", simagv::json::Value{true});
            diValues.emplace_back(std::move(diValue));
            diRef.emplace("referenceValue", simagv::json::Value{std::move(diValues)});
            diRefs.emplace_back(std::move(diRef));
            di.emplace("infoReferences", simagv::json::Value{std::move(diRefs)});
            di.emplace("infoDescription", simagv::json::Value{std::string("info of DI")});
            di.emplace("infoLevel", simagv::json::Value{std::string("INFO")});
            information.emplace_back(std::move(di));

            simagv::json::Object dOut;
            dOut.emplace("infoType", simagv::json::Value{std::string("DO")});
            simagv::json::Array doRefs;
            simagv::json::Object doRef;
            doRef.emplace("referenceKey", simagv::json::Value{std::string("DO")});
            simagv::json::Array doValues;
            simagv::json::Object doValue;
            doValue.emplace("id", simagv::json::Value{0.0});
            doValue.emplace("status", simagv::json::Value{liftTotalMs_ != 0U});
            doValues.emplace_back(std::move(doValue));
            doRef.emplace("referenceValue", simagv::json::Value{std::move(doValues)});
            doRefs.emplace_back(std::move(doRef));
            dOut.emplace("infoReferences", simagv::json::Value{std::move(doRefs)});
            dOut.emplace("infoDescription", simagv::json::Value{std::string("info of DO")});
            dOut.emplace("infoLevel", simagv::json::Value{std::string("INFO")});
            information.emplace_back(std::move(dOut));
        }
        out.emplace("information", simagv::json::Value{std::move(information)});
        if (hasLoad_) {
            simagv::json::Object load;
            load.emplace("loadId", simagv::json::Value{hasLoadModel_ ? loadModel_.loadId : std::string("-1")});
            load.emplace("loadType", simagv::json::Value{hasLoadModel_ ? loadModel_.loadType : std::string("6")});
            load.emplace("loadPosition", simagv::json::Value{std::string("")});
            simagv::json::Object box;
            box.emplace("x", simagv::json::Value{poseX_});
            box.emplace("y", simagv::json::Value{poseY_});
            box.emplace("z", simagv::json::Value{forkHeightM_});
            box.emplace("theta", simagv::json::Value{0.0});
            load.emplace("boundingBoxReference", simagv::json::Value{std::move(box)});
            simagv::json::Object dims;
            dims.emplace("length", simagv::json::Value{hasLoadModel_ ? loadModel_.dimensions.length : 1.2F});
            dims.emplace("width", simagv::json::Value{hasLoadModel_ ? loadModel_.dimensions.width : 0.9F});
            dims.emplace("height", simagv::json::Value{hasLoadModel_ ? loadModel_.dimensions.height : 0.15F});
            load.emplace("loadDimensions", simagv::json::Value{std::move(dims)});
            load.emplace("weight", simagv::json::Value{hasLoadModel_ ? loadModel_.weightKg : 0.0F});
            out.emplace("loads", simagv::json::Value{simagv::json::Array{simagv::json::Value{std::move(load)}}});
        } else {
            out.emplace("loads", simagv::json::Value{simagv::json::Array{}});
        }

        simagv::json::Object battery;
        const double level = static_cast<double>(batteryChargeLevel_);
        const double voltage = 24.0 * (0.8 + 0.2 * (level / 100.0));
        battery.emplace("batteryCharge", simagv::json::Value{level});
        battery.emplace("batteryVoltage", simagv::json::Value{voltage});
        battery.emplace("batteryHealth", simagv::json::Value{100.0});
        battery.emplace("charging", simagv::json::Value{batteryCharging_});
        battery.emplace("reach", simagv::json::Value{0.0});
        out.emplace("batteryState", simagv::json::Value{battery});

        simagv::json::Object safetyState;
        safetyState.emplace("eStop", simagv::json::Value{std::string("NONE")});
        safetyState.emplace("fieldViolation", simagv::json::Value{false});
        out.emplace("safetyState", simagv::json::Value{safetyState});

        out.emplace("paused", simagv::json::Value{paused_});
        out.emplace("newBaseRequest", simagv::json::Value{false});

        simagv::json::Object agvPos;
        agvPos.emplace("x", simagv::json::Value{poseX_});
        agvPos.emplace("y", simagv::json::Value{poseY_});
        agvPos.emplace("theta", simagv::json::Value{poseTheta_});
        agvPos.emplace("mapId", simagv::json::Value{mapId_});
        agvPos.emplace("mapDescription", simagv::json::Value{std::string("")});
        agvPos.emplace("positionInitialized", simagv::json::Value{true});
        agvPos.emplace("localizationScore", simagv::json::Value{0.99});
        agvPos.emplace("deviationRange", simagv::json::Value{0.0});
        out.emplace("agvPosition", simagv::json::Value{agvPos});

        simagv::json::Object vel;
        vel.emplace("vx", simagv::json::Value{velVx_});
        vel.emplace("vy", simagv::json::Value{velVy_});
        vel.emplace("omega", simagv::json::Value{velOmega_});
        out.emplace("velocity", simagv::json::Value{vel});

        out.emplace("zoneSetId", simagv::json::Value{mapId_});
        out.emplace("waitingForInteractionZoneRelease", simagv::json::Value{false});

        simagv::json::Object fork;
        fork.emplace("forkHeight", simagv::json::Value{forkHeightM_});
        out.emplace("forkState", simagv::json::Value{fork});

        out.emplace("errors", simagv::json::Value{errorManager_.buildVdaErrorsArray()});
        return out;
    }

    simagv::json::Object makeVisualizationObjectLocked() const
    {
        simagv::json::Object out = makeStateObjectLocked();

        simagv::json::Object safety;
        simagv::json::Object center;
        center.emplace("x", simagv::json::Value{poseX_});
        center.emplace("y", simagv::json::Value{poseY_});
        safety.emplace("center", simagv::json::Value{center});
        const float vehicleLength = static_cast<float>(config_.physicalParameters.length);
        const float vehicleWidth = static_cast<float>(config_.physicalParameters.width);
        float loadLength = 0.0f;
        float loadWidth = 0.0f;
        if (hasLoad_ && hasLoadModel_) {
            const float fpLength = static_cast<float>(loadModel_.footprint.lengthM);
            const float fpWidth = static_cast<float>(loadModel_.footprint.widthM);
            if (fpLength > 1e-6f && fpWidth > 1e-6f) {
                loadLength = fpLength;
                loadWidth = fpWidth;
            } else {
                loadLength = static_cast<float>(loadModel_.dimensions.length);
                loadWidth = static_cast<float>(loadModel_.dimensions.width);
            }
        }
        const float safetyBaseLength = std::max(vehicleLength, loadLength);
        const float safetyBaseWidth = std::max(vehicleWidth, loadWidth);
        safety.emplace("length", simagv::json::Value{static_cast<double>(safetyBaseLength * safetyScale_)});
        safety.emplace("width", simagv::json::Value{static_cast<double>(safetyBaseWidth * safetyScale_)});
        safety.emplace("theta", simagv::json::Value{poseTheta_});
        out.emplace("safety", simagv::json::Value{safety});

        simagv::json::Object radar;
        constexpr float kPi = 3.14159265358979323846f;
        const float halfLength = std::max(0.0f, vehicleLength * 0.5f);
        const float forwardOffset = (halfLength > 0.1f) ? (halfLength - 0.1f) : 0.0f;
        const float headTheta = poseTheta_ + kPi;
        const float radarOx = poseX_ + std::cos(headTheta) * forwardOffset;
        const float radarOy = poseY_ + std::sin(headTheta) * forwardOffset;
        simagv::json::Object origin;
        origin.emplace("x", simagv::json::Value{radarOx});
        origin.emplace("y", simagv::json::Value{radarOy});
        radar.emplace("origin", simagv::json::Value{origin});
        radar.emplace("theta", simagv::json::Value{poseTheta_});
        radar.emplace("fovDeg", simagv::json::Value{radarFovDeg_});
        radar.emplace("radius", simagv::json::Value{radarRadiusM_});
        out.emplace("radar", simagv::json::Value{radar});

        simagv::json::Object overlaps;
        simagv::json::Array radarOverlaps;
        radarOverlaps.reserve(perceptionBlockedBy_.size());
        for (const auto& item : perceptionBlockedBy_) {
            simagv::json::Object obj;
            obj.emplace("manufacturer", simagv::json::Value{item.manufacturer});
            obj.emplace("serialNumber", simagv::json::Value{item.serialNumber});
            obj.emplace("with", simagv::json::Value{item.manufacturer + std::string("/") + item.serialNumber});
            radarOverlaps.emplace_back(simagv::json::Value{std::move(obj)});
        }
        simagv::json::Array safetyOverlaps;
        safetyOverlaps.reserve(perceptionCollidedWith_.size());
        for (const auto& item : perceptionCollidedWith_) {
            simagv::json::Object obj;
            obj.emplace("manufacturer", simagv::json::Value{item.manufacturer});
            obj.emplace("serialNumber", simagv::json::Value{item.serialNumber});
            obj.emplace("with", simagv::json::Value{item.manufacturer + std::string("/") + item.serialNumber});
            safetyOverlaps.emplace_back(simagv::json::Value{std::move(obj)});
        }
        overlaps.emplace("radar", simagv::json::Value{std::move(radarOverlaps)});
        overlaps.emplace("safety", simagv::json::Value{std::move(safetyOverlaps)});
        out.emplace("overlaps", simagv::json::Value{overlaps});
        return out;
    }

    simagv::json::Object makeConnectionObjectLocked() const
    {
        simagv::json::Object out;
        out.emplace("connectionState", simagv::json::Value{std::string("ONLINE")});
        return out;
    }

    simagv::json::Object makeFactsheetObjectLocked() const
    {
        simagv::json::Object out;
        simagv::json::Object typeSpec;
        typeSpec.emplace("seriesName", simagv::json::Value{config_.vehicle.serialNumber});
        typeSpec.emplace("seriesDescription", simagv::json::Value{config_.vehicle.serialNumber});
        typeSpec.emplace("agvKinematic", simagv::json::Value{std::string("STEER")});
        typeSpec.emplace("agvClass", simagv::json::Value{std::string("FORKLIFT")});
        typeSpec.emplace("maxLoadMass", simagv::json::Value{0.5});
        typeSpec.emplace("localizationTypes", simagv::json::Value{simagv::json::Array{simagv::json::Value{std::string("SLAM")}}});
        typeSpec.emplace(
            "navigationTypes", simagv::json::Value{simagv::json::Array{simagv::json::Value{std::string("VIRTUAL_LINE_GUIDED")}}});
        out.emplace("typeSpecification", simagv::json::Value{typeSpec});

        simagv::json::Object pp;
        pp.emplace("speedMin", simagv::json::Value{config_.physicalParameters.speedMin});
        pp.emplace("speedMax", simagv::json::Value{config_.physicalParameters.speedMax});
        pp.emplace("accelerationMax", simagv::json::Value{config_.physicalParameters.accelerationMax});
        pp.emplace("decelerationMax", simagv::json::Value{config_.physicalParameters.decelerationMax});
        pp.emplace("heightMin", simagv::json::Value{config_.physicalParameters.heightMin});
        pp.emplace("heightMax", simagv::json::Value{config_.physicalParameters.heightMax});
        pp.emplace("width", simagv::json::Value{config_.physicalParameters.width});
        pp.emplace("length", simagv::json::Value{config_.physicalParameters.length});
        out.emplace("physicalParameters", simagv::json::Value{pp});

        out.emplace("protocolLimits", simagv::json::Value{simagv::json::Object{}});
        out.emplace("protocolFeatures", simagv::json::Value{simagv::json::Object{}});
        out.emplace("agvGeometry", simagv::json::Value{simagv::json::Object{}});
        out.emplace("loadSpecification", simagv::json::Value{simagv::json::Object{}});
        out.emplace("localizationParameters", simagv::json::Value{simagv::json::Object{}});
        out.emplace("vehicleConfig", simagv::json::Value{simagv::json::Object{}});
        return out;
    }
};

bool StubSimulatorEngine::isChargingPointNodeId(std::string_view nodeId)
{
    if (nodeId.size() < 2U) {
        return false;
    }
    const char c0 = static_cast<char>(std::toupper(static_cast<unsigned char>(nodeId[0])));
    const char c1 = static_cast<char>(std::toupper(static_cast<unsigned char>(nodeId[1])));
    return c0 == 'C' && c1 == 'P';
}

bool StubSimulatorEngine::isStoppedAtNodeLocked() const
{
    if (openLoopMode_ != OpenLoopMode::None) {
        return false;
    }
    if (!driving_) {
        return true;
    }
    if (waitingForHardAction_ || navigationBlocked_) {
        return true;
    }
    if (activePath_.empty() || activePathKind_ == ActivePathKind::None) {
        return true;
    }
    return false;
}

bool StubSimulatorEngine::isStoppedAtChargingPointLocked() const
{
    return isChargingPointNodeId(lastNodeId_) && isStoppedAtNodeLocked();
}

void StubSimulatorEngine::refreshChargingStateLocked()
{
    const bool atChargingPoint = isStoppedAtChargingPointLocked();
    const bool allowedByMode = automaticCharging_ ? true : chargingCommanded_;
    batteryCharging_ = atChargingPoint && allowedByMode;
}

void runInputLoop(
    std::atomic<bool>& stopFlag,
    simagv::l1::MqttEntry& mqttEntry,
    simagv::l1::HttpEntry& httpEntry)
{
    std::string line;
    while (!stopFlag.load()) {
        if (!std::getline(std::cin, line)) {
            stopFlag.store(true);
            return;
        }
        if (line.empty()) {
            continue;
        }
        if (line == "quit") {
            stopFlag.store(true);
            return;
        }

        std::istringstream iss(line);
        std::string mode;
        iss >> mode;
        if (mode == "mqtt") {
            std::string topic;
            iss >> topic;
            const std::string payload = readRemainder(iss);
            const simagv::l1::EntryAck ack = mqttEntry.handleMessage(topic, payload);
            std::ostringstream oss;
            oss << "stdin_ack ok=" << (ack.ok ? "true" : "false") << " topicType=" << static_cast<int>(ack.topicType)
                << " headerId=" << ack.headerId << " message=" << ack.message;
            simagv::l4::logInfo(oss.str());
            continue;
        }
        if (mode == "http") {
            std::string path;
            iss >> path;
            const std::string body = readRemainder(iss);
            const simagv::l1::EntryAck ack = httpEntry.handleControlRequest(path, body);
            std::ostringstream oss;
            oss << "stdin_ack ok=" << (ack.ok ? "true" : "false") << " topicType=" << static_cast<int>(ack.topicType)
                << " headerId=" << ack.headerId << " message=" << ack.message;
            simagv::l4::logInfo(oss.str());
            continue;
        }

        simagv::l4::logWarn("stdin_unknown_command");
    }
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const RuntimeCliConfig cliConfig = parseArgs(argc, argv);
        const std::string configPath = resolveReadableFilePath(cliConfig.configPath);
        const simagv::l1::SimInstanceConfig fileConfig = simagv::l1::loadSimInstanceConfig(configPath);
        const std::string baseTopic = cliConfig.mqttBaseTopic.empty()
            ? simagv::l2::generateVdaMqttBaseTopic(
                  fileConfig.vehicle.vdaInterface,
                  fileConfig.vehicle.vdaVersion,
                  fileConfig.vehicle.manufacturer,
                  fileConfig.vehicle.serialNumber)
            : cliConfig.mqttBaseTopic;

        simagv::l2::RuntimeConfig initialConfig;
        initialConfig.simTimeScale = cliConfig.simTimeScale;
        initialConfig.stateFrequencyHz = cliConfig.stateFrequencyHz;
        initialConfig.visualizationFrequencyHz = cliConfig.visualizationFrequencyHz;
        initialConfig.connectionFrequencyHz = cliConfig.stateFrequencyHz;
        initialConfig.factsheetFrequencyHz = 0.0F;
        initialConfig.publishLimits.maxPublishHz = cliConfig.maxPublishHz;

        const std::string clientId = std::string("simagv_runtime_") + fileConfig.vehicle.serialNumber;
        {
            std::ostringstream oss;
            oss << "sim_start manufacturer=" << fileConfig.vehicle.manufacturer << " serialNumber=" << fileConfig.vehicle.serialNumber
                << " vdaInterface=" << fileConfig.vehicle.vdaInterface << " vdaVersion=" << fileConfig.vehicle.vdaVersion
                << " vdaFullVersion=" << fileConfig.vehicle.vdaFullVersion << " baseTopic=" << baseTopic
                << " mqttHost=" << fileConfig.mqttBroker.host << " mqttPort=" << fileConfig.mqttBroker.port
                << " mqttClientId=" << clientId << " tickMs=" << cliConfig.tickMs << " traceCapacity=" << cliConfig.traceCapacity
                << " simTimeScale=" << cliConfig.simTimeScale << " stateHz=" << cliConfig.stateFrequencyHz
                << " visHz=" << cliConfig.visualizationFrequencyHz << " maxPublishHz=" << cliConfig.maxPublishHz
                << " stdoutMqtt=" << (cliConfig.stdoutMqtt ? "true" : "false");
            simagv::l4::logInfo(oss.str());
        }
        std::unique_ptr<simagv::l2::IMqttDiplomat> mqttDiplomat;
        simagv::l1::MqttBrokerDiplomat* brokerDiplomat = nullptr;
        if (cliConfig.stdoutMqtt) {
            mqttDiplomat = std::make_unique<StdoutMqttDiplomat>();
        } else {
            auto broker = std::make_unique<simagv::l1::MqttBrokerDiplomat>(fileConfig.mqttBroker.host, fileConfig.mqttBroker.port, clientId, 60);
            brokerDiplomat = broker.get();
            mqttDiplomat = std::move(broker);
        }

        if (brokerDiplomat != nullptr) {
            simagv::json::Object willObj;
            willObj.emplace("headerId", simagv::json::Value{999999999.0});
            willObj.emplace("manufacturer", simagv::json::Value{fileConfig.vehicle.manufacturer});
            willObj.emplace("serialNumber", simagv::json::Value{fileConfig.vehicle.serialNumber});
            willObj.emplace("timestamp", simagv::json::Value{isoNow()});
            willObj.emplace("version", simagv::json::Value{fileConfig.vehicle.vdaFullVersion});
            willObj.emplace("connectionState", simagv::json::Value{std::string("CONNECTIONBROKEN")});
            brokerDiplomat->setWill(baseTopic + "/connection", simagv::l2::toJsonString(simagv::json::Value{willObj}), 0, false);
        }

        StubSimulatorEngine simulatorEngine("default", fileConfig);
        simagv::l2::SimInstanceCoordinator coordinator(
            simulatorEngine,
            *mqttDiplomat,
            baseTopic,
            fileConfig.vehicle.vdaFullVersion,
            fileConfig.vehicle.manufacturer,
            fileConfig.vehicle.serialNumber,
            initialConfig,
            cliConfig.traceCapacity);

        simagv::l1::MqttEntry mqttEntry(coordinator);
        simagv::l1::HttpEntry httpEntry(coordinator);

        if (brokerDiplomat != nullptr) {
            brokerDiplomat->setMessageHandler([&](std::string topic, std::string payload) {
                (void)mqttEntry.handleMessage(topic, payload);
            });

            if (!brokerDiplomat->connect()) {
                std::ostringstream oss;
                oss << "mqtt_connect_failed host=" << fileConfig.mqttBroker.host << " port=" << fileConfig.mqttBroker.port
                    << " clientId=" << clientId;
                simagv::l4::logWarn(oss.str());
            } else {
                (void)brokerDiplomat->subscribe(baseTopic + "/order", 0);
                (void)brokerDiplomat->subscribe(baseTopic + "/instantActions", 0);
                (void)brokerDiplomat->subscribe(baseTopic + "/simConfig", 0);
                (void)brokerDiplomat->subscribe(fileConfig.vehicle.vdaInterface + "/" + fileConfig.vehicle.vdaVersion + "/+/+/visualization", 0);
                std::ostringstream oss;
                oss << "mqtt_connected host=" << fileConfig.mqttBroker.host << " port=" << fileConfig.mqttBroker.port
                    << " clientId=" << clientId << " subscribe=" << baseTopic << "/{order,instantActions,simConfig}";
                simagv::l4::logInfo(oss.str());
            }
        }

        std::atomic<bool> stopFlag{false};
        std::thread inputThread([&] { runInputLoop(stopFlag, mqttEntry, httpEntry); });
        if (!configPath.empty()) {
            simagv::json::Object simConfigObj;
            std::string loadError;
            if (simagv::l1::tryLoadHotSimConfig(configPath, simConfigObj, loadError)) {
                coordinator.submitCommand(buildFileSimConfigIntent(configPath, std::move(simConfigObj)));
            }
        }

        while (!stopFlag.load()) {
            coordinator.tickOnce(cliConfig.tickMs);
            std::this_thread::sleep_for(std::chrono::milliseconds(cliConfig.tickMs));
        }

        if (inputThread.joinable()) {
            inputThread.join();
        }
        return 0;
    } catch (const std::exception& e) {
        simagv::l4::logError(std::string("fatal_exception err=") + e.what());
        return 1;
    } catch (...) {
        simagv::l4::logError("fatal_unknown");
        return 1;
    }
}
