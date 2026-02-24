#pragma once

#include "../atom_functions/json_min.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace simagv::l2 {

inline uint64_t nowMs() {
    const auto nowTp = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(nowTp.time_since_epoch()).count();
    return (ms < 0) ? 0U : static_cast<uint64_t>(ms);
}

inline std::string canonicalizeMapId(std::string_view mapId) {
    std::string s(mapId);
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), s.end());
    if (s.empty()) {
        return "default";
    }
    std::replace(s.begin(), s.end(), '\\', '/');
    const size_t slashPos = s.rfind('/');
    std::string fname = (slashPos == std::string::npos) ? s : s.substr(slashPos + 1);
    const std::string sceneExt = ".scene";
    if (fname.size() >= sceneExt.size()) {
        const std::string suffix = fname.substr(fname.size() - sceneExt.size());
        std::string lowerSuffix = suffix;
        std::transform(lowerSuffix.begin(), lowerSuffix.end(), lowerSuffix.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lowerSuffix == sceneExt) {
            fname.resize(fname.size() - sceneExt.size());
        }
    }
    if (fname.empty()) {
        return "default";
    }
    return fname;
}

inline std::string snakeToCamelKey(std::string_view key) {
    std::string out;
    out.reserve(key.size());
    bool upperNext = false;
    for (char ch : key) {
        if (ch == '_') {
            upperNext = true;
            continue;
        }
        if (upperNext) {
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
            upperNext = false;
            continue;
        }
        out.push_back(ch);
    }
    return out;
}

inline simagv::json::Value convertKeysToCamel(const simagv::json::Value& inValue) {
    if (inValue.isObject()) {
        simagv::json::Object outObj;
        for (const auto& kv : inValue.asObject()) {
            outObj.emplace(snakeToCamelKey(kv.first), convertKeysToCamel(kv.second));
        }
        return simagv::json::Value{std::move(outObj)};
    }
    if (inValue.isArray()) {
        simagv::json::Array outArr;
        outArr.reserve(inValue.asArray().size());
        for (const auto& item : inValue.asArray()) {
            outArr.emplace_back(convertKeysToCamel(item));
        }
        return simagv::json::Value{std::move(outArr)};
    }
    return inValue;
}

inline const simagv::json::Value* tryGetObjValue(const simagv::json::Object& obj, std::string_view key) {
    const auto it = obj.find(std::string(key));
    if (it == obj.end()) {
        return nullptr;
    }
    return &it->second;
}

inline const simagv::json::Value* tryGetSnakeOrCamel(const simagv::json::Object& obj, std::string_view snakeKey, std::string_view camelKey) {
    if (const auto* v = tryGetObjValue(obj, snakeKey)) {
        return v;
    }
    return tryGetObjValue(obj, camelKey);
}

inline uint64_t readUintOr(const simagv::json::Object& obj, std::string_view snakeKey, std::string_view camelKey, uint64_t defaultValue) {
    const auto* v = tryGetSnakeOrCamel(obj, snakeKey, camelKey);
    if (v == nullptr) {
        return defaultValue;
    }
    if (v->isNumber()) {
        const double raw = v->asNumber();
        if (raw < 0.0) {
            return defaultValue;
        }
        return static_cast<uint64_t>(raw);
    }
    if (v->isString()) {
        try {
            return static_cast<uint64_t>(std::stoull(v->asString()));
        } catch (...) {
            return defaultValue;
        }
    }
    return defaultValue;
}

inline std::string readStringOr(const simagv::json::Object& obj, std::string_view snakeKey, std::string_view camelKey, std::string_view defaultValue) {
    const auto* v = tryGetSnakeOrCamel(obj, snakeKey, camelKey);
    if (v == nullptr) {
        return std::string(defaultValue);
    }
    if (v->isString()) {
        return v->asString();
    }
    if (v->isNumber()) {
        std::ostringstream oss;
        oss << v->asNumber();
        return oss.str();
    }
    return std::string(defaultValue);
}

inline float readFloatOr(const simagv::json::Object& obj, std::string_view snakeKey, std::string_view camelKey, float defaultValue) {
    const auto* v = tryGetSnakeOrCamel(obj, snakeKey, camelKey);
    if (v == nullptr) {
        return defaultValue;
    }
    if (v->isNumber()) {
        return static_cast<float>(v->asNumber());
    }
    if (v->isString()) {
        try {
            return std::stof(v->asString());
        } catch (...) {
            return defaultValue;
        }
    }
    return defaultValue;
}

inline bool readBoolOr(const simagv::json::Object& obj, std::string_view snakeKey, std::string_view camelKey, bool defaultValue) {
    const auto* v = tryGetSnakeOrCamel(obj, snakeKey, camelKey);
    if (v == nullptr) {
        return defaultValue;
    }
    if (v->isBool()) {
        return v->asBool();
    }
    if (v->isNumber()) {
        return std::abs(v->asNumber()) > 1e-12;
    }
    if (v->isString()) {
        std::string s = v->asString();
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (s == "true" || s == "1" || s == "yes" || s == "y" || s == "on") {
            return true;
        }
        if (s == "false" || s == "0" || s == "no" || s == "n" || s == "off") {
            return false;
        }
        return defaultValue;
    }
    return defaultValue;
}

inline simagv::json::Object asObjectOrThrow(const simagv::json::Value& value) {
    if (!value.isObject()) {
        throw std::runtime_error("expected object");
    }
    return value.asObject();
}

inline std::string jsonStringEscape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char ch : s) {
        if (ch == '"') {
            out += "\\\"";
            continue;
        }
        if (ch == '\\') {
            out += "\\\\";
            continue;
        }
        if (ch == '\n') {
            out += "\\n";
            continue;
        }
        if (ch == '\r') {
            out += "\\r";
            continue;
        }
        if (ch == '\t') {
            out += "\\t";
            continue;
        }
        out.push_back(ch);
    }
    return out;
}

inline void writeJsonValue(std::ostringstream& oss, const simagv::json::Value& v);

inline void writeJsonObject(std::ostringstream& oss, const simagv::json::Object& obj) {
    oss << '{';
    bool first = true;
    for (const auto& kv : obj) {
        if (!first) {
            oss << ',';
        }
        first = false;
        oss << '"' << jsonStringEscape(kv.first) << "\":";
        writeJsonValue(oss, kv.second);
    }
    oss << '}';
}

inline void writeJsonArray(std::ostringstream& oss, const simagv::json::Array& arr) {
    oss << '[';
    bool first = true;
    for (const auto& item : arr) {
        if (!first) {
            oss << ',';
        }
        first = false;
        writeJsonValue(oss, item);
    }
    oss << ']';
}

inline void writeJsonValue(std::ostringstream& oss, const simagv::json::Value& v) {
    if (v.isNull()) {
        oss << "null";
        return;
    }
    if (v.isBool()) {
        oss << (v.asBool() ? "true" : "false");
        return;
    }
    if (v.isNumber()) {
        oss << v.asNumber();
        return;
    }
    if (v.isString()) {
        oss << '"' << jsonStringEscape(v.asString()) << '"';
        return;
    }
    if (v.isArray()) {
        writeJsonArray(oss, v.asArray());
        return;
    }
    if (v.isObject()) {
        writeJsonObject(oss, v.asObject());
        return;
    }
    oss << "null";
}

inline std::string toJsonString(const simagv::json::Value& v) {
    std::ostringstream oss;
    writeJsonValue(oss, v);
    return oss.str();
}

inline const std::vector<std::string_view>* tryGetStateKeyOrder(std::string_view objectName) {
    static const std::vector<std::string_view> stateOrder{
        "headerId",
        "timestamp",
        "version",
        "manufacturer",
        "serialNumber",
        "driving",
        "operatingMode",
        "nodeStates",
        "edgeStates",
        "lastNodeId",
        "orderId",
        "orderUpdateId",
        "lastNodeSequenceId",
        "actionStates",
        "information",
        "loads",
        "batteryState",
        "safetyState",
        "paused",
        "newBaseRequest",
        "agvPosition",
        "velocity",
        "zoneSetId",
        "waitingForInteractionZoneRelease",
        "forkState",
        "errors"};
    static const std::vector<std::string_view> batteryStateOrder{"batteryCharge", "batteryVoltage", "batteryHealth", "charging", "reach"};
    static const std::vector<std::string_view> safetyStateOrder{"eStop", "fieldViolation"};
    static const std::vector<std::string_view> agvPositionOrder{
        "x",
        "y",
        "theta",
        "mapId",
        "mapDescription",
        "positionInitialized",
        "localizationScore",
        "deviationRange"};
    static const std::vector<std::string_view> velocityOrder{"vx", "vy", "omega"};
    static const std::vector<std::string_view> forkStateOrder{"forkHeight"};
    static const std::vector<std::string_view> actionStateItemOrder{
        "actionId",
        "actionStatus",
        "actionType",
        "actionDescription",
        "resultDescription"};

    if (objectName == "state") {
        return &stateOrder;
    }
    if (objectName == "batteryState") {
        return &batteryStateOrder;
    }
    if (objectName == "safetyState") {
        return &safetyStateOrder;
    }
    if (objectName == "agvPosition") {
        return &agvPositionOrder;
    }
    if (objectName == "velocity") {
        return &velocityOrder;
    }
    if (objectName == "forkState") {
        return &forkStateOrder;
    }
    if (objectName == "actionStateItem") {
        return &actionStateItemOrder;
    }
    return nullptr;
}

inline void writeJsonValueStateOrdered(std::ostringstream& oss, const simagv::json::Value& v, std::string_view contextName);

inline void writeJsonObjectStateOrdered(std::ostringstream& oss, const simagv::json::Object& obj, std::string_view objectName) {
    const auto* order = tryGetStateKeyOrder(objectName);
    if (order == nullptr) {
        writeJsonObject(oss, obj);
        return;
    }

    oss << '{';
    bool first = true;
    std::unordered_set<std::string> usedKeys;
    usedKeys.reserve(order->size());

    for (const std::string_view key : *order) {
        const auto it = obj.find(std::string(key));
        if (it == obj.end()) {
            continue;
        }
        if (!first) {
            oss << ',';
        }
        first = false;
        oss << '"' << jsonStringEscape(key) << "\":";
        writeJsonValueStateOrdered(oss, it->second, key);
        usedKeys.emplace(key);
    }

    for (const auto& kv : obj) {
        if (usedKeys.find(kv.first) != usedKeys.end()) {
            continue;
        }
        if (!first) {
            oss << ',';
        }
        first = false;
        oss << '"' << jsonStringEscape(kv.first) << "\":";
        writeJsonValueStateOrdered(oss, kv.second, "");
    }

    oss << '}';
}

inline void writeJsonArrayStateOrdered(std::ostringstream& oss, const simagv::json::Array& arr, std::string_view arrayName) {
    oss << '[';
    bool first = true;
    for (const auto& item : arr) {
        if (!first) {
            oss << ',';
        }
        first = false;
        const std::string_view itemContext = (arrayName == "actionStates" && item.isObject()) ? "actionStateItem" : "";
        writeJsonValueStateOrdered(oss, item, itemContext);
    }
    oss << ']';
}

inline void writeJsonValueStateOrdered(std::ostringstream& oss, const simagv::json::Value& v, std::string_view contextName) {
    if (v.isNull()) {
        oss << "null";
        return;
    }
    if (v.isBool()) {
        oss << (v.asBool() ? "true" : "false");
        return;
    }
    if (v.isNumber()) {
        oss << v.asNumber();
        return;
    }
    if (v.isString()) {
        oss << '"' << jsonStringEscape(v.asString()) << '"';
        return;
    }
    if (v.isArray()) {
        writeJsonArrayStateOrdered(oss, v.asArray(), contextName);
        return;
    }
    if (v.isObject()) {
        writeJsonObjectStateOrdered(oss, v.asObject(), contextName);
        return;
    }
    oss << "null";
}

inline std::string toStateJsonStringOrdered(const simagv::json::Object& obj) {
    std::ostringstream oss;
    writeJsonObjectStateOrdered(oss, obj, "state");
    return oss.str();
}

inline std::string generateVdaMqttBaseTopic(std::string_view vdaInterface, std::string_view vdaVersion, std::string_view manufacturer, std::string_view serialNumber) {
    std::string out;
    out.reserve(vdaInterface.size() + vdaVersion.size() + manufacturer.size() + serialNumber.size() + 3);
    out.append(vdaInterface);
    out.push_back('/');
    out.append(vdaVersion);
    out.push_back('/');
    out.append(manufacturer);
    out.push_back('/');
    out.append(serialNumber);
    return out;
}

inline float clampMin(float value, float minValue) {
    return (value < minValue) ? minValue : value;
}

inline float clampMax(float value, float maxValue) {
    return (value > maxValue) ? maxValue : value;
}

inline float clampRange(float value, float minValue, float maxValue) {
    return clampMax(clampMin(value, minValue), maxValue);
}

} // namespace simagv::l2
