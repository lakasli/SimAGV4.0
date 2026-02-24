#include "mqtt_broker_diplomat.hpp"

namespace simagv::l1 {

MqttBrokerDiplomat::MqttBrokerDiplomat(std::string host, uint16_t port, std::string clientId, uint16_t keepAliveSec)
    : host_(std::move(host)),
      port_(port),
      clientId_(std::move(clientId)),
      keepAliveSec_(keepAliveSec),
      will_(),
      hasWill_(false),
      client_()
{
}

MqttBrokerDiplomat::~MqttBrokerDiplomat()
{
    disconnect();
}

void MqttBrokerDiplomat::setWill(std::string topic, std::string payload, uint8_t qos, bool retain)
{
    will_.topic = std::move(topic);
    will_.payload = std::move(payload);
    will_.qos = qos;
    will_.retain = retain;
    hasWill_ = !will_.topic.empty();
}

void MqttBrokerDiplomat::setMessageHandler(std::function<void(std::string, std::string)> handler)
{
    client_.setMessageHandler(std::move(handler));
}

bool MqttBrokerDiplomat::connect()
{
    const simagv::l4::MqttWillConfig* p_Will = hasWill_ ? &will_ : nullptr;
    return client_.connect(host_, port_, clientId_, keepAliveSec_, p_Will);
}

void MqttBrokerDiplomat::disconnect()
{
    client_.disconnect();
}

bool MqttBrokerDiplomat::subscribe(std::string topicFilter, uint8_t qos)
{
    return client_.subscribe(std::move(topicFilter), qos);
}

void MqttBrokerDiplomat::publish(std::string topic, std::string payload, uint8_t qos, bool retain)
{
    (void)qos;
    (void)retain;
    if (!client_.isConnected()) {
        (void)connect();
    }
    (void)client_.publish(std::move(topic), std::move(payload), qos, retain);
}

} // namespace simagv::l1

