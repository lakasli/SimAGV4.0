#pragma once

#include "json_min.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace simagv::l4 {

class SimErrorManager final {
public:
    void setErrorByCode(int32_t code);
    void setErrorCustom(std::string errorType, std::string errorLevel, std::string errorName, std::string errorDescription);

    void clearErrorByCode(int32_t code);
    void clearErrorByName(std::string_view errorName);
    void clearAll();

    void updateBattery(float batteryChargePercent);
    void updateMovementBlocked(bool movementBlocked);

    simagv::json::Array buildVdaErrorsArray(size_t maxCount = 10U) const;

private:
    struct ErrorDefinition final {
        int32_t code;
        const char* errorType;
        const char* errorLevel;
        const char* errorDescription;
    };

    struct ErrorItem final {
        std::string errorType;
        std::string errorLevel;
        std::string errorName;
        std::string errorDescription;
    };

    static const ErrorDefinition* findDefinition(int32_t code);
    static std::string toErrorName(int32_t code);

    void setFromDefinition(const ErrorDefinition& definition);

    std::unordered_map<std::string, ErrorItem> activeErrorsByName_;
    float lowBatteryThreshold_{10.0F};
    float zeroBatteryThreshold_{0.1F};
};

} // namespace simagv::l4

