#pragma once

#include "../common/l3_types.hpp"

namespace simagv::l3 {

/**
 * @brief VDA载荷发布分子 - 发布单一topic载荷
 *
 * @param [topic] 发布目标topic
 * @param [qosLevel] 发布QoS等级
 * @param [payload] connection载荷
 * @return PublishResult 发布结果
 */
PublishResult publishVdaConnectionPayload(const std::string& topic, const QualityOfService& qosLevel, const VdaConnectionPayload& payload);

/**
 * @brief VDA载荷发布分子 - 发布单一topic载荷
 *
 * @param [topic] 发布目标topic
 * @param [qosLevel] 发布QoS等级
 * @param [payload] state载荷
 * @return PublishResult 发布结果
 */
PublishResult publishVdaStatePayload(const std::string& topic, const QualityOfService& qosLevel, const VdaStatePayload& payload);

/**
 * @brief VDA载荷发布分子 - 发布单一topic载荷
 *
 * @param [topic] 发布目标topic
 * @param [qosLevel] 发布QoS等级
 * @param [payload] visualization载荷
 * @return PublishResult 发布结果
 */
PublishResult publishVdaVisualizationPayload(const std::string& topic, const QualityOfService& qosLevel, const VdaStatePayload& payload);

/**
 * @brief VDA载荷发布分子 - 发布单一topic载荷
 *
 * @param [topic] 发布目标topic
 * @param [qosLevel] 发布QoS等级
 * @param [payload] factsheet载荷
 * @return PublishResult 发布结果
 */
PublishResult publishFactsheetPayload(const std::string& topic, const QualityOfService& qosLevel, const FactsheetPayload& payload);

} // namespace simagv::l3

