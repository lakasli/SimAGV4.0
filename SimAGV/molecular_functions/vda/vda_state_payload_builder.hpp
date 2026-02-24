#pragma once

#include "../common/l3_types.hpp"

namespace simagv::l3 {

/**
 * @brief VDA状态载荷构建分子 - 生成state业务载荷对象
 *
 * @param [report] 状态报告
 * @param [vehicleId] 车辆ID
 * @param [buildOptions] 构建选项
 * @return VdaStatePayload state载荷对象
 */
VdaStatePayload buildVdaStatePayload(const ComprehensiveStateReport& report, const std::string& vehicleId, const StateBuildOptions& buildOptions);

} // namespace simagv::l3

