#include "map_atoms.hpp"

#include "json_min.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace simagv::l4 {
namespace {

float hypot2(float dx, float dy) {
    return std::sqrt(dx * dx + dy * dy); // 二维勾股
}

std::string readTextFile(const std::string& filePath) {
    std::ifstream ifs(filePath); // 输入流
    if (!ifs.is_open()) {
        throw std::runtime_error("failed to open file"); // 打开失败
    }
    std::ostringstream oss; // 缓冲
    oss << ifs.rdbuf(); // 读取
    return oss.str(); // 返回
}

const simagv::json::Object* findObjectWithPointsRoutes(const simagv::json::Value& root) {
    struct StackItem {
        const simagv::json::Value* v; // 值指针
    };
    std::vector<StackItem> stack; // 栈
    stack.push_back(StackItem{&root}); // 初始
    while (!stack.empty()) {
        const simagv::json::Value* cur = stack.back().v; // 当前
        stack.pop_back(); // 出栈
        if (cur->isObject()) {
            const simagv::json::Object& obj = cur->asObject(); // 对象
            const auto itPoints = obj.find("points"); // points
            const auto itRoutes = obj.find("routes"); // routes
            if (itPoints != obj.end() && itRoutes != obj.end() && itPoints->second.isArray() && itRoutes->second.isArray()) {
                return &obj; // 匹配
            }
            for (const auto& kv : obj) {
                stack.push_back(StackItem{&kv.second}); // 深搜
            }
        } else if (cur->isArray()) {
            for (const auto& item : cur->asArray()) {
                stack.push_back(StackItem{&item}); // 深搜
            }
        }
    }
    return nullptr; // 未找到
}

float getNumberOr(const simagv::json::Object& obj, const std::string& key, float defaultValue) {
    const auto it = obj.find(key); // 查找
    if (it == obj.end() || !it->second.isNumber()) {
        return defaultValue; // 默认
    }
    return static_cast<float>(it->second.asNumber()); // 转换
}

std::string getStringOr(const simagv::json::Object& obj, const std::string& key, const std::string& defaultValue) {
    const auto it = obj.find(key); // 查找
    if (it == obj.end() || !it->second.isString()) {
        return defaultValue; // 默认
    }
    return it->second.asString(); // 返回
}

} // namespace

std::string resolveScenePath(const std::string& mapName) {
    std::string s = mapName; // 输入副本
    if (s.empty()) {
        throw std::invalid_argument("mapName empty"); // 参数异常
    }
    std::replace(s.begin(), s.end(), '\\', '/'); // 统一分隔符

    std::string fileName = s; // 文件名
    if (fileName.find('/') != std::string::npos) {
        fileName = fileName.substr(fileName.find_last_of('/') + 1); // 取尾部
    }
    if (fileName.size() >= 6U) {
        const std::string tail = fileName.substr(fileName.size() - 6U); // 后缀
        if (tail == ".scene" || tail == ".SCENE") {
            fileName = fileName.substr(0U, fileName.size() - 6U); // 去后缀
        }
    }
    if (fileName.empty()) {
        throw std::invalid_argument("mapName invalid"); // 参数异常
    }

    const std::filesystem::path base("../../maps"); // 地图根
    const std::filesystem::path vehiclePath = base / "VehicleMap" / (fileName + ".scene"); // VehicleMap
    const std::filesystem::path viewerPath = base / "ViewerMap" / (fileName + ".scene"); // ViewerMap

    if (std::filesystem::exists(vehiclePath)) {
        return std::filesystem::absolute(vehiclePath).string(); // 返回
    }
    if (std::filesystem::exists(viewerPath)) {
        return std::filesystem::absolute(viewerPath).string(); // 返回
    }
    throw std::runtime_error("scene file not found"); // 未找到
}

SceneTopologyData parseSceneTopologyFile(const std::string& sceneFilePath) {
    if (sceneFilePath.empty()) {
        throw std::invalid_argument("sceneFilePath empty"); // 参数异常
    }
    const std::string text = readTextFile(sceneFilePath); // 读取
    return parseSceneTopologyJson(text); // 解析
}

SceneTopologyData parseSceneTopologyJson(const std::string& sceneJson) {
    const simagv::json::Value root = simagv::json::parse(sceneJson); // 解析
    const simagv::json::Object* p_Target = findObjectWithPointsRoutes(root); // 查找目标对象
    if (p_Target == nullptr) {
        throw std::runtime_error("scene topology not found"); // 未找到
    }
    const simagv::json::Object& topObj = *p_Target; // 顶层对象
    const simagv::json::Array& points = topObj.at("points").asArray(); // points
    const simagv::json::Array& routes = topObj.at("routes").asArray(); // routes

    SceneTopologyData outData{}; // 输出
    outData.stations.reserve(points.size()); // 预分配
    outData.stationCatalog.reserve(points.size()); // 预分配

    for (const simagv::json::Value& p : points) {
        if (!p.isObject()) {
            continue; // 跳过
        }
        const simagv::json::Object& po = p.asObject(); // 点对象
        const std::string id = getStringOr(po, "id", ""); // id
        const std::string name = getStringOr(po, "name", ""); // name
        const float x = getNumberOr(po, "x", 0.0f); // x
        const float y = getNumberOr(po, "y", 0.0f); // y
        if (id.empty()) {
            continue; // 跳过
        }
        outData.stations.push_back(SceneStationNode{id, x, y}); // 节点

        SceneStationCatalogItem item{}; // 站点项
        item.id = id; // id
        item.instanceName = name; // 名称
        item.pointName = name; // 兼容
        item.pos = Position{x, y, 0.0f}; // 坐标
        item.type = (name.size() >= 2U) ? name.substr(0, 2) : ""; // 类型
        outData.stationCatalog.push_back(item); // 写入
    }

    outData.paths.reserve(routes.size()); // 预分配
    for (const simagv::json::Value& r : routes) {
        if (!r.isObject()) {
            continue; // 跳过
        }
        const simagv::json::Object& ro = r.asObject(); // 路径对象
        const std::string rid = getStringOr(ro, "id", ""); // id
        const std::string desc = getStringOr(ro, "desc", ""); // desc
        const std::string from = getStringOr(ro, "from", ""); // from
        const std::string to = getStringOr(ro, "to", ""); // to
        if (from.empty() || to.empty()) {
            continue; // 缺失
        }
        ScenePathEdge edge{}; // 边
        edge.id = !desc.empty() ? desc : (!rid.empty() ? rid : (from + "->" + to)); // 标识
        edge.from = from; // 起点
        edge.to = to; // 终点
        edge.length = 0.0f; // 待算
        edge.cp1 = Position{0.0f, 0.0f, 0.0f}; // 控制点
        edge.cp2 = Position{0.0f, 0.0f, 0.0f}; // 控制点

        const auto itC1 = ro.find("c1"); // c1
        if (itC1 != ro.end() && itC1->second.isObject()) {
            const simagv::json::Object& c1 = itC1->second.asObject(); // c1对象
            edge.cp1.x = getNumberOr(c1, "x", 0.0f); // x
            edge.cp1.y = getNumberOr(c1, "y", 0.0f); // y
        }
        const auto itC2 = ro.find("c2"); // c2
        if (itC2 != ro.end() && itC2->second.isObject()) {
            const simagv::json::Object& c2 = itC2->second.asObject(); // c2对象
            edge.cp2.x = getNumberOr(c2, "x", 0.0f); // x
            edge.cp2.y = getNumberOr(c2, "y", 0.0f); // y
        }
        outData.paths.push_back(edge); // 写入
    }

    auto findStation = [&](const std::string& sid) -> const SceneStationNode* {
        for (const SceneStationNode& st : outData.stations) {
            if (st.id == sid) {
                return &st; // 命中
            }
        }
        return nullptr; // 未找到
    };

    for (ScenePathEdge& edge : outData.paths) {
        const SceneStationNode* fromNode = findStation(edge.from); // from节点
        const SceneStationNode* toNode = findStation(edge.to); // to节点
        if (fromNode == nullptr || toNode == nullptr) {
            edge.length = 0.0f; // 无法计算
            continue;
        }
        const float dx = toNode->x - fromNode->x; // dx
        const float dy = toNode->y - fromNode->y; // dy
        edge.length = hypot2(dx, dy); // 近似长度
    }

    return outData; // 返回
}

StationPositionResult findStationPosition(const std::string& sceneFilePath, const std::string& stationId) {
    if (sceneFilePath.empty() || stationId.empty()) {
        throw std::invalid_argument("params empty"); // 参数异常
    }
    const SceneTopologyData topo = parseSceneTopologyFile(sceneFilePath); // 解析
    for (const SceneStationCatalogItem& item : topo.stationCatalog) {
        if (item.id == stationId || item.instanceName == stationId || item.pointName == stationId) {
            return StationPositionResult{true, item.pos}; // 命中
        }
    }
    return StationPositionResult{false, Position{0.0f, 0.0f, 0.0f}}; // 未找到
}

std::string findPointNameById(const std::string& sceneFilePath, const std::string& nodeId) {
    if (sceneFilePath.empty() || nodeId.empty()) {
        throw std::invalid_argument("params empty"); // 参数异常
    }
    const SceneTopologyData topo = parseSceneTopologyFile(sceneFilePath); // 解析
    for (const SceneStationCatalogItem& item : topo.stationCatalog) {
        if (item.id == nodeId) {
            return item.instanceName; // 返回
        }
    }
    return ""; // 未找到
}

std::string nearestStation(const Position& pos, const std::vector<SceneStationNode>& stations) {
    if (stations.empty()) {
        return ""; // 无站点
    }
    std::string bestId = ""; // 最佳ID
    float bestDist = std::numeric_limits<float>::infinity(); // 最佳距离
    for (const SceneStationNode& st : stations) {
        const float dx = pos.x - st.x; // dx
        const float dy = pos.y - st.y; // dy
        const float dist = hypot2(dx, dy); // 距离
        if (dist < bestDist) {
            bestDist = dist; // 更新
            bestId = st.id; // 更新
        }
    }
    return bestId; // 返回
}

LoadModel parseLoadModelFile(const std::string& modelFilePath) {
    if (modelFilePath.empty()) {
        throw std::invalid_argument("modelFilePath empty"); // 参数异常
    }
    const std::string text = readTextFile(modelFilePath); // 读取
    const simagv::json::Value root = simagv::json::parse(text); // 解析
    if (!root.isObject()) {
        throw std::runtime_error("load model invalid"); // 结构异常
    }
    const simagv::json::Object& obj = root.asObject(); // 对象

    LoadModel outModel{}; // 输出
    outModel.loadId = getStringOr(obj, "name", ""); // id
    outModel.loadType = getStringOr(obj, "loadType", getStringOr(obj, "type", "")); // 类型
    outModel.weightKg = getNumberOr(obj, "weight", 0.0f); // 重量

    outModel.dimensions = LoadDimensions{0.0f, 0.0f, 0.0f}; // 初始化
    const auto itDim = obj.find("loadDimensions"); // loadDimensions
    if (itDim != obj.end() && itDim->second.isObject()) {
        const simagv::json::Object& d = itDim->second.asObject(); // 维度
        outModel.dimensions.length = getNumberOr(d, "length", 0.0f); // 长
        outModel.dimensions.width = getNumberOr(d, "width", 0.0f); // 宽
        outModel.dimensions.height = getNumberOr(d, "height", 0.0f); // 高
    }

    outModel.footprint = BoundingBox{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}; // 初始化
    const auto itFp = obj.find("footprint"); // footprint
    if (itFp != obj.end() && itFp->second.isObject()) {
        const simagv::json::Object& fp = itFp->second.asObject(); // 足迹对象
        outModel.footprint.lengthM = getNumberOr(fp, "length_m", outModel.dimensions.length); // 长
        outModel.footprint.widthM = getNumberOr(fp, "width_m", outModel.dimensions.width); // 宽
        outModel.footprint.heightM = getNumberOr(fp, "height_m", outModel.dimensions.height); // 高
    }

    return outModel; // 返回
}

} // namespace simagv::l4

