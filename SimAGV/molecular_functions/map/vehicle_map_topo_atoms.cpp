#include "vehicle_map_topo_atoms.hpp"

#include "../../atom_functions/json_min.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace simagv::l4 {
namespace {

float hypot2(float dx, float dy)
{
    return std::sqrt(dx * dx + dy * dy); // 二维勾股
}

std::string readTextFile(const std::string& filePath)
{
    std::ifstream ifs(filePath); // 输入流
    if (!ifs.is_open()) {
        throw std::runtime_error("failed to open file"); // 打开失败
    }
    std::ostringstream oss; // 缓冲
    oss << ifs.rdbuf(); // 读取
    return oss.str(); // 返回
}

const simagv::json::Object* findObjectWithTopoFields(const simagv::json::Value& root)
{
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
            const auto itHeader = obj.find("header"); // header
            const auto itPoints = obj.find("advancedPointList"); // advancedPointList
            const auto itCurves = obj.find("advancedCurveList"); // advancedCurveList
            if (itHeader != obj.end() && itHeader->second.isObject() && itPoints != obj.end() && itPoints->second.isArray() &&
                itCurves != obj.end() && itCurves->second.isArray()) {
                return &obj; // 匹配
            }
            for (const auto& kv : obj) {
                stack.push_back(StackItem{&kv.second}); // 深搜
            }
            continue;
        }

        if (cur->isArray()) {
            for (const auto& item : cur->asArray()) {
                stack.push_back(StackItem{&item}); // 深搜
            }
        }
    }

    return nullptr; // 未找到
}

float getNumberOr(const simagv::json::Object& obj, const std::string& key, float defaultValue)
{
    const auto it = obj.find(key); // 查找
    if (it == obj.end() || !it->second.isNumber()) {
        return defaultValue; // 默认
    }
    return static_cast<float>(it->second.asNumber()); // 转换
}

std::string getStringOr(const simagv::json::Object& obj, const std::string& key, const std::string& defaultValue)
{
    const auto it = obj.find(key); // 查找
    if (it == obj.end() || !it->second.isString()) {
        return defaultValue; // 默认
    }
    return it->second.asString(); // 返回
}

Point2D parsePoint2DOrDefault(const simagv::json::Object& obj, float defaultX, float defaultY)
{
    Point2D outPoint{defaultX, defaultY}; // 输出点
    outPoint.x = getNumberOr(obj, "x", defaultX); // x
    outPoint.y = getNumberOr(obj, "y", defaultY); // y
    return outPoint; // 返回
}

Position parsePosition2DOrDefault(const simagv::json::Object& obj, float defaultX, float defaultY)
{
    Position outPos{defaultX, defaultY, 0.0F}; // 输出位置
    outPos.x = getNumberOr(obj, "x", defaultX); // x
    outPos.y = getNumberOr(obj, "y", defaultY); // y
    outPos.z = 0.0F; // z
    return outPos; // 返回
}

} // namespace

std::string resolveVehicleMapTopoPath(const std::string& mapName)
{
    auto toLowerAscii = [](std::string s) {
        for (char& ch : s) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        return s;
    };

    std::string normalized = mapName; // 输入副本
    if (normalized.empty()) {
        throw std::invalid_argument("mapName empty"); // 参数异常
    }
    std::replace(normalized.begin(), normalized.end(), '\\', '/'); // 统一分隔符

    std::string lastSegment = normalized; // 尾段
    if (lastSegment.find('/') != std::string::npos) {
        lastSegment = lastSegment.substr(lastSegment.find_last_of('/') + 1); // 取尾部
    }

    std::string folderName = lastSegment; // 文件夹名
    if (folderName == "topo.json" || folderName == "TOPO.JSON") {
        std::string parent = normalized; // 父路径
        if (!parent.empty() && parent.back() == '/') {
            parent.pop_back(); // 去尾
        }
        const size_t lastSlash = parent.find_last_of('/'); // 最后分隔符
        if (lastSlash == std::string::npos) {
            throw std::invalid_argument("mapName invalid"); // 参数异常
        }
        const std::string parentPath = parent.substr(0U, lastSlash); // 去文件名
        const size_t parentSlash = parentPath.find_last_of('/'); // 父分隔符
        folderName = (parentSlash == std::string::npos) ? parentPath : parentPath.substr(parentSlash + 1); // 父尾段
    }

    if (folderName.empty()) {
        throw std::invalid_argument("mapName invalid"); // 参数异常
    }

    const std::filesystem::path base("../../maps"); // 地图根
    const std::filesystem::path topoPath = base / "VehicleMap" / folderName / "topo.json"; // topo路径
    if (!std::filesystem::exists(topoPath)) {
        const std::filesystem::path vehicleBase = base / "VehicleMap";
        std::error_code ec;
        if (std::filesystem::exists(vehicleBase, ec) && std::filesystem::is_directory(vehicleBase, ec)) {
            const std::string targetLower = toLowerAscii(folderName);
            for (const auto& ent : std::filesystem::directory_iterator(vehicleBase, ec)) {
                if (ec) {
                    break;
                }
                if (!ent.is_directory(ec) || ec) {
                    continue;
                }
                const std::filesystem::path dirPath = ent.path();
                const std::string dirName = dirPath.filename().string();
                if (toLowerAscii(dirName) != targetLower) {
                    continue;
                }
                const std::filesystem::path altTopo = dirPath / "topo.json";
                if (std::filesystem::exists(altTopo, ec) && !ec) {
                    return std::filesystem::absolute(altTopo).string();
                }
            }
        }
        throw std::runtime_error("topo.json not found"); // 未找到
    }
    return std::filesystem::absolute(topoPath).string(); // 返回
}

VehicleMapTopoPackage parseVehicleMapTopoFile(const std::string& topoFilePath)
{
    if (topoFilePath.empty()) {
        throw std::invalid_argument("topoFilePath empty"); // 参数异常
    }
    const std::string text = readTextFile(topoFilePath); // 读取
    return parseVehicleMapTopoJson(text); // 解析
}

VehicleMapTopoPackage parseVehicleMapTopoJson(const std::string& topoJson)
{
    const simagv::json::Value root = simagv::json::parse(topoJson); // 解析
    const simagv::json::Object* p_Target = findObjectWithTopoFields(root); // 查找目标对象
    if (p_Target == nullptr) {
        throw std::runtime_error("topo fields not found"); // 未找到
    }
    const simagv::json::Object& topObj = *p_Target; // 顶层对象

    VehicleMapTopoPackage outPkg{}; // 输出

    const simagv::json::Object& headerObj = topObj.at("header").asObject(); // header对象
    outPkg.header.mapType = getStringOr(headerObj, "mapType", ""); // 地图类型
    outPkg.header.mapName = getStringOr(headerObj, "mapName", ""); // 地图名称
    outPkg.header.resolution = getNumberOr(headerObj, "resolution", 0.0F); // 分辨率
    outPkg.header.version = getStringOr(headerObj, "version", ""); // 版本

    const auto itMinPos = headerObj.find("minPos"); // minPos
    if (itMinPos != headerObj.end() && itMinPos->second.isObject()) {
        outPkg.header.minPos = parsePoint2DOrDefault(itMinPos->second.asObject(), 0.0F, 0.0F); // 最小点
    } else {
        outPkg.header.minPos = Point2D{0.0F, 0.0F}; // 默认
    }
    const auto itMaxPos = headerObj.find("maxPos"); // maxPos
    if (itMaxPos != headerObj.end() && itMaxPos->second.isObject()) {
        outPkg.header.maxPos = parsePoint2DOrDefault(itMaxPos->second.asObject(), 0.0F, 0.0F); // 最大点
    } else {
        outPkg.header.maxPos = Point2D{0.0F, 0.0F}; // 默认
    }

    const simagv::json::Array& pointList = topObj.at("advancedPointList").asArray(); // 点列表
    outPkg.topology.stations.reserve(pointList.size()); // 预分配
    outPkg.topology.stationCatalog.reserve(pointList.size()); // 预分配

    for (const simagv::json::Value& p : pointList) {
        if (!p.isObject()) {
            continue; // 跳过
        }
        const simagv::json::Object& po = p.asObject(); // 点对象
        const std::string className = getStringOr(po, "className", ""); // 分类
        const std::string instanceName = getStringOr(po, "instanceName", ""); // 名称
        if (instanceName.empty()) {
            continue; // 缺失
        }

        Position pos{0.0F, 0.0F, 0.0F}; // 坐标
        const auto itPos = po.find("pos"); // pos
        if (itPos != po.end() && itPos->second.isObject()) {
            pos = parsePosition2DOrDefault(itPos->second.asObject(), 0.0F, 0.0F); // 解析
        }

        outPkg.topology.stations.push_back(SceneStationNode{instanceName, pos.x, pos.y}); // 节点

        SceneStationCatalogItem item{}; // 站点项
        item.id = instanceName; // id
        item.instanceName = instanceName; // 名称
        item.pointName = instanceName; // 兼容字段
        item.pos = pos; // 坐标
        item.type = className; // 类型
        outPkg.topology.stationCatalog.push_back(item); // 写入
    }

    const simagv::json::Array& curveList = topObj.at("advancedCurveList").asArray(); // 路径列表
    outPkg.topology.paths.reserve(curveList.size()); // 预分配

    for (const simagv::json::Value& c : curveList) {
        if (!c.isObject()) {
            continue; // 跳过
        }
        const simagv::json::Object& co = c.asObject(); // 路径对象
        const std::string instanceName = getStringOr(co, "instanceName", ""); // 名称
        if (instanceName.empty()) {
            continue; // 缺失
        }

        std::string startId = ""; // 起点id
        Position startPos{0.0F, 0.0F, 0.0F}; // 起点坐标
        const auto itStartPos = co.find("startPos"); // startPos
        if (itStartPos != co.end() && itStartPos->second.isObject()) {
            const simagv::json::Object& so = itStartPos->second.asObject(); // 起点对象
            startId = getStringOr(so, "instanceName", ""); // 起点名称
            const auto itP = so.find("pos"); // 起点坐标对象
            if (itP != so.end() && itP->second.isObject()) {
                startPos = parsePosition2DOrDefault(itP->second.asObject(), 0.0F, 0.0F); // 起点坐标
            }
        }

        std::string endId = ""; // 终点id
        Position endPos{0.0F, 0.0F, 0.0F}; // 终点坐标
        const auto itEndPos = co.find("endPos"); // endPos
        if (itEndPos != co.end() && itEndPos->second.isObject()) {
            const simagv::json::Object& eo = itEndPos->second.asObject(); // 终点对象
            endId = getStringOr(eo, "instanceName", ""); // 终点名称
            const auto itP = eo.find("pos"); // 终点坐标对象
            if (itP != eo.end() && itP->second.isObject()) {
                endPos = parsePosition2DOrDefault(itP->second.asObject(), 0.0F, 0.0F); // 终点坐标
            }
        }

        if (startId.empty() || endId.empty()) {
            continue; // 缺失
        }

        ScenePathEdge edge{}; // 边
        edge.id = instanceName; // 标识
        edge.from = startId; // 起点
        edge.to = endId; // 终点
        edge.cp1 = Position{0.0F, 0.0F, 0.0F}; // 控制点1
        edge.cp2 = Position{0.0F, 0.0F, 0.0F}; // 控制点2

        const auto itC1 = co.find("controlPos1"); // controlPos1
        if (itC1 != co.end() && itC1->second.isObject()) {
            edge.cp1 = parsePosition2DOrDefault(itC1->second.asObject(), 0.0F, 0.0F); // 控制点1
        }
        const auto itC2 = co.find("controlPos2"); // controlPos2
        if (itC2 != co.end() && itC2->second.isObject()) {
            edge.cp2 = parsePosition2DOrDefault(itC2->second.asObject(), 0.0F, 0.0F); // 控制点2
        }

        const float dx = endPos.x - startPos.x; // dx
        const float dy = endPos.y - startPos.y; // dy
        edge.length = hypot2(dx, dy); // 近似长度

        outPkg.topology.paths.push_back(edge); // 写入
    }

    return outPkg; // 返回
}

} // namespace simagv::l4
