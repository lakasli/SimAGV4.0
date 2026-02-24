#include "mqtt311_client_atoms.hpp"
#include "console_log_atoms.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <sstream>

namespace simagv::l4 {
namespace {

constexpr uint8_t kPacketTypeConnect = 0x10;
constexpr uint8_t kPacketTypeConnAck = 0x20;
constexpr uint8_t kPacketTypePublish = 0x30;
constexpr uint8_t kPacketTypeSubscribe = 0x82;
constexpr uint8_t kPacketTypeSubAck = 0x90;
constexpr uint8_t kPacketTypePingReq = 0xC0;
constexpr uint8_t kPacketTypePingResp = 0xD0;
constexpr uint8_t kPacketTypeDisconnect = 0xE0;

uint16_t readU16be(const uint8_t* pBuf)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(pBuf[0]) << 8U) | static_cast<uint16_t>(pBuf[1]));
}

} // namespace

Mqtt311Client::Mqtt311Client() : sockFd_(-1), connected_(false), stopRx_(false), rxThread_(), nextPacketId_(1), onMessage_(), lastConn_()
{
}

Mqtt311Client::~Mqtt311Client()
{
    disconnect();
}

void Mqtt311Client::setMessageHandler(std::function<void(std::string, std::string)> handler)
{
    std::lock_guard<std::mutex> lock(mutex_);
    onMessage_ = std::move(handler);
}

bool Mqtt311Client::connect(std::string host, uint16_t port, std::string clientId, uint16_t keepAliveSec, const MqttWillConfig* willConfig)
{
    disconnect();

    ConnectionInfo connInfo;
    connInfo.host = std::move(host);
    connInfo.port = port;
    connInfo.clientId = std::move(clientId);
    connInfo.keepAliveSec = keepAliveSec;
    connInfo.hasWill = (willConfig != nullptr) && (!willConfig->topic.empty());
    if (connInfo.hasWill) {
        connInfo.will = *willConfig;
    }

    int fd = -1;
    if (!openTcpSocket(connInfo.host, connInfo.port, fd)) {
        return false;
    }

    std::vector<uint8_t> packet = buildConnectPacket(connInfo);
    if (!sendPacket(fd, packet)) {
        closeSocket(fd);
        return false;
    }

    uint8_t header = 0;
    std::vector<uint8_t> body;
    if (!readPacket(fd, header, body)) {
        closeSocket(fd);
        return false;
    }
    if ((header & 0xF0U) != kPacketTypeConnAck || body.size() < 2U) {
        closeSocket(fd);
        return false;
    }
    const uint8_t returnCode = body[1];
    if (returnCode != 0U) {
        closeSocket(fd);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        sockFd_ = fd;
        lastConn_ = connInfo;
    }

    connected_.store(true);
    stopRx_.store(false);
    rxThread_ = std::thread([this] { rxLoop(); });
    {
        std::ostringstream oss;
        oss << "mqtt311_connected host=" << connInfo.host << " port=" << connInfo.port << " clientId=" << connInfo.clientId
            << " keepAliveSec=" << connInfo.keepAliveSec << " hasWill=" << (connInfo.hasWill ? "true" : "false");
        logInfo(oss.str());
    }
    return true;
}

void Mqtt311Client::disconnect()
{
    stopRx_.store(true);
    connected_.store(false);

    int fd = -1;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        fd = sockFd_;
        sockFd_ = -1;
    }
    if (fd >= 0) {
        std::vector<uint8_t> packet;
        packet.push_back(kPacketTypeDisconnect);
        packet.push_back(0x00);
        (void)sendPacket(fd, packet);
        closeSocket(fd);
    }

    if (rxThread_.joinable()) {
        rxThread_.join();
    }
}

bool Mqtt311Client::isConnected() const
{
    return connected_.load();
}

bool Mqtt311Client::publish(std::string topic, std::string payload, uint8_t qos, bool retain)
{
    if (qos > 0U) {
        qos = 0U;
    }

    std::vector<uint8_t> packet = buildPublishPacket(topic, payload, qos, retain);
    int fd = -1;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        fd = sockFd_;
    }
    if (fd < 0) {
        return false;
    }
    return sendPacket(fd, packet);
}

bool Mqtt311Client::subscribe(std::string topicFilter, uint8_t qos)
{
    if (qos > 0U) {
        qos = 0U;
    }

    uint16_t packetId = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        packetId = nextPacketId_++;
        if (nextPacketId_ == 0U) {
            nextPacketId_ = 1U;
        }
    }

    std::vector<uint8_t> packet = buildSubscribePacket(packetId, topicFilter, qos);
    int fd = -1;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        fd = sockFd_;
    }
    if (fd < 0) {
        return false;
    }
    return sendPacket(fd, packet);
}

bool Mqtt311Client::sendPacket(int sockFd, const std::vector<uint8_t>& packet)
{
    size_t written = 0;
    while (written < packet.size()) {
        const ssize_t n = ::send(sockFd, packet.data() + written, packet.size() - written, MSG_NOSIGNAL);
        if (n <= 0) {
            return false;
        }
        written += static_cast<size_t>(n);
    }
    return true;
}

bool Mqtt311Client::readExact(int sockFd, uint8_t* p_Buf, size_t len)
{
    size_t got = 0;
    while (got < len) {
        const ssize_t n = ::recv(sockFd, p_Buf + got, len - got, 0);
        if (n <= 0) {
            return false;
        }
        got += static_cast<size_t>(n);
    }
    return true;
}

bool Mqtt311Client::readPacket(int sockFd, uint8_t& header, std::vector<uint8_t>& body)
{
    uint8_t hdr = 0;
    if (!readExact(sockFd, &hdr, 1U)) {
        return false;
    }
    header = hdr;

    std::vector<uint8_t> rem;
    rem.reserve(4);
    for (int i = 0; i < 4; ++i) {
        uint8_t b = 0;
        if (!readExact(sockFd, &b, 1U)) {
            return false;
        }
        rem.push_back(b);
        if ((b & 0x80U) == 0U) {
            break;
        }
    }

    size_t offset = 0;
    size_t remainingLength = 0;
    if (!decodeRemainingLength(rem, offset, remainingLength)) {
        return false;
    }
    body.resize(remainingLength);
    if (remainingLength == 0U) {
        return true;
    }
    return readExact(sockFd, reinterpret_cast<uint8_t*>(body.data()), remainingLength);
}

void Mqtt311Client::rxLoop()
{
    auto lastPing = std::chrono::steady_clock::now();
    auto markDisconnected = [&](const char* reason, int fd) {
        if (connected_.exchange(false)) {
            ConnectionInfo connCopy;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                connCopy = lastConn_;
            }
            std::ostringstream oss;
            oss << "mqtt311_disconnected reason=" << reason << " host=" << connCopy.host << " port=" << connCopy.port
                << " clientId=" << connCopy.clientId << " fd=" << fd;
            logWarn(oss.str());
        }
    };
    while (!stopRx_.load()) {
        int fd = -1;
        ConnectionInfo connCopy;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            fd = sockFd_;
            connCopy = lastConn_;
        }
        if (fd < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }

        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastPing).count();
        if (connCopy.keepAliveSec > 0U && elapsed >= static_cast<int64_t>(connCopy.keepAliveSec / 2U)) {
            lastPing = now;
            const std::vector<uint8_t> ping = buildPingReqPacket();
            if (!sendPacket(fd, ping)) {
                markDisconnected("send_ping_failed", fd);
                closeSocket(fd);
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (sockFd_ == fd) {
                        sockFd_ = -1;
                    }
                }
                continue;
            }
        }

        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        const int pollRc = ::poll(&pfd, 1, 200);
        if (pollRc == 0) {
            continue;
        }
        if (pollRc < 0) {
            markDisconnected("poll_failed", fd);
            closeSocket(fd);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (sockFd_ == fd) {
                    sockFd_ = -1;
                }
            }
            continue;
        }
        if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            markDisconnected("poll_error", fd);
            closeSocket(fd);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (sockFd_ == fd) {
                    sockFd_ = -1;
                }
            }
            continue;
        }

        uint8_t header = 0;
        std::vector<uint8_t> body;
        if (!readPacket(fd, header, body)) {
            markDisconnected("read_packet_failed", fd);
            closeSocket(fd);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (sockFd_ == fd) {
                    sockFd_ = -1;
                }
            }
            continue;
        }

        const uint8_t packetType = static_cast<uint8_t>(header & 0xF0U);
        if (packetType == kPacketTypePingResp || packetType == kPacketTypeSubAck) {
            continue;
        }
        if (packetType != kPacketTypePublish) {
            continue;
        }

        if (body.size() < 2U) {
            continue;
        }
        size_t pos = 0;
        const uint16_t topicLen = readU16be(body.data());
        pos += 2U;
        if (body.size() < pos + static_cast<size_t>(topicLen)) {
            continue;
        }
        std::string topic(reinterpret_cast<const char*>(body.data() + pos), static_cast<size_t>(topicLen));
        pos += static_cast<size_t>(topicLen);

        const uint8_t qos = static_cast<uint8_t>((header >> 1U) & 0x03U);
        if (qos > 0U) {
            if (body.size() < pos + 2U) {
                continue;
            }
            pos += 2U;
        }

        std::string payload;
        if (body.size() > pos) {
            payload.assign(reinterpret_cast<const char*>(body.data() + pos), body.size() - pos);
        }

        std::function<void(std::string, std::string)> handler;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            handler = onMessage_;
        }
        if (handler) {
            handler(std::move(topic), std::move(payload));
        }
    }
}

std::vector<uint8_t> Mqtt311Client::buildConnectPacket(const ConnectionInfo& connInfo)
{
    std::vector<uint8_t> vh;
    encodeUtf8String(vh, "MQTT");
    vh.push_back(0x04);

    uint8_t flags = 0x02;
    if (connInfo.hasWill) {
        flags |= 0x04;
        const uint8_t willQos = (connInfo.will.qos > 2U) ? 0U : connInfo.will.qos;
        flags |= static_cast<uint8_t>((willQos & 0x03U) << 3U);
        if (connInfo.will.retain) {
            flags |= 0x20;
        }
    }
    vh.push_back(flags);
    vh.push_back(static_cast<uint8_t>((connInfo.keepAliveSec >> 8U) & 0xFFU));
    vh.push_back(static_cast<uint8_t>(connInfo.keepAliveSec & 0xFFU));

    std::vector<uint8_t> pl;
    encodeUtf8String(pl, connInfo.clientId);
    if (connInfo.hasWill) {
        encodeUtf8String(pl, connInfo.will.topic);
        encodeUtf8String(pl, connInfo.will.payload);
    }

    std::vector<uint8_t> out;
    out.push_back(kPacketTypeConnect);
    encodeRemainingLength(out, vh.size() + pl.size());
    out.insert(out.end(), vh.begin(), vh.end());
    out.insert(out.end(), pl.begin(), pl.end());
    return out;
}

std::vector<uint8_t> Mqtt311Client::buildPublishPacket(const std::string& topic, const std::string& payload, uint8_t qos, bool retain)
{
    std::vector<uint8_t> vh;
    encodeUtf8String(vh, topic);
    (void)qos;

    std::vector<uint8_t> out;
    uint8_t header = kPacketTypePublish;
    if (retain) {
        header |= 0x01;
    }
    out.push_back(header);
    encodeRemainingLength(out, vh.size() + payload.size());
    out.insert(out.end(), vh.begin(), vh.end());
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

std::vector<uint8_t> Mqtt311Client::buildSubscribePacket(uint16_t packetId, const std::string& topicFilter, uint8_t qos)
{
    std::vector<uint8_t> vh;
    vh.push_back(static_cast<uint8_t>((packetId >> 8U) & 0xFFU));
    vh.push_back(static_cast<uint8_t>(packetId & 0xFFU));

    std::vector<uint8_t> pl;
    encodeUtf8String(pl, topicFilter);
    pl.push_back(static_cast<uint8_t>(qos & 0x03U));

    std::vector<uint8_t> out;
    out.push_back(kPacketTypeSubscribe);
    encodeRemainingLength(out, vh.size() + pl.size());
    out.insert(out.end(), vh.begin(), vh.end());
    out.insert(out.end(), pl.begin(), pl.end());
    return out;
}

std::vector<uint8_t> Mqtt311Client::buildPingReqPacket()
{
    return std::vector<uint8_t>{kPacketTypePingReq, 0x00};
}

void Mqtt311Client::encodeUtf8String(std::vector<uint8_t>& out, const std::string& text)
{
    const uint16_t len = static_cast<uint16_t>(text.size());
    out.push_back(static_cast<uint8_t>((len >> 8U) & 0xFFU));
    out.push_back(static_cast<uint8_t>(len & 0xFFU));
    out.insert(out.end(), text.begin(), text.end());
}

void Mqtt311Client::encodeRemainingLength(std::vector<uint8_t>& out, size_t remainingLength)
{
    size_t x = remainingLength;
    do {
        uint8_t digit = static_cast<uint8_t>(x % 128U);
        x /= 128U;
        if (x > 0U) {
            digit |= 0x80;
        }
        out.push_back(digit);
    } while (x > 0U);
}

bool Mqtt311Client::decodeRemainingLength(const std::vector<uint8_t>& in, size_t& offset, size_t& outLength)
{
    size_t multiplier = 1;
    size_t value = 0;
    size_t idx = offset;
    for (int i = 0; i < 4; ++i) {
        if (idx >= in.size()) {
            return false;
        }
        const uint8_t digit = in[idx++];
        value += static_cast<size_t>(digit & 0x7FU) * multiplier;
        if ((digit & 0x80U) == 0U) {
            offset = idx;
            outLength = value;
            return true;
        }
        multiplier *= 128U;
    }
    return false;
}

bool Mqtt311Client::openTcpSocket(const std::string& host, uint16_t port, int& outFd)
{
    outFd = -1;
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = nullptr;
    const std::string portText = std::to_string(port);
    if (::getaddrinfo(host.c_str(), portText.c_str(), &hints, &res) != 0) {
        return false;
    }

    int fd = -1;
    for (struct addrinfo* p = res; p != nullptr; p = p->ai_next) {
        fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
            break;
        }
        ::close(fd);
        fd = -1;
    }
    ::freeaddrinfo(res);

    if (fd < 0) {
        return false;
    }
    outFd = fd;
    return true;
}

void Mqtt311Client::closeSocket(int& fd)
{
    if (fd >= 0) {
        ::shutdown(fd, SHUT_RDWR);
        ::close(fd);
        fd = -1;
    }
}

} // namespace simagv::l4
