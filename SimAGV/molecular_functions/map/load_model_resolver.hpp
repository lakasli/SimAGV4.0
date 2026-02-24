#pragma once

#include "../common/l3_types.hpp"

namespace simagv::l3 {

/**
 * @brief 载荷模型解析分子 - 组合托盘/货架模型并解析动作参数
 *
 * @param [recognition] 识别配置
 * @param [vehicle] 车辆上下文
 * @param [action] 动作上下文
 * @return LoadResolution 载荷解析结果
 */
LoadResolution resolveLoadModel(const LoadRecognition& recognition, const VehicleContext& vehicle, const ActionContext& action);

} // namespace simagv::l3

