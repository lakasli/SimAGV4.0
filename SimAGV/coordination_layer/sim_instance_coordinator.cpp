#include "sim_instance_coordinator.hpp"

#include "map_id_filter.hpp"
#include "l2_utils.hpp"

#include "../atom_functions/console_log_atoms.hpp"
#include "../atom_functions/collision_detection_atoms.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace simagv::l2 {

namespace {

constexpr float kPi = 3.14159265358979323846f;

simagv::l4::Point2D localToWorld(const simagv::l4::Point2D& local, const simagv::l4::Point2D& center, float theta)
{
    const float s = std::cos(theta);
    const float c = std::sin(theta);
    const float wx = center.x + local.x * c + local.y * s;
    const float wy = center.y - local.x * s + local.y * c;
    return simagv::l4::Point2D{wx, wy};
}

std::vector<simagv::l4::Point2D> buildOrientedRectPolygon(const simagv::l4::Point2D& center, float lengthM, float widthM, float theta)
{
    const float hl = lengthM * 0.5f;
    const float hw = widthM * 0.5f;
    const std::vector<simagv::l4::Point2D> local = {
        simagv::l4::Point2D{-hw, -hl},
        simagv::l4::Point2D{hw, -hl},
        simagv::l4::Point2D{hw, hl},
        simagv::l4::Point2D{-hw, hl},
    };
    std::vector<simagv::l4::Point2D> out;
    out.reserve(local.size());
    for (const auto& p : local) {
        out.push_back(localToWorld(p, center, theta));
    }
    return out;
}

std::vector<simagv::l4::Point2D> buildSectorPolygon(const simagv::l4::Point2D& origin, float theta, float fovDeg, float radiusM, int segments)
{
    std::vector<simagv::l4::Point2D> pts;
    pts.reserve(static_cast<size_t>(segments) + 2U);
    const float half = (fovDeg * kPi / 180.0f) * 0.5f;
    const float alpha = theta + kPi;
    pts.push_back(origin);
    for (int i = 0; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const float a = alpha - half + (2.0f * half) * t;
        const float px = origin.x + std::cos(a) * radiusM;
        const float py = origin.y + std::sin(a) * radiusM;
        pts.push_back(simagv::l4::Point2D{px, py});
    }
    return pts;
}

std::string joinContacts(const std::vector<PerceptionContact>& contacts, size_t limit)
{
    std::ostringstream oss;
    const size_t n = contacts.size();
    const size_t take = std::min(n, limit);
    for (size_t i = 0; i < take; ++i) {
        if (i > 0) {
            oss << ",";
        }
        oss << contacts[i].manufacturer << "/" << contacts[i].serialNumber;
    }
    if (n > take) {
        oss << "...";
    }
    return oss.str();
}

std::string readMapIdFromSimConfig(const simagv::json::Object& obj)
{
    const auto* v = tryGetSnakeOrCamel(obj, "map_id", "mapId");
    if (v == nullptr || !v->isString()) {
        return "";
    }
    const std::string raw = v->asString();
    const auto itNonSpace = std::find_if(raw.begin(), raw.end(), [](unsigned char ch) { return !std::isspace(ch); });
    if (itNonSpace == raw.end()) {
        return "";
    }
    return canonicalizeMapId(raw);
}

std::string readSwitchMapTarget(const simagv::json::Object& obj)
{
    const auto itActions = obj.find("actions");
    if (itActions == obj.end() || !itActions->second.isArray()) {
        return "";
    }

    for (const simagv::json::Value& actionValue : itActions->second.asArray()) {
        if (!actionValue.isObject()) {
            continue;
        }
        const simagv::json::Object& actionObj = actionValue.asObject();
        const std::string actionType = readStringOr(actionObj, "action_type", "actionType", "");
        if (actionType != "switchMap") {
            continue;
        }
        const auto itParams = actionObj.find("actionParameters");
        if (itParams == actionObj.end() || !itParams->second.isArray()) {
            continue;
        }
        for (const simagv::json::Value& paramValue : itParams->second.asArray()) {
            if (!paramValue.isObject()) {
                continue;
            }
            const simagv::json::Object& paramObj = paramValue.asObject();
            const std::string key = readStringOr(paramObj, "key", "key", "");
            if (key != "map" && key != "mapId" && key != "map_id") {
                continue;
            }
            const auto itValue = paramObj.find("value");
            if (itValue == paramObj.end()) {
                return "";
            }
            if (itValue->second.isString()) {
                return canonicalizeMapId(itValue->second.asString());
            }
            return "";
        }
    }
    return "";
}

std::optional<double> readActionParamNumberAnyKey(const simagv::json::Object& actionObj, const std::vector<std::string>& keys)
{
    const auto itParams = actionObj.find("actionParameters");
    if (itParams == actionObj.end() || !itParams->second.isArray()) {
        return std::nullopt;
    }
    for (const simagv::json::Value& paramValue : itParams->second.asArray()) {
        if (!paramValue.isObject()) {
            continue;
        }
        const simagv::json::Object& paramObj = paramValue.asObject();
        const std::string key = readStringOr(paramObj, "key", "key", "");
        if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
            continue;
        }
        const auto itValue = paramObj.find("value");
        if (itValue == paramObj.end() || !itValue->second.isNumber()) {
            return std::nullopt;
        }
        return itValue->second.asNumber();
    }
    return std::nullopt;
}

std::string readActionParamStringAnyKey(const simagv::json::Object& actionObj, const std::vector<std::string>& keys)
{
    const auto itParams = actionObj.find("actionParameters");
    if (itParams == actionObj.end() || !itParams->second.isArray()) {
        return "";
    }
    for (const simagv::json::Value& paramValue : itParams->second.asArray()) {
        if (!paramValue.isObject()) {
            continue;
        }
        const simagv::json::Object& paramObj = paramValue.asObject();
        const std::string key = readStringOr(paramObj, "key", "key", "");
        if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
            continue;
        }
        const auto itValue = paramObj.find("value");
        if (itValue == paramObj.end() || !itValue->second.isString()) {
            return "";
        }
        return itValue->second.asString();
    }
    return "";
}

bool hasActionParamNumberAnyKey(const simagv::json::Object& actionObj, const std::vector<std::string>& keys)
{
    return readActionParamNumberAnyKey(actionObj, keys).has_value();
}

simagv::json::Array& ensureActionParametersArray(simagv::json::Object& actionObj)
{
    auto& v = actionObj["actionParameters"];
    if (!v.isArray()) {
        v = simagv::json::Value{simagv::json::Array{}};
    }
    return v.asArray();
}

void upsertActionParamNumber(simagv::json::Object& actionObj, const std::string& key, double value)
{
    simagv::json::Array& params = ensureActionParametersArray(actionObj);
    for (simagv::json::Value& paramValue : params) {
        if (!paramValue.isObject()) {
            continue;
        }
        simagv::json::Object& paramObj = paramValue.asObject();
        const std::string k = readStringOr(paramObj, "key", "key", "");
        if (k != key) {
            continue;
        }
        paramObj["value"] = simagv::json::Value{value};
        return;
    }
    params.push_back(simagv::json::Value{simagv::json::Object{{"key", simagv::json::Value{key}}, {"value", simagv::json::Value{value}}}});
}

const simagv::l4::SceneStationCatalogItem* findStationInTopo(const simagv::l4::VehicleMapTopoPackage& pkg, const std::string& stationId)
{
    if (stationId.empty()) {
        return nullptr;
    }
    for (const auto& item : pkg.topology.stationCatalog) {
        if (item.id == stationId || item.instanceName == stationId || item.pointName == stationId) {
            return &item;
        }
    }
    return nullptr;
}

struct InstantRequests final {
    bool factsheet;
    bool state;
};

InstantRequests patchInstantActionsByTopoIfNeeded(simagv::json::Object& rootObj, const simagv::l4::VehicleMapTopoPackage* p_Topo)
{
    InstantRequests req{false, false};
    auto itActions = rootObj.find("actions");
    if (itActions == rootObj.end() || !itActions->second.isArray()) {
        return req;
    }
    for (simagv::json::Value& actionValue : itActions->second.asArray()) {
        if (!actionValue.isObject()) {
            continue;
        }
        simagv::json::Object& actionObj = actionValue.asObject();
        const std::string actionType = readStringOr(actionObj, "action_type", "actionType", "");
        if (actionType == "factsheetRequest") {
            req.factsheet = true;
        } else if (actionType == "stateRequest") {
            req.state = true;
        }
        if (actionType != "switchMap" || p_Topo == nullptr) {
            continue;
        }
        if (hasActionParamNumberAnyKey(actionObj, {"center_x", "centerX"}) && hasActionParamNumberAnyKey(actionObj, {"center_y", "centerY"})) {
            continue;
        }
        const std::string switchPoint = readActionParamStringAnyKey(actionObj, {"switchPoint"});
        if (switchPoint.empty()) {
            continue;
        }
        const auto* p_Found = findStationInTopo(*p_Topo, switchPoint);
        if (p_Found == nullptr) {
            continue;
        }
        upsertActionParamNumber(actionObj, "center_x", static_cast<double>(p_Found->pos.x));
        upsertActionParamNumber(actionObj, "center_y", static_cast<double>(p_Found->pos.y));
        if (!hasActionParamNumberAnyKey(actionObj, {"initiate_angle", "initiateAngle"})) {
            upsertActionParamNumber(actionObj, "initiate_angle", 0.0);
        }
    }
    return req;
}

float computeEffectiveHz(float configuredHz, float simTimeScale, float maxHz)
{
    const float scale = clampMin(simTimeScale, 0.0001F);
    const float hz = clampMin(configuredHz, 1e-6F) * scale;
    return clampRange(hz, 1e-6F, clampMin(maxHz, 1e-6F));
}

float computeEffectiveHzOrZero(float configuredHz, float simTimeScale, float maxHz)
{
    if (configuredHz <= 0.0F) {
        return 0.0F;
    }
    return computeEffectiveHz(configuredHz, simTimeScale, maxHz);
}

std::string formatIsoTimestamp(uint64_t epochMs)
{
    const std::time_t sec = static_cast<std::time_t>(epochMs / 1000U);
    const uint32_t ms = static_cast<uint32_t>(epochMs % 1000U);
    std::tm tmUtc;
    ::gmtime_r(&sec, &tmUtc);
    std::ostringstream oss;
    oss << std::put_time(&tmUtc, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << ms << 'Z';
    return oss.str();
}

void validateOrderNodesEdgesOrThrow(const simagv::json::Object& orderObj, const simagv::l3::MapTopology& topology)
{
    const auto itNodes = orderObj.find("nodes");
    if (itNodes == orderObj.end() || !itNodes->second.isArray()) {
        throw std::runtime_error("missing_nodes");
    }

    const simagv::json::Array& nodes = itNodes->second.asArray();
    if (nodes.empty()) {
        throw std::runtime_error("empty_nodes");
    }

    std::unordered_set<std::string> nodeIdSet;
    std::unordered_set<uint32_t> nodeSeqSet;
    nodeIdSet.reserve(nodes.size());
    nodeSeqSet.reserve(nodes.size());
    for (const simagv::json::Value& nodeValue : nodes) {
        if (!nodeValue.isObject()) {
            throw std::runtime_error("node_not_object");
        }
        const simagv::json::Object& nodeObj = nodeValue.asObject();
        const std::string nodeId = readStringOr(nodeObj, "node_id", "nodeId", "");
        if (nodeId.empty()) {
            throw std::runtime_error("missing_node_id");
        }
        const uint32_t seq = readUintOr(nodeObj, "sequence_id", "sequenceId", 0U);

        const auto itPos = nodeObj.find("nodePosition");
        if (itPos == nodeObj.end() || !itPos->second.isObject()) {
            throw std::runtime_error("missing_node_position");
        }
        const simagv::json::Object& posObj = itPos->second.asObject();
        const auto itX = posObj.find("x");
        const auto itY = posObj.find("y");
        if (itX == posObj.end() || itY == posObj.end() || !itX->second.isNumber() || !itY->second.isNumber()) {
            throw std::runtime_error("invalid_node_position_xy");
        }

        if (!nodeSeqSet.insert(seq).second) {
            throw std::runtime_error("duplicate_node_sequence");
        }
        nodeIdSet.insert(nodeId);
    }

    std::unordered_set<std::string> validEdgeIds;
    validEdgeIds.reserve(topology.edges.size());
    for (const simagv::l3::PathEdge& e : topology.edges) {
        validEdgeIds.insert(e.id);
    }

    const auto itEdges = orderObj.find("edges");
    if (itEdges == orderObj.end() || itEdges->second.isNull()) {
        return;
    }
    if (!itEdges->second.isArray()) {
        throw std::runtime_error("edges_not_array");
    }
    const simagv::json::Array& edges = itEdges->second.asArray();
    if (edges.empty()) {
        return;
    }

    std::unordered_set<uint32_t> edgeSeqSet;
    edgeSeqSet.reserve(edges.size());
    for (const simagv::json::Value& edgeValue : edges) {
        if (!edgeValue.isObject()) {
            throw std::runtime_error("edge_not_object");
        }
        const simagv::json::Object& edgeObj = edgeValue.asObject();
        const std::string edgeId = readStringOr(edgeObj, "edge_id", "edgeId", "");
        if (edgeId.empty()) {
            throw std::runtime_error("missing_edge_id");
        }
        if (validEdgeIds.count(edgeId) == 0U) {
            throw std::runtime_error(std::string("path_not_found:") + edgeId);
        }

        const uint32_t seq = readUintOr(edgeObj, "sequence_id", "sequenceId", 0U);
        if (!edgeSeqSet.insert(seq).second) {
            throw std::runtime_error("duplicate_edge_sequence");
        }

        const std::string startNodeId = readStringOr(edgeObj, "start_node_id", "startNodeId", "");
        const std::string endNodeId = readStringOr(edgeObj, "end_node_id", "endNodeId", "");
        if (startNodeId.empty() || endNodeId.empty()) {
            throw std::runtime_error("missing_edge_link");
        }
        if (startNodeId == endNodeId) {
            throw std::runtime_error("invalid_edge_loop");
        }
        if (nodeIdSet.count(startNodeId) == 0U || nodeIdSet.count(endNodeId) == 0U) {
            throw std::runtime_error("edge_references_unknown_node");
        }

        const auto itTraj = edgeObj.find("trajectory");
        if (itTraj != edgeObj.end() && itTraj->second.isObject()) {
            const simagv::json::Object trajObj = itTraj->second.asObject();
            const std::string type = readStringOr(trajObj, "type", "type", "");
            if (!(type == "CubicBezier" || type == "CUBIC_BEZIER")) {
                throw std::runtime_error("unsupported_trajectory_type");
            }
            const auto itCps = trajObj.find("controlPoints");
            if (itCps == trajObj.end() || !itCps->second.isArray() || itCps->second.asArray().size() < 4U) {
                throw std::runtime_error("invalid_trajectory_control_points");
            }
        }
    }
}

/**
 * @brief 解析ISO8601 UTC时间戳 - 将VDA5050 timestamp解析为epoch毫秒
 *
 * 支持形如 YYYY-MM-DDTHH:MM:SSZ 与 YYYY-MM-DDTHH:MM:SS.mmmZ 的UTC格式
 *
 * @param [timestamp] ISO8601 UTC时间戳字符串
 * @return uint64_t epoch毫秒
 * @throws std::runtime_error 时间戳格式非法
 */
uint64_t parseIso8601UtcMs(const std::string& timestamp)
{
    if (timestamp.size() < 20U) {
        throw std::runtime_error("timestamp_too_short");
    }
    if (timestamp.back() != 'Z') {
        throw std::runtime_error("timestamp_not_utc");
    }

    const auto read2 = [&](size_t pos) -> int {
        if (pos + 2U > timestamp.size()) {
            throw std::runtime_error("timestamp_out_of_range");
        }
        if (!std::isdigit(static_cast<unsigned char>(timestamp[pos])) || !std::isdigit(static_cast<unsigned char>(timestamp[pos + 1U]))) {
            throw std::runtime_error("timestamp_not_digit");
        }
        return (timestamp[pos] - '0') * 10 + (timestamp[pos + 1U] - '0');
    };

    const auto read4 = [&](size_t pos) -> int {
        if (pos + 4U > timestamp.size()) {
            throw std::runtime_error("timestamp_out_of_range");
        }
        for (size_t i = 0U; i < 4U; ++i) {
            if (!std::isdigit(static_cast<unsigned char>(timestamp[pos + i]))) {
                throw std::runtime_error("timestamp_not_digit");
            }
        }
        return (timestamp[pos] - '0') * 1000 + (timestamp[pos + 1U] - '0') * 100 + (timestamp[pos + 2U] - '0') * 10 + (timestamp[pos + 3U] - '0');
    };

    if (timestamp[4] != '-' || timestamp[7] != '-' || timestamp[10] != 'T' || timestamp[13] != ':' || timestamp[16] != ':') {
        throw std::runtime_error("timestamp_layout_invalid");
    }

    const int year = read4(0U);  // 年
    const int month = read2(5U); // 月
    const int day = read2(8U);   // 日
    const int hour = read2(11U); // 时
    const int min = read2(14U);  // 分
    const int sec = read2(17U);  // 秒

    uint32_t ms = 0U; // 毫秒
    const size_t msStart = 19U;
    if (msStart < timestamp.size() && timestamp[msStart] == '.') {
        const size_t msDigitsStart = msStart + 1U;
        const size_t msDigitsEnd = timestamp.find('Z', msDigitsStart);
        if (msDigitsEnd == std::string::npos) {
            throw std::runtime_error("timestamp_layout_invalid");
        }
        const size_t digitCount = msDigitsEnd - msDigitsStart;
        if (digitCount == 0U || digitCount > 3U) {
            throw std::runtime_error("timestamp_ms_invalid");
        }
        uint32_t raw = 0U;
        for (size_t i = 0U; i < digitCount; ++i) {
            const char ch = timestamp[msDigitsStart + i];
            if (!std::isdigit(static_cast<unsigned char>(ch))) {
                throw std::runtime_error("timestamp_ms_invalid");
            }
            raw = raw * 10U + static_cast<uint32_t>(ch - '0');
        }
        if (digitCount == 1U) {
            ms = raw * 100U;
        } else if (digitCount == 2U) {
            ms = raw * 10U;
        } else {
            ms = raw;
        }
    }

    std::tm tmUtc{};
    tmUtc.tm_year = year - 1900;
    tmUtc.tm_mon = month - 1;
    tmUtc.tm_mday = day;
    tmUtc.tm_hour = hour;
    tmUtc.tm_min = min;
    tmUtc.tm_sec = sec;
    tmUtc.tm_isdst = 0;

    const std::time_t epochSec = ::timegm(&tmUtc);
    if (epochSec < 0) {
        throw std::runtime_error("timestamp_epoch_invalid");
    }
    return static_cast<uint64_t>(epochSec) * 1000U + static_cast<uint64_t>(ms);
}

/**
 * @brief 解析订单地图标识 - 从order.nodes或order.mapId解析mapId
 *
 * @param [orderObj] order对象
 * @param [fallbackMapId] 兜底mapId
 * @return std::string 规范化mapId，空表示缺失
 */
std::string resolveOrderMapId(const simagv::json::Object& orderObj, std::string_view fallbackMapId)
{
    const std::string direct = readStringOr(orderObj, "map_id", "mapId", ""); // 顶层mapId
    if (!direct.empty()) {
        return canonicalizeMapId(direct);
    }

    const auto itNodes = orderObj.find("nodes"); // nodes字段
    if (itNodes != orderObj.end() && itNodes->second.isArray()) {
        for (const simagv::json::Value& nodeValue : itNodes->second.asArray()) {
            if (!nodeValue.isObject()) {
                continue;
            }
            const simagv::json::Object& nodeObj = nodeValue.asObject(); // node对象

            const auto itNodePos = nodeObj.find("nodePosition"); // nodePosition
            if (itNodePos != nodeObj.end() && itNodePos->second.isObject()) {
                const simagv::json::Object& nodePosObj = itNodePos->second.asObject(); // nodePosition对象
                const std::string posMapId = readStringOr(nodePosObj, "map_id", "mapId", ""); // nodePosition.mapId
                if (!posMapId.empty()) {
                    return canonicalizeMapId(posMapId);
                }
            }

            const std::string nodeMapId = readStringOr(nodeObj, "map_id", "mapId", ""); // node.mapId
            if (!nodeMapId.empty()) {
                return canonicalizeMapId(nodeMapId);
            }
        }
    }

    const std::string fallback(fallbackMapId); // 兜底
    if (fallback.empty()) {
        return "";
    }
    return canonicalizeMapId(fallback);
}

bool orderNodesAllMissingNodePosition(const simagv::json::Object& orderObj)
{
    const auto itNodes = orderObj.find("nodes");
    if (itNodes == orderObj.end() || !itNodes->second.isArray()) {
        return false;
    }
    const simagv::json::Array& nodes = itNodes->second.asArray();
    bool sawNodeObject = false;
    for (const simagv::json::Value& nodeValue : nodes) {
        if (!nodeValue.isObject()) {
            continue;
        }
        sawNodeObject = true;
        const simagv::json::Object& nodeObj = nodeValue.asObject();
        const auto itNodePos = nodeObj.find("nodePosition");
        if (itNodePos != nodeObj.end() && itNodePos->second.isObject()) {
            return false;
        }
    }
    return sawNodeObject;
}

/**
 * @brief 补全节点位姿 - 当nodePosition为空时从topo.json按nodeId补全坐标
 *
 * @param [nodeObj] node对象
 * @param [pkg] topo.json解析包
 * @param [mapId] 订单地图标识
 * @throws std::runtime_error nodeId缺失或地图中不存在
 */
void fillNodePositionFromTopoIfMissing(simagv::json::Object& nodeObj, const simagv::l4::VehicleMapTopoPackage& pkg, const std::string& mapId)
{
    const std::string nodeId = readStringOr(nodeObj, "node_id", "nodeId", ""); // 节点ID
    if (nodeId.empty()) {
        throw std::runtime_error("nodeId_empty");
    }

    const simagv::l4::SceneStationCatalogItem* p_Found = nullptr; // 匹配站点
    for (const auto& item : pkg.topology.stationCatalog) {
        if (item.id == nodeId || item.instanceName == nodeId || item.pointName == nodeId) {
            p_Found = &item;
            break;
        }
    }
    if (p_Found == nullptr) {
        throw std::runtime_error("nodeId_not_in_map");
    }

    auto& posValue = nodeObj["nodePosition"]; // nodePosition字段
    if (!posValue.isObject()) {
        posValue = simagv::json::Value{simagv::json::Object{}}; // 创建对象
    }
    simagv::json::Object& posObj = posValue.asObject(); // nodePosition对象

    const auto hasX = (posObj.find("x") != posObj.end()) && posObj.at("x").isNumber();
    const auto hasY = (posObj.find("y") != posObj.end()) && posObj.at("y").isNumber();
    if (!(hasX && hasY)) {
        posObj["x"] = simagv::json::Value{static_cast<double>(p_Found->pos.x)};
        posObj["y"] = simagv::json::Value{static_cast<double>(p_Found->pos.y)};
    }

    if (posObj.find("theta") == posObj.end()) {
        posObj["theta"] = simagv::json::Value{0.0};
    }
    if (posObj.find("allowedDeviationXY") == posObj.end()) {
        posObj["allowedDeviationXY"] = simagv::json::Value{0.0};
    }
    if (posObj.find("allowedDeviationTheta") == posObj.end()) {
        posObj["allowedDeviationTheta"] = simagv::json::Value{0.0};
    }
    if (posObj.find("mapId") == posObj.end()) {
        posObj["mapId"] = simagv::json::Value{mapId};
    }
}

void patchEdgesTrajectoryFromTopoIfMissing(simagv::json::Object& orderObj, const simagv::l4::VehicleMapTopoPackage& pkg)
{
    auto itEdges = orderObj.find("edges");
    if (itEdges == orderObj.end() || !itEdges->second.isArray()) {
        return;
    }
    auto itNodes = orderObj.find("nodes");
    if (itNodes == orderObj.end() || !itNodes->second.isArray()) {
        return;
    }

    std::unordered_map<std::string, simagv::l4::Position> topoPosById;
    topoPosById.reserve(pkg.topology.stationCatalog.size() * 2U);
    for (const auto& st : pkg.topology.stationCatalog) {
        auto tryEmplace = [&](const std::string& key) {
            if (key.empty()) {
                return;
            }
            if (topoPosById.find(key) != topoPosById.end()) {
                return;
            }
            topoPosById.emplace(key, st.pos);
        };
        tryEmplace(st.id);
        tryEmplace(st.instanceName);
        tryEmplace(st.pointName);
    }

    std::unordered_map<std::string, simagv::l4::Position> nodePosById;
    nodePosById.reserve(itNodes->second.asArray().size());
    for (const simagv::json::Value& nodeValue : itNodes->second.asArray()) {
        if (!nodeValue.isObject()) {
            continue;
        }
        const simagv::json::Object& nodeObj = nodeValue.asObject();
        const std::string nodeId = readStringOr(nodeObj, "node_id", "nodeId", "");
        if (nodeId.empty()) {
            continue;
        }
        const auto itPos = nodeObj.find("nodePosition");
        if (itPos == nodeObj.end() || !itPos->second.isObject()) {
            continue;
        }
        const simagv::json::Object& posObj = itPos->second.asObject();
        const auto itX = posObj.find("x");
        const auto itY = posObj.find("y");
        if (itX == posObj.end() || itY == posObj.end() || !itX->second.isNumber() || !itY->second.isNumber()) {
            continue;
        }
        simagv::l4::Position pos{};
        pos.x = static_cast<float>(itX->second.asNumber());
        pos.y = static_cast<float>(itY->second.asNumber());
        pos.z = 0.0F;
        nodePosById.emplace(nodeId, pos);
    }

    const auto* p_Paths = &pkg.topology.paths;
    for (simagv::json::Value& edgeValue : itEdges->second.asArray()) {
        if (!edgeValue.isObject()) {
            continue;
        }
        simagv::json::Object& edgeObj = edgeValue.asObject();
        const auto itTraj = edgeObj.find("trajectory");
        if (itTraj != edgeObj.end() && itTraj->second.isObject()) {
            continue;
        }

        const std::string startNodeId = readStringOr(edgeObj, "start_node_id", "startNodeId", "");
        const std::string endNodeId = readStringOr(edgeObj, "end_node_id", "endNodeId", "");
        if (startNodeId.empty() || endNodeId.empty()) {
            continue;
        }
        const auto itStartPos = nodePosById.find(startNodeId);
        const auto itEndPos = nodePosById.find(endNodeId);
        if (itStartPos == nodePosById.end() || itEndPos == nodePosById.end()) {
            continue;
        }

        const simagv::l4::ScenePathEdge* p_Found = nullptr;
        bool reversed = false;
        for (const auto& e : *p_Paths) {
            if (e.from == startNodeId && e.to == endNodeId) {
                p_Found = &e;
                reversed = false;
                break;
            }
            if (e.from == endNodeId && e.to == startNodeId) {
                p_Found = &e;
                reversed = true;
                break;
            }
        }
        if (p_Found == nullptr) {
            continue;
        }

        const simagv::l4::Position cp1 = reversed ? p_Found->cp2 : p_Found->cp1;
        const simagv::l4::Position cp2 = reversed ? p_Found->cp1 : p_Found->cp2;
        const simagv::l4::Position p0 = itStartPos->second;
        const simagv::l4::Position p3 = itEndPos->second;
        const auto itTopoP0 = topoPosById.find(startNodeId);
        const auto itTopoP3 = topoPosById.find(endNodeId);
        const simagv::l4::Position src0 = (itTopoP0 != topoPosById.end()) ? itTopoP0->second : p0;
        const simagv::l4::Position src3 = (itTopoP3 != topoPosById.end()) ? itTopoP3->second : p3;
        const bool cpLooksUnset = (cp1.x == 0.0F && cp1.y == 0.0F && cp2.x == 0.0F && cp2.y == 0.0F)
            && !((p0.x == 0.0F && p0.y == 0.0F) || (p3.x == 0.0F && p3.y == 0.0F));
        if (cpLooksUnset) {
            continue;
        }

        simagv::json::Array controlPoints;
        controlPoints.reserve(4U);
        controlPoints.push_back(simagv::json::Value{simagv::json::Object{
            {"x", simagv::json::Value{static_cast<double>(src0.x)}},
            {"y", simagv::json::Value{static_cast<double>(src0.y)}},
            {"weight", simagv::json::Value{1.0}},
        }});
        controlPoints.push_back(simagv::json::Value{simagv::json::Object{
            {"x", simagv::json::Value{static_cast<double>(cp1.x)}},
            {"y", simagv::json::Value{static_cast<double>(cp1.y)}},
            {"weight", simagv::json::Value{1.0}},
        }});
        controlPoints.push_back(simagv::json::Value{simagv::json::Object{
            {"x", simagv::json::Value{static_cast<double>(cp2.x)}},
            {"y", simagv::json::Value{static_cast<double>(cp2.y)}},
            {"weight", simagv::json::Value{1.0}},
        }});
        controlPoints.push_back(simagv::json::Value{simagv::json::Object{
            {"x", simagv::json::Value{static_cast<double>(src3.x)}},
            {"y", simagv::json::Value{static_cast<double>(src3.y)}},
            {"weight", simagv::json::Value{1.0}},
        }});

        edgeObj["trajectory"] = simagv::json::Value{simagv::json::Object{
            {"type", simagv::json::Value{std::string("CUBIC_BEZIER")}},
            {"degree", simagv::json::Value{3.0}},
            {"controlPoints", simagv::json::Value{std::move(controlPoints)}},
        }};
    }
}

std::string buildOrderIdentityKey(const std::string& orderId, uint64_t orderUpdateId)
{
    std::string key;
    key.reserve(orderId.size() + 1U + 20U);
    key.append(orderId);
    key.push_back('\x1F');
    key.append(std::to_string(orderUpdateId));
    return key;
}

} // namespace

SimInstanceCoordinator::SimInstanceCoordinator(
    ISimulatorEngine& engine,
    IMqttDiplomat& diplomat,
    std::string mqttBaseTopic,
    std::string protocolVersion,
    std::string manufacturer,
    std::string serialNumber,
    RuntimeConfig initialConfig,
    size_t traceCapacity)
    : engine_(engine),
      diplomat_(diplomat),
      mqttBaseTopic_(std::move(mqttBaseTopic)),
      protocolVersion_(std::move(protocolVersion)),
      manufacturer_(std::move(manufacturer)),
      serialNumber_(std::move(serialNumber)),
      inbox_(),
      config_(initialConfig),
      schedule_{0.0F, 0.0F, 0.0F, 0.0F},
      trace_(traceCapacity),
      booted_(false),
      powerOn_(true),
      lastInstantHeaderId_(0),
      stateHeaderId_(0),
      visualizationHeaderId_(0),
      connectionHeaderId_(0),
      factsheetHeaderId_(0),
      loadedMapId_(""),
      loadedMapTopo_(nullptr),
      loadedTopology_(nullptr),
      loadedMapTopoCache_(),
      loadedTopologyCache_(),
      acceptedOrderIdentities_(),
      acceptedOrderIdentitySet_(),
      lastAcceptedOrderId_(""),
      lastAcceptedOrderUpdateId_(0U),
      orderTimestampWindowMs_(10000U)
{
}

void SimInstanceCoordinator::start()
{
    const uint64_t startTs = nowMs();
    const uint64_t stepStart = nowMs();
    booted_ = true;
    pushTrace(startTs, "simBoot", "connectMqtt", 0, TopicType::Unknown, 0, "ok", static_cast<uint32_t>(nowMs() - stepStart), "");

    const Snapshot initialSnapshot = engine_.buildSnapshot(nowMs());
    CommandIntent bootIntent{};
    bootIntent.serial = 0;
    bootIntent.topicType = TopicType::Unknown;
    bootIntent.headerId = 0;
    preloadMapIfNeeded(initialSnapshot.mapId, startTs, "simBoot", "preloadMapTopo", bootIntent);

    const uint64_t pubStart = nowMs();
    publishBootOnce(nowMs());
    pushTrace(nowMs(), "simBoot", "publishBootOnce", 0, TopicType::Unknown, 0, "ok", static_cast<uint32_t>(nowMs() - pubStart), "");
}

void SimInstanceCoordinator::submitCommand(CommandIntent intent)
{
    std::lock_guard<std::mutex> lock(mutex_);
    inbox_.emplace_back(std::move(intent));
}

void SimInstanceCoordinator::tickOnce(uint32_t tickMs)
{
    if (!powerOn_) {
        pushTrace(nowMs(), "tick", "keepAlive", 0, TopicType::Unknown, 0, "paused", 0, "");
        return;
    }

    const uint64_t tickStart = nowMs();
    const uint64_t inboxStart = nowMs();
    processInbox();
    pushTrace(tickStart, "tick", "consumeInbox", 0, TopicType::Unknown, 0, "ok", static_cast<uint32_t>(nowMs() - inboxStart), "");

    if (!booted_) {
        start();
    }

    const uint64_t perceptionStart = nowMs();
    try {
        const uint64_t nowTs = nowMs();
        for (auto it = otherVisualizationsByKey_.begin(); it != otherVisualizationsByKey_.end();) {
            const uint64_t age = (nowTs >= it->second.receiveTimestampMs) ? (nowTs - it->second.receiveTimestampMs) : 0U;
            if (age > otherVisualizationStaleMs_) {
                it = otherVisualizationsByKey_.erase(it);
            } else {
                ++it;
            }
        }

        const Snapshot ownSnapshot = engine_.buildSnapshot(nowTs);
        const auto itRadar = ownSnapshot.visualization.find("radar");
        const auto itSafety = ownSnapshot.visualization.find("safety");
        if (itRadar != ownSnapshot.visualization.end() && itSafety != ownSnapshot.visualization.end() && itRadar->second.isObject() &&
            itSafety->second.isObject()) {
            const auto hasErrorName = [](const simagv::json::Value& errorsValue, std::string_view target) -> bool {
                if (!errorsValue.isArray()) {
                    return false;
                }
                for (const auto& item : errorsValue.asArray()) {
                    if (!item.isObject()) {
                        continue;
                    }
                    const simagv::json::Object& obj = item.asObject();
                    const auto itName = obj.find("errorName");
                    if (itName == obj.end() || !itName->second.isString()) {
                        continue;
                    }
                    if (itName->second.asString() == target) {
                        return true;
                    }
                }
                return false;
            };

            const simagv::json::Object& radarObj = itRadar->second.asObject();
            const simagv::json::Object& safetyObj = itSafety->second.asObject();

            const auto itOrigin = radarObj.find("origin");
            simagv::l4::Point2D radarOrigin{0.0f, 0.0f};
            if (itOrigin != radarObj.end() && itOrigin->second.isObject()) {
                const simagv::json::Object& originObj = itOrigin->second.asObject();
                radarOrigin.x = readFloatOr(originObj, "x", "x", 0.0f);
                radarOrigin.y = readFloatOr(originObj, "y", "y", 0.0f);
            }

            const float radarTheta = readFloatOr(radarObj, "theta", "theta", 0.0f);
            const float radarFovDeg = readFloatOr(radarObj, "fovDeg", "fovDeg", 60.0f);
            const float radarRadius = readFloatOr(radarObj, "radius", "radius", 0.5f);
            const float gateRadius = std::max(0.0f, radarRadius) * 10.0f;
            const float gateRadius2 = gateRadius * gateRadius;

            float ownVx = 0.0f;
            float ownVy = 0.0f;
            float ownOmega = 0.0f;
            const auto itVel = ownSnapshot.visualization.find("velocity");
            if (itVel != ownSnapshot.visualization.end() && itVel->second.isObject()) {
                const simagv::json::Object& velObj = itVel->second.asObject();
                ownVx = readFloatOr(velObj, "vx", "vx", 0.0f);
                ownVy = readFloatOr(velObj, "vy", "vy", 0.0f);
                ownOmega = readFloatOr(velObj, "omega", "omega", 0.0f);
            }
            const bool rotating = disableRadarBlockedOnRotation_ && (std::abs(ownOmega) > disableRadarBlockedOmegaThreshold_);
            const float speed2 = ownVx * ownVx + ownVy * ownVy;
            const float speed = std::sqrt(std::max(0.0f, speed2));
            const float forwardSpeed = ownVx * std::cos(radarTheta) + ownVy * std::sin(radarTheta);
            const bool movingForward = (speed > radarBlockedMinSpeedThreshold_) && (forwardSpeed > radarBlockedForwardSpeedThreshold_);
            bool alreadyBlocked = false;
            if (const auto itErrors = ownSnapshot.visualization.find("errors"); itErrors != ownSnapshot.visualization.end()) {
                alreadyBlocked = hasErrorName(itErrors->second, "54330");
            }
            const bool radarBlockedEnabled = !rotating && (movingForward || alreadyBlocked);

            const auto itCenter = safetyObj.find("center");
            simagv::l4::Point2D safetyCenter{0.0f, 0.0f};
            if (itCenter != safetyObj.end() && itCenter->second.isObject()) {
                const simagv::json::Object& centerObj = itCenter->second.asObject();
                safetyCenter.x = readFloatOr(centerObj, "x", "x", 0.0f);
                safetyCenter.y = readFloatOr(centerObj, "y", "y", 0.0f);
            }
            const float safetyLength = readFloatOr(safetyObj, "length", "length", 0.0f);
            const float safetyWidth = readFloatOr(safetyObj, "width", "width", 0.0f);
            const float safetyTheta = readFloatOr(safetyObj, "theta", "theta", 0.0f);

            std::vector<simagv::l4::Point2D> ownSafetyPoly;
            if (safetyLength > 1e-6f && safetyWidth > 1e-6f) {
                ownSafetyPoly = buildOrientedRectPolygon(safetyCenter, safetyLength, safetyWidth, safetyTheta);
            }
            const std::vector<simagv::l4::Point2D> ownRadarPoly = buildSectorPolygon(radarOrigin, radarTheta, radarFovDeg, radarRadius, 24);

            std::unordered_set<std::string> blockedKeySet;
            std::unordered_set<std::string> collisionKeySet;
            std::vector<PerceptionContact> blockedBy;
            std::vector<PerceptionContact> collidedWith;

            for (const auto& kv : otherVisualizationsByKey_) {
                const OtherVehicleVisualization& other = kv.second;
                if (!isMapIdCompatible(ownSnapshot.mapId, other.mapId)) {
                    continue;
                }
                const float dx = other.safetyCenterX - safetyCenter.x;
                const float dy = other.safetyCenterY - safetyCenter.y;
                const float dist2 = dx * dx + dy * dy;
                if (dist2 > gateRadius2) {
                    continue;
                }
                if (other.safetyLength <= 1e-6f || other.safetyWidth <= 1e-6f) {
                    continue;
                }
                const simagv::l4::Point2D otherCenter{other.safetyCenterX, other.safetyCenterY};
                const std::vector<simagv::l4::Point2D> otherSafetyPoly =
                    buildOrientedRectPolygon(otherCenter, other.safetyLength, other.safetyWidth, other.safetyTheta);

                const std::string key = other.manufacturer + "/" + other.serialNumber;
                bool collided = false;
                if (!ownSafetyPoly.empty()) {
                    const simagv::l4::CollisionResult overlap = simagv::l4::checkPolygonCollision(ownSafetyPoly, otherSafetyPoly);
                    collided = overlap.isColliding;
                }
                if (collided) {
                    if (collisionKeySet.insert(key).second) {
                        collidedWith.push_back(PerceptionContact{other.manufacturer, other.serialNumber});
                    }
                    continue;
                }

                if (radarBlockedEnabled) {
                    const simagv::l4::CollisionResult radarOverlap = simagv::l4::checkPolygonCollision(ownRadarPoly, otherSafetyPoly);
                    if (radarOverlap.isColliding) {
                        if (blockedKeySet.insert(key).second) {
                            blockedBy.push_back(PerceptionContact{other.manufacturer, other.serialNumber});
                        }
                    }
                }
            }

            PerceptionUpdate update;
            update.blocked = !blockedBy.empty();
            update.collision = !collidedWith.empty();
            update.blockedBy = std::move(blockedBy);
            update.collidedWith = std::move(collidedWith);
            engine_.applyPerceptionUpdate(update);
            {
                std::ostringstream oss;
                oss << "blocked=" << (update.blocked ? "true" : "false") << " collision=" << (update.collision ? "true" : "false")
                    << " blockedBy=" << update.blockedBy.size() << " collidedWith=" << update.collidedWith.size()
                    << " blockedKeys=" << joinContacts(update.blockedBy, 3) << " collidedKeys=" << joinContacts(update.collidedWith, 3)
                    << " radarBlockedEnabled=" << (radarBlockedEnabled ? "true" : "false") << " movingForward=" << (movingForward ? "true" : "false")
                    << " alreadyBlocked=" << (alreadyBlocked ? "true" : "false") << " v=(" << ownVx << "," << ownVy << ") speed=" << speed
                    << " forwardSpeed=" << forwardSpeed << " rotating=" << (rotating ? "true" : "false") << " omega=" << ownOmega;
                pushTrace(tickStart, "tick", "applyPerception", 0, TopicType::Unknown, 0, "ok", static_cast<uint32_t>(nowMs() - perceptionStart), oss.str());
            }
        } else {
            engine_.applyPerceptionUpdate(PerceptionUpdate{false, false, {}, {}});
            pushTrace(tickStart, "tick", "applyPerception", 0, TopicType::Unknown, 0, "ok", static_cast<uint32_t>(nowMs() - perceptionStart), "no_own_radar_or_safety");
        }
    } catch (const std::exception& e) {
        pushTrace(tickStart, "tick", "applyPerception", 0, TopicType::Unknown, 0, "error", static_cast<uint32_t>(nowMs() - perceptionStart), e.what());
    } catch (...) {
        pushTrace(tickStart, "tick", "applyPerception", 0, TopicType::Unknown, 0, "error", static_cast<uint32_t>(nowMs() - perceptionStart), "unknown");
    }

    const uint64_t updateStart = nowMs();
    try {
        engine_.updateState(tickMs);
        pushTrace(tickStart, "tick", "updateState", 0, TopicType::Unknown, 0, "ok", static_cast<uint32_t>(nowMs() - updateStart), "");
    } catch (const std::exception& e) {
        pushTrace(tickStart, "tick", "updateState", 0, TopicType::Unknown, 0, "error", static_cast<uint32_t>(nowMs() - updateStart), e.what());
    } catch (...) {
        pushTrace(tickStart, "tick", "updateState", 0, TopicType::Unknown, 0, "error", static_cast<uint32_t>(nowMs() - updateStart), "unknown");
    }

    const uint64_t publishStart = nowMs();
    publishBySchedule(tickMs, nowMs());
    pushTrace(tickStart, "tick", "publishBySchedule", 0, TopicType::Unknown, 0, "ok", static_cast<uint32_t>(nowMs() - publishStart), "");
}

Snapshot SimInstanceCoordinator::getSnapshot() const
{
    return engine_.buildSnapshot(nowMs());
}

std::vector<TraceRecord> SimInstanceCoordinator::getTrace() const
{
    return trace_.snapshot();
}

void SimInstanceCoordinator::processInbox()
{
    std::deque<CommandIntent> local;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        local.swap(inbox_);
    }

    std::optional<CommandIntent> latestInstant;
    std::optional<CommandIntent> latestConfig;

    for (const auto& intent : local) {
        if (intent.topicType == TopicType::InstantActions) {
            latestInstant = intent;
            continue;
        }
        if (intent.topicType == TopicType::SimConfig) {
            latestConfig = intent;
            continue;
        }
        if (intent.topicType == TopicType::Order) {
            handleOrder(intent);
            continue;
        }
        if (intent.topicType == TopicType::Visualization) {
            handleVisualization(intent);
            continue;
        }
        pushTrace(nowMs(), "inbox", "dropUnknown", intent.serial, intent.topicType, intent.headerId, "ignored", 0, intent.topic);
    }

    if (latestConfig.has_value()) {
        handleSimConfig(*latestConfig);
    }
    if (latestInstant.has_value()) {
        handleInstantActions(*latestInstant);
    }
}

void SimInstanceCoordinator::handleVisualization(const CommandIntent& intent)
{
    const uint64_t startTs = nowMs();
    const uint64_t parseStart = nowMs();
    try {
        const simagv::json::Object obj = asObjectOrThrow(intent.payload);
        const std::string manufacturer = readStringOr(obj, "manufacturer", "manufacturer", "");
        const std::string serialNumber = readStringOr(obj, "serialNumber", "serialNumber", "");
        if (manufacturer.empty() || serialNumber.empty()) {
            throw std::runtime_error("missing_identity");
        }
        if (manufacturer == manufacturer_ && serialNumber == serialNumber_) {
            pushTrace(startTs, "visIn", "dropSelf", intent.serial, intent.topicType, intent.headerId, "ignored", static_cast<uint32_t>(nowMs() - parseStart), "");
            return;
        }

        std::string mapId;
        if (const auto itPos = obj.find("agvPosition"); itPos != obj.end() && itPos->second.isObject()) {
            const simagv::json::Object& posObj = itPos->second.asObject();
            mapId = canonicalizeMapId(readStringOr(posObj, "mapId", "mapId", ""));
        }

        const auto itSafety = obj.find("safety");
        if (itSafety == obj.end() || !itSafety->second.isObject()) {
            throw std::runtime_error("missing_safety");
        }
        const simagv::json::Object& safetyObj = itSafety->second.asObject();
        const auto itCenter = safetyObj.find("center");
        if (itCenter == safetyObj.end() || !itCenter->second.isObject()) {
            throw std::runtime_error("missing_safety_center");
        }
        const simagv::json::Object& centerObj = itCenter->second.asObject();
        const float cx = readFloatOr(centerObj, "x", "x", 0.0f);
        const float cy = readFloatOr(centerObj, "y", "y", 0.0f);
        const float length = readFloatOr(safetyObj, "length", "length", 0.0f);
        const float width = readFloatOr(safetyObj, "width", "width", 0.0f);
        const float theta = readFloatOr(safetyObj, "theta", "theta", 0.0f);
        if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(length) || !std::isfinite(width) || !std::isfinite(theta)) {
            throw std::runtime_error("invalid_safety_number");
        }
        if (length <= 1e-6f || width <= 1e-6f) {
            throw std::runtime_error("invalid_safety_size");
        }

        OtherVehicleVisualization vis;
        vis.manufacturer = manufacturer;
        vis.serialNumber = serialNumber;
        vis.mapId = std::move(mapId);
        vis.safetyCenterX = cx;
        vis.safetyCenterY = cy;
        vis.safetyLength = length;
        vis.safetyWidth = width;
        vis.safetyTheta = theta;
        vis.receiveTimestampMs = intent.receiveTimestampMs;

        const std::string key = manufacturer + "/" + serialNumber;
        otherVisualizationsByKey_.insert_or_assign(key, std::move(vis));
        pushTrace(startTs, "visIn", "cache", intent.serial, intent.topicType, intent.headerId, "ok", static_cast<uint32_t>(nowMs() - parseStart), key);
    } catch (const std::exception& e) {
        pushTrace(startTs, "visIn", "cache", intent.serial, intent.topicType, intent.headerId, "fail", static_cast<uint32_t>(nowMs() - parseStart), e.what());
    } catch (...) {
        pushTrace(startTs, "visIn", "cache", intent.serial, intent.topicType, intent.headerId, "fail", static_cast<uint32_t>(nowMs() - parseStart), "unknown");
    }
}

void SimInstanceCoordinator::handleOrder(const CommandIntent& intent)
{
    const uint64_t startTs = nowMs();
    const uint64_t parseStart = nowMs(); // 解析开始
    simagv::json::Object orderObj; // order对象
    std::string orderId;
    uint64_t orderUpdateId = 0U;
    std::string timestamp;
    try {
        if (!intent.payload.isObject()) {
            throw std::runtime_error("order_payload_not_object");
        }
        orderObj = intent.payload.asObject();
        timestamp = readStringOr(orderObj, "timestamp", "timestamp", "");
        orderId = readStringOr(orderObj, "order_id", "orderId", "");
        orderUpdateId = readUintOr(orderObj, "order_update_id", "orderUpdateId", 0U);
        size_t nodesCount = 0U;
        size_t edgesCount = 0U;
        if (const auto itNodes = orderObj.find("nodes"); itNodes != orderObj.end() && itNodes->second.isArray()) {
            nodesCount = itNodes->second.asArray().size();
        }
        if (const auto itEdges = orderObj.find("edges"); itEdges != orderObj.end() && itEdges->second.isArray()) {
            edgesCount = itEdges->second.asArray().size();
        }
        {
            std::ostringstream oss;
            oss << "order_received headerId=" << intent.headerId << " timestamp=" << timestamp << " orderId=" << orderId
                << " orderUpdateId=" << orderUpdateId << " nodes=" << nodesCount << " edges=" << edgesCount
                << " topic=" << intent.topic << " mapIdHint=" << intent.mapId;
            simagv::l4::logInfo(oss.str());
        }
        pushTrace(startTs, "orderIn", "parseOrder", intent.serial, intent.topicType, intent.headerId, "ok", static_cast<uint32_t>(nowMs() - parseStart), "");
    } catch (const std::exception& e) {
        {
            std::ostringstream oss;
            oss << "order_reject step=parseOrder headerId=" << intent.headerId << " topic=" << intent.topic << " err=" << e.what();
            simagv::l4::logWarn(oss.str());
        }
        pushTrace(startTs, "orderIn", "parseOrder", intent.serial, intent.topicType, intent.headerId, "reject", static_cast<uint32_t>(nowMs() - parseStart), e.what());
        return;
    }

    const uint64_t identityStart = nowMs(); // 身份校验开始
    try {
        const std::string manufacturer = readStringOr(orderObj, "manufacturer", "manufacturer", ""); // 厂商
        const std::string serialNumber = readStringOr(orderObj, "serial_number", "serialNumber", ""); // 车号
        const std::string version = readStringOr(orderObj, "version", "version", ""); // 协议版本
        if (manufacturer != manufacturer_ || serialNumber != serialNumber_ || version != protocolVersion_) {
            throw std::runtime_error("identity_mismatch");
        }
        pushTrace(startTs, "orderIn", "validateIdentity", intent.serial, intent.topicType, intent.headerId, "ok", static_cast<uint32_t>(nowMs() - identityStart), "");
    } catch (const std::exception& e) {
        {
            std::ostringstream oss;
            oss << "order_reject step=validateIdentity headerId=" << intent.headerId << " orderId=" << orderId << " err=" << e.what();
            simagv::l4::logWarn(oss.str());
        }
        pushTrace(startTs, "orderIn", "validateIdentity", intent.serial, intent.topicType, intent.headerId, "reject", static_cast<uint32_t>(nowMs() - identityStart), e.what());
        return;
    }

    const uint64_t tsStart = nowMs(); // 时间校验开始
    try {
        timestamp = readStringOr(orderObj, "timestamp", "timestamp", ""); // 订单时间戳
        const uint64_t orderTsMs = parseIso8601UtcMs(timestamp); // 订单时间(ms)
        const uint64_t nowTsMs = nowMs(); // 当前时间(ms)
        const uint64_t deltaMs = (orderTsMs > nowTsMs) ? (orderTsMs - nowTsMs) : (nowTsMs - orderTsMs); // 时间差
        if (deltaMs > orderTimestampWindowMs_) {
            throw std::runtime_error("timestamp_out_of_window");
        }
        pushTrace(startTs, "orderIn", "validateTimestamp", intent.serial, intent.topicType, intent.headerId, "ok", static_cast<uint32_t>(nowMs() - tsStart), "");
    } catch (const std::exception& e) {
        {
            std::ostringstream oss;
            oss << "order_reject step=validateTimestamp headerId=" << intent.headerId << " orderId=" << orderId << " timestamp=" << timestamp
                << " err=" << e.what();
            simagv::l4::logWarn(oss.str());
        }
        pushTrace(startTs, "orderIn", "validateTimestamp", intent.serial, intent.topicType, intent.headerId, "reject", static_cast<uint32_t>(nowMs() - tsStart), e.what());
        return;
    }

    const uint64_t mapStart = nowMs(); // 地图预加载开始
    std::string mapId; // 地图ID
    try {
        const bool allowFallbackToLoadedMap = orderNodesAllMissingNodePosition(orderObj);
        mapId = resolveOrderMapId(orderObj, intent.mapId); // 地图解析
        bool mapReady = false;
        if (!mapId.empty()) {
            preloadMapIfNeeded(mapId, startTs, "orderIn", "loadMapTopo", intent);
            mapReady = (loadedMapTopo_ != nullptr && loadedMapId_ == mapId);
        }
        if (!mapReady && allowFallbackToLoadedMap && loadedMapTopo_ != nullptr && !loadedMapId_.empty()) {
            mapId = loadedMapId_;
            mapReady = true;
        }
        if (!mapReady) {
            if (mapId.empty()) {
                throw std::runtime_error("missing_map_id");
            }
            throw std::runtime_error("map_not_loaded");
        }
        {
            std::ostringstream oss;
            oss << "order_map_ready orderId=" << orderId << " orderUpdateId=" << orderUpdateId << " mapId=" << mapId;
            simagv::l4::logInfo(oss.str());
        }
        pushTrace(startTs, "orderIn", "preloadMapTopo", intent.serial, intent.topicType, intent.headerId, "ok", static_cast<uint32_t>(nowMs() - mapStart), mapId);
    } catch (const std::exception& e) {
        {
            std::ostringstream oss;
            oss << "order_reject step=preloadMapTopo headerId=" << intent.headerId << " orderId=" << orderId << " err=" << e.what();
            simagv::l4::logWarn(oss.str());
        }
        pushTrace(startTs, "orderIn", "preloadMapTopo", intent.serial, intent.topicType, intent.headerId, "reject", static_cast<uint32_t>(nowMs() - mapStart), e.what());
        return;
    }

    const uint64_t patchStart = nowMs(); // 补全开始
    try {
        auto itNodes = orderObj.find("nodes"); // nodes
        if (itNodes != orderObj.end() && itNodes->second.isArray()) {
            simagv::json::Array& nodes = itNodes->second.asArray(); // 节点数组
            for (simagv::json::Value& nodeValue : nodes) {
                if (!nodeValue.isObject()) {
                    continue;
                }
                simagv::json::Object& nodeObj = nodeValue.asObject(); // 节点对象
                const auto itNodePos = nodeObj.find("nodePosition"); // nodePosition字段
                const bool needFill = (itNodePos == nodeObj.end()) || itNodePos->second.isNull() || !itNodePos->second.isObject();
                if (needFill) {
                    fillNodePositionFromTopoIfMissing(nodeObj, *loadedMapTopo_, mapId);
                    continue;
                }

                const simagv::json::Object& nodePosObj = itNodePos->second.asObject(); // nodePosition对象
                const bool hasX = (nodePosObj.find("x") != nodePosObj.end()) && nodePosObj.at("x").isNumber();
                const bool hasY = (nodePosObj.find("y") != nodePosObj.end()) && nodePosObj.at("y").isNumber();
                if (!(hasX && hasY)) {
                    fillNodePositionFromTopoIfMissing(nodeObj, *loadedMapTopo_, mapId);
                }
            }
        }
        pushTrace(startTs, "orderIn", "patchNodePositions", intent.serial, intent.topicType, intent.headerId, "ok", static_cast<uint32_t>(nowMs() - patchStart), "");
    } catch (const std::exception& e) {
        {
            std::ostringstream oss;
            oss << "order_reject step=patchNodePositions headerId=" << intent.headerId << " orderId=" << orderId << " err=" << e.what();
            simagv::l4::logWarn(oss.str());
        }
        pushTrace(startTs, "orderIn", "patchNodePositions", intent.serial, intent.topicType, intent.headerId, "reject", static_cast<uint32_t>(nowMs() - patchStart), e.what());
        return;
    }

    const uint64_t edgeTrajStart = nowMs();
    try {
        patchEdgesTrajectoryFromTopoIfMissing(orderObj, *loadedMapTopo_);
        pushTrace(startTs, "orderIn", "patchEdgeTrajectories", intent.serial, intent.topicType, intent.headerId, "ok", static_cast<uint32_t>(nowMs() - edgeTrajStart), "");
    } catch (const std::exception& e) {
        {
            std::ostringstream oss;
            oss << "order_reject step=patchEdgeTrajectories headerId=" << intent.headerId << " orderId=" << orderId << " err=" << e.what();
            simagv::l4::logWarn(oss.str());
        }
        pushTrace(startTs, "orderIn", "patchEdgeTrajectories", intent.serial, intent.topicType, intent.headerId, "reject", static_cast<uint32_t>(nowMs() - edgeTrajStart), e.what());
        return;
    }

    const uint64_t validateStart = nowMs();
    try {
        if (loadedTopology_ == nullptr) {
            throw std::runtime_error("map_topology_not_loaded");
        }
        validateOrderNodesEdgesOrThrow(orderObj, *loadedTopology_);
        pushTrace(startTs, "orderIn", "validateNodesEdges", intent.serial, intent.topicType, intent.headerId, "ok", static_cast<uint32_t>(nowMs() - validateStart), "");
    } catch (const std::exception& e) {
        {
            std::ostringstream oss;
            oss << "order_reject step=validateNodesEdges headerId=" << intent.headerId << " orderId=" << orderId << " err=" << e.what();
            simagv::l4::logWarn(oss.str());
        }
        pushTrace(startTs, "orderIn", "validateNodesEdges", intent.serial, intent.topicType, intent.headerId, "reject", static_cast<uint32_t>(nowMs() - validateStart), e.what());
        return;
    }

    std::string orderIdentityKey;
    const uint64_t acceptStart = nowMs(); // 接受校验开始
    try {
        orderId = readStringOr(orderObj, "order_id", "orderId", ""); // 订单号
        orderUpdateId = readUintOr(orderObj, "order_update_id", "orderUpdateId", 0U); // 订单更新号
        if (orderId.empty()) {
            throw std::runtime_error("missing_order_id");
        }

        orderIdentityKey = buildOrderIdentityKey(orderId, orderUpdateId);
        if (acceptedOrderIdentitySet_.count(orderIdentityKey) != 0U) {
            throw std::runtime_error("duplicate_order_identity");
        }
        if (!lastAcceptedOrderId_.empty() && orderId == lastAcceptedOrderId_ && orderUpdateId <= lastAcceptedOrderUpdateId_) {
            throw std::runtime_error("order_update_id_not_increment");
        }

        pushTrace(startTs, "orderIn", "validateOrderCache", intent.serial, intent.topicType, intent.headerId, "ok", static_cast<uint32_t>(nowMs() - acceptStart), orderId);
    } catch (const std::exception& e) {
        {
            std::ostringstream oss;
            oss << "order_reject step=validateOrderCache headerId=" << intent.headerId << " orderId=" << orderId << " orderUpdateId=" << orderUpdateId
                << " err=" << e.what();
            simagv::l4::logWarn(oss.str());
        }
        pushTrace(startTs, "orderIn", "validateOrderCache", intent.serial, intent.topicType, intent.headerId, "reject", static_cast<uint32_t>(nowMs() - acceptStart), e.what());
        return;
    }

    const uint64_t applyStart = nowMs(); // 应用开始
    try {
        engine_.applyOrder(simagv::json::Value{std::move(orderObj)});
        if (!lastAcceptedOrderId_.empty() && orderId == lastAcceptedOrderId_) {
            lastAcceptedOrderUpdateId_ = orderUpdateId;
        } else {
            lastAcceptedOrderId_ = orderId;
            lastAcceptedOrderUpdateId_ = orderUpdateId;
        }
        if (!orderIdentityKey.empty()) {
            const auto inserted = acceptedOrderIdentitySet_.insert(orderIdentityKey).second;
            if (inserted) {
                acceptedOrderIdentities_.push_back(orderIdentityKey);
                if (acceptedOrderIdentities_.size() > kAcceptedOrderIdentityCacheMax) {
                    const std::string evicted = acceptedOrderIdentities_.front();
                    acceptedOrderIdentities_.pop_front();
                    acceptedOrderIdentitySet_.erase(evicted);
                }
            }
        }
        {
            std::ostringstream oss;
            oss << "order_accepted headerId=" << intent.headerId << " orderId=" << orderId << " orderUpdateId=" << orderUpdateId;
            simagv::l4::logInfo(oss.str());
        }
        pushTrace(startTs, "orderIn", "applyOrder", intent.serial, intent.topicType, intent.headerId, "ok", static_cast<uint32_t>(nowMs() - applyStart), "");
    } catch (const std::exception& e) {
        {
            std::ostringstream oss;
            oss << "order_apply_failed headerId=" << intent.headerId << " orderId=" << orderId << " err=" << e.what();
            simagv::l4::logWarn(oss.str());
        }
        pushTrace(startTs, "orderIn", "applyOrder", intent.serial, intent.topicType, intent.headerId, "reject", static_cast<uint32_t>(nowMs() - applyStart), e.what());
    } catch (...) {
        {
            std::ostringstream oss;
            oss << "order_apply_failed headerId=" << intent.headerId << " orderId=" << orderId << " err=unknown";
            simagv::l4::logWarn(oss.str());
        }
        pushTrace(startTs, "orderIn", "applyOrder", intent.serial, intent.topicType, intent.headerId, "reject", static_cast<uint32_t>(nowMs() - applyStart), "unknown");
    }
}

void SimInstanceCoordinator::handleInstantActions(const CommandIntent& intent)
{
    const uint64_t startTs = nowMs();
    const uint64_t parseStart = nowMs();
    try {
        if (!intent.payload.isObject()) {
            throw std::runtime_error("instantActions payload not object");
        }
        if (intent.headerId != 0 && intent.headerId == lastInstantHeaderId_) {
            {
                std::ostringstream oss;
                oss << "instant_deduped headerId=" << intent.headerId << " topic=" << intent.topic;
                simagv::l4::logInfo(oss.str());
            }
            pushTrace(startTs, "instantIn", "dedupeInstant", intent.serial, intent.topicType, intent.headerId, "ignored", static_cast<uint32_t>(nowMs() - parseStart), "");
            return;
        }
        {
            const simagv::json::Object obj = intent.payload.asObject();
            const std::string timestamp = readStringOr(obj, "timestamp", "timestamp", "");
            size_t actionsCount = 0U;
            if (const auto itActions = obj.find("actions"); itActions != obj.end() && itActions->second.isArray()) {
                actionsCount = itActions->second.asArray().size();
            }
            std::ostringstream oss;
            oss << "instant_received headerId=" << intent.headerId << " timestamp=" << timestamp << " actions=" << actionsCount << " topic=" << intent.topic
                << " mapIdHint=" << intent.mapId;
            simagv::l4::logInfo(oss.str());
        }
        pushTrace(startTs, "instantIn", "parseInstantActions", intent.serial, intent.topicType, intent.headerId, "ok", static_cast<uint32_t>(nowMs() - parseStart), "");
    } catch (const std::exception& e) {
        {
            std::ostringstream oss;
            oss << "instant_reject step=parseInstantActions headerId=" << intent.headerId << " topic=" << intent.topic << " err=" << e.what();
            simagv::l4::logWarn(oss.str());
        }
        pushTrace(startTs, "instantIn", "parseInstantActions", intent.serial, intent.topicType, intent.headerId, "reject", static_cast<uint32_t>(nowMs() - parseStart), e.what());
        return;
    }

    const uint64_t applyStart = nowMs();
    try {
        simagv::json::Value patchedPayload = intent.payload;
        simagv::json::Object& patchedObj = patchedPayload.asObject();
        const std::string mapId = readSwitchMapTarget(patchedObj);
        preloadMapIfNeeded(mapId, startTs, "instantIn", "preloadMapTopo", intent);
        const simagv::l4::VehicleMapTopoPackage* p_Topo = nullptr;
        if (!mapId.empty() && loadedMapTopo_ != nullptr && loadedMapId_ == mapId) {
            p_Topo = loadedMapTopo_.get();
        }
        const InstantRequests req = patchInstantActionsByTopoIfNeeded(patchedObj, p_Topo);
        engine_.applyInstantActions(patchedPayload);
        lastInstantHeaderId_ = intent.headerId;
        {
            std::ostringstream oss;
            oss << "instant_applied headerId=" << intent.headerId << " switchMapTarget=" << mapId;
            simagv::l4::logInfo(oss.str());
        }
        if (req.factsheet || req.state) {
            const Snapshot snapshot = engine_.buildSnapshot(nowMs());
            if (req.factsheet) {
                publishTopic(TopicType::Factsheet, snapshot);
            }
            if (req.state) {
                publishTopic(TopicType::State, snapshot);
            }
        }
        pushTrace(startTs, "instantIn", "applyInstant", intent.serial, intent.topicType, intent.headerId, "ok", static_cast<uint32_t>(nowMs() - applyStart), "");
    } catch (const std::exception& e) {
        {
            std::ostringstream oss;
            oss << "instant_apply_failed headerId=" << intent.headerId << " err=" << e.what();
            simagv::l4::logWarn(oss.str());
        }
        pushTrace(startTs, "instantIn", "applyInstant", intent.serial, intent.topicType, intent.headerId, "reject", static_cast<uint32_t>(nowMs() - applyStart), e.what());
    } catch (...) {
        {
            std::ostringstream oss;
            oss << "instant_apply_failed headerId=" << intent.headerId << " err=unknown";
            simagv::l4::logWarn(oss.str());
        }
        pushTrace(startTs, "instantIn", "applyInstant", intent.serial, intent.topicType, intent.headerId, "reject", static_cast<uint32_t>(nowMs() - applyStart), "unknown");
    }
}

void SimInstanceCoordinator::handleSimConfig(const CommandIntent& intent)
{
    const uint64_t startTs = nowMs();
    std::string controlType = "applyConfig";
    std::string timestamp;
    try {
        if (!intent.payload.isObject()) {
            throw std::runtime_error("simConfig payload not object");
        }
        const simagv::json::Object obj = intent.payload.asObject();
        controlType = readStringOr(obj, "control_type", "controlType", controlType);
        timestamp = readStringOr(obj, "timestamp", "timestamp", "");
    } catch (...) {
        controlType = "applyConfig";
    }

    {
        std::ostringstream oss;
        oss << "simconfig_received headerId=" << intent.headerId << " controlType=" << controlType << " timestamp=" << timestamp << " topic=" << intent.topic;
        simagv::l4::logInfo(oss.str());
    }

    if (controlType == "power") {
        const uint64_t validateStart = nowMs();
        try {
            pushTrace(startTs, "controlPower", "validateRequest", intent.serial, intent.topicType, intent.headerId, "ok", static_cast<uint32_t>(nowMs() - validateStart), "");
        } catch (...) {
            pushTrace(startTs, "controlPower", "validateRequest", intent.serial, intent.topicType, intent.headerId, "fail", static_cast<uint32_t>(nowMs() - validateStart), "unknown");
            return;
        }

        const uint64_t publishStart = nowMs();
        try {
            const simagv::json::Object obj = intent.payload.asObject();
            const float onRaw = readFloatOr(obj, "power_on", "powerOn", 1.0F);
            powerOn_ = (onRaw >= 0.5F);
            {
                std::ostringstream oss;
                oss << "simconfig_power_applied headerId=" << intent.headerId << " powerOn=" << (powerOn_ ? "true" : "false");
                simagv::l4::logInfo(oss.str());
            }
            pushTrace(startTs, "controlPower", "publishPowerToMqtt", intent.serial, intent.topicType, intent.headerId, "ok", static_cast<uint32_t>(nowMs() - publishStart), "");
        } catch (const std::exception& e) {
            {
                std::ostringstream oss;
                oss << "simconfig_power_failed headerId=" << intent.headerId << " err=" << e.what();
                simagv::l4::logWarn(oss.str());
            }
            pushTrace(startTs, "controlPower", "publishPowerToMqtt", intent.serial, intent.topicType, intent.headerId, "fail", static_cast<uint32_t>(nowMs() - publishStart), e.what());
        } catch (...) {
            {
                std::ostringstream oss;
                oss << "simconfig_power_failed headerId=" << intent.headerId << " err=unknown";
                simagv::l4::logWarn(oss.str());
            }
            pushTrace(startTs, "controlPower", "publishPowerToMqtt", intent.serial, intent.topicType, intent.headerId, "fail", static_cast<uint32_t>(nowMs() - publishStart), "unknown");
        }
        return;
    }

    if (controlType == "delete") {
        const uint64_t validateStart = nowMs();
        try {
            pushTrace(startTs, "controlDelete", "validateRequest", intent.serial, intent.topicType, intent.headerId, "ok", static_cast<uint32_t>(nowMs() - validateStart), "");
        } catch (...) {
            pushTrace(startTs, "controlDelete", "validateRequest", intent.serial, intent.topicType, intent.headerId, "fail", static_cast<uint32_t>(nowMs() - validateStart), "unknown");
            return;
        }

        const uint64_t cleanupStart = nowMs();
        try {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                inbox_.clear();
            }
            booted_ = false;
            powerOn_ = false;
            {
                std::ostringstream oss;
                oss << "simconfig_delete_applied headerId=" << intent.headerId << " booted=false powerOn=false";
                simagv::l4::logInfo(oss.str());
            }
            pushTrace(startTs, "controlDelete", "shutdownAndCleanup", intent.serial, intent.topicType, intent.headerId, "ok", static_cast<uint32_t>(nowMs() - cleanupStart), "");
        } catch (const std::exception& e) {
            {
                std::ostringstream oss;
                oss << "simconfig_delete_failed headerId=" << intent.headerId << " err=" << e.what();
                simagv::l4::logWarn(oss.str());
            }
            pushTrace(startTs, "controlDelete", "shutdownAndCleanup", intent.serial, intent.topicType, intent.headerId, "fail", static_cast<uint32_t>(nowMs() - cleanupStart), e.what());
        } catch (...) {
            {
                std::ostringstream oss;
                oss << "simconfig_delete_failed headerId=" << intent.headerId << " err=unknown";
                simagv::l4::logWarn(oss.str());
            }
            pushTrace(startTs, "controlDelete", "shutdownAndCleanup", intent.serial, intent.topicType, intent.headerId, "fail", static_cast<uint32_t>(nowMs() - cleanupStart), "unknown");
        }
        return;
    }

    const uint64_t validateStart = nowMs();
    try {
        pushTrace(startTs, "controlApplyConfig", "validateRequest", intent.serial, intent.topicType, intent.headerId, "ok", static_cast<uint32_t>(nowMs() - validateStart), "");
    } catch (...) {
        pushTrace(startTs, "controlApplyConfig", "validateRequest", intent.serial, intent.topicType, intent.headerId, "fail", static_cast<uint32_t>(nowMs() - validateStart), "unknown");
        return;
    }

    const uint64_t buildStart = nowMs();
    try {
        updateRuntimeConfigFromSimConfig(intent.payload);
        {
            std::ostringstream oss;
            oss << "simconfig_apply_built headerId=" << intent.headerId << " stateHz=" << config_.stateFrequencyHz
                << " visHz=" << config_.visualizationFrequencyHz << " connectionHz=" << config_.connectionFrequencyHz
                << " factsheetHz=" << config_.factsheetFrequencyHz << " maxPublishHz=" << config_.publishLimits.maxPublishHz
                << " simTimeScale=" << config_.simTimeScale;
            simagv::l4::logInfo(oss.str());
        }
        pushTrace(startTs, "controlApplyConfig", "buildSimConfig", intent.serial, intent.topicType, intent.headerId, "ok", static_cast<uint32_t>(nowMs() - buildStart), "");
    } catch (const std::exception& e) {
        {
            std::ostringstream oss;
            oss << "simconfig_apply_build_failed headerId=" << intent.headerId << " err=" << e.what();
            simagv::l4::logWarn(oss.str());
        }
        pushTrace(startTs, "controlApplyConfig", "buildSimConfig", intent.serial, intent.topicType, intent.headerId, "fail", static_cast<uint32_t>(nowMs() - buildStart), e.what());
        return;
    }

    const uint64_t applyStart = nowMs();
    try {
        const simagv::json::Object obj = intent.payload.asObject();
        const std::string mapId = readMapIdFromSimConfig(obj);
        preloadMapIfNeeded(mapId, startTs, "controlApplyConfig", "preloadMapTopo", intent);
        engine_.applyConfig(intent.payload);
        {
            std::ostringstream oss;
            oss << "simconfig_apply_applied headerId=" << intent.headerId << " mapId=" << mapId;
            simagv::l4::logInfo(oss.str());
        }
        pushTrace(startTs, "controlApplyConfig", "publishConfigToMqtt", intent.serial, intent.topicType, intent.headerId, "ok", static_cast<uint32_t>(nowMs() - applyStart), "");
    } catch (const std::exception& e) {
        {
            std::ostringstream oss;
            oss << "simconfig_apply_failed headerId=" << intent.headerId << " err=" << e.what();
            simagv::l4::logWarn(oss.str());
        }
        pushTrace(startTs, "controlApplyConfig", "publishConfigToMqtt", intent.serial, intent.topicType, intent.headerId, "fail", static_cast<uint32_t>(nowMs() - applyStart), e.what());
    } catch (...) {
        {
            std::ostringstream oss;
            oss << "simconfig_apply_failed headerId=" << intent.headerId << " err=unknown";
            simagv::l4::logWarn(oss.str());
        }
        pushTrace(startTs, "controlApplyConfig", "publishConfigToMqtt", intent.serial, intent.topicType, intent.headerId, "fail", static_cast<uint32_t>(nowMs() - applyStart), "unknown");
    }
}

void SimInstanceCoordinator::preloadMapIfNeeded(const std::string& mapIdRaw, uint64_t startTs, const char* flowId, const char* stepId, const CommandIntent& intent)
{
    const auto itNonSpace = std::find_if(mapIdRaw.begin(), mapIdRaw.end(), [](unsigned char ch) { return !std::isspace(ch); });
    if (itNonSpace == mapIdRaw.end()) {
        return;
    }
    const std::string mapId = canonicalizeMapId(mapIdRaw);
    if (mapId == loadedMapId_) {
        return;
    }

    const uint64_t loadStart = nowMs();
    try {
        const auto itTopo = loadedMapTopoCache_.find(mapId);
        const auto itTopology = loadedTopologyCache_.find(mapId);
        if (itTopo != loadedMapTopoCache_.end() && itTopology != loadedTopologyCache_.end()) {
            loadedMapId_ = mapId;
            loadedMapTopo_ = itTopo->second;
            loadedTopology_ = itTopology->second;
            pushTrace(startTs, flowId, stepId, intent.serial, intent.topicType, intent.headerId, "ok", static_cast<uint32_t>(nowMs() - loadStart), loadedMapId_);
            return;
        }

        const std::string topoPath = simagv::l4::resolveVehicleMapTopoPath(mapId);
        simagv::l4::VehicleMapTopoPackage pkg = simagv::l4::parseVehicleMapTopoFile(topoPath);
        simagv::l3::TopologyOptions options{};
        options.positionToleranceM = 0.0F;
        simagv::l3::MapTopology topology = simagv::l3::buildMapTopology(pkg.topology, options);

        auto pkgPtr = std::make_shared<simagv::l4::VehicleMapTopoPackage>(std::move(pkg));
        auto topologyPtr = std::make_shared<simagv::l3::MapTopology>(std::move(topology));

        loadedMapTopoCache_[mapId] = pkgPtr;
        loadedTopologyCache_[mapId] = topologyPtr;
        loadedMapId_ = mapId;
        loadedMapTopo_ = std::move(pkgPtr);
        loadedTopology_ = std::move(topologyPtr);
        pushTrace(startTs, flowId, stepId, intent.serial, intent.topicType, intent.headerId, "ok", static_cast<uint32_t>(nowMs() - loadStart), loadedMapId_);
    } catch (const std::exception& e) {
        pushTrace(startTs, flowId, stepId, intent.serial, intent.topicType, intent.headerId, "fail", static_cast<uint32_t>(nowMs() - loadStart), e.what());
    } catch (...) {
        pushTrace(startTs, flowId, stepId, intent.serial, intent.topicType, intent.headerId, "fail", static_cast<uint32_t>(nowMs() - loadStart), "unknown");
    }
}

void SimInstanceCoordinator::publishBootOnce(uint64_t nowMsValue)
{
    const Snapshot snapshot = engine_.buildSnapshot(nowMsValue);
    publishTopic(TopicType::Connection, snapshot);
    publishTopic(TopicType::Factsheet, snapshot);
    publishTopic(TopicType::State, snapshot);
    publishTopic(TopicType::Visualization, snapshot);
}

void SimInstanceCoordinator::publishBySchedule(uint32_t tickMs, uint64_t nowMsValue)
{
    const float maxHz = config_.publishLimits.maxPublishHz;
    const float effStateHz = computeEffectiveHz(config_.stateFrequencyHz, config_.simTimeScale, maxHz);
    const float effVisHz = computeEffectiveHz(config_.visualizationFrequencyHz, config_.simTimeScale, std::min(maxHz, effStateHz));
    const float effConnHz = computeEffectiveHzOrZero(config_.connectionFrequencyHz, config_.simTimeScale, maxHz);
    const float effFactsheetHz = computeEffectiveHzOrZero(config_.factsheetFrequencyHz, config_.simTimeScale, maxHz);

    schedule_.stateElapsedMs += static_cast<float>(tickMs);
    schedule_.visualizationElapsedMs += static_cast<float>(tickMs);
    schedule_.connectionElapsedMs += static_cast<float>(tickMs);
    schedule_.factsheetElapsedMs += static_cast<float>(tickMs);

    const float statePeriodMs = 1000.0F / effStateHz;
    const float visPeriodMs = 1000.0F / effVisHz;
    const float connPeriodMs = (effConnHz > 0.0F) ? (1000.0F / effConnHz) : std::numeric_limits<float>::infinity();
    const float factsheetPeriodMs = (effFactsheetHz > 0.0F) ? (1000.0F / effFactsheetHz) : std::numeric_limits<float>::infinity();

    const bool shouldState = schedule_.stateElapsedMs >= statePeriodMs;
    const bool shouldVis = schedule_.visualizationElapsedMs >= visPeriodMs;
    const bool shouldConn = schedule_.connectionElapsedMs >= connPeriodMs;
    const bool shouldFactsheet = schedule_.factsheetElapsedMs >= factsheetPeriodMs;

    if (!(shouldState || shouldVis || shouldConn || shouldFactsheet)) {
        return;
    }

    Snapshot snapshot = engine_.buildSnapshot(nowMsValue);

    if (shouldState) {
        schedule_.stateElapsedMs = 0.0F;
        publishTopic(TopicType::State, snapshot);
    }
    if (shouldVis) {
        schedule_.visualizationElapsedMs = 0.0F;
        publishTopic(TopicType::Visualization, snapshot);
    }
    if (shouldConn) {
        schedule_.connectionElapsedMs = 0.0F;
        publishTopic(TopicType::Connection, snapshot);
    }
    if (shouldFactsheet) {
        schedule_.factsheetElapsedMs = 0.0F;
        publishTopic(TopicType::Factsheet, snapshot);
    }
}

void SimInstanceCoordinator::publishTopic(TopicType topicType, const Snapshot& snapshot)
{
    std::string topicSuffix;
    simagv::json::Object payloadData;
    bool retain = false;
    uint64_t headerId = 0;

    if (topicType == TopicType::State) {
        topicSuffix = "/state";
        payloadData = snapshot.state;
        headerId = ++stateHeaderId_;
    } else if (topicType == TopicType::Visualization) {
        topicSuffix = "/visualization";
        payloadData = snapshot.visualization;
        headerId = ++visualizationHeaderId_;
    } else if (topicType == TopicType::Connection) {
        topicSuffix = "/connection";
        payloadData = snapshot.connection;
        headerId = ++connectionHeaderId_;
    } else if (topicType == TopicType::Factsheet) {
        topicSuffix = "/factsheet";
        payloadData = snapshot.factsheet;
        headerId = ++factsheetHeaderId_;
        retain = true;
    } else {
        return;
    }

    payloadData["headerId"] = simagv::json::Value{static_cast<double>(headerId)};
    payloadData["timestamp"] = simagv::json::Value{formatIsoTimestamp(snapshot.timestampMs)};
    payloadData["version"] = simagv::json::Value{protocolVersion_};
    payloadData["manufacturer"] = simagv::json::Value{manufacturer_};
    payloadData["serialNumber"] = simagv::json::Value{serialNumber_};
    if (topicType == TopicType::State && loadedTopology_ != nullptr && !loadedTopology_->stations.empty()) {
        const auto itPos = payloadData.find("agvPosition");
        if (itPos != payloadData.end() && itPos->second.isObject()) {
            const simagv::json::Object& posObj = itPos->second.asObject();
            const auto itX = posObj.find("x");
            const auto itY = posObj.find("y");
            if (itX != posObj.end() && itY != posObj.end() && itX->second.isNumber() && itY->second.isNumber()) {
                const float x = static_cast<float>(itX->second.asNumber());
                const float y = static_cast<float>(itY->second.asNumber());
                float bestDist2 = std::numeric_limits<float>::infinity();
                std::string bestId;
                for (const auto& st : loadedTopology_->stations) {
                    const float dx = st.position.x - x;
                    const float dy = st.position.y - y;
                    const float d2 = dx * dx + dy * dy;
                    if (d2 < bestDist2) {
                        bestDist2 = d2;
                        bestId = !st.name.empty() ? st.name : st.id;
                    }
                }
                if (!bestId.empty()) {
                    payloadData["lastNodeId"] = simagv::json::Value{bestId};
                }
            }
        }
    }

    std::string jsonStr;
    if (topicType == TopicType::State) {
        jsonStr = toStateJsonStringOrdered(payloadData);
    } else {
        jsonStr = toJsonString(simagv::json::Value{std::move(payloadData)});
    }
    diplomat_.publish(mqttBaseTopic_ + topicSuffix, jsonStr, 0, retain);
}

void SimInstanceCoordinator::updateRuntimeConfigFromSimConfig(const simagv::json::Value& simConfig)
{
    const simagv::json::Object obj = asObjectOrThrow(simConfig);
    config_.simTimeScale = readFloatOr(obj, "sim_time_scale", "simTimeScale", config_.simTimeScale);
    config_.stateFrequencyHz = readFloatOr(obj, "state_frequency", "stateFrequency", config_.stateFrequencyHz);
    config_.visualizationFrequencyHz = readFloatOr(obj, "visualization_frequency", "visualizationFrequency", config_.visualizationFrequencyHz);
    config_.connectionFrequencyHz = readFloatOr(obj, "connection_frequency", "connectionFrequency", config_.connectionFrequencyHz);
    config_.factsheetFrequencyHz = readFloatOr(obj, "factsheet_frequency", "factsheetFrequency", config_.factsheetFrequencyHz);
    config_.publishLimits.maxPublishHz = readFloatOr(obj, "max_publish_hz", "maxPublishHz", config_.publishLimits.maxPublishHz);
    disableRadarBlockedOnRotation_ = readBoolOr(obj, "disable_radar_blocked_on_rotation", "disableRadarBlockedOnRotation", disableRadarBlockedOnRotation_);
    disableRadarBlockedOmegaThreshold_ =
        readFloatOr(obj, "disable_radar_blocked_omega_threshold", "disableRadarBlockedOmegaThreshold", disableRadarBlockedOmegaThreshold_);
    radarBlockedMinSpeedThreshold_ = readFloatOr(obj, "radar_blocked_min_speed_threshold", "radarBlockedMinSpeedThreshold", radarBlockedMinSpeedThreshold_);
    radarBlockedForwardSpeedThreshold_ =
        readFloatOr(obj, "radar_blocked_forward_speed_threshold", "radarBlockedForwardSpeedThreshold", radarBlockedForwardSpeedThreshold_);

    config_.simTimeScale = clampRange(config_.simTimeScale, 0.0001F, 1000.0F);
    config_.stateFrequencyHz = clampRange(config_.stateFrequencyHz, 1e-6F, 1000.0F);
    config_.visualizationFrequencyHz = clampRange(config_.visualizationFrequencyHz, 1e-6F, 1000.0F);
    config_.connectionFrequencyHz = clampRange(config_.connectionFrequencyHz, 0.0F, 1000.0F);
    config_.factsheetFrequencyHz = clampRange(config_.factsheetFrequencyHz, 0.0F, 1000.0F);
    config_.publishLimits.maxPublishHz = clampRange(config_.publishLimits.maxPublishHz, 1e-6F, 100.0F);
    disableRadarBlockedOmegaThreshold_ = clampRange(disableRadarBlockedOmegaThreshold_, 0.0F, 1000.0F);
    radarBlockedMinSpeedThreshold_ = clampRange(radarBlockedMinSpeedThreshold_, 0.0F, 1000.0F);
    radarBlockedForwardSpeedThreshold_ = clampRange(radarBlockedForwardSpeedThreshold_, 0.0F, 1000.0F);
    disableRadarBlockedOnRotation_ = true;
}

void SimInstanceCoordinator::pushTrace(
    uint64_t timestampMs,
    std::string flowId,
    std::string stepId,
    uint64_t serial,
    TopicType topicType,
    uint64_t headerId,
    std::string result,
    uint32_t elapsedMs,
    std::string summary)
{
    TraceRecord rec;
    rec.timestampMs = timestampMs;
    rec.flowId = std::move(flowId);
    rec.stepId = std::move(stepId);
    rec.serial = serial;
    rec.topicType = topicType;
    rec.headerId = headerId;
    rec.result = std::move(result);
    rec.elapsedMs = elapsedMs;
    rec.summary = std::move(summary);
    trace_.push(std::move(rec));
}

} // namespace simagv::l2
