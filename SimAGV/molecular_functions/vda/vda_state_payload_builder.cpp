#include "vda_state_payload_builder.hpp"

namespace simagv::l3 {
namespace {

std::vector<VdaError> buildErrors(const std::vector<ErrorItem>& errors, bool enableCompression)
{
    std::vector<VdaError> output; // 输出错误列表
    output.reserve(errors.size());

    for (const auto& item : errors) {
        VdaError error{}; // 错误项
        error.errorType = item.errorType;
        error.errorLevel = item.errorLevel;
        error.errorCode = item.errorCode;
        error.errorDescription = item.errorDescription;
        output.push_back(error);
    }

    if (enableCompression && (output.size() > 10U)) {
        output.resize(10U);
    }

    return output;
}

} // namespace

VdaStatePayload buildVdaStatePayload(const ComprehensiveStateReport& report, const std::string& vehicleId, const StateBuildOptions& buildOptions)
{
    (void)vehicleId;
    (void)buildOptions.enableActionStateOutput;

    VdaStatePayload payload{}; // state载荷
    payload.protocolVersion = report.protocolVersion;
    payload.manufacturer = "";
    payload.serialNumber = "";
    payload.currentPose = report.currentPose;
    payload.batteryLevel = report.batteryLevel;
    payload.errors = buildErrors(report.errors, buildOptions.enableErrorCompression);
    payload.timestamp = report.timestamp;
    return payload;
}

} // namespace simagv::l3

