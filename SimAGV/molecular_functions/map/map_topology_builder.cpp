#include "map_topology_builder.hpp"

#include <cmath>

namespace simagv::l3 {
namespace {

bool nearlyEqual(float value1, float value2, float eps)
{
    return std::fabs(value1 - value2) <= eps;
}

} // namespace

MapTopology buildMapTopology(const SceneData& sceneData, const TopologyOptions& options)
{
    MapTopology topology{}; // 拓扑结果

    for (const auto& item : sceneData.stationCatalog) {
        Station station{}; // 站点
        station.id = item.id;
        station.name = item.instanceName.empty() ? item.pointName : item.instanceName;
        station.position = item.pos;
        station.type = item.type;

        topology.stationById[station.id] = station;
        topology.stations.push_back(station);
    }

    for (const auto& path : sceneData.paths) {
        PathEdge edge{}; // 边
        edge.id = path.id;
        edge.from = path.from;
        edge.to = path.to;
        edge.length = path.length;

        topology.edges.push_back(edge);
        topology.edgesByStart[edge.from].push_back(edge);
    }

    if (options.positionToleranceM > 0.0F) {
        for (const auto& stationNode : sceneData.stations) {
            for (auto& station : topology.stations) {
                if (!station.name.empty()) {
                    continue;
                }
                if (nearlyEqual(station.position.x, stationNode.x, options.positionToleranceM) &&
                    nearlyEqual(station.position.y, stationNode.y, options.positionToleranceM)) {
                    station.name = stationNode.id;
                }
            }
        }
    }

    return topology;
}

} // namespace simagv::l3

