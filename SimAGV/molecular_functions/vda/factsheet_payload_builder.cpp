#include "factsheet_payload_builder.hpp"

namespace simagv::l3 {

FactsheetPayload buildFactsheetPayload(const FactsheetContext& factsheetContext, const FactsheetBuildOptions& buildOptions)
{
    (void)buildOptions;
    FactsheetPayload payload{}; // factsheet载荷
    payload.protocolVersion = factsheetContext.protocolVersion;
    payload.manufacturer = factsheetContext.manufacturer;
    payload.serialNumber = factsheetContext.serialNumber;
    payload.vehicleType = factsheetContext.vehicleType;
    payload.dimensions = factsheetContext.dimensions;
    payload.supportedActions = factsheetContext.supportedActions;
    return payload;
}

} // namespace simagv::l3

