#include "vda_connection_payload_builder.hpp"

#include <algorithm>

namespace simagv::l3 {

VdaConnectionPayload buildVdaConnectionPayload(const ConnectionContext& connectionContext, const ConnectionBuildOptions& buildOptions)
{
    VdaConnectionPayload payload{}; // connection载荷
    payload.protocolVersion = connectionContext.protocolVersion;
    payload.manufacturer = connectionContext.manufacturer;
    payload.serialNumber = connectionContext.serialNumber;
    payload.state = connectionContext.state;
    payload.timestamp = connectionContext.timestamp;

    if (buildOptions.enableTimestampNormalize) {
        payload.timestamp = std::max<int64_t>(0, payload.timestamp);
    }

    return payload;
}

} // namespace simagv::l3

