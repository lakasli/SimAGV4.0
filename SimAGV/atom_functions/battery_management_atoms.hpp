#pragma once

#include "motion_control_atoms.hpp"

#include <cstdint>
#include <string>

namespace simagv::l4 {

struct BatteryStatus {
    float chargeLevel; // 电量百分比(0-100)
    float voltage;     // 电池电压(V)
    float current;     // 电池电流(A)
    bool isCharging;   // 是否正在充电
    int64_t timestamp; // 状态时间戳
};

struct BatteryConsumption {
    bool success;         // 计算是否成功
    float consumedLevel;  // 消耗电量百分比
    float newChargeLevel; // 更新后电量百分比
    std::string errorMsg; // 错误信息
};

enum class ChargeMode {
    CONSTANT_CURRENT, // 恒流充电
    SMART_CHARGE      // 智能充电
};

struct ChargingResult {
    bool success;         // 操作是否成功
    float currentLevel;   // 当前电量百分比
    std::string errorMsg; // 错误信息
};

struct ChargingStation {
    std::string stationId; // 充电站标识
    Position pos;          // 充电站位置
};

struct ChargingActionResult {
    bool success;             // 动作是否成功
    float finalBatteryLevel;  // 最终电量(0-100)
    uint32_t executionTimeMs; // 执行时间(ms)
    std::string errorCode;    // 错误代码
    std::string errorMessage; // 错误信息
};

/**
 * @brief 获取仿真电池状态 - 获取当前电池电量和状态
 *
 * 基于请求时间生成确定性电池状态快照
 *
 * @param [requestTime] 请求时间戳
 * @return BatteryStatus 电池状态数据
 * @throws std::invalid_argument 时间戳异常
 */
BatteryStatus getSimBatteryStatus(int64_t requestTime);

/**
 * @brief 更新电池消耗 - 根据运动状态计算电池消耗
 *
 * 按配置的线性模型计算消耗量
 *
 * @param [distance] 运动距离(m)
 * @param [loadWeight] 负载重量(kg)
 * @param [motionTime] 运动时间(s)
 * @param [avgSpeed] 平均速度(m/s)
 * @return BatteryConsumption 电池消耗结果
 * @throws std::invalid_argument 参数异常
 */
BatteryConsumption updateBatteryConsumption(float distance, float loadWeight, float motionTime, float avgSpeed);

/**
 * @brief 开始虚拟充电 - 启动电池充电过程
 *
 * 以固定速率推进电量到目标值
 *
 * @param [chargeRate] 充电速率(C)
 * @param [targetLevel] 目标电量百分比
 * @param [chargeMode] 充电模式
 * @return ChargingResult 充电操作结果
 * @throws std::invalid_argument 参数异常
 */
ChargingResult startVirtualCharging(float chargeRate, float targetLevel, ChargeMode chargeMode);

/**
 * @brief 执行充电动作 - 控制AGV执行自动充电
 *
 * @param [chargingStation] 充电站标识和位置信息
 * @param [targetBatteryLevel] 目标充电电量(20-100)
 * @param [maxChargingTime] 最大充电时间(秒)
 * @return ChargingActionResult 充电动作结果
 * @throws std::invalid_argument 参数异常
 */
ChargingActionResult executeChargingAction(const ChargingStation& chargingStation, float targetBatteryLevel, uint32_t maxChargingTime);

} // namespace simagv::l4

