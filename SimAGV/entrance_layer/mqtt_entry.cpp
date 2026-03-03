#include "mqtt_entry.hpp"

#include "../atom_functions/console_log_atoms.hpp"

#include <sstream>
#include <stdexcept>
#include <utility>

namespace simagv::l1 {

MqttEntry::MqttEntry(simagv::l2::ICommandSubmitter& submitter) : submitter_(submitter), lastInstantHeaderId_(0)
{
}

EntryAck MqttEntry::handleMessage(const std::string& topic, const std::string& payload)
{
    const uint64_t nowMsValue = simagv::l2::nowMs();

    simagv::l2::CommandIntent intent;
    try {
        intent = buildIntent(topic, payload, nowMsValue);
    } catch (const std::exception& e) {
        std::ostringstream oss;
        oss << "mqtt_rx_reject topic=" << topic << " payloadBytes=" << payload.size() << " err=" << e.what();
        simagv::l4::logWarn(oss.str());
        return EntryAck{false, e.what(), 0, simagv::l2::topicTypeFromTopic(topic)};
    } catch (...) {
        std::ostringstream oss;
        oss << "mqtt_rx_reject topic=" << topic << " payloadBytes=" << payload.size() << " err=invalid_message";
        simagv::l4::logWarn(oss.str());
        return EntryAck{false, "invalid_message", 0, simagv::l2::topicTypeFromTopic(topic)};
    }

    try {
        if (intent.payload.isObject()) {
            const simagv::json::Object obj = intent.payload.asObject();
            const std::string timestamp = simagv::l2::readStringOr(obj, "timestamp", "timestamp", "");
            if (intent.topicType == simagv::l2::TopicType::Order) {
                const std::string orderId = simagv::l2::readStringOr(obj, "order_id", "orderId", "");
                const uint64_t orderUpdateId = simagv::l2::readUintOr(obj, "order_update_id", "orderUpdateId", 0U);
                std::ostringstream oss;
                oss << "mqtt_rx type=order topic=" << intent.topic << " headerId=" << intent.headerId << " timestamp=" << timestamp
                    << " orderId=" << orderId << " orderUpdateId=" << orderUpdateId << " mapId=" << intent.mapId
                    << " payloadBytes=" << payload.size();
                simagv::l4::logInfo(oss.str());
            } else if (intent.topicType == simagv::l2::TopicType::InstantActions) {
                size_t actionsCount = 0U;
                const auto itActions = obj.find("actions");
                if (itActions != obj.end() && itActions->second.isArray()) {
                    actionsCount = itActions->second.asArray().size();
                }
                std::ostringstream oss;
                oss << "mqtt_rx type=instantActions topic=" << intent.topic << " headerId=" << intent.headerId << " timestamp=" << timestamp
                    << " actions=" << actionsCount << " mapId=" << intent.mapId << " payloadBytes=" << payload.size();
                simagv::l4::logInfo(oss.str());
            } else if (intent.topicType == simagv::l2::TopicType::SimConfig) {
                std::ostringstream oss;
                oss << "mqtt_rx type=simConfig topic=" << intent.topic << " headerId=" << intent.headerId << " timestamp=" << timestamp
                    << " mapId=" << intent.mapId << " payloadBytes=" << payload.size();
                simagv::l4::logInfo(oss.str());
            }
        }
    } catch (...) {
    }

    if (intent.topicType == simagv::l2::TopicType::InstantActions) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (intent.headerId != 0 && intent.headerId == lastInstantHeaderId_) {
            std::ostringstream oss;
            oss << "mqtt_rx_deduped type=instantActions headerId=" << intent.headerId << " topic=" << intent.topic;
            simagv::l4::logInfo(oss.str());
            return EntryAck{true, "deduped", intent.headerId, intent.topicType};
        }
        lastInstantHeaderId_ = intent.headerId;
    }

    try {
        submitter_.submitCommand(std::move(intent));
    } catch (...) {
        std::ostringstream oss;
        oss << "mqtt_rx_submit_failed topic=" << topic << " payloadBytes=" << payload.size();
        simagv::l4::logWarn(oss.str());
        return EntryAck{false, "submit_failed", 0, simagv::l2::topicTypeFromTopic(topic)};
    }

    return EntryAck{true, "accepted", intent.headerId, intent.topicType};
}

simagv::l2::CommandIntent MqttEntry::buildIntent(const std::string& topic, const std::string& payload, uint64_t nowMsValue)
{
    const simagv::l2::TopicType topicType = simagv::l2::topicTypeFromTopic(topic);
    if (topicType == simagv::l2::TopicType::Unknown) {
        throw std::runtime_error("unknown_topic");
    }

    simagv::json::Value parsed = simagv::json::parse(payload);
    parsed = simagv::l2::convertKeysToCamel(parsed);
    if (!parsed.isObject()) {
        throw std::runtime_error("payload_not_object");
    }

    const simagv::json::Object obj = parsed.asObject();
    
    // 检查Order和InstantActions类型是否必须有headerId字段且为int类型
    if (topicType == simagv::l2::TopicType::Order || topicType == simagv::l2::TopicType::InstantActions) {
        const auto* headerIdValue = simagv::l2::tryGetSnakeOrCamel(obj, "header_id", "headerId");
        if (headerIdValue == nullptr) {
            throw std::runtime_error("missing_header_id");
        }
        if (!headerIdValue->isNumber()) {
            throw std::runtime_error("invalid_header_id_type");
        }
    }
    
    uint64_t headerId = simagv::l2::readUintOr(obj, "header_id", "headerId", 0);
    const uint64_t serial = simagv::l2::readUintOr(obj, "serial", "serial", 0);

    std::string mapId = simagv::l2::readStringOr(obj, "map_id", "mapId", "");
    mapId = simagv::l2::canonicalizeMapId(mapId);

    simagv::l2::CommandIntent intent;
    intent.topicType = topicType;
    intent.topic = topic;
    intent.payload = std::move(parsed);
    intent.receiveTimestampMs = nowMsValue;
    intent.serial = serial;
    intent.headerId = headerId;
    intent.mapId = std::move(mapId);
    return intent;
}

} // namespace simagv::l1
