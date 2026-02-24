#pragma once

#include "map_atoms.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace simagv::l4 {

struct RadarScanRegion;

struct ObstacleData {
    std::vector<float> distances; // 距离测量值(m)
    std::vector<float> angles;    // 对应角度(rad)
    int obstacleId;               // 障碍物ID

    bool isValid() const { return !distances.empty() && distances.size() == angles.size(); }
};

enum class ObstacleType {
    RECTANGLE,   // 矩形障碍物
    CIRCLE,      // 圆形障碍物
    POINT_CLOUD, // 点云障碍物
    POLYGON      // 多边形障碍物
};

struct RadarCollisionResult {
    bool hasCollision;                     // 碰撞标志
    float minDistance;                     // 最小距离(m)
    std::vector<int> collisionObstacleIds; // 碰撞障碍物ID列表

    struct CollisionDetail {
        int obstacleId;       // 障碍物ID
        float overlapArea;    // 重叠面积(m²)
        Point2D nearestPoint; // 最近点坐标
        float distance;       // 到最近点距离(m)
    };

    std::vector<CollisionDetail> collisionDetails; // 碰撞详情列表
    int64_t timestamp;                             // 检测时间戳(ms)
};

struct SafetyRange {
    float length;         // 安全范围长度(m)
    float width;          // 安全范围宽度(m)
    float originalLength; // 原始车辆长度(m)
    float originalWidth;  // 原始车辆宽度(m)
    float safetyFactor;   // 使用的安全系数
    bool valid;           // 数据有效性标志
};

enum class CollisionErrorLevel {
    NO_COLLISION, // 无碰撞
    WARNING,      // 碰撞警告
    ERROR         // 碰撞错误
};

struct SafetyOverlapResult {
    bool hasOverlap;                 // 是否存在重叠
    CollisionErrorLevel errorLevel;  // 错误级别
    std::string errorMessage;        // 错误描述信息
};

struct CollisionResult {
    bool isColliding;                    // 是否发生碰撞
    Vector2D separationAxis;             // 分离轴向量
    Point2D contactPoint;                // 接触点坐标
    std::vector<Point2D> contactPoints;  // 所有接触点
};

struct SafetyEnvelope {
    Position center;              // 包络中心
    float length;                 // 包络长度(m)
    float width;                  // 包络宽度(m)
    float heading;                // 包络朝向(rad)
    std::vector<Point2D> polygon; // 包络多边形顶点
};

struct RadarScanResult {
    bool hasObstacle;             // 是否检测到障碍物
    std::vector<int> obstacleIds; // 命中的障碍物ID集合
    float minDistance;            // 最近障碍物距离(m)
};

/**
 * @brief 检测雷达扫描区域与障碍物的碰撞 - 多边形相交检测
 *
 * 使用SAT与裁剪算法给出碰撞细节
 *
 * @param [radarRegion] 雷达扫描区域
 * @param [obstaclePolygons] 障碍物多边形列表
 * @return RadarCollisionResult 碰撞检测结果
 * @throws std::invalid_argument 几何数据异常
 */
RadarCollisionResult detectRadarCollision(const RadarScanRegion& radarRegion, const std::vector<Polygon2D>& obstaclePolygons);

/**
 * @brief 构建障碍物多边形 - 用于碰撞检测
 *
 * 将障碍物数据转换为二维多边形表示
 *
 * @param [obstacleData] 障碍物数据
 * @param [obstacleType] 障碍物类型
 * @return Polygon2D 障碍物多边形
 * @throws std::invalid_argument 数据格式异常
 */
Polygon2D buildObstaclePolygon(const ObstacleData& obstacleData, ObstacleType obstacleType);

/**
 * @brief 计算底盘车安全范围 - 通过配置文件获取车辆尺寸并应用安全系数
 *
 * 从SimVehicleSys的agv_configs读取width/length字段
 *
 * @param [vehicleId] 车辆标识符
 * @param [safetyFactor] 安全系数
 * @return SafetyRange 安全范围结构
 * @throws std::runtime_error 配置读取异常
 */
SafetyRange calculateSafetyRange(const std::string& vehicleId, float safetyFactor);

/**
 * @brief 安全范围重叠检测 - 检测两个车辆安全范围是否重叠
 *
 * 使用轴对齐矩形重叠规则判定
 *
 * @param [ownSafetyRange] 本车安全范围
 * @param [ownPosition] 本车当前位置
 * @param [otherSafetyRange] 其他车安全范围
 * @param [otherPosition] 其他车当前位置
 * @return SafetyOverlapResult 重叠检测结果
 * @throws std::invalid_argument 参数异常
 */
SafetyOverlapResult checkSafetyRangeOverlap(const SafetyRange& ownSafetyRange, const Position& ownPosition, const SafetyRange& otherSafetyRange, const Position& otherPosition);

/**
 * @brief 多边形碰撞检测 - 检测两个多边形是否相交
 *
 * 使用分离轴定理(SAT)检测凸多边形碰撞
 *
 * @param [poly1] 第一个多边形顶点数组
 * @param [poly2] 第二个多边形顶点数组
 * @return CollisionResult 碰撞检测结果
 * @throws std::invalid_argument 多边形数据异常
 */
CollisionResult checkPolygonCollision(const std::vector<Point2D>& poly1, const std::vector<Point2D>& poly2);

/**
 * @brief 计算安全包络 - 计算考虑负载的安全区域
 *
 * 在车辆坐标系下计算点集包围盒并扩展边距
 *
 * @param [vehiclePos] 车辆当前位置
 * @param [vehicleHeading] 车辆当前朝向
 * @param [vehicleSize] 车辆基础尺寸
 * @param [loadSize] 负载尺寸
 * @param [safetyMargin] 安全边距(米)
 * @return SafetyEnvelope 安全包络区域
 * @throws std::invalid_argument 参数异常
 */
SafetyEnvelope computeSafetyEnvelope(const Position& vehiclePos, float vehicleHeading, const Size2D& vehicleSize, const Size2D& loadSize, float safetyMargin);

/**
 * @brief 前方雷达扫描处理 - 模拟前向雷达障碍物检测
 *
 * 执行扇形扫描并返回命中障碍物ID集合
 *
 * @param [vehiclePos] 车辆当前位置
 * @param [vehicleHeading] 车辆当前朝向
 * @param [scanRange] 扫描距离范围
 * @param [scanAngle] 扫描角度范围
 * @param [mapData] 地图数据
 * @return RadarScanResult 雷达扫描结果
 * @throws std::invalid_argument 参数异常
 */
RadarScanResult computeFrontRadar(const Position& vehiclePos, float vehicleHeading, float scanRange, float scanAngle, const MapData& mapData);

} // namespace simagv::l4
