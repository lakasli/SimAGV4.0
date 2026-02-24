#pragma once

#include "../../atom_functions/battery_management_atoms.hpp"
#include "../../atom_functions/collision_detection_atoms.hpp"
#include "../../atom_functions/map_atoms.hpp"
#include "../../atom_functions/motion_control_atoms.hpp"
#include "../../atom_functions/path_planning_atoms.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace simagv::l3 {

using Position = simagv::l4::Position;
using Point2D = simagv::l4::Point2D;
using Vector2D = simagv::l4::Vector2D;
using Size2D = simagv::l4::Size2D;
using BoundingBox = simagv::l4::BoundingBox;
using LoadDimensions = simagv::l4::LoadDimensions;
using SafetyRange = simagv::l4::SafetyRange;
using ChargeMode = simagv::l4::ChargeMode;

struct Pose2D {
    Point2D position; // 位置坐标(x,y)
    float heading;    // 朝向角度(rad)
    int64_t timestamp; // 时间戳(ms)
};

enum class QualityOfService : uint8_t {
    AT_MOST_ONCE = 0,  // QoS0
    AT_LEAST_ONCE = 1, // QoS1
    EXACTLY_ONCE = 2   // QoS2
};

struct Obstacle {
    int id;                      // 障碍物ID
    std::vector<Point2D> polygon; // 多边形顶点
};

struct CollisionWarning {
    int obstacleId;      // 障碍物ID
    float distanceM;     // 距离(m)
    std::string message; // 提示信息
};

enum class NavigationPhase {
    PLANNING,          // 路径规划阶段
    EXECUTING,         // 执行运动阶段
    REPLANNING,        // 重规划阶段
    OBSTACLE_AVOIDING, // 避障阶段
    FINAL_APPROACH,    // 最终接近阶段
    COMPLETED,         // 完成
    FAILED             // 失败
};

struct NavigationMetrics {
    float planningTimeMs;        // 规划时间(ms)
    float pathSmoothingTimeMs;   // 路径平滑时间(ms)
    float totalMotionTimeMs;     // 总运动时间(ms)
    float averageSpeed;          // 平均速度(m/s)
    float pathEfficiency;        // 路径效率(实际/规划)
    uint32_t collisionChecks;    // 碰撞检测次数
    uint32_t safetyRangeChecks;  // 安全范围检测次数
};

struct NavigationConfig {
    float maxSpeed;               // 最大速度(m/s)
    float maxAngularSpeed;        // 最大角速度(rad/s)
    float positionTolerance;      // 位置容差(m)
    float headingTolerance;       // 朝向容差(rad)
    bool enableCollisionCheck;    // 启用碰撞检测
    bool enablePathSmoothing;     // 启用路径平滑
    uint32_t planningTimeoutMs;   // 规划超时时间(ms)
    uint32_t executionTimeoutMs;  // 执行超时时间(ms)
};

struct SafetyContext {
    std::vector<SafetyRange> otherVehicles; // 其他车辆安全范围
    std::vector<Obstacle> staticObstacles;  // 静态障碍物
    float minSafeDistance;                  // 最小安全距离
    bool emergencyStopEnabled;              // 启用急停
};

struct CompleteNavigationResult {
    bool success;                          // 导航是否成功
    NavigationPhase currentPhase;          // 当前导航阶段
    std::vector<Position> plannedPath;     // 规划路径点
    std::vector<Position> executedPath;    // 实际执行路径
    Pose2D finalPose;                      // 最终位姿
    float totalDistance;                   // 总运动距离(m)
    uint32_t executionTimeMs;              // 执行时间(ms)
    uint32_t replanningCount;              // 重规划次数
    std::vector<CollisionWarning> warnings; // 碰撞警告列表
    std::string errorCode;                 // 错误码
    std::string errorMessage;              // 错误信息
    NavigationMetrics metrics;             // 性能指标
};

struct DynamicObstacle {
    Point2D position; // 位置坐标(x,y)
    Vector2D velocity; // 速度(m/s)
    float radiusM;     // 半径(m)
    int id;            // 障碍物ID
};

struct AvoidanceConfig {
    float warningDistanceM;      // 预警距离(m)
    float criticalDistanceM;     // 临界距离(m)
    bool enableReplanning;       // 启用重规划
    uint32_t maxReplanningCount; // 最大重规划次数
};

struct DynamicAvoidanceResult {
    bool success;                       // 是否成功
    std::vector<Position> plannedPath;  // 规划路径点
    std::vector<Position> executedPath; // 实际执行路径
    uint32_t replanningCount;           // 重规划次数
    std::vector<CollisionWarning> warnings; // 警告列表
    std::string errorCode;              // 错误码
    std::string errorMessage;           // 错误信息
};

enum class VehicleState {
    IDLE,        // 空闲
    EXECUTING,   // 执行中
    CHARGING,    // 充电中
    ERROR,       // 错误
    MAINTENANCE  // 维护
};

enum class LoadStatus {
    EMPTY,     // 空载
    LOADED,    // 已装载
    UNKNOWN    // 未知
};

struct VehicleConstraints {
    float maxSpeed;        // 最大速度(m/s)
    float maxAcceleration; // 最大加速度(m/s²)
};

struct ActionDefinition {
    std::string actionType;                   // 动作类型
    std::map<std::string, std::string> params; // 动作参数
    uint32_t timeoutMs;                       // 动作超时时间
    bool blocking;                            // 是否阻塞
};

struct TaskNode {
    std::string nodeId;                          // 节点ID
    std::string nodeType;                        // 节点类型
    Position position;                           // 节点位置
    std::map<std::string, std::string> properties; // 节点属性
    std::vector<ActionDefinition> actions;       // 执行动作列表
};

struct TaskEdge {
    std::string edgeId;                          // 边ID
    std::string fromNodeId;                      // 起点节点ID
    std::string toNodeId;                        // 终点节点ID
    std::map<std::string, std::string> properties; // 边属性
};

struct TaskDefinition {
    std::string taskId;                            // 任务唯一标识
    std::string taskType;                          // 任务类型
    std::vector<TaskNode> nodes;                   // 任务节点序列
    std::vector<TaskEdge> edges;                   // 任务边序列
    std::map<std::string, std::string> params;     // 任务参数
    int64_t createTime;                            // 创建时间(ms)
    int64_t expireTime;                            // 过期时间(ms)
    uint32_t priority;                             // 任务优先级
};

struct VehicleContext {
    std::string vehicleId;                 // 车辆ID
    Pose2D currentPose;                    // 当前位姿
    float batteryLevel;                    // 电池电量(%)
    VehicleState vehicleState;             // 车辆状态
    LoadStatus loadStatus;                 // 负载状态
    std::vector<std::string> capabilities; // 车辆能力列表
    VehicleConstraints constraints;        // 车辆约束
};

struct ExecutionConfig {
    float maxSpeed;           // 最大速度(m/s)
    float positionTolerance;  // 位置容差(m)
    uint32_t overallTimeoutMs; // 总超时(ms)
};

struct CompleteTaskResult {
    bool success;                // 是否成功
    std::string taskId;          // 任务ID
    std::vector<Position> path;  // 执行轨迹
    uint32_t executionTimeMs;    // 执行时间(ms)
    std::string errorCode;       // 错误码
    std::string errorMessage;    // 错误信息
};

struct TransportTaskDefinition {
    std::string taskId;         // 任务ID
    Position pickupPosition;    // 取货点
    Position dropoffPosition;   // 放货点
    float targetLoadHeightMm;   // 目标装载高度(mm)
};

struct LoadSpecification {
    float maxWeightKg; // 最大重量(kg)
    LoadDimensions dimensions; // 尺寸
};

struct TransportConfig {
    float maxSpeed;          // 最大速度(m/s)
    float positionTolerance; // 位置容差(m)
    uint32_t actionTimeoutMs; // 动作超时(ms)
};

struct TransportTaskResult {
    bool success;             // 是否成功
    std::string taskId;       // 任务ID
    uint32_t executionTimeMs; // 执行时间(ms)
    std::string errorCode;    // 错误码
    std::string errorMessage; // 错误信息
};

struct MonitoringConfig {
    bool enablePositionMonitoring;    // 启用位置监控
    bool enableBatteryMonitoring;     // 启用电池监控
    bool enableTaskMonitoring;        // 启用任务监控
    bool enableErrorMonitoring;       // 启用错误监控
    bool enableCollisionMonitoring;   // 启用碰撞监控
    bool enableConnectionMonitoring;  // 启用连接监控
    uint32_t monitoringIntervalMs;    // 监控间隔(ms)
    uint32_t reportTimeoutMs;         // 报告超时(ms)
    std::vector<std::string> monitoredTopics; // 监控主题列表
};

struct PublishConfig {
    std::string protocolVersion;    // 协议版本
    bool enableRealtimePublish;     // 启用实时发布
    bool enableBatchPublish;        // 启用批量发布
    uint32_t publishIntervalMs;     // 发布间隔(ms)
    std::string stateTopic;         // 状态主题
    std::string connectionTopic;    // 连接状态主题
    std::string visualizationTopic; // 可视化主题
    std::string factsheetTopic;     // 事实表主题
    std::string orderTopic;         // 订单主题
    std::string instantActionsTopic; // 即时动作主题
    QualityOfService qosLevel;      // QoS级别
};

struct ErrorItem {
    std::string errorType;        // 错误类型
    std::string errorLevel;       // 错误等级
    std::string errorCode;        // 错误码
    std::string errorDescription; // 错误描述
};

struct ComprehensiveStateReport {
    std::string protocolVersion;   // 协议版本
    Pose2D currentPose;            // 当前位姿
    float batteryLevel;            // 电量(%)
    VehicleState vehicleState;     // 车辆状态
    std::vector<ErrorItem> errors; // 错误列表
    int64_t timestamp;             // 时间戳(ms)
};

struct SensorSample {
    std::string sensorId;    // 传感器ID
    float value;             // 采样值
    int64_t timestamp;       // 时间戳(ms)
};

struct SensorDataCollection {
    std::vector<SensorSample> samples; // 采样集合
};

struct StateHistory {
    std::vector<ComprehensiveStateReport> reports; // 历史报告
};

struct DetectionThresholds {
    float lowBatteryThreshold;  // 低电量阈值(%)
    float poseJumpDistanceM;    // 位姿跳变距离(m)
};

struct AnomalyItem {
    std::string code;        // 异常码
    std::string message;     // 异常描述
    std::string level;       // 严重等级
};

struct AnomalyDetectionResult {
    bool hasAnomaly;               // 是否有异常
    std::vector<AnomalyItem> items; // 异常列表
};

enum class BatteryState {
    DISCHARGING, // 放电中
    CHARGING,    // 充电中
    FULL,        // 充满
    LOW,         // 电量低
    CRITICAL,    // 电量临界
    ERROR,       // 错误状态
    MAINTENANCE  // 维护模式
};

struct CycleHistory {
    uint32_t cycleCount; // 循环次数
};

struct UsagePattern {
    float averageLoadKg;   // 平均负载(kg)
    float averageSpeed;    // 平均速度(m/s)
};

struct BatteryContext {
    std::string vehicleId;   // 车辆ID
    float currentCapacity;   // 当前容量(Ah)
    float ratedCapacity;     // 额定容量(Ah)
    float currentVoltage;    // 当前电压(V)
    float currentCurrent;    // 当前电流(A)
    float temperature;       // 电池温度(°C)
    BatteryState currentState; // 当前状态
    CycleHistory cycleHistory; // 循环历史
    UsagePattern usagePattern; // 使用模式
};

struct ChargeStrategy {
    float lowBatteryThreshold;       // 低电量阈值(%)
    float criticalBatteryThreshold;  // 临界电量阈值(%)
    float targetChargeLevel;         // 目标充电电量(%)
    ChargeMode preferredMode;        // 首选充电模式
    bool enableFastCharge;           // 启用快充
    bool enableTrickleCharge;        // 启用涓流充电
    uint32_t maxChargingTime;        // 最大充电时间(s)
    float temperatureLimit;          // 温度限制(°C)
};

struct PowerConstraints {
    float maxChargeCurrentA; // 最大充电电流(A)
    float maxDischargeCurrentA; // 最大放电电流(A)
};

struct SmartBatteryResult {
    bool success;           // 是否成功
    float finalBatteryLevel; // 最终电量(%)
    std::string errorCode;  // 错误码
    std::string errorMessage; // 错误信息
};

struct TaskProfile {
    float estimatedDistanceM; // 预计距离(m)
    float estimatedLoadKg;    // 预计负载(kg)
};

struct VehicleStatus {
    float batteryLevel; // 电量(%)
    float currentLoadKg; // 当前负载(kg)
};

struct OptimizationConfig {
    float energySavingFactor; // 节能系数(0-1)
};

struct EnergyOptimizationResult {
    bool success;              // 是否成功
    float recommendedMaxSpeed; // 推荐最大速度(m/s)
    std::string suggestion;    // 建议文本
};

struct StaticObstacle {
    int id;                      // 障碍物ID
    std::vector<Point2D> polygon; // 多边形顶点
};

struct OtherVehicleStatus {
    std::string vehicleId; // 车辆ID
    Pose2D pose;           // 位姿
    SafetyRange safetyRange; // 安全范围
};

struct EnvironmentModel {
    std::vector<StaticObstacle> staticObstacles;  // 静态障碍物
    std::vector<DynamicObstacle> dynamicObstacles; // 动态障碍物
    std::vector<OtherVehicleStatus> otherVehicles; // 其他车辆状态
    int64_t modelTimestamp;                       // 模型时间戳(ms)
    float modelConfidence;                        // 模型置信度
};

struct SafetyZone {
    std::string zoneId;            // 区域ID
    std::vector<Point2D> polygon;  // 区域多边形
};

struct SafetyConfiguration {
    float warningDistance;          // 警告距离(m)
    float criticalDistance;         // 临界距离(m)
    float safetyMargin;             // 安全边距(m)
    bool enableEmergencyStop;       // 启用急停
    bool enablePathReplanning;      // 启用路径重规划
    uint32_t reactionTimeMs;        // 反应时间(ms)
    std::vector<SafetyZone> safetyZones; // 安全区域配置
};

struct CollisionPreventionResult {
    bool safe;                       // 是否安全
    bool emergencyStopTriggered;     // 是否触发急停
    std::vector<CollisionWarning> warnings; // 警告列表
    std::string errorCode;           // 错误码
    std::string errorMessage;        // 错误信息
};

using SceneData = simagv::l4::SceneTopologyData;

struct TopologyOptions {
    float positionToleranceM; // 坐标容差(m)
};

struct Station {
    std::string id;        // 站点ID
    std::string name;      // 站点名称
    Position position;     // 站点位置
    std::string type;      // 站点类别
};

struct PathEdge {
    std::string id;   // 边ID
    std::string from; // 起点节点ID
    std::string to;   // 终点节点ID
    float length;     // 长度(m)
};

struct MapTopology {
    std::vector<Station> stations; // 站点列表
    std::vector<PathEdge> edges;   // 路径边列表
    std::map<std::string, Station> stationById; // 站点索引
    std::map<std::string, std::vector<PathEdge>> edgesByStart; // 起点索引
};

struct LoadRecognition {
    std::string modelFilePath; // 模型文件路径
    std::string loadId;        // 载荷标识
};

struct ActionContext {
    std::string actionType;                   // 动作类型
    std::map<std::string, std::string> params; // 动作参数
};

struct LoadResolution {
    std::string loadId;         // 载荷标识
    LoadDimensions dimensions;  // 载荷尺寸
    float weightKg;             // 重量(kg)
    float targetForkHeightM;    // 目标叉高(m)
    BoundingBox footprint;      // 足迹
};

struct VdaActionParameter {
    std::string key;   // 参数键
    std::string value; // 参数值
};

struct VdaAction {
    std::string actionType;                     // 动作类型
    std::string actionId;                       // 动作ID
    std::string blockingType;                   // 阻塞类型(None|Soft|Hard)
    std::vector<VdaActionParameter> parameters; // 动作参数
};

struct VdaNode {
    std::string nodeId;         // 节点ID
    uint32_t sequenceId;        // 序号
    bool released;              // 是否释放
    std::vector<VdaAction> actions; // 节点动作
};

struct VdaEdge {
    std::string edgeId;         // 边ID
    uint32_t sequenceId;        // 序号
    bool released;              // 是否释放
    std::string startNodeId;    // 起点节点ID
    std::string endNodeId;      // 终点节点ID
    std::vector<VdaAction> actions; // 边动作
};

struct VdaOrder {
    std::string orderId;        // 订单ID
    uint32_t orderUpdateId;     // 更新序号
    std::vector<VdaNode> nodes; // 节点序列
    std::vector<VdaEdge> edges; // 边序列
};

struct OrderAcceptanceConfig {
    bool enableStrictSequenceCheck; // 启用序号严格检查
    bool enableCapabilityCheck;     // 启用能力校验
    uint32_t maxNodeCount;          // 最大节点数量
    uint32_t maxEdgeCount;          // 最大边数量
};

struct VdaOrderAcceptanceResult {
    bool accepted;          // 是否接受
    TaskDefinition task;    // 任务定义
    std::string rejectCode; // 拒绝码
    std::string rejectMessage; // 拒绝原因
};

struct VdaInstantActions {
    std::vector<VdaAction> actions; // 动作列表
};

struct InstantActionsConfig {
    bool enableCapabilityCheck; // 启用能力校验
    uint32_t maxActionCount;    // 最大动作数量
};

struct VdaRejectedAction {
    std::string actionId;       // 动作ID
    std::string rejectCode;     // 拒绝码
    std::string rejectMessage;  // 拒绝原因
};

struct VdaInstantActionsResult {
    bool accepted;                    // 是否整体接受
    std::vector<ActionDefinition> commands; // 可执行指令
    std::vector<VdaRejectedAction> rejected; // 拒绝列表
};

struct VehicleDimensions {
    float lengthM; // 车长(m)
    float widthM;  // 车宽(m)
    float heightM; // 车高(m)
};

struct FactsheetContext {
    std::string protocolVersion;            // 协议版本
    std::string manufacturer;               // 厂商
    std::string serialNumber;               // 序列号
    std::string vehicleType;                // 车型
    VehicleDimensions dimensions;           // 尺寸
    std::vector<std::string> supportedActions; // 支持动作
};

struct FactsheetBuildOptions {
    bool includeOptionalFields; // 包含可选字段
};

struct FactsheetPayload {
    std::string protocolVersion;            // 协议版本
    std::string manufacturer;               // 厂商
    std::string serialNumber;               // 序列号
    std::string vehicleType;                // 车型
    VehicleDimensions dimensions;           // 尺寸
    std::vector<std::string> supportedActions; // 支持动作
};

enum class VdaConnectionState {
    ONLINE,           // 在线
    OFFLINE,          // 离线
    CONNECTION_BROKEN // 连接中断
};

struct ConnectionContext {
    std::string protocolVersion; // 协议版本
    std::string manufacturer;    // 厂商
    std::string serialNumber;    // 序列号
    VdaConnectionState state;    // 连接状态
    int64_t timestamp;           // 时间戳(ms)
};

struct ConnectionBuildOptions {
    bool enableTimestampNormalize; // 启用时间戳归一化
};

struct VdaConnectionPayload {
    std::string protocolVersion; // 协议版本
    std::string manufacturer;    // 厂商
    std::string serialNumber;    // 序列号
    VdaConnectionState state;    // 连接状态
    int64_t timestamp;           // 时间戳(ms)
};

struct StateBuildOptions {
    bool enableErrorCompression;  // 启用错误压缩
    bool enableActionStateOutput; // 启用动作状态输出
};

struct VdaError {
    std::string errorType;        // 错误类型
    std::string errorLevel;       // 错误等级
    std::string errorCode;        // 错误码
    std::string errorDescription; // 错误描述
};

struct VdaStatePayload {
    std::string protocolVersion; // 协议版本
    std::string manufacturer;    // 厂商
    std::string serialNumber;    // 序列号
    Pose2D currentPose;          // 当前位姿
    float batteryLevel;          // 电量(%)
    std::vector<VdaError> errors; // 错误列表
    int64_t timestamp;           // 时间戳(ms)
};

struct PublishResult {
    bool success;          // 发布是否成功
    uint32_t retryCount;   // 重试次数
    std::string errorCode; // 错误码
    std::string errorMessage; // 错误信息
};

} // namespace simagv::l3

