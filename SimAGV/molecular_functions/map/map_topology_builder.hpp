#pragma once

#include "../common/l3_types.hpp"

namespace simagv::l3 {

/**
 * @brief 地图拓扑构建分子
 *
 * @param [sceneData] 场景数据
 * @param [options] 构建选项
 * @return MapTopology 地图拓扑结构
 */
MapTopology buildMapTopology(const SceneData& sceneData, const TopologyOptions& options);

} // namespace simagv::l3

