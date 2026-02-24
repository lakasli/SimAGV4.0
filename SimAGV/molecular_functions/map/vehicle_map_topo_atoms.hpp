#pragma once

#include "../../atom_functions/map_atoms.hpp"

#include <string>

namespace simagv::l4 {

struct VehicleMapHeader {
    std::string mapType;   // 地图类型
    std::string mapName;   // 地图名称
    Point2D minPos;        // 点云最小坐标
    Point2D maxPos;        // 点云最大坐标
    float resolution;      // 地图分辨率(m/px)
    std::string version;   // 地图版本
};

struct VehicleMapTopoPackage {
    VehicleMapHeader header;     // 地图头信息
    SceneTopologyData topology;  // 拓扑数据
};

/**
 * @brief 解析地图包topo.json路径 - 将地图标识解析为可读取的topo.json路径
 *
 * 按VehicleMap/{mapName}/topo.json规则定位
 *
 * @param [mapName] 地图标识或相对/绝对路径
 * @return std::string topo.json文件绝对路径
 * @throws std::invalid_argument 参数为空
 * @throws std::runtime_error 文件不存在
 */
std::string resolveVehicleMapTopoPath(const std::string& mapName);

/**
 * @brief 解析车辆地图拓扑文件 - 将topo.json解析为头信息与拓扑数据
 *
 * 读取topo.json并解析header/advancedPointList/advancedCurveList字段
 *
 * @param [topoFilePath] topo.json文件绝对路径
 * @return VehicleMapTopoPackage 地图包解析结果
 * @throws std::invalid_argument 参数为空
 * @throws std::runtime_error 文件读取或解析异常
 */
VehicleMapTopoPackage parseVehicleMapTopoFile(const std::string& topoFilePath);

/**
 * @brief 解析车辆地图拓扑JSON - 将JSON文本解析为头信息与拓扑数据
 *
 * 容错解析header/advancedPointList/advancedCurveList字段
 *
 * @param [topoJson] topo.json JSON文本
 * @return VehicleMapTopoPackage 地图包解析结果
 * @throws std::runtime_error JSON解析异常
 */
VehicleMapTopoPackage parseVehicleMapTopoJson(const std::string& topoJson);

} // namespace simagv::l4

