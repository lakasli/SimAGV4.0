#pragma once

#include "../common/l3_types.hpp"

namespace simagv::l3 {

/**
 * @brief 综合状态监控分子 - 实时监控AGV完整状态
 *
 * @param [monitoringConfig] 监控配置
 * @param [publishConfig] 发布配置
 * @param [vehicleId] 车辆ID
 * @return ComprehensiveStateReport 综合状态报告
 */
ComprehensiveStateReport monitorComprehensiveState(const MonitoringConfig& monitoringConfig, const PublishConfig& publishConfig, const std::string& vehicleId);

/**
 * @brief 异常检测与报告分子 - 智能检测系统异常
 *
 * @param [sensorData] 传感器数据
 * @param [stateHistory] 状态历史
 * @param [detectionThresholds] 检测阈值
 * @return AnomalyDetectionResult 异常检测结果
 */
AnomalyDetectionResult detectAndReportAnomalies(const SensorDataCollection& sensorData, const StateHistory& stateHistory, const DetectionThresholds& detectionThresholds);

} // namespace simagv::l3

