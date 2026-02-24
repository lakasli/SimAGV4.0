#pragma once

#include "../common/l3_types.hpp"

namespace simagv::l3 {

/**
 * @brief VDA订单接收分子 - 校验订单并生成可执行任务
 *
 * @param [order] VDA订单
 * @param [vehicleContext] 车辆上下文
 * @param [acceptConfig] 接收配置
 * @return VdaOrderAcceptanceResult 订单接收结果
 */
VdaOrderAcceptanceResult acceptVdaOrder(const VdaOrder& order, const VehicleContext& vehicleContext, const OrderAcceptanceConfig& acceptConfig);

} // namespace simagv::l3

