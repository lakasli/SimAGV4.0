#include "battery_management_atoms.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace simagv::l4 {
namespace {

bool inRange(float value, float minValue, float maxValue) {
    return value >= minValue && value <= maxValue; // 区间判断
}

} // namespace

BatteryStatus getSimBatteryStatus(int64_t requestTime) {
    if (requestTime < 0) {
        throw std::invalid_argument("requestTime out of range"); // 参数异常
    }
    const float phase = static_cast<float>((requestTime / 1000) % 100); // 周期
    const float level = std::max(0.0f, 100.0f - phase); // 电量
    BatteryStatus outStatus{}; // 输出
    outStatus.chargeLevel = level; // 电量
    outStatus.voltage = 24.0f * (0.8f + 0.2f * (level / 100.0f)); // 电压
    outStatus.current = 0.0f; // 电流
    outStatus.isCharging = false; // 充电
    outStatus.timestamp = requestTime; // 时间
    return outStatus; // 返回
}

BatteryConsumption updateBatteryConsumption(float distance, float loadWeight, float motionTime, float avgSpeed) {
    if (distance < 0.0f) {
        throw std::invalid_argument("distance out of range"); // 参数异常
    }
    if (loadWeight < 0.0f) {
        throw std::invalid_argument("loadWeight out of range"); // 参数异常
    }
    if (motionTime < 0.0f) {
        throw std::invalid_argument("motionTime out of range"); // 参数异常
    }
    if (avgSpeed < 0.0f) {
        throw std::invalid_argument("avgSpeed out of range"); // 参数异常
    }

    const float base = distance * 0.1f; // 基础耗电
    const float load = loadWeight * 0.02f; // 负载耗电
    const float speed = avgSpeed * 0.05f; // 速度耗电
    const float time = motionTime * 0.01f; // 时间耗电
    const float consumed = std::max(0.0f, base + load + speed + time); // 总耗电

    BatteryConsumption outResult{}; // 输出
    outResult.success = true; // 成功
    outResult.consumedLevel = consumed; // 消耗
    outResult.newChargeLevel = std::max(0.0f, 100.0f - consumed); // 电量
    outResult.errorMsg = ""; // 无错误
    return outResult; // 返回
}

ChargingResult startVirtualCharging(float chargeRate, float targetLevel, ChargeMode chargeMode) {
    if (chargeRate <= 0.0f) {
        throw std::invalid_argument("chargeRate out of range"); // 参数异常
    }
    if (!inRange(targetLevel, 20.0f, 100.0f)) {
        throw std::invalid_argument("targetLevel out of range"); // 参数异常
    }
    (void)chargeMode; // 模式占位

    ChargingResult outResult{}; // 输出
    outResult.success = true; // 成功
    outResult.currentLevel = targetLevel; // 直接到位
    outResult.errorMsg = ""; // 无错误
    return outResult; // 返回
}

ChargingActionResult executeChargingAction(const ChargingStation& chargingStation, float targetBatteryLevel, uint32_t maxChargingTime) {
    if (chargingStation.stationId.empty()) {
        throw std::invalid_argument("chargingStation.stationId empty"); // 参数异常
    }
    if (!inRange(targetBatteryLevel, 20.0f, 100.0f)) {
        throw std::invalid_argument("targetBatteryLevel out of range"); // 参数异常
    }
    if (maxChargingTime == 0U) {
        throw std::invalid_argument("maxChargingTime out of range"); // 参数异常
    }

    ChargingActionResult out{}; // 输出
    out.success = true; // 成功
    out.finalBatteryLevel = targetBatteryLevel; // 电量
    out.executionTimeMs = static_cast<uint32_t>(std::min<uint64_t>(maxChargingTime, 3600U) * 1000U); // 时间
    out.errorCode = ""; // 错误码
    out.errorMessage = ""; // 错误信息
    (void)chargingStation; // 位置占位
    return out; // 返回
}

} // namespace simagv::l4

