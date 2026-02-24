#include "task_execution_molecule.hpp"

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

bool isBlockingType(const std::string& blockingType)
{
    return (blockingType == "Hard") || (blockingType == "Soft");
}

simagv::l4::LiftActionType toLiftActionType(const std::string& actionType)
{
    if (actionType == "LIFT_UP") {
        return simagv::l4::LiftActionType::LIFT_UP;
    }
    if (actionType == "LIFT_DOWN") {
        return simagv::l4::LiftActionType::LIFT_DOWN;
    }
    if (actionType == "LIFT_HOME") {
        return simagv::l4::LiftActionType::LIFT_HOME;
    }
    return simagv::l4::LiftActionType::LIFT_STOP;
}

void executeNodeActions(const std::vector<ActionDefinition>& actions, uint32_t defaultTimeoutMs)
{
    for (const auto& action : actions) {
        if (action.actionType.rfind("LIFT_", 0) == 0) {
            const auto liftType = toLiftActionType(action.actionType); // 举升动作类型
            const float heightMm = action.params.count("targetHeightMm") ? std::stof(action.params.at("targetHeightMm")) : 0.0F; // 目标高度
            const float speedMmS = action.params.count("actionSpeedMmS") ? std::stof(action.params.at("actionSpeedMmS")) : 50.0F; // 动作速度
            const auto liftResult = simagv::l4::executeLiftAction(liftType, heightMm, speedMmS); // 举升结果
            if (!liftResult.success) {
                throw std::runtime_error("lift action failed: " + liftResult.errorMessage);
            }
            (void)defaultTimeoutMs;
        }
    }
}

} // namespace

CompleteTaskResult executeCompleteTask(const TaskDefinition& taskDefinition, const VehicleContext& vehicleContext, const ExecutionConfig& executionConfig)
{
    if (taskDefinition.taskId.empty()) {
        throw std::invalid_argument("taskId is empty");
    }
    if (executionConfig.maxSpeed <= 0.0F) {
        throw std::invalid_argument("maxSpeed must be > 0");
    }

    const int64_t startMs = nowMs(); // 执行开始时间
    std::vector<Position> fullPath;  // 全路径

    Position currentPos{}; // 当前位姿
    currentPos.x = vehicleContext.currentPose.position.x;
    currentPos.y = vehicleContext.currentPose.position.y;
    currentPos.z = 0.0F;

    for (const auto& node : taskDefinition.nodes) {
        const auto moveResult = simagv::l4::simulateToPosition(node.position, executionConfig.maxSpeed, executionConfig.positionTolerance); // 移动结果
        fullPath.insert(fullPath.end(), moveResult.path.begin(), moveResult.path.end());
        if (!moveResult.reached) {
            const int64_t endMs = nowMs(); // 执行结束时间
            CompleteTaskResult result{}; // 返回结果
            result.success = false;
            result.taskId = taskDefinition.taskId;
            result.path = std::move(fullPath);
            result.executionTimeMs = static_cast<uint32_t>(endMs - startMs);
            result.errorCode = "MOVE_FAILED";
            result.errorMessage = "node not reached: " + node.nodeId;
            return result;
        }
        currentPos = moveResult.finalPosition;
        executeNodeActions(node.actions, executionConfig.overallTimeoutMs);
        (void)currentPos;
    }

    const int64_t endMs = nowMs(); // 执行结束时间
    CompleteTaskResult result{}; // 返回结果
    result.success = true;
    result.taskId = taskDefinition.taskId;
    result.path = std::move(fullPath);
    result.executionTimeMs = static_cast<uint32_t>(endMs - startMs);
    result.errorCode = "";
    result.errorMessage = "";
    return result;
}

TransportTaskResult executeTransportTask(const TransportTaskDefinition& transportTask, const LoadSpecification& loadSpecification, const TransportConfig& transportConfig)
{
    (void)loadSpecification;
    if (transportTask.taskId.empty()) {
        throw std::invalid_argument("taskId is empty");
    }
    if (transportConfig.maxSpeed <= 0.0F) {
        throw std::invalid_argument("maxSpeed must be > 0");
    }

    const int64_t startMs = nowMs(); // 执行开始时间

    const auto toPickup = simagv::l4::simulateToPosition(transportTask.pickupPosition, transportConfig.maxSpeed, transportConfig.positionTolerance); // 到取货点
    if (!toPickup.reached) {
        const int64_t endMs = nowMs(); // 结束时间
        TransportTaskResult result{}; // 返回结果
        result.success = false;
        result.taskId = transportTask.taskId;
        result.executionTimeMs = static_cast<uint32_t>(endMs - startMs);
        result.errorCode = "PICKUP_NOT_REACHED";
        result.errorMessage = "pickup position not reached";
        return result;
    }

    const auto liftUp = simagv::l4::executeLiftAction(simagv::l4::LiftActionType::LIFT_UP, transportTask.targetLoadHeightMm, 50.0F); // 装载举升
    if (!liftUp.success) {
        const int64_t endMs = nowMs(); // 结束时间
        TransportTaskResult result{}; // 返回结果
        result.success = false;
        result.taskId = transportTask.taskId;
        result.executionTimeMs = static_cast<uint32_t>(endMs - startMs);
        result.errorCode = "LOAD_FAILED";
        result.errorMessage = liftUp.errorMessage;
        return result;
    }

    const auto toDropoff = simagv::l4::simulateToPosition(transportTask.dropoffPosition, transportConfig.maxSpeed, transportConfig.positionTolerance); // 到放货点
    if (!toDropoff.reached) {
        const int64_t endMs = nowMs(); // 结束时间
        TransportTaskResult result{}; // 返回结果
        result.success = false;
        result.taskId = transportTask.taskId;
        result.executionTimeMs = static_cast<uint32_t>(endMs - startMs);
        result.errorCode = "DROPOFF_NOT_REACHED";
        result.errorMessage = "dropoff position not reached";
        return result;
    }

    const auto liftDown = simagv::l4::executeLiftAction(simagv::l4::LiftActionType::LIFT_DOWN, 0.0F, 50.0F); // 卸载下降
    if (!liftDown.success) {
        const int64_t endMs = nowMs(); // 结束时间
        TransportTaskResult result{}; // 返回结果
        result.success = false;
        result.taskId = transportTask.taskId;
        result.executionTimeMs = static_cast<uint32_t>(endMs - startMs);
        result.errorCode = "UNLOAD_FAILED";
        result.errorMessage = liftDown.errorMessage;
        return result;
    }

    const int64_t endMs = nowMs(); // 执行结束时间
    TransportTaskResult result{}; // 返回结果
    result.success = true;
    result.taskId = transportTask.taskId;
    result.executionTimeMs = static_cast<uint32_t>(endMs - startMs);
    result.errorCode = "";
    result.errorMessage = "";
    (void)transportConfig.actionTimeoutMs;
    (void)isBlockingType;
    return result;
}

} // namespace simagv::l3

