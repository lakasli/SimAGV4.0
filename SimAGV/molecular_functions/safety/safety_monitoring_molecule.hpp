#pragma once

#include "../common/l3_types.hpp"

namespace simagv::l3 {

/**
 * @brief 碰撞预防安全分子 - 多层次碰撞预防系统
 *
 * @param [vehiclePose] 当前车辆位姿
 * @param [environmentModel] 环境模型
 * @param [safetyConfiguration] 安全配置
 * @return CollisionPreventionResult 碰撞预防结果
 */
CollisionPreventionResult preventCollisions(const Pose2D& vehiclePose, const EnvironmentModel& environmentModel, const SafetyConfiguration& safetyConfiguration);

} // namespace simagv::l3

