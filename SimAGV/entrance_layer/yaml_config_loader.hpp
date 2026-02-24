#pragma once

#include <cstdint>
#include <string>

namespace simagv::l1 {

struct MqttBrokerConfig {
    std::string host;
    uint16_t port;
};

struct VehicleIdentityConfig {
    std::string vdaInterface;
    std::string vdaVersion;
    std::string vdaFullVersion;
    std::string manufacturer;
    std::string serialNumber;
};

struct FactsheetPhysicalParametersConfig {
    double speedMin;
    double speedMax;
    double accelerationMax;
    double decelerationMax;
    double heightMin;
    double heightMax;
    double width;
    double length;
};

struct InitialPoseConfig {
    bool hasPose;
    double x;
    double y;
    double theta;
};

struct SimInstanceConfig {
    MqttBrokerConfig mqttBroker;
    VehicleIdentityConfig vehicle;
    FactsheetPhysicalParametersConfig physicalParameters;
    InitialPoseConfig initialPose;
};

SimInstanceConfig loadSimInstanceConfig(const std::string& filePath);

} // namespace simagv::l1
