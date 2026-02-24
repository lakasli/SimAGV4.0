#include "vda_payload_publisher_molecule.hpp"

#include <stdexcept>

namespace simagv::l3 {
namespace {

void validateTopic(const std::string& topic)
{
    if (topic.empty()) {
        throw std::invalid_argument("topic is empty");
    }
}

template <typename Payload>
PublishResult publishNoop(const std::string& topic, const QualityOfService& qosLevel, const Payload& payload)
{
    (void)payload;
    validateTopic(topic);

    PublishResult result{}; // 发布结果
    result.success = true;
    result.retryCount = 0U;
    result.errorCode = "";
    result.errorMessage = "";
    (void)qosLevel;
    return result;
}

} // namespace

PublishResult publishVdaConnectionPayload(const std::string& topic, const QualityOfService& qosLevel, const VdaConnectionPayload& payload)
{
    return publishNoop(topic, qosLevel, payload);
}

PublishResult publishVdaStatePayload(const std::string& topic, const QualityOfService& qosLevel, const VdaStatePayload& payload)
{
    return publishNoop(topic, qosLevel, payload);
}

PublishResult publishVdaVisualizationPayload(const std::string& topic, const QualityOfService& qosLevel, const VdaStatePayload& payload)
{
    return publishNoop(topic, qosLevel, payload);
}

PublishResult publishFactsheetPayload(const std::string& topic, const QualityOfService& qosLevel, const FactsheetPayload& payload)
{
    return publishNoop(topic, qosLevel, payload);
}

} // namespace simagv::l3

