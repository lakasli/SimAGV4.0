#include "yaml_config_loader.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace simagv::l1 {
namespace {

std::string trim(std::string s)
{
    size_t start = 0;
    while (start < s.size() && static_cast<unsigned char>(s[start]) <= 32U) {
        start += 1;
    }
    size_t end = s.size();
    while (end > start && static_cast<unsigned char>(s[end - 1]) <= 32U) {
        end -= 1;
    }
    return s.substr(start, end - start);
}

std::string stripQuotes(std::string s)
{
    s = trim(std::move(s));
    if (s.size() >= 2U) {
        const char q0 = s.front();
        const char q1 = s.back();
        if ((q0 == '"' && q1 == '"') || (q0 == '\'' && q1 == '\'')) {
            return s.substr(1, s.size() - 2U);
        }
    }
    return s;
}

bool tryParseU16(const std::string& text, uint16_t& outValue)
{
    try {
        const unsigned long v = std::stoul(text);
        if (v > 65535UL) {
            return false;
        }
        outValue = static_cast<uint16_t>(v);
        return true;
    } catch (...) {
        return false;
    }
}

bool tryParseDouble(const std::string& text, double& outValue)
{
    try {
        outValue = std::stod(text);
        return true;
    } catch (...) {
        return false;
    }
}

std::unordered_map<std::string, std::string> parseYamlFlatMap(const std::string& filePath)
{
    std::ifstream ifs(filePath);
    if (!ifs.good()) {
        throw std::runtime_error("failed_to_open_config");
    }

    std::vector<std::pair<int, std::string>> stack;
    std::unordered_map<std::string, std::string> out;

    std::string line;
    while (std::getline(ifs, line)) {
        const size_t hashPos = line.find('#');
        if (hashPos != std::string::npos) {
            line = line.substr(0, hashPos);
        }
        if (trim(line).empty()) {
            continue;
        }

        int indent = 0;
        while (indent < static_cast<int>(line.size()) && line[static_cast<size_t>(indent)] == ' ') {
            indent += 1;
        }

        std::string content = trim(line.substr(static_cast<size_t>(indent)));
        const size_t colonPos = content.find(':');
        if (colonPos == std::string::npos) {
            continue;
        }

        std::string key = trim(content.substr(0, colonPos));
        std::string value = trim(content.substr(colonPos + 1));

        while (!stack.empty() && stack.back().first >= indent) {
            stack.pop_back();
        }

        if (value.empty()) {
            stack.emplace_back(indent, key);
            continue;
        }

        std::string fullKey;
        for (const auto& item : stack) {
            fullKey.append(item.second);
            fullKey.push_back('.');
        }
        fullKey.append(key);
        out.emplace(std::move(fullKey), stripQuotes(std::move(value)));
    }

    return out;
}

std::string readRequiredString(const std::unordered_map<std::string, std::string>& kv, std::string_view key)
{
    const auto it = kv.find(std::string(key));
    if (it == kv.end() || it->second.empty()) {
        throw std::runtime_error("missing_config_field");
    }
    return it->second;
}

double readOptionalDouble(const std::unordered_map<std::string, std::string>& kv, std::string_view key, double defaultValue)
{
    const auto it = kv.find(std::string(key));
    if (it == kv.end() || it->second.empty()) {
        return defaultValue;
    }
    double v = defaultValue;
    if (!tryParseDouble(it->second, v)) {
        return defaultValue;
    }
    return v;
}

bool hasKey(const std::unordered_map<std::string, std::string>& kv, std::string_view key)
{
    return kv.find(std::string(key)) != kv.end();
}

} // namespace

SimInstanceConfig loadSimInstanceConfig(const std::string& filePath)
{
    const auto kv = parseYamlFlatMap(filePath);

    SimInstanceConfig out;
    out.mqttBroker.host = readRequiredString(kv, "mqtt_broker.host");
    {
        const std::string portText = readRequiredString(kv, "mqtt_broker.port");
        uint16_t portValue = 0;
        if (!tryParseU16(portText, portValue)) {
            throw std::runtime_error("invalid_mqtt_port");
        }
        out.mqttBroker.port = portValue;
    }

    out.vehicle.vdaInterface = readRequiredString(kv, "vehicle.vda_interface");
    out.vehicle.vdaVersion = readRequiredString(kv, "vehicle.vda_version");
    out.vehicle.vdaFullVersion = readRequiredString(kv, "vehicle.vda_full_version");
    out.vehicle.manufacturer = readRequiredString(kv, "vehicle.manufacturer");
    out.vehicle.serialNumber = readRequiredString(kv, "vehicle.serial_number");

    out.physicalParameters.speedMin = readOptionalDouble(kv, "factsheet.physicalParameters.speedMin", 0.01);
    out.physicalParameters.speedMax = readOptionalDouble(kv, "factsheet.physicalParameters.speedMax", 2.0);
    out.physicalParameters.accelerationMax = readOptionalDouble(kv, "factsheet.physicalParameters.accelerationMax", 2.0);
    out.physicalParameters.decelerationMax = readOptionalDouble(kv, "factsheet.physicalParameters.decelerationMax", 2.0);
    out.physicalParameters.heightMin = readOptionalDouble(kv, "factsheet.physicalParameters.heightMin", 0.01);
    out.physicalParameters.heightMax = readOptionalDouble(kv, "factsheet.physicalParameters.heightMax", 0.10);
    out.physicalParameters.width = readOptionalDouble(kv, "factsheet.physicalParameters.width", 0.745);
    out.physicalParameters.length = readOptionalDouble(kv, "factsheet.physicalParameters.length", 1.03);

    out.initialPose.hasPose = hasKey(kv, "initial_pose.pose_x") || hasKey(kv, "initial_pose.pose_y") || hasKey(kv, "initial_pose.pose_theta");
    out.initialPose.x = readOptionalDouble(kv, "initial_pose.pose_x", 0.0);
    out.initialPose.y = readOptionalDouble(kv, "initial_pose.pose_y", 0.0);
    out.initialPose.theta = readOptionalDouble(kv, "initial_pose.pose_theta", 0.0);
    return out;
}

} // namespace simagv::l1
