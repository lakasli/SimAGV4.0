#include "collision_detection_atoms.hpp"

#include "json_min.hpp"
#include "sensor_simulation_atoms.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>

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

std::string trimCopy(const std::string& s) {
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) {
        ++b;
    }
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) {
        --e;
    }
    return s.substr(b, e - b);
}

std::optional<float> findYamlFloatValue(const std::string& text, const std::string& key) {
    if (key.empty()) {
        return std::nullopt;
    }
    std::istringstream iss(text);
    std::string line;
    const std::string prefix = key + ":";
    while (std::getline(iss, line)) {
        const size_t hashPos = line.find('#');
        if (hashPos != std::string::npos) {
            line = line.substr(0, hashPos);
        }
        std::string t = trimCopy(line);
        if (t.size() < prefix.size()) {
            continue;
        }
        if (t.rfind(prefix, 0) != 0) {
            continue;
        }
        std::string v = trimCopy(t.substr(prefix.size()));
        if (v.empty()) {
            continue;
        }
        if ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\'')) {
            if (v.size() >= 2) {
                v = v.substr(1, v.size() - 2);
            }
        }
        try {
            return std::stof(v);
        } catch (...) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

float hypot2(float dx, float dy) {
    return std::sqrt(dx * dx + dy * dy); // 二维勾股
}

float polygonArea(const std::vector<Point2D>& poly) {
    if (poly.size() < 3) {
        return 0.0f; // 不成面
    }
    double sum = 0.0; // 面积累加
    for (size_t i = 0; i < poly.size(); ++i) {
        const Point2D& a = poly[i]; // 点A
        const Point2D& b = poly[(i + 1) % poly.size()]; // 点B
        sum += static_cast<double>(a.x) * static_cast<double>(b.y) - static_cast<double>(a.y) * static_cast<double>(b.x); // 叉积
    }
    return static_cast<float>(std::abs(sum) * 0.5); // 返回
}

float polygonSignedArea(const std::vector<Point2D>& poly) {
    if (poly.size() < 3) {
        return 0.0f; // 退化
    }
    double sum = 0.0; // 累加
    for (size_t i = 0; i < poly.size(); ++i) {
        const Point2D& a = poly[i]; // 点A
        const Point2D& b = poly[(i + 1) % poly.size()]; // 点B
        sum += static_cast<double>(a.x) * static_cast<double>(b.y) - static_cast<double>(a.y) * static_cast<double>(b.x); // 叉积
    }
    return static_cast<float>(sum * 0.5); // 有符号面积
}

std::vector<Point2D> ensureCcw(const std::vector<Point2D>& poly) {
    std::vector<Point2D> out = poly; // 拷贝
    if (polygonSignedArea(out) < 0.0f) {
        std::reverse(out.begin(), out.end()); // 转为CCW
    }
    return out; // 返回
}

float cross2(const Vector2D& a, const Vector2D& b) {
    return a.x * b.y - a.y * b.x; // 叉积
}

Vector2D perpNorm(const Vector2D& edge) {
    Vector2D axis{-edge.y, edge.x}; // 法向
    float norm = hypot2(axis.x, axis.y); // 范数
    if (norm <= 0.0f) {
        return Vector2D{0.0f, 0.0f}; // 退化
    }
    axis.x /= norm; // 归一
    axis.y /= norm; // 归一
    return axis; // 返回
}

std::pair<float, float> projectPoly(const std::vector<Point2D>& poly, const Vector2D& axis) {
    float minProj = std::numeric_limits<float>::infinity(); // 最小投影
    float maxProj = -std::numeric_limits<float>::infinity(); // 最大投影
    for (const Point2D& p : poly) {
        float proj = p.x * axis.x + p.y * axis.y; // 投影
        minProj = std::min(minProj, proj); // 更新
        maxProj = std::max(maxProj, proj); // 更新
    }
    return {minProj, maxProj}; // 返回
}

bool polysOverlapSat(const std::vector<Point2D>& a, const std::vector<Point2D>& b, Vector2D* p_MinAxis) {
    if (a.size() < 3 || b.size() < 3) {
        return false; // 退化
    }
    float minOverlap = std::numeric_limits<float>::infinity(); // 最小重叠
    Vector2D bestAxis{0.0f, 0.0f}; // 最佳轴
    auto checkAxes = [&](const std::vector<Point2D>& poly) {
        for (size_t i = 0; i < poly.size(); ++i) {
            const Point2D& p1 = poly[i]; // 边起点
            const Point2D& p2 = poly[(i + 1) % poly.size()]; // 边终点
            const Vector2D edge{p2.x - p1.x, p2.y - p1.y}; // 边向量
            const Vector2D axis = perpNorm(edge); // 轴
            if (axis.x == 0.0f && axis.y == 0.0f) {
                continue; // 跳过
            }
            const auto [aMin, aMax] = projectPoly(a, axis); // 投影A
            const auto [bMin, bMax] = projectPoly(b, axis); // 投影B
            if (aMax < bMin || bMax < aMin) {
                return false; // 分离
            }
            float overlap = std::min(aMax, bMax) - std::max(aMin, bMin); // 重叠
            if (overlap < minOverlap) {
                minOverlap = overlap; // 更新
                bestAxis = axis; // 更新
            }
        }
        return true; // 无分离
    };
    if (!checkAxes(a)) {
        return false; // 分离
    }
    if (!checkAxes(b)) {
        return false; // 分离
    }
    if (p_MinAxis != nullptr) {
        *p_MinAxis = bestAxis; // 输出轴
    }
    return true; // 相交
}

float pointToSegmentDistance(const Point2D& p, const Point2D& a, const Point2D& b, Point2D* p_Closest) {
    const float dx = b.x - a.x; // 线段dx
    const float dy = b.y - a.y; // 线段dy
    const float len2 = dx * dx + dy * dy; // 长度平方
    if (len2 <= 0.0f) {
        if (p_Closest != nullptr) {
            *p_Closest = a; // 退化点
        }
        return hypot2(p.x - a.x, p.y - a.y); // 距离
    }
    const float tRaw = ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2; // 投影参数
    const float t = std::max(0.0f, std::min(1.0f, tRaw)); // 限制
    Point2D c{a.x + t * dx, a.y + t * dy}; // 最近点
    if (p_Closest != nullptr) {
        *p_Closest = c; // 输出
    }
    return hypot2(p.x - c.x, p.y - c.y); // 距离
}

bool pointInPoly(const Point2D& p, const std::vector<Point2D>& poly) {
    bool inside = false; // 标志
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const Point2D& pi = poly[i]; // 点i
        const Point2D& pj = poly[j]; // 点j
        const bool intersect = ((pi.y > p.y) != (pj.y > p.y)) &&
                               (p.x < (pj.x - pi.x) * (p.y - pi.y) / ((pj.y - pi.y) + 1e-12f) + pi.x); // 射线交点
        if (intersect) {
            inside = !inside; // 翻转
        }
    }
    return inside; // 返回
}

float minDistancePointToPoly(const Point2D& p, const std::vector<Point2D>& poly, Point2D* p_Closest) {
    if (poly.empty()) {
        return std::numeric_limits<float>::infinity(); // 无数据
    }
    if (pointInPoly(p, poly)) {
        if (p_Closest != nullptr) {
            *p_Closest = p; // 点内
        }
        return 0.0f; // 内部距离
    }
    float bestDist = std::numeric_limits<float>::infinity(); // 最小距离
    Point2D bestPoint{0.0f, 0.0f}; // 最近点
    for (size_t i = 0; i < poly.size(); ++i) {
        const Point2D& a = poly[i]; // 边起点
        const Point2D& b = poly[(i + 1) % poly.size()]; // 边终点
        Point2D c{0.0f, 0.0f}; // 最近点
        const float dist = pointToSegmentDistance(p, a, b, &c); // 距离
        if (dist < bestDist) {
            bestDist = dist; // 更新
            bestPoint = c; // 更新
        }
    }
    if (p_Closest != nullptr) {
        *p_Closest = bestPoint; // 输出
    }
    return bestDist; // 返回
}

bool isInsideHalfPlane(const Point2D& p, const Point2D& a, const Point2D& b) {
    const Vector2D ab{b.x - a.x, b.y - a.y}; // AB
    const Vector2D ap{p.x - a.x, p.y - a.y}; // AP
    return cross2(ab, ap) >= -1e-6f; // CCW内侧
}

Point2D intersectSegmentLine(const Point2D& s, const Point2D& e, const Point2D& a, const Point2D& b) {
    const Vector2D r{b.x - a.x, b.y - a.y}; // 方向r
    const Vector2D sv{e.x - s.x, e.y - s.y}; // 方向s
    const float denom = cross2(r, sv); // 分母
    if (std::abs(denom) < 1e-12f) {
        return e; // 平行退化
    }
    const Vector2D sa{s.x - a.x, s.y - a.y}; // SA
    const float t = cross2(sa, sv) / denom; // 参数t
    return Point2D{a.x + t * r.x, a.y + t * r.y}; // 交点
}

std::vector<Point2D> convexPolygonIntersection(const std::vector<Point2D>& subject, const std::vector<Point2D>& clip) {
    if (subject.size() < 3 || clip.size() < 3) {
        return {}; // 退化
    }
    std::vector<Point2D> out = ensureCcw(subject); // subject
    const std::vector<Point2D> clipCcw = ensureCcw(clip); // clip
    for (size_t i = 0; i < clipCcw.size(); ++i) {
        const Point2D a = clipCcw[i]; // clip点A
        const Point2D b = clipCcw[(i + 1) % clipCcw.size()]; // clip点B
        std::vector<Point2D> input = out; // 输入
        out.clear(); // 清空
        if (input.empty()) {
            break; // 结束
        }
        Point2D S = input.back(); // 上一个
        for (const Point2D& E : input) {
            const bool insideE = isInsideHalfPlane(E, a, b); // E内
            const bool insideS = isInsideHalfPlane(S, a, b); // S内
            if (insideE) {
                if (!insideS) {
                    out.push_back(intersectSegmentLine(S, E, a, b)); // 交点
                }
                out.push_back(E); // E
            } else if (insideS) {
                out.push_back(intersectSegmentLine(S, E, a, b)); // 交点
            }
            S = E; // 前进
        }
    }
    if (out.size() < 3) {
        return {}; // 退化
    }
    return out; // 返回
}

Point2D vehicleLocalToWorld(const Point2D& local, const Point2D& center, float theta) {
    const float s = std::cos(theta); // cos
    const float c = std::sin(theta); // sin
    const float wx = center.x + local.x * c + local.y * s; // X
    const float wy = center.y - local.x * s + local.y * c; // Y
    return Point2D{wx, wy}; // 返回
}

Point2D worldToVehicleLocal(const Point2D& world, const Point2D& center, float theta) {
    const float dx = world.x - center.x; // dx
    const float dy = world.y - center.y; // dy
    const float s = std::cos(theta); // cos
    const float c = std::sin(theta); // sin
    const float lx = dx * c - dy * s; // lx
    const float ly = dx * s + dy * c; // ly
    return Point2D{lx, ly}; // 返回
}

std::vector<Point2D> orientedRectPolygon(const Point2D& center, float lengthM, float widthM, float theta) {
    const float hl = lengthM * 0.5f; // 半长
    const float hw = widthM * 0.5f;  // 半宽
    const std::vector<Point2D> local = {
        Point2D{-hw, -hl},
        Point2D{hw, -hl},
        Point2D{hw, hl},
        Point2D{-hw, hl},
    }; // 局部矩形
    std::vector<Point2D> out; // 输出
    out.reserve(local.size()); // 预分配
    for (const Point2D& p : local) {
        out.push_back(vehicleLocalToWorld(p, center, theta)); // 转换
    }
    return out; // 返回
}

std::string readTextFile(const std::string& filePath) {
    std::ifstream ifs(filePath); // 输入流
    if (!ifs.is_open()) {
        throw std::runtime_error("failed to open file"); // 打开失败
    }
    std::ostringstream oss; // 缓冲
    oss << ifs.rdbuf(); // 读取
    return oss.str(); // 返回
}

[[maybe_unused]] float getNumberOr(const simagv::json::Object& obj, const std::string& key, float defaultValue) {
    const auto it = obj.find(key); // 查找
    if (it == obj.end() || !it->second.isNumber()) {
        return defaultValue; // 默认
    }
    return static_cast<float>(it->second.asNumber()); // 转换
}

} // namespace

RadarCollisionResult detectRadarCollision(const RadarScanRegion& radarRegion, const std::vector<Polygon2D>& obstaclePolygons) {
    if (radarRegion.sectorPolygon.size() < 3) {
        throw std::invalid_argument("radarRegion invalid"); // 参数异常
    }
    RadarCollisionResult outResult{}; // 输出
    outResult.hasCollision = false; // 默认
    outResult.minDistance = std::numeric_limits<float>::infinity(); // 初始化
    outResult.timestamp = nowMs(); // 时间

    const Point2D origin{radarRegion.scanOrigin.x, radarRegion.scanOrigin.y}; // 原点
    Vector2D dummyAxis{0.0f, 0.0f}; // 轴
    for (size_t idx = 0; idx < obstaclePolygons.size(); ++idx) {
        const Polygon2D& obstacle = obstaclePolygons[idx]; // 障碍物
        if (!obstacle.isValid()) {
            continue; // 跳过
        }
        const bool overlap = polysOverlapSat(radarRegion.sectorPolygon, obstacle.vertices, &dummyAxis); // SAT
        if (!overlap) {
            continue; // 无碰撞
        }
        Point2D nearestPoint{0.0f, 0.0f}; // 最近点
        const float dist = minDistancePointToPoly(origin, obstacle.vertices, &nearestPoint); // 距离
        const std::vector<Point2D> interPoly = convexPolygonIntersection(obstacle.vertices, radarRegion.sectorPolygon); // 交集
        const float area = polygonArea(interPoly); // 面积
        RadarCollisionResult::CollisionDetail detail{}; // 详情
        detail.obstacleId = static_cast<int>(idx); // 索引ID
        detail.nearestPoint = nearestPoint; // 最近点
        detail.distance = dist; // 距离
        detail.overlapArea = area; // 面积

        outResult.hasCollision = true; // 标记
        outResult.collisionDetails.push_back(detail); // 写入
        outResult.collisionObstacleIds.push_back(detail.obstacleId); // 写入
        outResult.minDistance = std::min(outResult.minDistance, dist); // 更新
    }
    if (!outResult.hasCollision) {
        outResult.minDistance = std::numeric_limits<float>::infinity(); // 保持
    }
    return outResult; // 返回
}

Polygon2D buildObstaclePolygon(const ObstacleData& obstacleData, ObstacleType obstacleType) {
    if (!obstacleData.isValid()) {
        throw std::invalid_argument("obstacleData invalid"); // 参数异常
    }

    std::vector<Point2D> points; // 点集
    points.reserve(obstacleData.distances.size()); // 预分配
    for (size_t i = 0; i < obstacleData.distances.size(); ++i) {
        const float r = obstacleData.distances[i]; // 距离
        const float a = obstacleData.angles[i]; // 角度
        const float x = std::cos(a) * r; // X
        const float y = std::sin(a) * r; // Y
        points.push_back(Point2D{x, y}); // 追加
    }

    Polygon2D outPoly{}; // 输出
    if (obstacleType == ObstacleType::POINT_CLOUD || obstacleType == ObstacleType::POLYGON) {
        outPoly.vertices = std::move(points); // 直接使用
        return outPoly; // 返回
    }

    float minX = std::numeric_limits<float>::infinity(); // minX
    float maxX = -std::numeric_limits<float>::infinity(); // maxX
    float minY = std::numeric_limits<float>::infinity(); // minY
    float maxY = -std::numeric_limits<float>::infinity(); // maxY
    for (const Point2D& p : points) {
        minX = std::min(minX, p.x); // 更新
        maxX = std::max(maxX, p.x); // 更新
        minY = std::min(minY, p.y); // 更新
        maxY = std::max(maxY, p.y); // 更新
    }

    if (obstacleType == ObstacleType::RECTANGLE) {
        outPoly.vertices = {Point2D{minX, minY}, Point2D{maxX, minY}, Point2D{maxX, maxY}, Point2D{minX, maxY}}; // AABB
        return outPoly; // 返回
    }

    if (obstacleType == ObstacleType::CIRCLE) {
        const float cx = (minX + maxX) * 0.5f; // 中心X
        const float cy = (minY + maxY) * 0.5f; // 中心Y
        const float radius = 0.5f * std::max(maxX - minX, maxY - minY); // 半径
        const int segments = 16; // 段数
        outPoly.vertices.reserve(static_cast<size_t>(segments)); // 预分配
        for (int i = 0; i < segments; ++i) {
            const float ang = (2.0f * kPi) * (static_cast<float>(i) / static_cast<float>(segments)); // 角
            outPoly.vertices.push_back(Point2D{cx + std::cos(ang) * radius, cy + std::sin(ang) * radius}); // 点
        }
        return outPoly; // 返回
    }

    return outPoly; // 返回
}

SafetyRange calculateSafetyRange(const std::string& vehicleId, float safetyFactor) {
    if (!inRange(safetyFactor, 1.1f, 2.0f)) {
        throw std::invalid_argument("safetyFactor out of range"); // 参数异常
    }
    if (vehicleId.empty()) {
        throw std::invalid_argument("vehicleId empty"); // 参数异常
    }

    const std::filesystem::path baseDir("../");
    const std::filesystem::path configPath = baseDir / vehicleId / "config.yaml";

    float length = 0.0f;
    float width = 0.0f;
    bool found = false;

    if (std::filesystem::exists(configPath)) {
        const std::string text = readTextFile(configPath.string());
        const auto w = findYamlFloatValue(text, "width");
        const auto l = findYamlFloatValue(text, "length");
        if (w.has_value() && l.has_value() && w.value() > 0.0f && l.value() > 0.0f) {
            width = w.value();
            length = l.value();
            found = true;
        }
    }
    if (!found) {
        throw std::runtime_error("vehicle size missing");
    }

    SafetyRange outRange{}; // 输出
    outRange.originalLength = length; // 原长
    outRange.originalWidth = width; // 原宽
    outRange.safetyFactor = safetyFactor; // 系数
    outRange.length = length * safetyFactor; // 安全长
    outRange.width = width * safetyFactor; // 安全宽
    outRange.valid = true; // 有效
    return outRange; // 返回
}

SafetyOverlapResult checkSafetyRangeOverlap(const SafetyRange& ownSafetyRange, const Position& ownPosition, const SafetyRange& otherSafetyRange, const Position& otherPosition) {
    if (!ownSafetyRange.valid || !otherSafetyRange.valid) {
        throw std::invalid_argument("safety range invalid"); // 参数异常
    }

    const float dx = std::abs(ownPosition.x - otherPosition.x); // 中心距X
    const float dy = std::abs(ownPosition.y - otherPosition.y); // 中心距Y
    const float overlapX = (ownSafetyRange.width * 0.5f + otherSafetyRange.width * 0.5f) - dx; // X重叠
    const float overlapY = (ownSafetyRange.length * 0.5f + otherSafetyRange.length * 0.5f) - dy; // Y重叠
    const bool hasOverlap = (overlapX > 0.0f) && (overlapY > 0.0f); // 重叠

    SafetyOverlapResult outResult{}; // 输出
    outResult.hasOverlap = hasOverlap; // 标志
    outResult.errorLevel = hasOverlap ? CollisionErrorLevel::ERROR : CollisionErrorLevel::NO_COLLISION; // 级别
    outResult.errorMessage = hasOverlap ? "safety range overlap" : ""; // 信息
    return outResult; // 返回
}

CollisionResult checkPolygonCollision(const std::vector<Point2D>& poly1, const std::vector<Point2D>& poly2) {
    if (poly1.size() < 3 || poly2.size() < 3) {
        throw std::invalid_argument("polygon invalid"); // 参数异常
    }
    Vector2D minAxis{0.0f, 0.0f}; // 最小轴
    const bool isOverlap = polysOverlapSat(poly1, poly2, &minAxis); // SAT

    CollisionResult outResult{}; // 输出
    outResult.isColliding = isOverlap; // 标志
    outResult.separationAxis = minAxis; // 分离轴
    outResult.contactPoint = Point2D{0.0f, 0.0f}; // 默认
    outResult.contactPoints.clear(); // 清空

    if (!isOverlap) {
        return outResult; // 返回
    }

    const std::vector<Point2D> interPoly = convexPolygonIntersection(poly1, poly2); // 交集
    outResult.contactPoints = interPoly; // 接触多点
    if (!interPoly.empty()) {
        Point2D avg{0.0f, 0.0f}; // 平均点
        for (const Point2D& p : interPoly) {
            avg.x += p.x; // 累加
            avg.y += p.y; // 累加
        }
        avg.x /= static_cast<float>(interPoly.size()); // 平均
        avg.y /= static_cast<float>(interPoly.size()); // 平均
        outResult.contactPoint = avg; // 输出
    }
    return outResult; // 返回
}

SafetyEnvelope computeSafetyEnvelope(const Position& vehiclePos, float vehicleHeading, const Size2D& vehicleSize, const Size2D& loadSize, float safetyMargin) {
    if (!inRange(vehicleHeading, -kPi, kPi)) {
        throw std::invalid_argument("vehicleHeading out of range"); // 参数异常
    }
    if (vehicleSize.length <= 0.0f || vehicleSize.width <= 0.0f) {
        throw std::invalid_argument("vehicleSize invalid"); // 参数异常
    }
    if (safetyMargin < 0.0f) {
        throw std::invalid_argument("safetyMargin out of range"); // 参数异常
    }

    const Point2D center{vehiclePos.x, vehiclePos.y}; // 中心
    const std::vector<Point2D> vPoly = orientedRectPolygon(center, vehicleSize.length, vehicleSize.width, vehicleHeading); // 车体
    std::vector<Point2D> pts = vPoly; // 点集
    if (loadSize.length > 0.0f && loadSize.width > 0.0f) {
        const std::vector<Point2D> lPoly = orientedRectPolygon(center, loadSize.length, loadSize.width, vehicleHeading); // 负载
        pts.insert(pts.end(), lPoly.begin(), lPoly.end()); // 合并
    }

    float minLx = std::numeric_limits<float>::infinity(); // minLx
    float maxLx = -std::numeric_limits<float>::infinity(); // maxLx
    float minLy = std::numeric_limits<float>::infinity(); // minLy
    float maxLy = -std::numeric_limits<float>::infinity(); // maxLy
    for (const Point2D& p : pts) {
        const Point2D lp = worldToVehicleLocal(p, center, vehicleHeading); // 局部
        minLx = std::min(minLx, lp.x); // 更新
        maxLx = std::max(maxLx, lp.x); // 更新
        minLy = std::min(minLy, lp.y); // 更新
        maxLy = std::max(maxLy, lp.y); // 更新
    }

    const float baseWidth = std::max(0.0f, maxLx - minLx); // 基宽
    const float baseLength = std::max(0.0f, maxLy - minLy); // 基长
    const float outWidth = baseWidth + 2.0f * safetyMargin; // 扩展宽
    const float outLength = baseLength + 2.0f * safetyMargin; // 扩展长
    const float lcX = (minLx + maxLx) * 0.5f; // 局部中心X
    const float lcY = (minLy + maxLy) * 0.5f; // 局部中心Y
    const Point2D outCenter2d = vehicleLocalToWorld(Point2D{lcX, lcY}, center, vehicleHeading); // 世界中心
    const std::vector<Point2D> outPoly = orientedRectPolygon(outCenter2d, outLength, outWidth, vehicleHeading); // 包络多边形

    SafetyEnvelope outEnvelope{}; // 输出
    outEnvelope.center = Position{outCenter2d.x, outCenter2d.y, vehiclePos.z}; // 中心
    outEnvelope.length = outLength; // 长度
    outEnvelope.width = outWidth; // 宽度
    outEnvelope.heading = vehicleHeading; // 朝向
    outEnvelope.polygon = outPoly; // 多边形
    return outEnvelope; // 返回
}

RadarScanResult computeFrontRadar(const Position& vehiclePos, float vehicleHeading, float scanRange, float scanAngle, const MapData& mapData) {
    if (!inRange(vehicleHeading, -kPi, kPi)) {
        throw std::invalid_argument("vehicleHeading out of range"); // 参数异常
    }
    if (scanRange <= 0.0f) {
        throw std::invalid_argument("scanRange out of range"); // 参数异常
    }
    if (scanAngle <= 0.0f || scanAngle > 360.0f) {
        throw std::invalid_argument("scanAngle out of range"); // 参数异常
    }

    const RadarConfig cfg{scanAngle, scanRange}; // 配置
    const RadarScanRegion region = computeFrontRadarScan(vehiclePos, vehicleHeading, 1.0f, cfg); // 区域
    RadarScanResult outResult{}; // 输出
    outResult.hasObstacle = false; // 默认
    outResult.minDistance = std::numeric_limits<float>::infinity(); // 初始化

    const Point2D origin{region.scanOrigin.x, region.scanOrigin.y}; // 原点
    for (const MapObstacle& ob : mapData.obstacles) {
        if (!ob.poly.isValid()) {
            continue; // 跳过
        }
        Vector2D axis{0.0f, 0.0f}; // 轴
        if (!polysOverlapSat(region.sectorPolygon, ob.poly.vertices, &axis)) {
            continue; // 无命中
        }
        Point2D nearestPoint{0.0f, 0.0f}; // 最近点
        const float dist = minDistancePointToPoly(origin, ob.poly.vertices, &nearestPoint); // 距离
        outResult.hasObstacle = true; // 标记
        outResult.obstacleIds.push_back(ob.id); // 追加
        outResult.minDistance = std::min(outResult.minDistance, dist); // 更新
    }
    if (!outResult.hasObstacle) {
        outResult.minDistance = std::numeric_limits<float>::infinity(); // 无障碍
    }
    return outResult; // 返回
}

} // namespace simagv::l4
