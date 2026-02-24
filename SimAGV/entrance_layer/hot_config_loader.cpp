#include "hot_config_loader.hpp"

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>
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

bool tryParseDoubleFull(const std::string& text, double& outValue)
{
    try {
        size_t parsedLen = 0;
        const double v = std::stod(text, &parsedLen);
        if (parsedLen != text.size()) {
            return false;
        }
        outValue = v;
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

std::string toLower(std::string s)
{
    for (char& ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

bool startsWith(std::string_view s, std::string_view prefix)
{
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

simagv::json::Value parseYamlScalarValue(const std::string& raw)
{
    const std::string v = trim(raw);
    const std::string vLower = toLower(v);
    if (vLower == "null") {
        return simagv::json::Value{nullptr};
    }
    if (vLower == "true") {
        return simagv::json::Value{true};
    }
    if (vLower == "false") {
        return simagv::json::Value{false};
    }

    double numberValue = 0.0;
    if (tryParseDoubleFull(v, numberValue)) {
        return simagv::json::Value{numberValue};
    }
    return simagv::json::Value{v};
}

} // namespace

bool tryLoadHotSimConfig(const std::string& filePath, simagv::json::Object& outSimConfig, std::string& outError)
{
    outSimConfig.clear();
    outError.clear();

    try {
        const auto kv = parseYamlFlatMap(filePath);
        for (const auto& it : kv) {
            const std::string_view key = it.first;
            if (!startsWith(key, "sim_config.")) {
                continue;
            }
            const std::string childKey = std::string(key.substr(std::string_view("sim_config.").size()));
            outSimConfig.emplace(childKey, parseYamlScalarValue(it.second));
        }
        if (outSimConfig.empty()) {
            outError = "empty_sim_config";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        outError = e.what();
        return false;
    } catch (...) {
        outError = "unknown";
        return false;
    }
}

} // namespace simagv::l1
