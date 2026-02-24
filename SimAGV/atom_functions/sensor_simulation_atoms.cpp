#include "sensor_simulation_atoms.hpp"

#include <chrono>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace simagv::l4 {
namespace {

constexpr float kPi = 3.14159265358979323846f; // 圆周率

int64_t nowMs() {
    const auto nowTp = std::chrono::system_clock::now(); // 当前时间点
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(nowTp.time_since_epoch()).count(); // 毫秒
    return static_cast<int64_t>(ms); // 返回
}

bool inRange(float value, float minValue, float maxValue) {
    return value >= minValue && value <= maxValue; // 区间判断
}

std::vector<Point2D> sectorPolygon(const Point2D& origin, float theta, float fovDeg, float radiusM, int segments) {
    std::vector<Point2D> pts; // 点集
    pts.reserve(static_cast<size_t>(segments) + 2U); // 预分配
    const float half = (fovDeg * kPi / 180.0f) * 0.5f; // 半角
    const float alpha = theta + kPi; // 与SimVehicleSys一致
    pts.push_back(origin); // 原点
    for (int i = 0; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments); // 插值
        const float a = alpha - half + (2.0f * half) * t; // 角度
        const float px = origin.x + std::cos(a) * radiusM; // X
        const float py = origin.y + std::sin(a) * radiusM; // Y
        pts.push_back(Point2D{px, py}); // 追加
    }
    return pts; // 返回
}

} // namespace

LaserScanData generateLaserScan(const Position& vehiclePos, float vehicleHeading, float vehicleLength, const RadarConfig& radarConfig) {
    if (!inRange(vehicleHeading, -kPi, kPi)) {
        throw std::invalid_argument("vehicleHeading out of range"); // 参数异常
    }
    if (vehicleLength <= 0.1f) {
        throw std::invalid_argument("vehicleLength out of range"); // 参数异常
    }
    if (radarConfig.fovDeg <= 0.0f || radarConfig.fovDeg > 360.0f) {
        throw std::invalid_argument("radarConfig.fovDeg out of range"); // 参数异常
    }
    if (radarConfig.radiusM <= 0.0f) {
        throw std::invalid_argument("radarConfig.radiusM out of range"); // 参数异常
    }

    const RadarScanRegion region = computeFrontRadarScan(vehiclePos, vehicleHeading, vehicleLength, radarConfig); // 扫描区域
    const int beams = 24; // 光束数
    LaserScanData outData{}; // 输出
    outData.scanOrigin = region.scanOrigin; // 原点
    outData.vehicleHeading = region.vehicleHeading; // 朝向
    outData.fovDeg = radarConfig.fovDeg; // 视野
    outData.radiusM = radarConfig.radiusM; // 半径
    outData.timestamp = region.timestamp; // 时间
    outData.sequence = region.sequence; // 序列
    outData.collisionWarning = false; // 默认
    outData.obstacleDistances.assign(static_cast<size_t>(beams), radarConfig.radiusM); // 距离
    outData.obstacleAngles.resize(static_cast<size_t>(beams)); // 角度
    for (int i = 0; i < beams; ++i) {
        const float t = (beams == 1) ? 0.0f : static_cast<float>(i) / static_cast<float>(beams - 1); // 插值
        const float half = (radarConfig.fovDeg * kPi / 180.0f) * 0.5f; // 半角
        outData.obstacleAngles[static_cast<size_t>(i)] = (-half + 2.0f * half * t); // 相对角
    }
    return outData; // 返回
}

RadarScanRegion computeFrontRadarScan(const Position& vehiclePos, float vehicleHeading, float vehicleLength, const RadarConfig& radarConfig) {
    if (!inRange(vehicleHeading, -kPi, kPi)) {
        throw std::invalid_argument("vehicleHeading out of range"); // 参数异常
    }
    if (vehicleLength <= 0.1f) {
        throw std::invalid_argument("vehicleLength out of range"); // 参数异常
    }
    if (radarConfig.fovDeg <= 0.0f || radarConfig.fovDeg > 360.0f) {
        throw std::invalid_argument("radarConfig.fovDeg out of range"); // 参数异常
    }
    if (radarConfig.radiusM <= 0.0f) {
        throw std::invalid_argument("radarConfig.radiusM out of range"); // 参数异常
    }

    const float headTheta = vehicleHeading + kPi;
    const float halfLength = vehicleLength * 0.5f;
    const float forwardOffset = (halfLength > 0.1f) ? (halfLength - 0.1f) : 0.0f;
    const float ox = vehiclePos.x + std::cos(headTheta) * forwardOffset; // 原点X
    const float oy = vehiclePos.y + std::sin(headTheta) * forwardOffset; // 原点Y
    const Point2D origin2d{ox, oy}; // 2D原点
    const std::vector<Point2D> sector = sectorPolygon(origin2d, vehicleHeading, radarConfig.fovDeg, radarConfig.radiusM, 24); // 扇形

    RadarScanRegion outRegion{}; // 输出
    outRegion.scanOrigin = Position{ox, oy, vehiclePos.z}; // 原点
    outRegion.vehicleHeading = vehicleHeading; // 朝向
    outRegion.fovRad = radarConfig.fovDeg * kPi / 180.0f; // 视野
    outRegion.radiusM = radarConfig.radiusM; // 半径
    outRegion.sectorPolygon = sector; // 多边形
    outRegion.timestamp = nowMs(); // 时间
    outRegion.sequence = static_cast<uint32_t>(outRegion.timestamp % static_cast<int64_t>(std::numeric_limits<uint32_t>::max())); // 序列
    return outRegion; // 返回
}

PublishResult publishConnectionState(ConnectionState connectionState, float connectionQuality, int64_t lastHeartbeat) {
    if (!inRange(connectionQuality, 0.0f, 100.0f)) {
        throw std::invalid_argument("connectionQuality out of range"); // 参数异常
    }
    if (lastHeartbeat < 0) {
        throw std::invalid_argument("lastHeartbeat out of range"); // 参数异常
    }

    std::ostringstream oss; // JSON构建
    oss << "{\"connectionState\":" << static_cast<int>(connectionState) << ",\"connectionQuality\":" << connectionQuality << ",\"lastHeartbeat\":" << lastHeartbeat << "}"; // 输出

    PublishResult out{}; // 输出
    out.success = true; // 成功
    out.payload = oss.str(); // 载荷
    out.errorMsg = ""; // 无错误
    return out; // 返回
}

PublishResult publishAgvState(const AgvState& agvState, int64_t stateTimestamp) {
    if (agvState.agvId.empty()) {
        throw std::invalid_argument("agvId empty"); // 参数异常
    }
    if (stateTimestamp < 0) {
        throw std::invalid_argument("stateTimestamp out of range"); // 参数异常
    }

    std::ostringstream oss; // JSON构建
    oss << "{\"agvId\":\"" << agvState.agvId << "\""
        << ",\"x\":" << agvState.position.x
        << ",\"y\":" << agvState.position.y
        << ",\"heading\":" << agvState.heading
        << ",\"batteryLevel\":" << agvState.batteryLevel
        << ",\"timestamp\":" << stateTimestamp << "}"; // 输出

    PublishResult out{}; // 输出
    out.success = true; // 成功
    out.payload = oss.str(); // 载荷
    out.errorMsg = ""; // 无错误
    return out; // 返回
}

ErrorReport detectAndReportError(ErrorType errorType, const std::string& errorDetails, ErrorSeverity severity) {
    ErrorReport out{}; // 输出
    out.errorType = errorType; // 类型
    out.severity = severity; // 级别
    out.timestamp = nowMs(); // 时间
    out.errorCode = "E" + std::to_string(1000 + static_cast<int>(errorType)); // 错误码
    out.errorMessage = errorDetails; // 详情
    return out; // 返回
}

} // namespace simagv::l4
