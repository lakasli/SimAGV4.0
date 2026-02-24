#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace simagv::l4 {

struct MqttWillConfig {
    std::string topic;
    std::string payload;
    uint8_t qos;
    bool retain;
};

class Mqtt311Client final {
public:
    Mqtt311Client();
    ~Mqtt311Client();

    Mqtt311Client(const Mqtt311Client&) = delete;
    Mqtt311Client& operator=(const Mqtt311Client&) = delete;

    void setMessageHandler(std::function<void(std::string, std::string)> handler);

    bool connect(std::string host, uint16_t port, std::string clientId, uint16_t keepAliveSec, const MqttWillConfig* willConfig);
    void disconnect();

    bool isConnected() const;

    bool publish(std::string topic, std::string payload, uint8_t qos, bool retain);
    bool subscribe(std::string topicFilter, uint8_t qos);

private:
    struct ConnectionInfo {
        std::string host;
        uint16_t port;
        std::string clientId;
        uint16_t keepAliveSec;
        bool hasWill;
        MqttWillConfig will;
    };

    mutable std::mutex mutex_;
    int sockFd_;
    std::atomic<bool> connected_;
    std::atomic<bool> stopRx_;
    std::thread rxThread_;
    uint16_t nextPacketId_;
    std::function<void(std::string, std::string)> onMessage_;
    ConnectionInfo lastConn_;

    void rxLoop();

    static bool sendPacket(int sockFd, const std::vector<uint8_t>& packet);
    static bool readExact(int sockFd, uint8_t* buf, size_t len);
    static bool readPacket(int sockFd, uint8_t& header, std::vector<uint8_t>& body);

    static std::vector<uint8_t> buildConnectPacket(const ConnectionInfo& connInfo);
    static std::vector<uint8_t> buildPublishPacket(const std::string& topic, const std::string& payload, uint8_t qos, bool retain);
    static std::vector<uint8_t> buildSubscribePacket(uint16_t packetId, const std::string& topicFilter, uint8_t qos);
    static std::vector<uint8_t> buildPingReqPacket();
    static void encodeUtf8String(std::vector<uint8_t>& out, const std::string& text);
    static void encodeRemainingLength(std::vector<uint8_t>& out, size_t remainingLength);
    static bool decodeRemainingLength(const std::vector<uint8_t>& in, size_t& offset, size_t& outLength);

    static bool openTcpSocket(const std::string& host, uint16_t port, int& outFd);
    static void closeSocket(int& fd);
};

} // namespace simagv::l4
