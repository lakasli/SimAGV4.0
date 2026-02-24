#pragma once

#include "../atom_functions/mqtt311_client_atoms.hpp"
#include "../coordination_layer/ports.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace simagv::l1 {

class MqttBrokerDiplomat final : public simagv::l2::IMqttDiplomat {
public:
    MqttBrokerDiplomat(std::string host, uint16_t port, std::string clientId, uint16_t keepAliveSec);
    ~MqttBrokerDiplomat() override;

    MqttBrokerDiplomat(const MqttBrokerDiplomat&) = delete;
    MqttBrokerDiplomat& operator=(const MqttBrokerDiplomat&) = delete;

    void setWill(std::string topic, std::string payload, uint8_t qos, bool retain);
    void setMessageHandler(std::function<void(std::string, std::string)> handler);

    bool connect();
    void disconnect();

    bool subscribe(std::string topicFilter, uint8_t qos);

    void publish(std::string topic, std::string payload, uint8_t qos, bool retain) override;

private:
    std::string host_;
    uint16_t port_;
    std::string clientId_;
    uint16_t keepAliveSec_;
    simagv::l4::MqttWillConfig will_;
    bool hasWill_;
    simagv::l4::Mqtt311Client client_;
};

} // namespace simagv::l1

