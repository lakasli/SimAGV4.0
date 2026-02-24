#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace simagv::l4 {

struct Position {
    float x; // X坐标(m)
    float y; // Y坐标(m)
    float z; // Z坐标(m)
};

struct Point2D {
    float x; // X坐标(m)
    float y; // Y坐标(m)
};

struct Vector2D {
    float x; // X分量
    float y; // Y分量
};

struct Vector3 {
    float x; // X分量
    float y; // Y分量
    float z; // Z分量
};

struct Size2D {
    float length; // 长度(m)
    float width;  // 宽度(m)
};

struct BoundingBox {
    float x;       // 中心X坐标(m)
    float y;       // 中心Y坐标(m)
    float theta;   // 朝向(rad)
    float lengthM; // 长度(m)
    float widthM;  // 宽度(m)
    float heightM; // 高度(m)
};

struct Transform2D {
    float translateX;  // 平移X(m)
    float translateY;  // 平移Y(m)
    float rotationRad; // 旋转(rad)
};

enum class CoordinateFrame {
    WORLD,   // 世界坐标系
    VEHICLE, // 车辆坐标系
    LOCAL    // 局部坐标系
};

struct TransformedPoint {
    float x; // X坐标(m)
    float y; // Y坐标(m)
};

struct MotionResult {
    bool success;           // 运动是否成功完成
    float actualDistance;   // 实际运动距离(m)
    Position finalPosition; // 最终位置坐标
    float finalHeading;     // 最终朝向角度(rad)
    uint32_t errorCode;     // 错误码，0表示成功
    std::string errorMsg;   // 错误描述信息
};

struct PositionResult {
    bool reached;               // 是否成功到达目标
    float totalDistance;        // 总运动距离(m)
    uint32_t executionTimeMs;   // 执行时间(ms)
    std::vector<Position> path; // 实际运动轨迹点
    Position finalPosition;     // 最终到达位置
    float positionError;        // 位置误差(m)
};

struct OdometryData {
    Position position;          // 当前位置(x,y,z)
    float heading;              // 当前朝向角度(rad)
    Vector3 linearVelocity;     // 线速度向量(m/s)
    Vector3 angularVelocity;    // 角速度向量(rad/s)
    Vector3 linearAcceleration; // 线加速度(m/s²)
    uint32_t sequence;          // 数据序列号
    int64_t timestamp;          // 数据时间戳(ms)
    bool valid;                 // 数据有效性标志
};

struct ConstraintCheckResult {
    bool isValid;                        // 是否所有约束都满足
    float maxLinearSpeed;                // 最大允许线速度
    float maxAngularSpeed;               // 最大允许角速度
    float maxLinearAccel;                // 最大允许线加速度
    std::vector<std::string> violations; // 违反的约束列表
};

enum class LiftActionType {
    LIFT_UP,   // 上升
    LIFT_DOWN, // 下降
    LIFT_STOP, // 停止
    LIFT_HOME  // 回零位
};

struct LiftActionResult {
    bool success;             // 动作是否成功
    float actualHeight;       // 实际到达高度
    uint32_t executionTimeMs; // 执行时间(ms)
    std::string errorCode;    // 错误代码
    std::string errorMessage; // 错误信息
};

struct TimeInterval {
    int64_t startTime; // 开始时间戳(ms)
    int64_t endTime;   // 结束时间戳(ms)
    int64_t deltaMs;   // 时间差(ms)
};

/**
 * @brief 底盘速度控制运动 - 控制AGV按指定速度运动
 *
 * 根据给定的线速度和角速度执行离散运动学仿真
 *
 * @param [linearSpeed] 线速度，单位m/s，范围[-2.0, 2.0]
 * @param [angularSpeed] 角速度，单位rad/s，范围[-1.0, 1.0]
 * @param [durationMs] 运动持续时间，单位毫秒，范围[100, 30000]
 * @return MotionResult 运动执行结果
 * @throws std::invalid_argument 参数超出范围
 */
MotionResult simulateMovement(float linearSpeed, float angularSpeed, uint32_t durationMs);

/**
 * @brief 底盘位置控制运动 - 控制AGV运动到指定位置
 *
 * 根据目标位置执行简单的匀速到位仿真
 *
 * @param [targetPos] 目标位置坐标
 * @param [maxSpeed] 最大运动速度，单位m/s，范围[0.1, 2.0]
 * @param [tolerance] 位置容差，单位m，范围[0.01, 0.5]
 * @return PositionResult 位置控制结果
 * @throws std::invalid_argument 参数异常
 */
PositionResult simulateToPosition(const Position& targetPos, float maxSpeed, float tolerance);

/**
 * @brief 获取里程计数据 - 读取底盘当前运动状态
 *
 * 根据请求时间戳生成与时间相关的确定性里程计快照
 *
 * @param [requestTime] 请求时间戳
 * @return OdometryData 里程计数据结构
 * @throws std::invalid_argument 时间戳异常
 */
OdometryData getOdometryData(int64_t requestTime);

/**
 * @brief 检查运动约束 - 验证运动参数是否符合物理约束
 *
 * 校验速度与加速度是否超出预设上限
 *
 * @param [linearSpeed] 待验证的线速度
 * @param [angularSpeed] 待验证的角速度
 * @param [linearAccel] 待验证的线加速度
 * @return ConstraintCheckResult 约束检查结果
 */
ConstraintCheckResult checkMotionConstraints(float linearSpeed, float angularSpeed, float linearAccel);

/**
 * @brief 执行举升动作 - 控制AGV执行托盘举升动作
 *
 * @param [actionType] 举升动作类型
 * @param [targetHeight] 目标高度(mm)
 * @param [actionSpeed] 动作速度(mm/s)
 * @return LiftActionResult 动作结果
 * @throws std::invalid_argument 参数异常
 */
LiftActionResult executeLiftAction(LiftActionType actionType, float targetHeight, float actionSpeed);

/**
 * @brief 坐标系变换 - 在不同坐标系之间转换点坐标
 *
 * @param [point] 待转换的点坐标
 * @param [sourceFrame] 源坐标系
 * @param [targetFrame] 目标坐标系
 * @param [transform] 变换参数
 * @return TransformedPoint 转换后的点坐标
 * @throws std::invalid_argument 参数异常
 */
TransformedPoint transformCoordinates(const Point2D& point, CoordinateFrame sourceFrame, CoordinateFrame targetFrame, const Transform2D& transform);

/**
 * @brief 计算两点间距离 - 计算二维平面上两点间的欧几里得距离
 *
 * @param [point1] 第一个点坐标
 * @param [point2] 第二个点坐标
 * @return float 两点间距离
 */
float calculateDistance(const Point2D& point1, const Point2D& point2);

/**
 * @brief 向量叉积计算 - 计算两个二维向量的叉积
 *
 * @param [vec1] 第一个向量
 * @param [vec2] 第二个向量
 * @return float 叉积结果
 */
float calculateCrossProduct(const Vector2D& vec1, const Vector2D& vec2);

/**
 * @brief 生成ISO格式时间戳 - 生成标准化的ISO 8601时间戳
 *
 * @param [timestamp] 时间戳（毫秒）
 * @return std::string ISO格式时间戳字符串
 * @throws std::invalid_argument 时间戳异常
 */
std::string generateISOTimestamp(int64_t timestamp);

/**
 * @brief 计算时间间隔 - 计算两个时间戳之间的时间差
 *
 * @param [startTime] 开始时间戳
 * @param [endTime] 结束时间戳
 * @return TimeInterval 时间间隔结果
 * @throws std::invalid_argument 时间戳顺序异常
 */
TimeInterval calculateTimeInterval(int64_t startTime, int64_t endTime);

} // namespace simagv::l4

