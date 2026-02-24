#include "safety_monitoring_molecule.hpp"

#include <cmath>
#include <limits>

namespace simagv::l3 {
namespace {

float distance2D(const Point2D& p1, const Point2D& p2)
{
    const float dx = p1.x - p2.x; // X差值
    const float dy = p1.y - p2.y; // Y差值
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace

CollisionPreventionResult preventCollisions(const Pose2D& vehiclePose, const EnvironmentModel& environmentModel, const SafetyConfiguration& safetyConfiguration)
{
    CollisionPreventionResult result{}; // 返回结果
    result.safe = true;
    result.emergencyStopTriggered = false;

    float minDistance = std::numeric_limits<float>::infinity(); // 最小距离
    int minObstacleId = -1; // 最近障碍物ID

    for (const auto& obstacle : environmentModel.staticObstacles) {
        for (const auto& vertex : obstacle.polygon) {
            const float d = distance2D(vehiclePose.position, vertex); // 距离
            if (d < minDistance) {
                minDistance = d;
                minObstacleId = obstacle.id;
            }
        }
    }

    for (const auto& obstacle : environmentModel.dynamicObstacles) {
        const float d = distance2D(vehiclePose.position, obstacle.position) - obstacle.radiusM; // 距离
        if (d < minDistance) {
            minDistance = d;
            minObstacleId = obstacle.id;
        }
    }

    if (minDistance < safetyConfiguration.warningDistance) {
        CollisionWarning warning{}; // 警告
        warning.obstacleId = minObstacleId;
        warning.distanceM = minDistance;
        warning.message = "obstacle within warning distance";
        result.warnings.push_back(warning);
    }

    if (minDistance < safetyConfiguration.criticalDistance) {
        result.safe = false;
        if (safetyConfiguration.enableEmergencyStop) {
            result.emergencyStopTriggered = true;
        }
    }

    result.errorCode = "";
    result.errorMessage = "";
    (void)environmentModel.modelConfidence;
    (void)environmentModel.modelTimestamp;
    (void)safetyConfiguration.safetyMargin;
    (void)safetyConfiguration.enablePathReplanning;
    (void)safetyConfiguration.reactionTimeMs;
    (void)safetyConfiguration.safetyZones;
    return result;
}

} // namespace simagv::l3
