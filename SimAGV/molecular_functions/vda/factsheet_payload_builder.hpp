#pragma once

#include "../common/l3_types.hpp"

namespace simagv::l3 {

/**
 * @brief Factsheet载荷构建分子 - 生成factsheet业务载荷对象
 *
 * @param [factsheetContext] 构建上下文
 * @param [buildOptions] 构建选项
 * @return FactsheetPayload factsheet载荷对象
 */
FactsheetPayload buildFactsheetPayload(const FactsheetContext& factsheetContext, const FactsheetBuildOptions& buildOptions);

} // namespace simagv::l3

