#include "http_entry.hpp"

#include <stdexcept>
#include <utility>

namespace simagv::l1 {

namespace {

std::string controlTypeFromPath(const std::string& path)
{
    if (path.find("applyConfig") != std::string::npos) {
        return "applyConfig";
    }
    if (path.find("power") != std::string::npos) {
        return "power";
    }
    if (path.find("delete") != std::string::npos) {
        return "delete";
    }
    return "unknown";
}

} // namespace

HttpEntry::HttpEntry(simagv::l2::ICommandSubmitter& submitter) : submitter_(submitter)
{
}

EntryAck HttpEntry::handleControlRequest(const std::string& path, const std::string& jsonBody)
{
    const uint64_t nowMsValue = simagv::l2::nowMs();
    simagv::l2::CommandIntent intent;
    try {
        simagv::json::Value parsed = simagv::json::parse(jsonBody);
        parsed = simagv::l2::convertKeysToCamel(parsed);
        if (!parsed.isObject()) {
            throw std::runtime_error("payload_not_object");
        }
        simagv::json::Object obj = parsed.asObject();
        obj.emplace("controlType", simagv::json::Value{controlTypeFromPath(path)});

        intent.topicType = simagv::l2::TopicType::SimConfig;
        intent.topic = "http/control";
        intent.payload = simagv::json::Value{std::move(obj)};
        intent.receiveTimestampMs = nowMsValue;
        intent.serial = 0;
        intent.headerId = 0;
        intent.mapId = simagv::l2::canonicalizeMapId(simagv::l2::readStringOr(intent.payload.asObject(), "map_id", "mapId", ""));
    } catch (const std::exception& e) {
        return EntryAck{false, e.what(), 0, simagv::l2::TopicType::SimConfig};
    } catch (...) {
        return EntryAck{false, "invalid_request", 0, simagv::l2::TopicType::SimConfig};
    }

    try {
        submitter_.submitCommand(std::move(intent));
    } catch (...) {
        return EntryAck{false, "submit_failed", 0, simagv::l2::TopicType::SimConfig};
    }
    return EntryAck{true, "accepted", 0, simagv::l2::TopicType::SimConfig};
}

} // namespace simagv::l1

