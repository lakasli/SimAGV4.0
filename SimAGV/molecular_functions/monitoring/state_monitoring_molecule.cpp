#include "state_monitoring_molecule.hpp"

#include <cmath>
#include <chrono>

namespace simagv::l3 {
namespace {

int64_t nowMs()
{
    const auto nowTime = std::chrono::system_clock::now(); // 当前时间
    const auto nowMsTp = std::chrono::time_point_cast<std::chrono::milliseconds>(nowTime); // 毫秒时间点
    return nowMsTp.time_since_epoch().count(); // 毫秒时间戳
}

} // namespace

ComprehensiveStateReport monitorComprehensiveState(const MonitoringConfig& monitoringConfig, const PublishConfig& publishConfig, const std::string& vehicleId)
{
    (void)publishConfig;
    (void)vehicleId;

    const int64_t requestTimeMs = nowMs(); // 请求时间
    const auto odomData = monitoringConfig.enablePositionMonitoring ? simagv::l4::getOdometryData(requestTimeMs) : simagv::l4::OdometryData{}; // 里程计数据
    const auto batteryData = monitoringConfig.enableBatteryMonitoring ? simagv::l4::getSimBatteryStatus(requestTimeMs) : simagv::l4::BatteryStatus{}; // 电池数据

    ComprehensiveStateReport report{}; // 状态报告
    report.protocolVersion = publishConfig.protocolVersion;
    report.currentPose.position.x = odomData.position.x;
    report.currentPose.position.y = odomData.position.y;
    report.currentPose.heading = odomData.heading;
    report.currentPose.timestamp = odomData.timestamp;
    report.batteryLevel = batteryData.chargeLevel;
    report.vehicleState = VehicleState::IDLE;
    report.timestamp = requestTimeMs;
    (void)monitoringConfig.reportTimeoutMs;
    (void)monitoringConfig.monitoredTopics;
    return report;
}

AnomalyDetectionResult detectAndReportAnomalies(const SensorDataCollection& sensorData, const StateHistory& stateHistory, const DetectionThresholds& detectionThresholds)
{
    (void)sensorData;
    AnomalyDetectionResult result{}; // 返回结果
    result.hasAnomaly = false;

    if (!stateHistory.reports.empty()) {
        const auto& latest = stateHistory.reports.back(); // 最新报告
        if (latest.batteryLevel < detectionThresholds.lowBatteryThreshold) {
            AnomalyItem item{}; // 异常项
            item.code = "LOW_BATTERY";
            item.level = "WARNING";
            item.message = "battery level below threshold";
            result.items.push_back(item);
            result.hasAnomaly = true;
        }
    }

    if (stateHistory.reports.size() >= 2U) {
        const auto& prev = stateHistory.reports[stateHistory.reports.size() - 2U]; // 前一报告
        const auto& latest = stateHistory.reports.back(); // 最新报告
        const float dx = latest.currentPose.position.x - prev.currentPose.position.x; // X差值
        const float dy = latest.currentPose.position.y - prev.currentPose.position.y; // Y差值
        const float dist = std::sqrt(dx * dx + dy * dy); // 位移
        if (dist > detectionThresholds.poseJumpDistanceM) {
            AnomalyItem item{}; // 异常项
            item.code = "POSE_JUMP";
            item.level = "ERROR";
            item.message = "pose jump exceeds threshold";
            result.items.push_back(item);
            result.hasAnomaly = true;
        }
    }

    return result;
}

} // namespace simagv::l3
