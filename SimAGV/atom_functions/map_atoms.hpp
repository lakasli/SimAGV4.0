#pragma once

#include "motion_control_atoms.hpp"

#include <string>
#include <vector>

namespace simagv::l4 {

struct Polygon2D {
    std::vector<Point2D> vertices; // 多边形顶点数组

    bool isValid() const { return vertices.size() >= 3; }
    size_t vertexCount() const { return vertices.size(); }
};

struct MapObstacle {
    int id;         // 障碍物ID
    Polygon2D poly; // 障碍物多边形
};

struct MapData {
    std::vector<MapObstacle> obstacles; // 地图障碍物集合
};

struct LoadDimensions {
    float length; // 长度(m)
    float width;  // 宽度(m)
    float height; // 高度(m)
};

struct LoadModel {
    std::string loadId;    // 载荷标识
    std::string loadType;  // 载荷类型
    LoadDimensions dimensions; // 尺寸
    float weightKg;        // 重量(kg)
    BoundingBox footprint; // 足迹
};

struct SceneStationNode {
    std::string id; // 站点节点ID
    float x;        // X坐标
    float y;        // Y坐标
};

struct SceneStationCatalogItem {
    std::string id;           // 站点标识
    std::string instanceName; // 站点名称
    std::string pointName;    // 站点名称兼容字段
    Position pos;             // 站点位置
    std::string type;         // 站点类别
};

struct ScenePathEdge {
    std::string id;   // 边标识
    std::string from; // 起点节点ID
    std::string to;   // 终点节点ID
    float length;     // 边长度
    Position cp1;     // 控制点1
    Position cp2;     // 控制点2
};

struct SceneTopologyData {
    std::vector<SceneStationNode> stations;              // 拓扑节点集合
    std::vector<SceneStationCatalogItem> stationCatalog; // 业务站点集合
    std::vector<ScenePathEdge> paths;                    // 拓扑边集合
};

struct StationPositionResult {
    bool found;   // 是否找到站点
    Position pos; // 站点坐标
};

/**
 * @brief 解析地图文件路径 - 将地图标识解析为可读取的.scene路径
 *
 * 按VehicleMap优先并回退ViewerMap的规则解析
 *
 * @param [mapName] 地图标识或相对路径
 * @return std::string .scene文件绝对路径
 * @throws std::invalid_argument 参数为空
 * @throws std::runtime_error 文件不存在
 */
std::string resolveScenePath(const std::string& mapName);

/**
 * @brief 解析场景拓扑文件 - 将.scene解析为拓扑数据
 *
 * 读取.scene文件并解析points/routes字段
 *
 * @param [sceneFilePath] 场景文件绝对路径
 * @return SceneTopologyData 场景拓扑数据
 * @throws std::runtime_error 文件读取异常
 */
SceneTopologyData parseSceneTopologyFile(const std::string& sceneFilePath);

/**
 * @brief 解析场景拓扑JSON - 将JSON文本解析为拓扑数据
 *
 * 容错解析points/routes字段
 *
 * @param [sceneJson] 场景JSON文本
 * @return SceneTopologyData 场景拓扑数据
 * @throws std::runtime_error JSON解析异常
 */
SceneTopologyData parseSceneTopologyJson(const std::string& sceneJson);

/**
 * @brief 查询站点坐标 - 在.scene中按站点标识查询位置
 *
 * 支持按id与name匹配
 *
 * @param [sceneFilePath] 场景文件绝对路径
 * @param [stationId] 站点标识
 * @return StationPositionResult 查询结果
 * @throws std::runtime_error 文件读取异常
 */
StationPositionResult findStationPosition(const std::string& sceneFilePath, const std::string& stationId);

/**
 * @brief 查询点位名称 - 在.scene中按points.id查询name
 *
 * @param [sceneFilePath] 场景文件绝对路径
 * @param [nodeId] 点位ID
 * @return std::string 点位名称
 * @throws std::runtime_error 文件读取异常
 */
std::string findPointNameById(const std::string& sceneFilePath, const std::string& nodeId);

/**
 * @brief 查询最近站点 - 在站点集合中查询距离最近的节点ID
 *
 * @param [pos] 当前二维坐标
 * @param [stations] 站点节点集合
 * @return std::string 最近站点ID
 */
std::string nearestStation(const Position& pos, const std::vector<SceneStationNode>& stations);

/**
 * @brief 解析载荷模型文件 - 解析.shelf/.pallet为载荷模型
 *
 * @param [modelFilePath] 载荷模型文件路径
 * @return LoadModel 载荷模型数据
 * @throws std::runtime_error 文件读取或解析异常
 */
LoadModel parseLoadModelFile(const std::string& modelFilePath);

} // namespace simagv::l4

