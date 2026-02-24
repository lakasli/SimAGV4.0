#include "error_management_atoms.hpp"

#include <algorithm>

namespace simagv::l4 {

const SimErrorManager::ErrorDefinition* SimErrorManager::findDefinition(int32_t code)
{
    static constexpr ErrorDefinition kErrorDefinitions[] = {
        {50401, "Battery", "FATAL", "battery too low and shutdown"},
        {52503, "MovementDenied", "FATAL", "battery is too low to move"},
        {54211, "Battery", "WARNING", "low battery"},
        {52700, "PathPlanning", "FATAL", "can not find a feasible path"},
        {52701, "PathPlanning", "FATAL", "can not find target id"},
        {52702, "PathPlanning", "FATAL", "path plan failed"},
        {52705, "PathPlanning", "FATAL", "motion planning failed"},
        {52708, "PathPlanning", "FATAL", "destination has obstacles"},
        {52715, "PathPlanning", "FATAL", "current pos has obstacles"},
        {52201, "PathPlanning", "FATAL", "robot out of path"},
        {54231, "Navigation", "WARNING", "Caution: robot is blocked"},
        {54330, "Navigation", "WARNING", "radar blocked by other vehicle"},
        {54331, "Collision", "FATAL", "collision detected"},
        {50100, "Map", "FATAL", "map parse error"},
        {50101, "Map", "FATAL", "map load error"},
        {50102, "Map", "FATAL", "map is too large"},
        {50103, "Map", "FATAL", "map is empty"},
        {50104, "Map", "FATAL", "map meta error"},
        {50105, "Map", "FATAL", "map resolution is illegal"},
        {50106, "Map", "FATAL", "map format invalid"},
        {52015, "Map", "FATAL", "stations with the same id number in the map"},
        {52107, "Map", "FATAL", "Switch map error in current station point"},
        {90000, "Common", "WARNING", "any - 任意错误"},
        {90001, "Common", "WARNING", "operationMode is not AUTOMATIC - 操作模式不是自动模式"},
        {90002, "Common", "WARNING", "send Order to rbk Failed - 向rbk发送订单失败"},
        {90003, "Common", "WARNING", "new OrderId But NotLock - 收到新订单ID但未锁定"},
        {90004, "Common", "WARNING", "edge.endNodeId != NodeId - 边的结束节点ID与当前节点ID不匹配"},
        {90005, "Common", "WARNING", "edge.startNodeId != NodeId - 边的起始节点ID与当前节点ID不匹配"},
        {90006, "Common", "WARNING", "newOrderId rec,But Order is Running - 收到新订单ID，但订单正在运行中"},
        {90007, "Common", "WARNING", "node Or Edge is empty - 节点或边为空"},
        {90008, "Common", "WARNING", "orderUpdateId is lower than last one,msg pass - 订单更新ID低于上一个，消息跳过"},
        {90009, "Common", "WARNING", "orderUpdateId is == than last one,msg pass - 订单更新ID与上一个相同，消息跳过"},
        {90010, "Common", "WARNING", "try to create Order Failed - 尝试创建订单失败"},
        {90011, "Common", "WARNING", "new node base error - 新建节点基础错误"},
        {90012, "Common", "WARNING", "order's nodePosition not in map - 订单的节点位置不在地图中"},
        {90013, "Common", "WARNING", "actionPackEmpty - 动作包为空"},
        {90014, "Common", "WARNING", "nonOrderCancel - 非订单取消"},
    };

    for (const auto& definition : kErrorDefinitions) {
        if (definition.code == code) {
            return &definition;
        }
    }
    return nullptr;
}

std::string SimErrorManager::toErrorName(int32_t code)
{
    return std::to_string(static_cast<long long>(code));
}

void SimErrorManager::setFromDefinition(const ErrorDefinition& definition)
{
    ErrorItem item;
    item.errorType = definition.errorType;
    item.errorLevel = definition.errorLevel;
    item.errorName = toErrorName(definition.code);
    item.errorDescription = definition.errorDescription;
    activeErrorsByName_.insert_or_assign(item.errorName, std::move(item));
}

void SimErrorManager::setErrorByCode(int32_t code)
{
    const ErrorDefinition* p_Definition = findDefinition(code);
    if (p_Definition == nullptr) {
        setErrorCustom("Common", "WARNING", toErrorName(code), "unknown error");
        return;
    }
    setFromDefinition(*p_Definition);
}

void SimErrorManager::setErrorCustom(std::string errorType, std::string errorLevel, std::string errorName, std::string errorDescription)
{
    if (errorName.empty()) {
        return;
    }
    ErrorItem item;
    item.errorType = std::move(errorType);
    item.errorLevel = std::move(errorLevel);
    item.errorName = std::move(errorName);
    item.errorDescription = std::move(errorDescription);
    activeErrorsByName_.insert_or_assign(item.errorName, std::move(item));
}

void SimErrorManager::clearErrorByCode(int32_t code)
{
    clearErrorByName(toErrorName(code));
}

void SimErrorManager::clearErrorByName(std::string_view errorName)
{
    if (errorName.empty()) {
        return;
    }
    activeErrorsByName_.erase(std::string(errorName));
}

void SimErrorManager::clearAll()
{
    activeErrorsByName_.clear();
}

void SimErrorManager::updateBattery(float batteryChargePercent)
{
    if (batteryChargePercent <= zeroBatteryThreshold_) {
        setErrorByCode(50401);
        setErrorByCode(52503);
        clearErrorByCode(54211);
        return;
    }

    clearErrorByCode(50401);
    clearErrorByCode(52503);

    if (batteryChargePercent <= lowBatteryThreshold_) {
        setErrorByCode(54211);
        return;
    }

    clearErrorByCode(54211);
}

void SimErrorManager::updateMovementBlocked(bool movementBlocked)
{
    if (movementBlocked) {
        setErrorByCode(54231);
        return;
    }
    clearErrorByCode(54231);
}

simagv::json::Array SimErrorManager::buildVdaErrorsArray(size_t maxCount) const
{
    std::vector<std::string> keys;
    keys.reserve(activeErrorsByName_.size());
    for (const auto& kv : activeErrorsByName_) {
        keys.push_back(kv.first);
    }
    std::sort(keys.begin(), keys.end());

    simagv::json::Array out;
    out.reserve(std::min(maxCount, keys.size()));

    for (size_t i = 0; i < keys.size() && out.size() < maxCount; ++i) {
        const auto it = activeErrorsByName_.find(keys[i]);
        if (it == activeErrorsByName_.end()) {
            continue;
        }
        const ErrorItem& item = it->second;

        simagv::json::Object obj;
        obj.emplace("errorType", simagv::json::Value{item.errorType});
        obj.emplace("errorLevel", simagv::json::Value{item.errorLevel});
        obj.emplace("errorName", simagv::json::Value{item.errorName});
        obj.emplace("errorDescription", simagv::json::Value{item.errorDescription});
        obj.emplace("errorReference", simagv::json::Value{simagv::json::Array{}});
        out.emplace_back(std::move(obj));
    }

    return out;
}

} // namespace simagv::l4
