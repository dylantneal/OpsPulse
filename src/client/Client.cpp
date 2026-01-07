#include "client/Client.hpp"
#include <iostream>
#include <cstring>
#include <random>
#include <sstream>
#include <iomanip>

#ifdef _WIN32
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <errno.h>
#endif

namespace opspulse {

Client::Client() = default;

Client::~Client() {
    disconnect();
}

std::string Client::generateRequestId() {
    static std::atomic<uint64_t> counter{0};
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;

    std::stringstream ss;
    ss << std::hex << (dis(gen) & 0xFFFF) << "-" << ++counter;
    return ss.str();
}

bool Client::connect(const std::string& host, int port) {
    if (connected_.load()) {
        return true;  // Already connected
    }

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        if (errorCallback_) errorCallback_("WSAStartup failed");
        return false;
    }
#endif

    // Resolve host
    struct addrinfo hints{}, *result;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    std::string portStr = std::to_string(port);
    int res = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result);
    if (res != 0) {
        if (errorCallback_) errorCallback_("Failed to resolve host: " + host);
        return false;
    }

    // Create socket
    socket_ = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (socket_ == INVALID_SOCK) {
        freeaddrinfo(result);
        if (errorCallback_) errorCallback_("Failed to create socket");
        return false;
    }

    // Connect
    if (::connect(socket_, result->ai_addr, static_cast<int>(result->ai_addrlen)) < 0) {
        freeaddrinfo(result);
#ifdef _WIN32
        closesocket(socket_);
#else
        close(socket_);
#endif
        socket_ = INVALID_SOCK;
        if (errorCallback_) errorCallback_("Failed to connect to " + host + ":" + portStr);
        return false;
    }

    freeaddrinfo(result);
    connected_.store(true);

    std::cout << "[Client] Connected to " << host << ":" << port << std::endl;
    return true;
}

void Client::disconnect() {
    stopReceiving();

    connected_.store(false);
    authenticated_.store(false);

    if (socket_ != INVALID_SOCK) {
#ifdef _WIN32
        closesocket(socket_);
#else
        close(socket_);
#endif
        socket_ = INVALID_SOCK;
    }

#ifdef _WIN32
    WSACleanup();
#endif
}

bool Client::sendMessage(const Message& msg) {
    if (!connected_.load()) {
        return false;
    }

    std::string data = msg.serialize();
    auto frame = Protocol::encode(data);

#ifdef _WIN32
    int sent = send(socket_, reinterpret_cast<const char*>(frame.data()),
                    static_cast<int>(frame.size()), 0);
#else
    ssize_t sent = send(socket_, frame.data(), frame.size(), 0);
#endif

    return sent == static_cast<decltype(sent)>(frame.size());
}

std::optional<Message> Client::receiveMessage() {
    if (!connected_.load()) {
        return std::nullopt;
    }

    uint8_t buffer[4096];
    
    while (true) {
#ifdef _WIN32
        int bytesRead = recv(socket_, reinterpret_cast<char*>(buffer), sizeof(buffer), 0);
#else
        ssize_t bytesRead = recv(socket_, buffer, sizeof(buffer), 0);
#endif

        if (bytesRead <= 0) {
            if (bytesRead == 0) {
                // Server closed connection
                connected_.store(false);
                if (disconnectCallback_) disconnectCallback_();
            }
            return std::nullopt;
        }

        std::lock_guard<std::mutex> lock(recvMutex_);
        auto result = frameParser_.feed(buffer, static_cast<size_t>(bytesRead));

        if (result == Protocol::ParseResult::ERROR) {
            if (errorCallback_) errorCallback_("Protocol error: " + frameParser_.getError());
            return std::nullopt;
        }

        if (result == Protocol::ParseResult::MESSAGE_COMPLETE) {
            std::string rawMsg = frameParser_.getMessage();
            try {
                return Message::parse(rawMsg);
            }
            catch (const std::exception& e) {
                if (errorCallback_) errorCallback_(std::string("Parse error: ") + e.what());
                return std::nullopt;
            }
        }

        // NEED_MORE_DATA - continue reading
    }
}

std::optional<Message> Client::sendAndWait(const Message& msg, int timeoutMs) {
    if (!sendMessage(msg)) {
        return std::nullopt;
    }

    // If receiving thread is running, wait for response via queue
    if (receiving_.load()) {
        std::unique_lock<std::mutex> lock(responseMutex_);
        if (responseCV_.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                                  [this] { return !responseQueue_.empty(); })) {
            Message response = std::move(responseQueue_.front());
            responseQueue_.pop();
            return response;
        }
        return std::nullopt;
    }

    // Otherwise, receive directly
    return receiveMessage();
}

// ============ Authentication ============

bool Client::authenticate(const std::string& username, const std::string& token) {
    Message msg;
    msg.type = MessageType::AUTH;
    msg.requestId = generateRequestId();
    msg.payload = AuthRequest{username, token}.toJson();

    auto response = sendAndWait(msg);
    if (!response) {
        return false;
    }

    if (response->type == MessageType::AUTH_RESPONSE) {
        bool success = response->payload.value("success", false);
        if (success) {
            sessionId_ = response->payload.value("session_id", "");
            authenticated_.store(true);
            std::cout << "[Client] Authenticated as " << username << std::endl;
            return true;
        } else {
            std::string reason = response->payload.value("message", "Unknown error");
            if (errorCallback_) errorCallback_("Auth failed: " + reason);
        }
    }

    return false;
}

// ============ Subscriptions ============

bool Client::subscribe(const std::vector<std::string>& channels) {
    Message msg;
    msg.type = MessageType::SUBSCRIBE;
    msg.requestId = generateRequestId();
    msg.payload = SubscribeRequest{channels}.toJson();

    auto response = sendAndWait(msg);
    return response && response->type == MessageType::ACK;
}

bool Client::unsubscribe(const std::vector<std::string>& channels) {
    Message msg;
    msg.type = MessageType::UNSUBSCRIBE;
    msg.requestId = generateRequestId();
    msg.payload = UnsubscribeRequest{channels}.toJson();

    auto response = sendAndWait(msg);
    return response && response->type == MessageType::ACK;
}

// ============ Events ============

bool Client::publishEvent(const std::string& channel, const std::string& level,
                           const std::string& message, const std::vector<std::string>& tags) {
    Message msg;
    msg.type = MessageType::EVENT_PUBLISH;
    msg.requestId = generateRequestId();
    msg.payload = EventPublishRequest{channel, level, message, tags}.toJson();

    auto response = sendAndWait(msg);
    return response && response->type == MessageType::ACK;
}

std::vector<Event> Client::listEvents(const std::string& channel, int limit) {
    Message msg;
    msg.type = MessageType::EVENT_LIST;
    msg.requestId = generateRequestId();

    EventListRequest req;
    if (!channel.empty()) req.channel = channel;
    req.limit = limit;
    msg.payload = req.toJson();

    auto response = sendAndWait(msg);
    if (!response || response->type != MessageType::EVENT_LIST_RESPONSE) {
        return {};
    }

    std::vector<Event> events;
    if (response->payload.contains("events") && response->payload["events"].is_array()) {
        for (const auto& ej : response->payload["events"]) {
            Event e;
            from_json(ej, e);
            events.push_back(e);
        }
    }
    return events;
}

// ============ Incidents ============

std::optional<std::string> Client::createIncident(int severity, const std::string& title,
                                                    const std::string& channel,
                                                    const std::string& description) {
    Message msg;
    msg.type = MessageType::INCIDENT_CREATE;
    msg.requestId = generateRequestId();
    msg.payload = IncidentCreateRequest{severity, title, channel, description}.toJson();

    auto response = sendAndWait(msg);
    if (response && response->type == MessageType::ACK) {
        return response->payload.value("resource_id", "");
    }
    return std::nullopt;
}

bool Client::updateIncident(const std::string& incidentId,
                             const std::optional<std::string>& status,
                             const std::optional<std::string>& owner,
                             const std::optional<int>& severity,
                             const std::optional<std::string>& comment) {
    Message msg;
    msg.type = MessageType::INCIDENT_UPDATE;
    msg.requestId = generateRequestId();

    IncidentUpdateRequest req;
    req.incidentId = incidentId;
    req.status = status;
    req.owner = owner;
    req.severity = severity;
    req.comment = comment;
    msg.payload = req.toJson();

    auto response = sendAndWait(msg);
    return response && response->type == MessageType::ACK;
}

std::vector<Incident> Client::listIncidents(const std::string& channel,
                                             const std::string& status,
                                             int limit) {
    Message msg;
    msg.type = MessageType::INCIDENT_LIST;
    msg.requestId = generateRequestId();

    IncidentListRequest req;
    if (!channel.empty()) req.channel = channel;
    if (!status.empty()) req.status = status;
    req.limit = limit;
    msg.payload = req.toJson();

    auto response = sendAndWait(msg);
    if (!response || response->type != MessageType::INCIDENT_LIST_RESPONSE) {
        return {};
    }

    std::vector<Incident> incidents;
    if (response->payload.contains("incidents") && response->payload["incidents"].is_array()) {
        for (const auto& ij : response->payload["incidents"]) {
            Incident inc;
            from_json(ij, inc);
            incidents.push_back(inc);
        }
    }
    return incidents;
}

// ============ Receiving ============

void Client::startReceiving() {
    if (receiving_.exchange(true)) {
        return;  // Already running
    }

    receiverThread_ = std::thread(&Client::receiverLoop, this);
}

void Client::stopReceiving() {
    if (!receiving_.exchange(false)) {
        return;
    }

    // Close socket to unblock recv
    if (socket_ != INVALID_SOCK) {
#ifdef _WIN32
        shutdown(socket_, SD_BOTH);
#else
        shutdown(socket_, SHUT_RDWR);
#endif
    }

    if (receiverThread_.joinable()) {
        receiverThread_.join();
    }
}

void Client::receiverLoop() {
    uint8_t buffer[4096];

    while (receiving_.load() && connected_.load()) {
#ifdef _WIN32
        int bytesRead = recv(socket_, reinterpret_cast<char*>(buffer), sizeof(buffer), 0);
#else
        ssize_t bytesRead = recv(socket_, buffer, sizeof(buffer), 0);
#endif

        if (bytesRead <= 0) {
            if (bytesRead == 0 && connected_.load()) {
                connected_.store(false);
                if (disconnectCallback_) disconnectCallback_();
            }
            break;
        }

        std::lock_guard<std::mutex> lock(recvMutex_);
        auto result = frameParser_.feed(buffer, static_cast<size_t>(bytesRead));

        while (result == Protocol::ParseResult::MESSAGE_COMPLETE) {
            std::string rawMsg = frameParser_.getMessage();
            try {
                Message msg = Message::parse(rawMsg);
                handleMessage(msg);
            }
            catch (const std::exception& e) {
                if (errorCallback_) errorCallback_(std::string("Parse error: ") + e.what());
            }

            // Check if there's another complete message in buffer
            result = frameParser_.feed(nullptr, 0);
        }

        if (result == Protocol::ParseResult::ERROR) {
            if (errorCallback_) errorCallback_("Protocol error");
            break;
        }
    }
}

void Client::handleMessage(const Message& msg) {
    switch (msg.type) {
        case MessageType::PUSH_EVENT:
            if (eventCallback_) {
                Event event;
                from_json(msg.payload, event);
                eventCallback_(event);
            }
            break;

        case MessageType::PUSH_INCIDENT:
            if (incidentCallback_) {
                Incident incident;
                from_json(msg.payload, incident);
                incidentCallback_(incident);
            }
            break;

        case MessageType::PUSH_INCIDENT_UPDATE:
            if (updateCallback_) {
                std::string id = msg.payload.value("id", "");
                std::string field = msg.payload.value("field", "");
                std::string newVal = msg.payload.value("new_value", "");
                updateCallback_(id, field, newVal);
            }
            break;

        case MessageType::ERROR:
            if (errorCallback_) {
                std::string errMsg = msg.payload.value("message", "Unknown error");
                errorCallback_(errMsg);
            }
            break;

        // Response messages - put in queue for sendAndWait
        case MessageType::AUTH_RESPONSE:
        case MessageType::ACK:
        case MessageType::INCIDENT_LIST_RESPONSE:
        case MessageType::EVENT_LIST_RESPONSE:
        case MessageType::PONG:
            {
                std::lock_guard<std::mutex> lock(responseMutex_);
                responseQueue_.push(msg);
                responseCV_.notify_one();
            }
            break;

        default:
            break;
    }
}

} // namespace opspulse

