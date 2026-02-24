#pragma once

#include "../common/l3_types.hpp"

namespace simagv::l3 {

/**
 * @brief 完整导航仿真分子 - 组合定位、路径规划、运动仿真
 *
 * @param [startPose] 起始位姿
 * @param [targetPose] 目标位姿
 * @param [navigationConfig] 导航配置
 * @param [safetyContext] 安全上下文
 * @return CompleteNavigationResult 完整导航执行结果
 */
CompleteNavigationResult simulateCompleteNavigation(const Pose2D& startPose, const Pose2D& targetPose, const NavigationConfig& navigationConfig, const SafetyContext& safetyContext);

/**
 * @brief 动态避障导航分子 - 实时检测并避开动态障碍物
 *
 * @param [currentPose] 当前位姿
 * @param [targetPose] 目标位姿
 * @param [dynamicObstacles] 动态障碍物列表
 * @param [avoidanceConfig] 避障配置
 * @return DynamicAvoidanceResult 动态避障结果
 */
DynamicAvoidanceResult simulateNavigationWithAvoidance(const Pose2D& currentPose, const Pose2D& targetPose, const std::vector<DynamicObstacle>& dynamicObstacles, const AvoidanceConfig& avoidanceConfig);

} // namespace simagv::l3

