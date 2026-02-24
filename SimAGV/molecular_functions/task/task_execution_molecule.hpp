#pragma once

#include "../common/l3_types.hpp"

namespace simagv::l3 {

/**
 * @brief 完整任务执行分子 - 管理AGV任务完整生命周期
 *
 * @param [taskDefinition] 任务定义
 * @param [vehicleContext] 车辆上下文
 * @param [executionConfig] 执行配置
 * @return CompleteTaskResult 任务执行结果
 */
CompleteTaskResult executeCompleteTask(const TaskDefinition& taskDefinition, const VehicleContext& vehicleContext, const ExecutionConfig& executionConfig);

/**
 * @brief 运输任务执行分子 - 专门处理货物搬运任务
 *
 * @param [transportTask] 运输任务
 * @param [loadSpecification] 装载规格
 * @param [transportConfig] 运输配置
 * @return TransportTaskResult 运输任务结果
 */
TransportTaskResult executeTransportTask(const TransportTaskDefinition& transportTask, const LoadSpecification& loadSpecification, const TransportConfig& transportConfig);

} // namespace simagv::l3

