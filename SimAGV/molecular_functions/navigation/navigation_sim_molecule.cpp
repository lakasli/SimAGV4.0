#include "navigation_sim_molecule.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace simagv::l3 {
namespace {

int64_t nowMs()
{
    const auto nowTime = std::chrono::system_clock::now(); // 当前时间
    const auto nowMsTp = std::chrono::time_point_cast<std::chrono::milliseconds>(nowTime); // 毫秒时间点
    return nowMsTp.time_since_epoch().count(); // 毫秒时间戳
}

Position toPosition(const Pose2D& pose)
{
    Position pos{}; // 位置信息
    pos.x = pose.position.x;
    pos.y = pose.position.y;
    pos.z = 0.0F;
    return pos;
}

std::vector<Position> buildLinearPath(const Position& startPos, const Position& targetPos, uint32_t steps)
{
    std::vector<Position> pathPoints; // 路径点集合
    const uint32_t safeSteps = std::max<uint32_t>(steps, 2U); // 采样步数
    pathPoints.reserve(safeSteps);

    for (uint32_t i = 0U; i < safeSteps; ++i) {
        const float alpha = static_cast<float>(i) / static_cast<float>(safeSteps - 1U); // 插值系数
        Position stepPos{}; // 当前点
        stepPos.x = startPos.x + (targetPos.x - startPos.x) * alpha;
        stepPos.y = startPos.y + (targetPos.y - startPos.y) * alpha;
        stepPos.z = 0.0F;
        pathPoints.push_back(stepPos);
    }

    return pathPoints;
}

void validateNavigationConfig(const NavigationConfig& navigationConfig)
{
    if (navigationConfig.maxSpeed <= 0.0F) {
        throw std::invalid_argument("maxSpeed must be > 0");
    }
    if (navigationConfig.positionTolerance <= 0.0F) {
        throw std::invalid_argument("positionTolerance must be > 0");
    }
}

} // namespace

CompleteNavigationResult simulateCompleteNavigation(const Pose2D& startPose, const Pose2D& targetPose, const NavigationConfig& navigationConfig, const SafetyContext& safetyContext)
{
    (void)safetyContext;
    validateNavigationConfig(navigationConfig);

    const Position startPos = toPosition(startPose);   // 起点
    const Position targetPos = toPosition(targetPose); // 终点

    const float plannedDistance = simagv::l4::calculateDistance(startPose.position, targetPose.position); // 规划距离
    const uint32_t planSteps = navigationConfig.enablePathSmoothing ? 20U : 2U; // 规划采样步数
    const auto plannedPath = buildLinearPath(startPos, targetPos, planSteps); // 规划路径

    const int64_t motionStartMs = nowMs(); // 运动开始时间
    const auto motionResult = simagv::l4::simulateToPosition(targetPos, navigationConfig.maxSpeed, navigationConfig.positionTolerance); // 运动仿真结果
    const int64_t motionEndMs = nowMs(); // 运动结束时间

    CompleteNavigationResult result{}; // 返回结果
    result.success = motionResult.reached;
    result.currentPhase = motionResult.reached ? NavigationPhase::COMPLETED : NavigationPhase::FAILED;
    result.plannedPath = plannedPath;
    result.executedPath = motionResult.path;
    result.finalPose = targetPose;
    result.finalPose.position.x = motionResult.finalPosition.x;
    result.finalPose.position.y = motionResult.finalPosition.y;
    result.finalPose.timestamp = motionEndMs;
    result.totalDistance = motionResult.totalDistance;
    result.executionTimeMs = static_cast<uint32_t>(std::max<int64_t>(0, motionEndMs - motionStartMs));
    result.replanningCount = 0U;
    result.errorCode = motionResult.reached ? "" : "MOTION_NOT_REACHED";
    result.errorMessage = motionResult.reached ? "" : "target not reached";

    result.metrics.planningTimeMs = 0.0F;
    result.metrics.pathSmoothingTimeMs = navigationConfig.enablePathSmoothing ? 1.0F : 0.0F;
    result.metrics.totalMotionTimeMs = static_cast<float>(result.executionTimeMs);
    result.metrics.averageSpeed = (result.executionTimeMs > 0U) ? (result.totalDistance / (static_cast<float>(result.executionTimeMs) / 1000.0F)) : 0.0F;
    result.metrics.pathEfficiency = (plannedDistance > 0.0F) ? (result.totalDistance / plannedDistance) : 1.0F;
    result.metrics.collisionChecks = 0U;
    result.metrics.safetyRangeChecks = 0U;

    return result;
}

DynamicAvoidanceResult simulateNavigationWithAvoidance(const Pose2D& currentPose, const Pose2D& targetPose, const std::vector<DynamicObstacle>& dynamicObstacles, const AvoidanceConfig& avoidanceConfig)
{
    if (avoidanceConfig.criticalDistanceM <= 0.0F) {
        throw std::invalid_argument("criticalDistanceM must be > 0");
    }

    const Position startPos = toPosition(currentPose);  // 起点
    const Position targetPos = toPosition(targetPose);  // 终点

    float minObstacleDistance = std::numeric_limits<float>::infinity(); // 最近障碍物距离
    int minObstacleId = -1; // 最近障碍物ID
    for (const auto& obstacle : dynamicObstacles) {
        const float dx = obstacle.position.x - currentPose.position.x; // X距离
        const float dy = obstacle.position.y - currentPose.position.y; // Y距离
        const float distance = std::sqrt(dx * dx + dy * dy) - obstacle.radiusM; // 距离
        if (distance < minObstacleDistance) {
            minObstacleDistance = distance;
            minObstacleId = obstacle.id;
        }
    }

    DynamicAvoidanceResult result{}; // 返回结果
    result.plannedPath = buildLinearPath(startPos, targetPos, 10U);
    result.replanningCount = 0U;

    if (minObstacleDistance < avoidanceConfig.warningDistanceM) {
        CollisionWarning warning{}; // 警告信息
        warning.obstacleId = minObstacleId;
        warning.distanceM = minObstacleDistance;
        warning.message = "dynamic obstacle within warning distance";
        result.warnings.push_back(warning);
    }

    if (avoidanceConfig.enableReplanning && (minObstacleDistance < avoidanceConfig.criticalDistanceM)) {
        result.replanningCount = std::min<uint32_t>(avoidanceConfig.maxReplanningCount, 1U);
    }

    const auto motionResult = simagv::l4::simulateToPosition(targetPos, 1.0F, 0.05F); // 运动仿真结果
    result.executedPath = motionResult.path;
    result.success = motionResult.reached;
    result.errorCode = motionResult.reached ? "" : "MOTION_NOT_REACHED";
    result.errorMessage = motionResult.reached ? "" : "target not reached";

    return result;
}

} // namespace simagv::l3

