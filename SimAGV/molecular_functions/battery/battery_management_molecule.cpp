#include "battery_management_molecule.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace simagv::l3 {
namespace {

int64_t nowMs()
{
    const auto nowTime = std::chrono::system_clock::now(); // 当前时间
    const auto nowMsTp = std::chrono::time_point_cast<std::chrono::milliseconds>(nowTime); // 毫秒时间点
    return nowMsTp.time_since_epoch().count(); // 毫秒时间戳
}

} // namespace

SmartBatteryResult manageSmartBatteryOperation(const BatteryContext& batteryContext, const ChargeStrategy& chargeStrategy, const PowerConstraints& powerConstraints)
{
    (void)powerConstraints;
    if (batteryContext.vehicleId.empty()) {
        throw std::invalid_argument("vehicleId is empty");
    }
    if (chargeStrategy.targetChargeLevel <= 0.0F || chargeStrategy.targetChargeLevel > 100.0F) {
        throw std::invalid_argument("targetChargeLevel out of range");
    }

    const int64_t requestTimeMs = nowMs(); // 请求时间
    const auto simBattery = simagv::l4::getSimBatteryStatus(requestTimeMs); // 仿真电池状态

    SmartBatteryResult result{}; // 返回结果
    result.success = true;
    result.finalBatteryLevel = simBattery.chargeLevel;

    if (simBattery.chargeLevel <= chargeStrategy.lowBatteryThreshold) {
        const float chargeRateC = chargeStrategy.enableFastCharge ? 2.0F : 1.0F; // 充电倍率
        const auto chargeResult = simagv::l4::startVirtualCharging(chargeRateC, chargeStrategy.targetChargeLevel, chargeStrategy.preferredMode); // 充电结果
        if (!chargeResult.success) {
            result.success = false;
            result.errorCode = "CHARGE_FAILED";
            result.errorMessage = chargeResult.errorMsg;
            return result;
        }
        result.finalBatteryLevel = chargeResult.currentLevel;
    }

    result.errorCode = "";
    result.errorMessage = "";
    (void)batteryContext;
    return result;
}

EnergyOptimizationResult optimizeEnergyConsumption(const TaskProfile& taskProfile, const VehicleStatus& vehicleStatus, const OptimizationConfig& optimizationConfig)
{
    EnergyOptimizationResult result{}; // 返回结果
    result.success = true;

    const float baseSpeed = 1.0F; // 基础速度
    const float batteryFactor = std::clamp(vehicleStatus.batteryLevel / 100.0F, 0.2F, 1.0F); // 电量因子
    const float distanceFactor = (taskProfile.estimatedDistanceM > 10.0F) ? 0.8F : 1.0F; // 距离因子
    const float savingFactor = std::clamp(optimizationConfig.energySavingFactor, 0.0F, 1.0F); // 节能系数

    result.recommendedMaxSpeed = baseSpeed * batteryFactor * distanceFactor * (1.0F - 0.3F * savingFactor);
    result.suggestion = "reduce speed to save energy";
    (void)vehicleStatus.currentLoadKg;
    (void)taskProfile.estimatedLoadKg;
    return result;
}

} // namespace simagv::l3

