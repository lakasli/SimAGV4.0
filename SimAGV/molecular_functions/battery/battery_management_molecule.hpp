#pragma once

#include "../common/l3_types.hpp"

namespace simagv::l3 {

/**
 * @brief 智能充放电管理分子 - 综合管理电池充放电过程
 *
 * @param [batteryContext] 电池上下文
 * @param [chargeStrategy] 充电策略
 * @param [powerConstraints] 功率约束
 * @return SmartBatteryResult 智能电池管理结果
 */
SmartBatteryResult manageSmartBatteryOperation(const BatteryContext& batteryContext, const ChargeStrategy& chargeStrategy, const PowerConstraints& powerConstraints);

/**
 * @brief 能耗优化管理分子 - 优化AGV能耗表现
 *
 * @param [taskProfile] 任务特征
 * @param [vehicleStatus] 车辆状态
 * @param [optimizationConfig] 优化配置
 * @return EnergyOptimizationResult 能耗优化结果
 */
EnergyOptimizationResult optimizeEnergyConsumption(const TaskProfile& taskProfile, const VehicleStatus& vehicleStatus, const OptimizationConfig& optimizationConfig);

} // namespace simagv::l3

