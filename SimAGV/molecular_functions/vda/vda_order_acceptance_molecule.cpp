#include "vda_order_acceptance_molecule.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <unordered_set>

namespace simagv::l3 {
namespace {

int64_t nowMs()
{
    const auto nowTime = std::chrono::system_clock::now(); // 当前时间
    const auto nowMsTp = std::chrono::time_point_cast<std::chrono::milliseconds>(nowTime); // 毫秒时间点
    return nowMsTp.time_since_epoch().count(); // 毫秒时间戳
}

bool hasCapability(const std::vector<std::string>& capabilities, const std::string& actionType)
{
    return std::find(capabilities.begin(), capabilities.end(), actionType) != capabilities.end();
}

ActionDefinition toActionDefinition(const VdaAction& vdaAction)
{
    ActionDefinition action{}; // 动作定义
    action.actionType = vdaAction.actionType;
    action.timeoutMs = 0U;
    action.blocking = (vdaAction.blockingType == "Hard");
    for (const auto& param : vdaAction.parameters) {
        action.params[param.key] = param.value;
    }
    return action;
}

bool isStrictSequenceValid(const std::vector<uint32_t>& sequenceIds)
{
    if (sequenceIds.empty()) {
        return true;
    }
    for (size_t i = 1U; i < sequenceIds.size(); ++i) {
        if (sequenceIds[i] <= sequenceIds[i - 1U]) {
            return false;
        }
    }
    return true;
}

} // namespace

VdaOrderAcceptanceResult acceptVdaOrder(const VdaOrder& order, const VehicleContext& vehicleContext, const OrderAcceptanceConfig& acceptConfig)
{
    VdaOrderAcceptanceResult result{}; // 返回结果
    result.accepted = false;

    if (order.orderId.empty()) {
        result.rejectCode = "INVALID_ORDER_ID";
        result.rejectMessage = "orderId is empty";
        return result;
    }
    if (order.nodes.size() > acceptConfig.maxNodeCount || order.edges.size() > acceptConfig.maxEdgeCount) {
        result.rejectCode = "ORDER_TOO_LARGE";
        result.rejectMessage = "order node/edge count exceeds limit";
        return result;
    }

    std::vector<uint32_t> nodeSeq; // 节点序号
    std::vector<uint32_t> edgeSeq; // 边序号
    nodeSeq.reserve(order.nodes.size());
    edgeSeq.reserve(order.edges.size());
    for (const auto& node : order.nodes) {
        nodeSeq.push_back(node.sequenceId);
    }
    for (const auto& edge : order.edges) {
        edgeSeq.push_back(edge.sequenceId);
    }

    if (acceptConfig.enableStrictSequenceCheck) {
        if (!isStrictSequenceValid(nodeSeq) || !isStrictSequenceValid(edgeSeq)) {
            result.rejectCode = "INVALID_SEQUENCE";
            result.rejectMessage = "sequenceId must be strictly increasing";
            return result;
        }
    }

    TaskDefinition task{}; // 任务定义
    task.taskId = order.orderId;
    task.taskType = "VDA_ORDER";
    task.createTime = nowMs();
    task.expireTime = 0;
    task.priority = 0U;

    std::unordered_set<std::string> nodeIds; // 节点集合
    for (const auto& node : order.nodes) {
        if (node.nodeId.empty()) {
            result.rejectCode = "INVALID_NODE_ID";
            result.rejectMessage = "nodeId is empty";
            return result;
        }
        nodeIds.insert(node.nodeId);

        TaskNode taskNode{}; // 任务节点
        taskNode.nodeId = node.nodeId;
        taskNode.nodeType = "VDA_NODE";
        taskNode.position = Position{0.0F, 0.0F, 0.0F};
        for (const auto& vdaAction : node.actions) {
            if (acceptConfig.enableCapabilityCheck && !hasCapability(vehicleContext.capabilities, vdaAction.actionType)) {
                result.rejectCode = "CAPABILITY_MISMATCH";
                result.rejectMessage = "unsupported action: " + vdaAction.actionType;
                return result;
            }
            taskNode.actions.push_back(toActionDefinition(vdaAction));
        }
        task.nodes.push_back(taskNode);
    }

    for (const auto& edge : order.edges) {
        if (edge.edgeId.empty()) {
            result.rejectCode = "INVALID_EDGE_ID";
            result.rejectMessage = "edgeId is empty";
            return result;
        }
        if (nodeIds.count(edge.startNodeId) == 0U || nodeIds.count(edge.endNodeId) == 0U) {
            result.rejectCode = "INVALID_EDGE_LINK";
            result.rejectMessage = "edge references unknown nodes";
            return result;
        }

        TaskEdge taskEdge{}; // 任务边
        taskEdge.edgeId = edge.edgeId;
        taskEdge.fromNodeId = edge.startNodeId;
        taskEdge.toNodeId = edge.endNodeId;
        task.edges.push_back(taskEdge);
        (void)edge.released;
        (void)edge.actions;
    }

    result.accepted = true;
    result.task = std::move(task);
    result.rejectCode = "";
    result.rejectMessage = "";
    (void)order.orderUpdateId;
    (void)vehicleContext;
    return result;
}

} // namespace simagv::l3

