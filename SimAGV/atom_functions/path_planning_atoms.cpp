#include "path_planning_atoms.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>
#include <unordered_map>

namespace simagv::l4 {
namespace {

constexpr float kPi = 3.14159265358979323846f; // 圆周率

float hypot2(float dx, float dy) {
    return std::sqrt(dx * dx + dy * dy); // 二维勾股
}

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

struct Similarity2D {
    float c;
    float s;
    float scale;
    float tx;
    float ty;
};

Similarity2D makeSimilarityFromTwoPoints(const Point2D& src0, const Point2D& src1, const Point2D& dst0, const Point2D& dst1)
{
    const float srcDx = src1.x - src0.x;
    const float srcDy = src1.y - src0.y;
    const float dstDx = dst1.x - dst0.x;
    const float dstDy = dst1.y - dst0.y;
    const float srcLen = hypot2(srcDx, srcDy);
    const float dstLen = hypot2(dstDx, dstDy);

    float scale = 1.0f;
    float rot = 0.0f;
    if (srcLen > 1e-6f && dstLen > 1e-6f) {
        scale = dstLen / srcLen;
        rot = std::atan2(dstDy, dstDx) - std::atan2(srcDy, srcDx);
    }

    Similarity2D out{};
    out.c = std::cos(rot);
    out.s = std::sin(rot);
    out.scale = scale;
    out.tx = dst0.x - (out.c * (scale * src0.x) - out.s * (scale * src0.y));
    out.ty = dst0.y - (out.s * (scale * src0.x) + out.c * (scale * src0.y));
    return out;
}

Point2D applySimilarity(const Similarity2D& tr, const Point2D& p)
{
    const float x = tr.c * (tr.scale * p.x) - tr.s * (tr.scale * p.y) + tr.tx;
    const float y = tr.s * (tr.scale * p.x) + tr.c * (tr.scale * p.y) + tr.ty;
    return Point2D{x, y};
}

std::vector<double> nurbsBasisFunctions(int degree, const std::vector<float>& knotVector, double u) {
    if (degree < 1) {
        return {}; // 退化
    }
    if (knotVector.size() < static_cast<size_t>(degree + 2)) {
        return {}; // 退化
    }
    const int n = static_cast<int>(knotVector.size()) - degree - 1; // 基函数数量
    if (n <= 0) {
        return {}; // 退化
    }

    std::vector<double> basis(static_cast<size_t>(n), 0.0); // basis
    int span = degree; // span
    for (int i = degree; i < static_cast<int>(knotVector.size()) - 1; ++i) {
        const double ki = static_cast<double>(knotVector[static_cast<size_t>(i)]); // ki
        const double kip = static_cast<double>(knotVector[static_cast<size_t>(i + 1)]); // ki+1
        if (ki <= u && u < kip) {
            span = i; // 命中
            break;
        }
    }
    if (u >= static_cast<double>(knotVector.back())) {
        span = static_cast<int>(knotVector.size()) - degree - 2; // 末端
    }
    if (span < 0 || span >= n) {
        return {}; // 无效
    }
    basis[static_cast<size_t>(span)] = 1.0; // 初始化

    for (int k = 1; k <= degree; ++k) {
        std::vector<double> temp(static_cast<size_t>(n), 0.0); // 临时
        const int jStart = span - k; // 起点
        const int jEnd = span; // 终点
        for (int j = jStart; j <= jEnd; ++j) {
            if (j < 0 || j >= n) {
                continue; // 越界
            }
            double saved = 0.0; // 累计
            if (basis[static_cast<size_t>(j)] != 0.0) {
                const double denom = static_cast<double>(knotVector[static_cast<size_t>(j + k)]) - static_cast<double>(knotVector[static_cast<size_t>(j)]); // 分母
                if (denom != 0.0) {
                    const double left = (u - static_cast<double>(knotVector[static_cast<size_t>(j)])) / denom; // 左
                    saved = basis[static_cast<size_t>(j)] * left; // 保存
                }
            }
            if (j < n - 1 && basis[static_cast<size_t>(j + 1)] != 0.0) {
                const double denom = static_cast<double>(knotVector[static_cast<size_t>(j + k + 1)]) - static_cast<double>(knotVector[static_cast<size_t>(j + 1)]); // 分母
                if (denom != 0.0) {
                    const double right = (static_cast<double>(knotVector[static_cast<size_t>(j + k + 1)]) - u) / denom; // 右
                    saved += basis[static_cast<size_t>(j + 1)] * right; // 累加
                }
            }
            temp[static_cast<size_t>(j)] = saved; // 写入
        }
        basis = std::move(temp); // 更新
    }
    return basis; // 返回
}

struct NurbsEvalResult {
    double x;              // X坐标
    double y;              // Y坐标
    double theta;          // 朝向
    bool hasExplicitTheta; // 是否存在显式朝向
    double tangentTheta;   // 切线朝向
};

NurbsEvalResult evaluateNurbsWithTangent(const Trajectory& trajectory, double u) {
    NurbsEvalResult out{0.0, 0.0, 0.0, false, 0.0}; // 输出
    const std::vector<double> basis = nurbsBasisFunctions(static_cast<int>(trajectory.degree), trajectory.knotVector, u); // basis
    if (basis.empty()) {
        return out; // 退化
    }

    double x = 0.0; // x
    double y = 0.0; // y
    double totalWeight = 0.0; // 权重和
    double thetaSum = 0.0; // theta和
    double thetaWeightSum = 0.0; // theta权重
    bool hasTheta = false; // 显式theta

    const size_t cpCount = std::min(trajectory.controlPoints.size(), basis.size()); // 控制点数
    for (size_t i = 0; i < cpCount; ++i) {
        const double w0 = basis[i]; // 基函数权重
        if (w0 <= 0.0) {
            continue; // 跳过
        }
        const TrajectoryControlPoint& cp = trajectory.controlPoints[i]; // 控制点
        const double cpW = (cp.weight > 0.0f) ? static_cast<double>(cp.weight) : 1.0; // 控制点权重
        const double w = w0 * cpW; // 合成权重
        x += static_cast<double>(cp.x) * w; // 加权
        y += static_cast<double>(cp.y) * w; // 加权
        if (!std::isnan(cp.orientation)) {
            thetaSum += static_cast<double>(cp.orientation) * w; // 加权theta
            thetaWeightSum += w; // 权重
            hasTheta = true; // 标志
        }
        totalWeight += w; // 累加
    }

    if (totalWeight > 0.0) {
        x /= totalWeight; // 归一
        y /= totalWeight; // 归一
        if (hasTheta && thetaWeightSum > 0.0) {
            thetaSum /= thetaWeightSum; // 归一
        }
    }
    out.x = x; // 输出
    out.y = y; // 输出
    out.theta = thetaSum; // 输出
    out.hasExplicitTheta = hasTheta; // 输出

    const double delta = 0.001; // 微分步长
    const double uNext = std::min(u + delta, 1.0); // 下一u
    const std::vector<double> basisNext = nurbsBasisFunctions(static_cast<int>(trajectory.degree), trajectory.knotVector, uNext); // basisNext
    double xNext = 0.0; // xNext
    double yNext = 0.0; // yNext
    double totalWeightNext = 0.0; // 权重
    const size_t cpCountNext = std::min(trajectory.controlPoints.size(), basisNext.size()); // 控制点数
    for (size_t i = 0; i < cpCountNext; ++i) {
        const double w0 = basisNext[i]; // 权重
        if (w0 <= 0.0) {
            continue; // 跳过
        }
        const TrajectoryControlPoint& cp = trajectory.controlPoints[i]; // 控制点
        const double cpW = (cp.weight > 0.0f) ? static_cast<double>(cp.weight) : 1.0; // 控制点权重
        const double w = w0 * cpW; // 合成
        xNext += static_cast<double>(cp.x) * w; // 累加
        yNext += static_cast<double>(cp.y) * w; // 累加
        totalWeightNext += w; // 累加
    }
    if (totalWeightNext > 0.0) {
        xNext /= totalWeightNext; // 归一
        yNext /= totalWeightNext; // 归一
    }
    out.tangentTheta = std::atan2(yNext - y, xNext - x); // 切线
    return out; // 返回
}

} // namespace

std::vector<std::string> aStarTopologyRouting(const std::string& startStationId, const std::string& targetStationId, const std::vector<SceneStationNode>& stations, const std::vector<ScenePathEdge>& paths) {
    if (startStationId.empty() || targetStationId.empty()) {
        throw std::invalid_argument("stationId empty"); // 参数异常
    }
    if (startStationId == targetStationId) {
        return {startStationId}; // 直接返回
    }

    std::unordered_map<std::string, Point2D> posMap; // 坐标表
    posMap.reserve(stations.size()); // 预分配
    for (const SceneStationNode& st : stations) {
        posMap.emplace(st.id, Point2D{st.x, st.y}); // 写入
    }
    if (posMap.find(startStationId) == posMap.end() || posMap.find(targetStationId) == posMap.end()) {
        return {}; // 不存在
    }

    std::unordered_map<std::string, std::vector<std::pair<std::string, float>>> adj; // 邻接表
    for (const ScenePathEdge& e : paths) {
        adj[e.from].push_back({e.to, e.length}); // 有向边
    }

    auto h = [&](const std::string& nid) -> float {
        const Point2D a = posMap.at(nid); // 点A
        const Point2D b = posMap.at(targetStationId); // 点B
        return hypot2(a.x - b.x, a.y - b.y); // 启发
    };

    struct NodeItem {
        float f;        // 估计总代价
        std::string id; // 节点ID
    };
    struct Cmp {
        bool operator()(const NodeItem& a, const NodeItem& b) const { return a.f > b.f; }
    };

    std::priority_queue<NodeItem, std::vector<NodeItem>, Cmp> open; // 开集
    std::unordered_map<std::string, float> gScore; // g代价
    std::unordered_map<std::string, std::string> cameFrom; // 回溯

    gScore[startStationId] = 0.0f; // 初始
    open.push(NodeItem{h(startStationId), startStationId}); // 入队

    while (!open.empty()) {
        const NodeItem cur = open.top(); // 当前
        open.pop(); // 出队
        if (cur.id == targetStationId) {
            break; // 命中
        }
        const auto itAdj = adj.find(cur.id); // 邻居
        if (itAdj == adj.end()) {
            continue; // 无边
        }
        for (const auto& next : itAdj->second) {
            const std::string& nid = next.first; // 邻居ID
            const float cost = next.second; // 边代价
            const float tentative = gScore[cur.id] + cost; // 临时代价
            const auto itG = gScore.find(nid); // 查找
            if (itG == gScore.end() || tentative < itG->second) {
                cameFrom[nid] = cur.id; // 回溯
                gScore[nid] = tentative; // 更新
                open.push(NodeItem{tentative + h(nid), nid}); // 入队
            }
        }
    }

    if (cameFrom.find(targetStationId) == cameFrom.end()) {
        return {}; // 不可达
    }

    std::vector<std::string> route; // 路由
    std::string curId = targetStationId; // 当前
    route.push_back(curId); // 追加
    while (curId != startStationId) {
        curId = cameFrom.at(curId); // 回溯
        route.push_back(curId); // 追加
    }
    std::reverse(route.begin(), route.end()); // 翻转
    return route; // 返回
}

std::vector<PosePoint> generateRoutePolyline(const std::vector<std::string>& routeNodeIds, const std::vector<SceneStationNode>& stations, const std::vector<ScenePathEdge>& paths, uint32_t stepsPerEdge) {
    if (routeNodeIds.size() < 2U) {
        throw std::invalid_argument("routeNodeIds invalid"); // 参数异常
    }
    if (stepsPerEdge < 2U || stepsPerEdge > 200U) {
        throw std::invalid_argument("stepsPerEdge out of range"); // 参数异常
    }

    std::unordered_map<std::string, Point2D> posMap; // 坐标表
    for (const SceneStationNode& st : stations) {
        posMap.emplace(st.id, Point2D{st.x, st.y}); // 写入
    }

    auto findEdge = [&](const std::string& from, const std::string& to) -> const ScenePathEdge* {
        for (const ScenePathEdge& e : paths) {
            if (e.from == from && e.to == to) {
                return &e; // 命中
            }
        }
        return nullptr; // 未找到
    };

    std::vector<PosePoint> outPoints; // 输出
    for (size_t i = 0; i + 1 < routeNodeIds.size(); ++i) {
        const std::string& fromId = routeNodeIds[i]; // from
        const std::string& toId = routeNodeIds[i + 1]; // to
        if (posMap.find(fromId) == posMap.end() || posMap.find(toId) == posMap.end()) {
            throw std::invalid_argument("route node missing"); // 缺失
        }
        const Point2D p0 = posMap.at(fromId); // 起点
        const Point2D p3 = posMap.at(toId); // 终点
        const ScenePathEdge* edge = findEdge(fromId, toId); // 边
        const Point2D p1 = (edge != nullptr) ? Point2D{edge->cp1.x, edge->cp1.y} : p0; // 控制点1
        const Point2D p2 = (edge != nullptr) ? Point2D{edge->cp2.x, edge->cp2.y} : p3; // 控制点2
        const std::string edgeId = (edge != nullptr) ? edge->id : (fromId + "->" + toId); // 边ID

        for (uint32_t s = 0; s <= stepsPerEdge; ++s) {
            const float t = static_cast<float>(s) / static_cast<float>(stepsPerEdge); // 参数
            const float u = 1.0f - t; // 反向
            const float bx = u * u * u * p0.x + 3.0f * u * u * t * p1.x + 3.0f * u * t * t * p2.x + t * t * t * p3.x; // 贝塞尔X
            const float by = u * u * u * p0.y + 3.0f * u * u * t * p1.y + 3.0f * u * t * t * p2.y + t * t * t * p3.y; // 贝塞尔Y
            float theta = 0.0f; // 朝向
            if (!outPoints.empty()) {
                const PosePoint& last = outPoints.back(); // 上一
                const float dx = bx - last.x; // dx
                const float dy = by - last.y; // dy
                if (hypot2(dx, dy) > 1e-6f) {
                    theta = std::atan2(dy, dx); // 朝向
                } else {
                    theta = last.theta; // 保持
                }
            }
            PosePoint pp{}; // 点
            pp.x = bx; // x
            pp.y = by; // y
            pp.theta = normalizeAngle(theta); // theta
            pp.edgeId = edgeId; // edgeId
            pp.turnAnchor = false; // 标志
            outPoints.push_back(pp); // 追加
        }
    }
    return outPoints; // 返回
}

std::vector<PosePoint> augmentWithCornerTurns(const std::vector<PosePoint>& inputPoints, float thetaThreshold, float stepDelta, float posEps) {
    if (inputPoints.size() < 2U) {
        return inputPoints; // 无需处理
    }
    if (!(thetaThreshold > 0.0f && thetaThreshold <= kPi)) {
        throw std::invalid_argument("thetaThreshold out of range"); // 参数异常
    }
    if (!(stepDelta > 0.0f && stepDelta <= kPi)) {
        throw std::invalid_argument("stepDelta out of range"); // 参数异常
    }
    if (posEps < 0.0f) {
        throw std::invalid_argument("posEps out of range"); // 参数异常
    }

    std::vector<PosePoint> out; // 输出
    out.reserve(inputPoints.size()); // 预分配
    out.push_back(inputPoints.front()); // 起点
    for (size_t i = 1; i < inputPoints.size(); ++i) {
        const PosePoint& prev = out.back(); // 前一
        const PosePoint& cur = inputPoints[i]; // 当前
        const float dx = cur.x - prev.x; // dx
        const float dy = cur.y - prev.y; // dy
        const float dpos = hypot2(dx, dy); // 位移
        const float dtheta = normalizeAngle(cur.theta - prev.theta); // 朝向差
        if (std::abs(dtheta) > thetaThreshold && dpos <= posEps) {
            const float sign = (dtheta >= 0.0f) ? 1.0f : -1.0f; // 方向
            float th = prev.theta; // 当前角
            while (std::abs(normalizeAngle(cur.theta - th)) > stepDelta) {
                th = normalizeAngle(th + sign * stepDelta); // 递进
                PosePoint tp = prev; // 拷贝
                tp.theta = th; // 更新
                tp.turnAnchor = true; // 标记
                out.push_back(tp); // 插入
            }
        }
        out.push_back(cur); // 追加
    }
    return out; // 返回
}

std::vector<PosePoint> trajectoryPolyline(const Trajectory& trajectory, const Position& startPos, const Position& endPos, const TrajectoryPolylineConfig& config) {
    if (config.steps < 2U || config.steps > 500U) {
        throw std::invalid_argument("config.steps out of range"); // 参数异常
    }
    const bool overrideTheta = !std::isnan(config.orientation); // 覆盖标志
    std::vector<PosePoint> out; // 输出
    out.reserve(config.steps + 1U); // 预分配

    for (uint32_t i = 0; i <= config.steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(config.steps); // 参数
        float x = 0.0f; // x
        float y = 0.0f; // y
        float thetaHint = std::numeric_limits<float>::quiet_NaN(); // 朝向提示
        bool thetaHintIsTangent = false;
        if (trajectory.type == TrajectoryType::STRAIGHT) {
            x = startPos.x + (endPos.x - startPos.x) * t; // 线性
            y = startPos.y + (endPos.y - startPos.y) * t; // 线性
        } else if (trajectory.type == TrajectoryType::CUBIC_BEZIER && trajectory.controlPoints.size() >= 4U) {
            const Point2D src0{trajectory.controlPoints[0].x, trajectory.controlPoints[0].y};
            const Point2D src1{trajectory.controlPoints[3].x, trajectory.controlPoints[3].y};
            const Point2D dst0{startPos.x, startPos.y};
            const Point2D dst1{endPos.x, endPos.y};
            const Similarity2D tr = makeSimilarityFromTwoPoints(src0, src1, dst0, dst1);

            const Point2D p0 = dst0;
            const Point2D p1 = applySimilarity(tr, Point2D{trajectory.controlPoints[1].x, trajectory.controlPoints[1].y});
            const Point2D p2 = applySimilarity(tr, Point2D{trajectory.controlPoints[2].x, trajectory.controlPoints[2].y});
            const Point2D p3 = dst1;

            const float w0 = (trajectory.controlPoints[0].weight > 0.0f) ? trajectory.controlPoints[0].weight : 1.0f;
            const float w1 = (trajectory.controlPoints[1].weight > 0.0f) ? trajectory.controlPoints[1].weight : 1.0f;
            const float w2 = (trajectory.controlPoints[2].weight > 0.0f) ? trajectory.controlPoints[2].weight : 1.0f;
            const float w3 = (trajectory.controlPoints[3].weight > 0.0f) ? trajectory.controlPoints[3].weight : 1.0f;

            const float u = 1.0f - t;
            const float b0 = u * u * u;
            const float b1 = 3.0f * u * u * t;
            const float b2 = 3.0f * u * t * t;
            const float b3 = t * t * t;

            const float denom = w0 * b0 + w1 * b1 + w2 * b2 + w3 * b3;
            if (std::abs(denom) > 1e-9f) {
                x = (w0 * b0 * p0.x + w1 * b1 * p1.x + w2 * b2 * p2.x + w3 * b3 * p3.x) / denom;
                y = (w0 * b0 * p0.y + w1 * b1 * p1.y + w2 * b2 * p2.y + w3 * b3 * p3.y) / denom;
            } else {
                x = startPos.x + (endPos.x - startPos.x) * t;
                y = startPos.y + (endPos.y - startPos.y) * t;
            }
        } else if (trajectory.type == TrajectoryType::CUBIC_BEZIER && trajectory.controlPoints.size() >= 2U) {
            const Point2D p0{startPos.x, startPos.y}; // p0
            const Point2D p1{trajectory.controlPoints[0].x, trajectory.controlPoints[0].y}; // p1
            const Point2D p2{trajectory.controlPoints[1].x, trajectory.controlPoints[1].y}; // p2
            const Point2D p3{endPos.x, endPos.y}; // p3
            const float u = 1.0f - t; // 反向
            x = u * u * u * p0.x + 3.0f * u * u * t * p1.x + 3.0f * u * t * t * p2.x + t * t * t * p3.x; // x
            y = u * u * u * p0.y + 3.0f * u * u * t * p1.y + 3.0f * u * t * t * p2.y + t * t * t * p3.y; // y
        } else if (trajectory.type == TrajectoryType::INFPNURBS && !trajectory.controlPoints.empty() && !trajectory.knotVector.empty()) {
            const NurbsEvalResult ev = evaluateNurbsWithTangent(trajectory, static_cast<double>(t)); // 求值
            x = static_cast<float>(ev.x); // x
            y = static_cast<float>(ev.y); // y
            thetaHint = ev.hasExplicitTheta ? static_cast<float>(ev.theta) : static_cast<float>(ev.tangentTheta); // theta
            thetaHintIsTangent = !ev.hasExplicitTheta;
        } else {
            x = startPos.x + (endPos.x - startPos.x) * t; // 降级
            y = startPos.y + (endPos.y - startPos.y) * t; // 降级
        }

        float theta = 0.0f; // theta
        if (!std::isnan(thetaHint)) {
            theta = thetaHint; // 使用提示
            if (thetaHintIsTangent) {
                theta = normalizeAngle(theta + kPi);
            }
        } else if (!out.empty()) {
            const PosePoint& last = out.back(); // last
            const float dx = x - last.x; // dx
            const float dy = y - last.y; // dy
            if (hypot2(dx, dy) > 1e-6f) {
                theta = normalizeAngle(std::atan2(dy, dx) + kPi);
            } else {
                theta = last.theta; // 保持
            }
        }

        if (overrideTheta) {
            theta = config.orientation; // 覆盖
        }
        if (config.direction == "BACKWARD" || config.direction == "REVERSE") {
            theta = normalizeAngle(theta + kPi); // 反向
        }

        PosePoint pp{}; // 点
        pp.x = x; // x
        pp.y = y; // y
        pp.theta = normalizeAngle(theta); // theta
        pp.edgeId = ""; // 空
        pp.turnAnchor = false; // 标志
        out.push_back(pp); // 追加
    }
    if (out.size() >= 2U) {
        out.front().theta = out[1].theta;
    }
    return out; // 返回
}

} // namespace simagv::l4
