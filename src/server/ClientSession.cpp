#include "server/ClientSession.hpp"
#include <cstring>
#include <iostream>
#include <random>
#include <sstream>
#include <iomanip>

#ifdef _WIN32
    #pragma comment(lib, "ws2_32.lib")
#endif

namespace opspulse {

std::atomic<uint64_t> ClientSession::sessionCounter_{0};

std::string ClientSession::generateSessionId() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;

    std::stringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(16) << dis(gen);
    return ss.str();
}

ClientSession::ClientSession(SocketType socket, const std::string& remoteAddr)
    : socket_(socket)
    , remoteAddr_(remoteAddr)
{
    id_ = "client-" + std::to_string(++sessionCounter_);
    recvBuffer_.reserve(config::CLIENT_BUFFER_SIZE);
}

ClientSession::~ClientSession() {
    close();
}

void ClientSession::setAuthenticated(const std::string& username, const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(mutex_);
    username_ = username;
    sessionId_ = sessionId.empty() ? generateSessionId() : sessionId;
    authenticated_.store(true);
}

// ============ Subscriptions ============

void ClientSession::subscribe(const std::string& channel) {
    std::lock_guard<std::mutex> lock(subMutex_);
    subscriptions_.insert(channel);
}

void ClientSession::unsubscribe(const std::string& channel) {
    std::lock_guard<std::mutex> lock(subMutex_);
    subscriptions_.erase(channel);
}

bool ClientSession::isSubscribed(const std::string& channel) const {
    std::lock_guard<std::mutex> lock(subMutex_);
    // Check for wildcard subscription or exact match
    if (subscriptions_.count(channels::ALL) > 0) {
        return true;
    }
    return subscriptions_.count(channel) > 0;
}

std::vector<std::string> ClientSession::getSubscriptions() const {
    std::lock_guard<std::mutex> lock(subMutex_);
    return std::vector<std::string>(subscriptions_.begin(), subscriptions_.end());
}

// ============ I/O Operations ============

int ClientSession::readIntoBuffer() {
    if (closed_.load()) {
        return -1;
    }

    uint8_t tempBuffer[config::CLIENT_BUFFER_SIZE];
    
#ifdef _WIN32
    int bytesRead = recv(socket_, reinterpret_cast<char*>(tempBuffer), 
                          sizeof(tempBuffer), 0);
#else
    ssize_t bytesRead = recv(socket_, tempBuffer, sizeof(tempBuffer), 0);
#endif

    if (bytesRead <= 0) {
        if (bytesRead == 0) {
            // Clean disconnect
            return 0;
        }
        // Error
        return -1;
    }

    // Feed to frame parser
    std::lock_guard<std::mutex> lock(recvMutex_);
    frameParser_.feed(tempBuffer, static_cast<size_t>(bytesRead));

    return static_cast<int>(bytesRead);
}

bool ClientSession::tryGetMessage(std::string& msg) {
    std::lock_guard<std::mutex> lock(recvMutex_);

    // Check if parser has error
    if (frameParser_.hasError()) {
        return false;
    }

    // Try to get a complete message
    msg = frameParser_.getMessage();
    return !msg.empty();
}

void ClientSession::queueMessage(const std::string& msg) {
    if (closed_.load()) {
        return;
    }
    
    // Encode with length prefix
    auto frame = Protocol::encode(msg);
    std::string frameStr(frame.begin(), frame.end());
    
    if (!sendQueue_.tryPush(std::move(frameStr))) {
        // Queue full - client is backpressured
        std::cerr << "[ClientSession] Send queue full for " << id_ << std::endl;
    }
}

void ClientSession::queueMessage(const Message& msg) {
    queueMessage(msg.serialize());
}

bool ClientSession::flushSendQueue() {
    if (closed_.load()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(sendMutex_);

    // If we have partially sent data, continue sending it
    while (sendOffset_ < sendBuffer_.size()) {
        const uint8_t* data = sendBuffer_.data() + sendOffset_;
        size_t remaining = sendBuffer_.size() - sendOffset_;

#ifdef _WIN32
        int sent = send(socket_, reinterpret_cast<const char*>(data), 
                        static_cast<int>(remaining), 0);
#else
        ssize_t sent = send(socket_, data, remaining, MSG_NOSIGNAL);
#endif

        if (sent < 0) {
            // Error - connection broken
            return false;
        }
        if (sent == 0) {
            // Would block - try again later
            return true;
        }

        sendOffset_ += static_cast<size_t>(sent);
    }

    // Buffer fully sent, clear it
    sendBuffer_.clear();
    sendOffset_ = 0;

    // Get next message from queue
    std::string msg;
    while (sendQueue_.tryPop(msg)) {
        sendBuffer_.insert(sendBuffer_.end(), msg.begin(), msg.end());

        // Try to send immediately
        while (sendOffset_ < sendBuffer_.size()) {
            const uint8_t* data = sendBuffer_.data() + sendOffset_;
            size_t remaining = sendBuffer_.size() - sendOffset_;

#ifdef _WIN32
            int sent = send(socket_, reinterpret_cast<const char*>(data),
                            static_cast<int>(remaining), 0);
#else
            ssize_t sent = send(socket_, data, remaining, MSG_NOSIGNAL);
#endif

            if (sent < 0) {
                return false;
            }
            if (sent == 0) {
                return true;  // Would block
            }

            sendOffset_ += static_cast<size_t>(sent);
        }

        sendBuffer_.clear();
        sendOffset_ = 0;
    }

    return true;
}

bool ClientSession::hasPendingWrites() const {
    std::lock_guard<std::mutex> lock(sendMutex_);
    return !sendQueue_.empty() || sendOffset_ < sendBuffer_.size();
}

bool ClientSession::isBackpressured() const {
    return sendQueue_.size() > 500;  // More than 500 pending messages
}

// ============ Lifecycle ============

void ClientSession::close() {
    if (closed_.exchange(true)) {
        return;  // Already closed
    }

    sendQueue_.close();

    if (socket_ != INVALID_SOCK) {
#ifdef _WIN32
        closesocket(socket_);
#else
        ::close(socket_);
#endif
        socket_ = INVALID_SOCK;
    }
}

} // namespace opspulse

