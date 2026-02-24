#include "motion_control_atoms.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace simagv::l4 {
namespace {

constexpr float kPi = 3.14159265358979323846f; // 圆周率

float normalizeAngle(float angleRad) {
    float t = angleRad; // 角度副本
    while (t <= -kPi) {
        t += 2.0f * kPi; // 上移
    }
    while (t > kPi) {
        t -= 2.0f * kPi; // 下移
    }
    return t; // 返回
}

bool inRange(float value, float minValue, float maxValue) {
    return value >= minValue && value <= maxValue; // 区间判断
}

float hypot2(float dx, float dy) {
    return std::sqrt(dx * dx + dy * dy); // 二维勾股
}

} // namespace

MotionResult simulateMovement(float linearSpeed, float angularSpeed, uint32_t durationMs) {
    if (!inRange(linearSpeed, -2.0f, 2.0f)) {
        throw std::invalid_argument("linearSpeed out of range"); // 参数异常
    }
    if (!inRange(angularSpeed, -1.0f, 1.0f)) {
        throw std::invalid_argument("angularSpeed out of range"); // 参数异常
    }
    if (durationMs < 100U || durationMs > 30000U) {
        throw std::invalid_argument("durationMs out of range"); // 参数异常
    }

    const float dt = static_cast<float>(durationMs) / 1000.0f; // 秒
    const float dTheta = angularSpeed * dt; // 朝向变化
    Position finalPos{0.0f, 0.0f, 0.0f}; // 最终位置
    float finalHeading = normalizeAngle(dTheta); // 最终朝向

    if (std::abs(angularSpeed) < 1e-6f) {
        finalPos.y = linearSpeed * dt; // 沿Y前进
    } else {
        const float radius = linearSpeed / angularSpeed; // 曲率半径
        finalPos.x = radius * (1.0f - std::cos(dTheta)); // X位移
        finalPos.y = radius * std::sin(dTheta); // Y位移
    }

    MotionResult outResult{}; // 输出
    outResult.success = true; // 成功
    outResult.actualDistance = std::abs(linearSpeed) * dt; // 距离
    outResult.finalPosition = finalPos; // 位置
    outResult.finalHeading = finalHeading; // 朝向
    outResult.errorCode = 0U; // OK
    outResult.errorMsg = ""; // 无错误
    return outResult; // 返回
}

PositionResult simulateToPosition(const Position& targetPos, float maxSpeed, float tolerance) {
    if (!inRange(maxSpeed, 0.1f, 2.0f)) {
        throw std::invalid_argument("maxSpeed out of range"); // 参数异常
    }
    if (!inRange(tolerance, 0.01f, 0.5f)) {
        throw std::invalid_argument("tolerance out of range"); // 参数异常
    }

    const Position startPos{0.0f, 0.0f, 0.0f}; // 起点
    const float dx = targetPos.x - startPos.x; // dx
    const float dy = targetPos.y - startPos.y; // dy
    const float dist = hypot2(dx, dy); // 距离
    const float timeSec = (maxSpeed > 0.0f) ? (dist / maxSpeed) : 0.0f; // 时间
    const uint32_t execMs = static_cast<uint32_t>(std::min(300000.0f, timeSec * 1000.0f)); // 运行时间

    const uint32_t steps = 10U; // 采样步数
    std::vector<Position> path; // 路径
    path.reserve(steps + 1U); // 预分配
    for (uint32_t i = 0; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps); // 插值
        Position p{startPos.x + dx * t, startPos.y + dy * t, targetPos.z}; // 点
        path.push_back(p); // 追加
    }

    PositionResult outResult{}; // 输出
    outResult.reached = true; // 到达
    outResult.totalDistance = dist; // 距离
    outResult.executionTimeMs = execMs; // 时间
    outResult.path = std::move(path); // 路径
    outResult.finalPosition = targetPos; // 最终
    outResult.positionError = 0.0f; // 误差
    if (dist <= tolerance) {
        outResult.positionError = dist; // 起点接近
    }
    return outResult; // 返回
}

OdometryData getOdometryData(int64_t requestTime) {
    if (requestTime < 0) {
        throw std::invalid_argument("requestTime out of range"); // 参数异常
    }
    const uint32_t seq = static_cast<uint32_t>(requestTime % static_cast<int64_t>(std::numeric_limits<uint32_t>::max())); // 序列
    OdometryData outData{}; // 输出
    outData.position = Position{0.0f, 0.0f, 0.0f}; // 位置
    outData.heading = 0.0f; // 朝向
    outData.linearVelocity = Vector3{0.0f, 0.0f, 0.0f}; // 速度
    outData.angularVelocity = Vector3{0.0f, 0.0f, 0.0f}; // 角速度
    outData.linearAcceleration = Vector3{0.0f, 0.0f, 0.0f}; // 加速度
    outData.sequence = seq; // 序号
    outData.timestamp = requestTime; // 时间
    outData.valid = true; // 有效
    return outData; // 返回
}

ConstraintCheckResult checkMotionConstraints(float linearSpeed, float angularSpeed, float linearAccel) {
    ConstraintCheckResult outResult{}; // 输出
    outResult.maxLinearSpeed = 2.0f; // 上限
    outResult.maxAngularSpeed = 1.0f; // 上限
    outResult.maxLinearAccel = 2.0f; // 上限

    if (std::abs(linearSpeed) > outResult.maxLinearSpeed) {
        outResult.violations.push_back("linearSpeed"); // 违规项
    }
    if (std::abs(angularSpeed) > outResult.maxAngularSpeed) {
        outResult.violations.push_back("angularSpeed"); // 违规项
    }
    if (std::abs(linearAccel) > outResult.maxLinearAccel) {
        outResult.violations.push_back("linearAccel"); // 违规项
    }
    outResult.isValid = outResult.violations.empty(); // 汇总
    return outResult; // 返回
}

LiftActionResult executeLiftAction(LiftActionType actionType, float targetHeight, float actionSpeed) {
    if (actionSpeed <= 0.0f) {
        throw std::invalid_argument("actionSpeed out of range"); // 参数异常
    }
    if (targetHeight < 0.0f) {
        throw std::invalid_argument("targetHeight out of range"); // 参数异常
    }

    float actualHeight = targetHeight; // 实际高度
    if (actionType == LiftActionType::LIFT_HOME) {
        actualHeight = 0.0f; // 回零
    }
    if (actionType == LiftActionType::LIFT_STOP) {
        actualHeight = 0.0f; // 停止
    }

    const float timeSec = (actionSpeed > 0.0f) ? (std::abs(actualHeight) / actionSpeed) : 0.0f; // 时间
    const uint32_t execMs = static_cast<uint32_t>(std::min(600000.0f, timeSec * 1000.0f)); // 毫秒

    LiftActionResult out{}; // 输出
    out.success = true; // 成功
    out.actualHeight = actualHeight; // 高度
    out.executionTimeMs = execMs; // 时间
    out.errorCode = ""; // 错误码
    out.errorMessage = ""; // 错误信息
    return out; // 返回
}

TransformedPoint transformCoordinates(const Point2D& point, CoordinateFrame sourceFrame, CoordinateFrame targetFrame, const Transform2D& transform) {
    if (sourceFrame == targetFrame) {
        return TransformedPoint{point.x, point.y}; // 无需变换
    }
    const float c = std::cos(transform.rotationRad); // cos
    const float s = std::sin(transform.rotationRad); // sin
    TransformedPoint out{}; // 输出
    if (sourceFrame == CoordinateFrame::WORLD && targetFrame == CoordinateFrame::VEHICLE) {
        const float dx = point.x - transform.translateX; // dx
        const float dy = point.y - transform.translateY; // dy
        out.x = dx * c + dy * s; // 逆旋转
        out.y = -dx * s + dy * c; // 逆旋转
        return out; // 返回
    }
    if (sourceFrame == CoordinateFrame::VEHICLE && targetFrame == CoordinateFrame::WORLD) {
        out.x = point.x * c - point.y * s + transform.translateX; // 正旋转
        out.y = point.x * s + point.y * c + transform.translateY; // 正旋转
        return out; // 返回
    }
    out.x = point.x; // 默认
    out.y = point.y; // 默认
    return out; // 返回
}

float calculateDistance(const Point2D& point1, const Point2D& point2) {
    const float dx = point1.x - point2.x; // dx
    const float dy = point1.y - point2.y; // dy
    return hypot2(dx, dy); // 距离
}

float calculateCrossProduct(const Vector2D& vec1, const Vector2D& vec2) {
    return vec1.x * vec2.y - vec1.y * vec2.x; // 叉积
}

std::string generateISOTimestamp(int64_t timestamp) {
    if (timestamp < 0) {
        throw std::invalid_argument("timestamp out of range"); // 参数异常
    }
    const std::time_t sec = static_cast<std::time_t>(timestamp / 1000); // 秒
    const int ms = static_cast<int>(timestamp % 1000); // 毫秒
    std::tm tmUtc{}; // UTC时间
    if (gmtime_r(&sec, &tmUtc) == nullptr) {
        throw std::runtime_error("time conversion failed"); // 转换失败
    }
    std::ostringstream oss; // 输出
    oss << std::put_time(&tmUtc, "%Y-%m-%dT%H:%M:%S"); // 格式
    oss << '.' << std::setw(3) << std::setfill('0') << ms << 'Z'; // 毫秒
    return oss.str(); // 返回
}

TimeInterval calculateTimeInterval(int64_t startTime, int64_t endTime) {
    if (startTime < 0 || endTime < 0) {
        throw std::invalid_argument("timestamp out of range"); // 参数异常
    }
    if (endTime < startTime) {
        throw std::invalid_argument("timestamp order invalid"); // 顺序异常
    }
    TimeInterval out{}; // 输出
    out.startTime = startTime; // start
    out.endTime = endTime; // end
    out.deltaMs = endTime - startTime; // delta
    return out; // 返回
}

} // namespace simagv::l4

