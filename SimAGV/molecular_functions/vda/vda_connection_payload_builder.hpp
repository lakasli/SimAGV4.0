#pragma once

#include "../common/l3_types.hpp"

namespace simagv::l3 {

/**
 * @brief VDA连接载荷构建分子 - 生成connection业务载荷对象
 *
 * @param [connectionContext] 连接上下文
 * @param [buildOptions] 构建选项
 * @return VdaConnectionPayload connection载荷对象
 */
VdaConnectionPayload buildVdaConnectionPayload(const ConnectionContext& connectionContext, const ConnectionBuildOptions& buildOptions);

} // namespace simagv::l3

