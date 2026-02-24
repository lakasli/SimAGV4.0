#pragma once

#include "map_atoms.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace simagv::l4 {

struct PosePoint {
    float x;            // X坐标
    float y;            // Y坐标
    float theta;        // 朝向(rad)
    std::string edgeId; // 边标识
    bool turnAnchor;    // 转弯锚点标志
};

struct TrajectoryPolylineConfig {
    uint32_t steps;        // 采样步数
    float orientation;     // 绝对朝向(NAN表示不覆盖)
    std::string direction; // 运动方向
};

struct TrajectoryControlPoint {
    float x;           // X坐标
    float y;           // Y坐标
    float weight;      // 权重
    float orientation; // 朝向(NAN表示无)
};

enum class TrajectoryType {
    STRAIGHT,     // 直线
    CUBIC_BEZIER, // 三次贝塞尔
    INFPNURBS     // NURBS
};

struct Trajectory {
    TrajectoryType type;                             // 轨迹类型
    std::vector<TrajectoryControlPoint> controlPoints; // 控制点集合
    uint32_t degree;                                 // NURBS阶数
    std::vector<float> knotVector;                   // NURBS节点向量
};

/**
 * @brief A*路径规划 - 计算从起点到终点的最优路径
 *
 * @param [startStationId] 起始站点ID
 * @param [targetStationId] 目标站点ID
 * @param [stations] 站点节点集合
 * @param [paths] 拓扑边集合
 * @return std::vector<std::string> 路由站点ID序列
 * @throws std::invalid_argument 参数异常
 */
std::vector<std::string> aStarTopologyRouting(const std::string& startStationId, const std::string& targetStationId, const std::vector<SceneStationNode>& stations, const std::vector<ScenePathEdge>& paths);

/**
 * @brief 生成路由点列 - 将站点路由转换为可执行的姿态点列
 *
 * @param [routeNodeIds] 路由站点ID序列
 * @param [stations] 站点节点集合
 * @param [paths] 拓扑边集合
 * @param [stepsPerEdge] 每条边采样步数
 * @return std::vector<PosePoint> 采样姿态点列
 * @throws std::invalid_argument 参数异常
 */
std::vector<PosePoint> generateRoutePolyline(const std::vector<std::string>& routeNodeIds, const std::vector<SceneStationNode>& stations, const std::vector<ScenePathEdge>& paths, uint32_t stepsPerEdge);

/**
 * @brief 插入拐角转弯点 - 在姿态点列中按朝向变化插入转弯点
 *
 * @param [inputPoints] 输入姿态点列
 * @param [thetaThreshold] 朝向阈值(rad)
 * @param [stepDelta] 单步角度增量(rad)
 * @param [posEps] 位置变化阈值
 * @return std::vector<PosePoint> 输出姿态点列
 * @throws std::invalid_argument 参数异常
 */
std::vector<PosePoint> augmentWithCornerTurns(const std::vector<PosePoint>& inputPoints, float thetaThreshold, float stepDelta, float posEps);

/**
 * @brief 采样订单轨迹 - 将订单轨迹采样为姿态点列
 *
 * @param [trajectory] 轨迹对象
 * @param [startPos] 起点坐标
 * @param [endPos] 终点坐标
 * @param [config] 采样配置参数
 * @return std::vector<PosePoint> 采样姿态点列
 * @throws std::invalid_argument 参数异常
 */
std::vector<PosePoint> trajectoryPolyline(const Trajectory& trajectory, const Position& startPos, const Position& endPos, const TrajectoryPolylineConfig& config);

} // namespace simagv::l4

