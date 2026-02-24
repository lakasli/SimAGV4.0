#pragma once

#include "motion_control_atoms.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace simagv::l4 {

struct RadarConfig {
    float fovDeg;  // 扫描视野角度(度)
    float radiusM; // 扫描半径(m)
};

struct RadarScanRegion {
    Position scanOrigin;                // 扫描原点(车头前方偏移)
    float vehicleHeading;               // 车辆朝向(rad)
    float fovRad;                       // 扫描视野角度(rad)
    float radiusM;                      // 扫描半径(m)
    std::vector<Point2D> sectorPolygon; // 扇形多边形顶点
    int64_t timestamp;                  // 计算时间戳(ms)
    uint32_t sequence;                  // 序列号
};

struct LaserScanData {
    Position scanOrigin;                   // 扫描原点位置
    float vehicleHeading;                  // 车辆朝向(rad)
    float fovDeg;                          // 扫描视野角度(度)
    float radiusM;                         // 扫描半径(m)
    int64_t timestamp;                     // 扫描时间戳(ms)
    uint32_t sequence;                     // 扫描序列号
    bool collisionWarning;                 // 碰撞警告标志
    std::vector<float> obstacleDistances;  // 障碍物距离数组(m)
    std::vector<float> obstacleAngles;     // 对应角度数组(rad)
};

enum class ConnectionState {
    CONNECTED,
    DISCONNECTED,
    CONNECTING,
    RECONNECTING,
    CONNECTION_LOST,
    CONNECTION_ERROR
};

struct PublishResult {
    bool success;         // 发布是否成功
    std::string payload;  // 序列化载荷
    std::string errorMsg; // 错误信息
};

struct AgvState {
    std::string agvId;               // AGV标识符
    Position position;               // 当前位置
    float heading;                   // 当前朝向
    float batteryLevel;              // 电池电量
    std::string currentAction;       // 当前动作
    std::string currentNode;         // 当前节点
    std::string currentEdge;         // 当前边
    std::vector<std::string> errors; // 错误列表
    int64_t timestamp;               // 状态时间戳
    uint32_t sequenceNumber;         // 序列号
};

enum class ErrorType {
    PATH_PLANNING_FAILED,
    NAVIGATION_ERROR,
    POSITIONING_ERROR,
    MOTION_CONTROL_ERROR,
    COLLISION_DETECTED,
    OBSTACLE_DETECTED,
    LASER_ERROR,
    BATTERY_LOW,
    BATTERY_CRITICAL,
    CHARGING_ERROR,
    COMMUNICATION_ERROR,
    SYSTEM_ERROR
};

enum class ErrorSeverity {
    INFO,
    WARNING,
    ERROR,
    FATAL
};

struct ErrorReport {
    ErrorType errorType;      // 错误类型
    ErrorSeverity severity;   // 严重级别
    std::string errorCode;    // 错误码
    std::string errorMessage; // 错误信息
    int64_t timestamp;        // 时间戳(ms)
};

/**
 * @brief 生成激光雷达扫描数据 - 模拟前向2D激光雷达扫描
 *
 * 生成简化扫描数据并给出碰撞提示
 *
 * @param [vehiclePos] 车辆当前位置
 * @param [vehicleHeading] 车辆当前朝向(rad)
 * @param [vehicleLength] 车辆长度(m)
 * @param [radarConfig] 雷达配置参数
 * @return LaserScanData 激光扫描数据
 * @throws std::invalid_argument 参数异常
 */
LaserScanData generateLaserScan(const Position& vehiclePos, float vehicleHeading, float vehicleLength, const RadarConfig& radarConfig);

/**
 * @brief 计算前向雷达扫描区域 - 基于车辆几何参数
 *
 * 计算扫描原点与扇形多边形边界
 *
 * @param [vehiclePos] 车辆中心位置
 * @param [vehicleHeading] 车辆朝向(rad)
 * @param [vehicleLength] 车辆长度(m)
 * @param [radarConfig] 雷达配置参数
 * @return RadarScanRegion 雷达扫描区域信息
 * @throws std::invalid_argument 参数异常
 */
RadarScanRegion computeFrontRadarScan(const Position& vehiclePos, float vehicleHeading, float vehicleLength, const RadarConfig& radarConfig);

/**
 * @brief 发布连接状态 - 发布AGV与主控系统的连接状态
 *
 * 生成可发布的状态载荷并返回
 *
 * @param [connectionState] 当前连接状态
 * @param [connectionQuality] 连接质量评分(0-100)
 * @param [lastHeartbeat] 最后心跳时间戳
 * @return PublishResult 发布结果
 * @throws std::invalid_argument 参数异常
 */
PublishResult publishConnectionState(ConnectionState connectionState, float connectionQuality, int64_t lastHeartbeat);

/**
 * @brief 发布运行状态 - 发布AGV当前运行状态
 *
 * 生成可发布的状态载荷并返回
 *
 * @param [agvState] AGV当前状态数据
 * @param [stateTimestamp] 状态时间戳
 * @return PublishResult 发布结果
 * @throws std::invalid_argument 参数异常
 */
PublishResult publishAgvState(const AgvState& agvState, int64_t stateTimestamp);

/**
 * @brief 检测并报告错误 - 检测系统错误并生成错误报告
 *
 * @param [errorType] 错误类型分类
 * @param [errorDetails] 错误详细信息
 * @param [severity] 错误严重级别
 * @return ErrorReport 标准化错误报告
 */
ErrorReport detectAndReportError(ErrorType errorType, const std::string& errorDetails, ErrorSeverity severity);

} // namespace simagv::l4

