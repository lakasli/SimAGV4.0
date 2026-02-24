#pragma once

#include "../common/l3_types.hpp"

namespace simagv::l3 {

/**
 * @brief VDA即时动作接收分子 - 校验动作并输出执行指令
 *
 * @param [instantActions] 即时动作
 * @param [vehicleContext] 车辆上下文
 * @param [acceptConfig] 接收配置
 * @return VdaInstantActionsResult 即时动作处理结果
 */
VdaInstantActionsResult acceptVdaInstantActions(const VdaInstantActions& instantActions, const VehicleContext& vehicleContext, const InstantActionsConfig& acceptConfig);

} // namespace simagv::l3

