#include "server/WebSocketServer.hpp"
#include <iostream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <random>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <poll.h>
#endif

namespace opspulse {

// =============================================================================
// SHA-1 Implementation (minimal, for WebSocket handshake)
// =============================================================================

// SHA-1 constants
static constexpr uint32_t SHA1_K[] = {0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xCA62C1D6};

static inline uint32_t sha1RotateLeft(uint32_t value, unsigned int count) {
    return (value << count) | (value >> (32 - count));
}

void WebSocketClient::sha1(const std::string& input, unsigned char output[20]) {
    uint32_t h0 = 0x67452301;
    uint32_t h1 = 0xEFCDAB89;
    uint32_t h2 = 0x98BADCFE;
    uint32_t h3 = 0x10325476;
    uint32_t h4 = 0xC3D2E1F0;

    // Pre-processing
    std::vector<uint8_t> msg(input.begin(), input.end());
    uint64_t originalBitLen = msg.size() * 8;
    
    msg.push_back(0x80);
    while ((msg.size() % 64) != 56) {
        msg.push_back(0x00);
    }

    // Append length in bits as big-endian 64-bit
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<uint8_t>((originalBitLen >> (i * 8)) & 0xFF));
    }

    // Process each 512-bit chunk
    for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        uint32_t w[80];
        
        // Break chunk into sixteen 32-bit big-endian words
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(msg[chunk + i*4]) << 24) |
                   (static_cast<uint32_t>(msg[chunk + i*4 + 1]) << 16) |
                   (static_cast<uint32_t>(msg[chunk + i*4 + 2]) << 8) |
                   (static_cast<uint32_t>(msg[chunk + i*4 + 3]));
        }

        // Extend the sixteen 32-bit words into eighty 32-bit words
        for (int i = 16; i < 80; ++i) {
            w[i] = sha1RotateLeft(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
        }

        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;

        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = SHA1_K[0];
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = SHA1_K[1];
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = SHA1_K[2];
            } else {
                f = b ^ c ^ d;
                k = SHA1_K[3];
            }

            uint32_t temp = sha1RotateLeft(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = sha1RotateLeft(b, 30);
            b = a;
            a = temp;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    // Produce the final hash value (big-endian)
    output[0] = (h0 >> 24) & 0xFF; output[1] = (h0 >> 16) & 0xFF;
    output[2] = (h0 >> 8) & 0xFF;  output[3] = h0 & 0xFF;
    output[4] = (h1 >> 24) & 0xFF; output[5] = (h1 >> 16) & 0xFF;
    output[6] = (h1 >> 8) & 0xFF;  output[7] = h1 & 0xFF;
    output[8] = (h2 >> 24) & 0xFF; output[9] = (h2 >> 16) & 0xFF;
    output[10] = (h2 >> 8) & 0xFF; output[11] = h2 & 0xFF;
    output[12] = (h3 >> 24) & 0xFF; output[13] = (h3 >> 16) & 0xFF;
    output[14] = (h3 >> 8) & 0xFF; output[15] = h3 & 0xFF;
    output[16] = (h4 >> 24) & 0xFF; output[17] = (h4 >> 16) & 0xFF;
    output[18] = (h4 >> 8) & 0xFF; output[19] = h4 & 0xFF;
}

// =============================================================================
// Base64 Encoding
// =============================================================================

static const char base64Chars[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string WebSocketClient::base64Encode(const unsigned char* data, size_t len) {
    std::string result;
    result.reserve(((len + 2) / 3) * 4);

    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len) n |= static_cast<uint32_t>(data[i + 2]);

        result.push_back(base64Chars[(n >> 18) & 0x3F]);
        result.push_back(base64Chars[(n >> 12) & 0x3F]);
        result.push_back((i + 1 < len) ? base64Chars[(n >> 6) & 0x3F] : '=');
        result.push_back((i + 2 < len) ? base64Chars[n & 0x3F] : '=');
    }

    return result;
}

// =============================================================================
// WebSocketClient Implementation
// =============================================================================

WebSocketClient::WebSocketClient(SocketType socket, const std::string& remoteAddr)
    : socket_(socket)
    , remoteAddr_(remoteAddr)
{
    // Generate unique ID
    static std::atomic<uint64_t> counter{0};
    id_ = "ws-" + std::to_string(++counter);
    
    // Set non-blocking
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(socket_, FIONBIO, &mode);
#else
    int flags = fcntl(socket_, F_GETFL, 0);
    fcntl(socket_, F_SETFL, flags | O_NONBLOCK);
#endif
}

WebSocketClient::~WebSocketClient() {
    close();
}

void WebSocketClient::close() {
    if (!closed_.exchange(true)) {
#ifdef _WIN32
        closesocket(socket_);
#else
        ::close(socket_);
#endif
    }
}

std::string WebSocketClient::computeAcceptKey(const std::string& clientKey) {
    // WebSocket magic GUID
    const std::string magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string concat = clientKey + magic;
    
    unsigned char hash[20];
    sha1(concat, hash);
    
    return base64Encode(hash, 20);
}

bool WebSocketClient::performHandshake() {
    // Read HTTP request (blocking for handshake)
#ifdef _WIN32
    u_long mode = 0;
    ioctlsocket(socket_, FIONBIO, &mode);
#else
    int flags = fcntl(socket_, F_GETFL, 0);
    fcntl(socket_, F_SETFL, flags & ~O_NONBLOCK);
#endif

    char buffer[4096];
    std::string request;
    
    // Read until we get \r\n\r\n
    while (request.find("\r\n\r\n") == std::string::npos) {
        int n = recv(socket_, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) {
            return false;
        }
        buffer[n] = '\0';
        request += buffer;
        
        if (request.size() > 8192) {
            return false;  // Request too large
        }
    }

    // Parse headers to find Sec-WebSocket-Key
    std::string wsKey;
    std::istringstream iss(request);
    std::string line;
    
    // First line should be GET request
    std::getline(iss, line);
    if (line.find("GET") != 0) {
        return false;
    }

    while (std::getline(iss, line)) {
        // Remove trailing \r
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        if (line.empty()) break;

        size_t colonPos = line.find(':');
        if (colonPos != std::string::npos) {
            std::string header = line.substr(0, colonPos);
            std::string value = line.substr(colonPos + 1);
            
            // Trim whitespace
            while (!value.empty() && value[0] == ' ') value.erase(0, 1);
            
            // Case-insensitive header comparison
            std::transform(header.begin(), header.end(), header.begin(), ::tolower);
            
            if (header == "sec-websocket-key") {
                wsKey = value;
            }
        }
    }

    if (wsKey.empty()) {
        return false;
    }

    // Compute accept key
    std::string acceptKey = computeAcceptKey(wsKey);

    // Send handshake response
    std::string response = 
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + acceptKey + "\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n";

    int sent = send(socket_, response.c_str(), static_cast<int>(response.size()), 0);
    if (sent != static_cast<int>(response.size())) {
        return false;
    }

    // Set back to non-blocking
#ifdef _WIN32
    mode = 1;
    ioctlsocket(socket_, FIONBIO, &mode);
#else
    flags = fcntl(socket_, F_GETFL, 0);
    fcntl(socket_, F_SETFL, flags | O_NONBLOCK);
#endif

    return true;
}

bool WebSocketClient::readExact(std::vector<uint8_t>& buffer, size_t n) {
    size_t start = buffer.size();
    buffer.resize(start + n);
    
    size_t totalRead = 0;
    while (totalRead < n) {
        int bytesRead = recv(socket_, reinterpret_cast<char*>(buffer.data() + start + totalRead),
                            static_cast<int>(n - totalRead), 0);
        if (bytesRead <= 0) {
#ifdef _WIN32
            if (WSAGetLastError() == WSAEWOULDBLOCK) {
#else
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
#endif
                // No data available yet
                buffer.resize(start);
                return false;
            }
            // Error or connection closed
            closed_.store(true);
            buffer.resize(start);
            return false;
        }
        totalRead += bytesRead;
    }
    return true;
}

std::optional<std::pair<WsOpcode, std::string>> WebSocketClient::receiveFrame() {
    if (closed_.load()) {
        return std::nullopt;
    }

    // Read first 2 bytes
    uint8_t header[2];
    int n = recv(socket_, reinterpret_cast<char*>(header), 2, MSG_PEEK);
    
    if (n <= 0) {
#ifdef _WIN32
        if (n < 0 && WSAGetLastError() == WSAEWOULDBLOCK) {
#else
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
#endif
            return std::nullopt;  // No data available
        }
        if (n == 0 || (n < 0)) {
            closed_.store(true);
            return std::nullopt;
        }
    }
    
    if (n < 2) {
        return std::nullopt;
    }

    // Actually read the header
    recv(socket_, reinterpret_cast<char*>(header), 2, 0);

    bool fin = (header[0] & 0x80) != 0;
    WsOpcode opcode = static_cast<WsOpcode>(header[0] & 0x0F);
    bool masked = (header[1] & 0x80) != 0;
    uint64_t payloadLen = header[1] & 0x7F;

    // Extended payload length
    if (payloadLen == 126) {
        uint8_t extended[2];
        if (recv(socket_, reinterpret_cast<char*>(extended), 2, 0) != 2) {
            closed_.store(true);
            return std::nullopt;
        }
        payloadLen = (static_cast<uint64_t>(extended[0]) << 8) | extended[1];
    } else if (payloadLen == 127) {
        uint8_t extended[8];
        if (recv(socket_, reinterpret_cast<char*>(extended), 8, 0) != 8) {
            closed_.store(true);
            return std::nullopt;
        }
        payloadLen = 0;
        for (int i = 0; i < 8; ++i) {
            payloadLen = (payloadLen << 8) | extended[i];
        }
    }

    // Masking key (client messages are always masked)
    uint8_t maskKey[4] = {0, 0, 0, 0};
    if (masked) {
        if (recv(socket_, reinterpret_cast<char*>(maskKey), 4, 0) != 4) {
            closed_.store(true);
            return std::nullopt;
        }
    }

    // Read payload
    std::string payload;
    if (payloadLen > 0) {
        payload.resize(payloadLen);
        size_t totalRead = 0;
        while (totalRead < payloadLen) {
            int bytesRead = recv(socket_, &payload[totalRead], 
                                static_cast<int>(payloadLen - totalRead), 0);
            if (bytesRead <= 0) {
                closed_.store(true);
                return std::nullopt;
            }
            totalRead += bytesRead;
        }

        // Unmask
        if (masked) {
            for (size_t i = 0; i < payloadLen; ++i) {
                payload[i] ^= maskKey[i % 4];
            }
        }
    }

    (void)fin;  // We don't handle fragmented messages for simplicity

    return std::make_pair(opcode, payload);
}

std::vector<uint8_t> WebSocketClient::encodeFrame(WsOpcode opcode, const std::string& payload) {
    std::vector<uint8_t> frame;
    
    // First byte: FIN + opcode
    frame.push_back(0x80 | static_cast<uint8_t>(opcode));
    
    // Second byte: mask bit (0 for server) + payload length
    if (payload.size() < 126) {
        frame.push_back(static_cast<uint8_t>(payload.size()));
    } else if (payload.size() < 65536) {
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>((payload.size() >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(payload.size() & 0xFF));
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<uint8_t>((payload.size() >> (i * 8)) & 0xFF));
        }
    }
    
    // Payload (server doesn't mask)
    frame.insert(frame.end(), payload.begin(), payload.end());
    
    return frame;
}

bool WebSocketClient::sendText(const std::string& message) {
    if (closed_.load()) {
        return false;
    }

    auto frame = encodeFrame(WsOpcode::TEXT, message);
    
    size_t totalSent = 0;
    while (totalSent < frame.size()) {
        int sent = send(socket_, reinterpret_cast<const char*>(frame.data() + totalSent),
                       static_cast<int>(frame.size() - totalSent), 0);
        if (sent <= 0) {
            closed_.store(true);
            return false;
        }
        totalSent += sent;
    }
    
    return true;
}

void WebSocketClient::sendClose(uint16_t code) {
    if (closed_.load()) {
        return;
    }

    std::string payload;
    payload.push_back(static_cast<char>((code >> 8) & 0xFF));
    payload.push_back(static_cast<char>(code & 0xFF));
    
    auto frame = encodeFrame(WsOpcode::CLOSE, payload);
    send(socket_, reinterpret_cast<const char*>(frame.data()), 
         static_cast<int>(frame.size()), 0);
    
    close();
}

// =============================================================================
// WebSocketServer Implementation
// =============================================================================

WebSocketServer::WebSocketServer(int port, StateStore& stateStore, 
                                 AuthManager& authManager, bool enableAuth)
    : port_(port)
    , stateStore_(stateStore)
    , authManager_(authManager)
    , enableAuth_(enableAuth)
{
}

WebSocketServer::~WebSocketServer() {
    stop();
}

bool WebSocketServer::start() {
    // Create socket
    listenSocket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket_ == INVALID_SOCK) {
        std::cerr << "[WebSocket] Failed to create socket" << std::endl;
        return false;
    }

    // Set socket options
    int opt = 1;
#ifdef _WIN32
    setsockopt(listenSocket_, SOL_SOCKET, SO_REUSEADDR, 
               reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
    setsockopt(listenSocket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    // Bind
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (bind(listenSocket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[WebSocket] Failed to bind to port " << port_ << std::endl;
#ifdef _WIN32
        closesocket(listenSocket_);
#else
        ::close(listenSocket_);
#endif
        return false;
    }

    // Listen
    if (listen(listenSocket_, SOMAXCONN) < 0) {
        std::cerr << "[WebSocket] Failed to listen" << std::endl;
#ifdef _WIN32
        closesocket(listenSocket_);
#else
        ::close(listenSocket_);
#endif
        return false;
    }

    running_.store(true);

    // Start threads
    acceptThread_ = std::thread(&WebSocketServer::acceptLoop, this);
    ioThread_ = std::thread(&WebSocketServer::ioLoop, this);

    std::cout << "[WebSocket] Server listening on port " << port_ << std::endl;
    return true;
}

void WebSocketServer::stop() {
    running_.store(false);

    if (listenSocket_ != INVALID_SOCK) {
#ifdef _WIN32
        closesocket(listenSocket_);
#else
        ::close(listenSocket_);
#endif
        listenSocket_ = INVALID_SOCK;
    }

    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }
    if (ioThread_.joinable()) {
        ioThread_.join();
    }

    // Close all clients
    std::lock_guard<std::mutex> lock(clientsMutex_);
    for (auto& [id, client] : clients_) {
        client->sendClose();
    }
    clients_.clear();
}

void WebSocketServer::acceptLoop() {
    while (running_.load()) {
#ifndef _WIN32
        pollfd pfd;
        pfd.fd = listenSocket_;
        pfd.events = POLLIN;
        int ready = poll(&pfd, 1, 1000);
#else
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listenSocket_, &readSet);
        timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        int ready = select(0, &readSet, nullptr, nullptr, &timeout);
#endif

        if (ready <= 0) {
            continue;
        }

        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);

        SocketType clientSocket = accept(listenSocket_,
                                         reinterpret_cast<sockaddr*>(&clientAddr),
                                         &clientLen);
        if (clientSocket == INVALID_SOCK) {
            continue;
        }

        char addrBuf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, addrBuf, sizeof(addrBuf));
        std::string remoteAddr = std::string(addrBuf) + ":" +
                                 std::to_string(ntohs(clientAddr.sin_port));

        handleNewConnection(clientSocket, remoteAddr);
    }
}

void WebSocketServer::handleNewConnection(SocketType clientSocket, const std::string& remoteAddr) {
    auto client = std::make_shared<WebSocketClient>(clientSocket, remoteAddr);
    
    // Perform WebSocket handshake
    if (!client->performHandshake()) {
        std::cerr << "[WebSocket] Handshake failed for " << remoteAddr << std::endl;
        return;
    }

    std::cout << "[WebSocket] Client connected: " << client->getId() 
              << " from " << remoteAddr << std::endl;

    registerClient(client);

    // Send welcome message
    nlohmann::json welcome;
    welcome["type"] = "welcome";
    welcome["payload"]["message"] = "Connected to OpsPulse WebSocket API";
    welcome["payload"]["version"] = "1.0";
    welcome["payload"]["auth_required"] = enableAuth_;
    client->sendText(welcome.dump());
}

void WebSocketServer::registerClient(WebSocketClientPtr client) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    clients_[client->getId()] = client;
}

void WebSocketServer::unregisterClient(const std::string& clientId) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    clients_.erase(clientId);
}

std::vector<WebSocketClientPtr> WebSocketServer::getAllClients() const {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    std::vector<WebSocketClientPtr> result;
    result.reserve(clients_.size());
    for (const auto& [id, client] : clients_) {
        result.push_back(client);
    }
    return result;
}

size_t WebSocketServer::clientCount() const {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    return clients_.size();
}

void WebSocketServer::ioLoop() {
    while (running_.load()) {
        auto clients = getAllClients();

        for (const auto& client : clients) {
            if (!client->isOpen()) {
                std::cout << "[WebSocket] Client disconnected: " << client->getId() << std::endl;
                unregisterClient(client->getId());
                continue;
            }

            // Try to receive a frame
            auto frameOpt = client->receiveFrame();
            if (!frameOpt) {
                continue;
            }

            auto [opcode, payload] = *frameOpt;

            switch (opcode) {
                case WsOpcode::TEXT:
                    processMessage(client, payload);
                    break;
                case WsOpcode::CLOSE:
                    client->sendClose();
                    unregisterClient(client->getId());
                    break;
                case WsOpcode::PING: {
                    // Respond with pong
                    auto pongFrame = std::vector<uint8_t>{0x8A, 0x00};  // Pong with no payload
                    send(client->getId().c_str()[0], reinterpret_cast<const char*>(pongFrame.data()), 2, 0);
                    break;
                }
                default:
                    break;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void WebSocketServer::processMessage(WebSocketClientPtr client, const std::string& message) {
    try {
        auto json = nlohmann::json::parse(message);
        std::string type = json.value("type", "");
        auto payload = json.value("payload", nlohmann::json::object());
        std::string requestId = json.value("request_id", "");

        nlohmann::json response;
        response["request_id"] = requestId;

        // Handle authentication
        if (type == "auth") {
            std::string user = payload.value("user", "");
            std::string token = payload.value("token", "");

            if (!enableAuth_) {
                client->setUser(user.empty() ? "anonymous" : user, UserRole::ADMIN);
                response["type"] = "auth_response";
                response["payload"]["success"] = true;
                response["payload"]["session_id"] = client->getId();
                response["payload"]["role"] = "admin";
            } else {
                auto username = authManager_.authenticate(user, token);
                if (username) {
                    auto role = authManager_.getUserRole(*username);
                    client->setUser(*username, role.value_or(UserRole::VIEWER));
                    response["type"] = "auth_response";
                    response["payload"]["success"] = true;
                    response["payload"]["session_id"] = client->getId();
                    response["payload"]["role"] = roleToString(role.value_or(UserRole::VIEWER));
                } else {
                    response["type"] = "auth_response";
                    response["payload"]["success"] = false;
                    response["payload"]["error"] = "Invalid credentials";
                }
            }
            client->sendText(response.dump());
            return;
        }

        // Check authentication for all other requests
        if (enableAuth_ && !client->isAuthenticated()) {
            response["type"] = "error";
            response["payload"]["code"] = 401;
            response["payload"]["message"] = "Authentication required";
            client->sendText(response.dump());
            return;
        }

        // Handle subscriptions
        if (type == "subscribe") {
            auto channels = payload.value("channels", std::vector<std::string>{});
            for (const auto& ch : channels) {
                client->subscribe(ch);
            }
            response["type"] = "ack";
            response["payload"]["message"] = "Subscribed";
            response["payload"]["channels"] = channels;
            client->sendText(response.dump());
            return;
        }

        if (type == "unsubscribe") {
            auto channels = payload.value("channels", std::vector<std::string>{});
            for (const auto& ch : channels) {
                client->unsubscribe(ch);
            }
            response["type"] = "ack";
            response["payload"]["message"] = "Unsubscribed";
            client->sendText(response.dump());
            return;
        }

        // Handle event creation
        if (type == "event") {
            if (enableAuth_ && client->getRole() == UserRole::VIEWER) {
                response["type"] = "error";
                response["payload"]["code"] = 403;
                response["payload"]["message"] = "Insufficient permissions";
                client->sendText(response.dump());
                return;
            }

            Event event;
            event.id = IdGenerator::generateEventId();
            event.timestamp = nowMs();
            event.channel = payload.value("channel", "general");
            event.level = stringToLevel(payload.value("level", "info"));
            event.message = payload.value("msg", "");

            stateStore_.addEvent(event);

            // Broadcast to subscribers
            nlohmann::json pushEvent;
            pushEvent["type"] = "push_event";
            pushEvent["payload"]["id"] = event.id;
            pushEvent["payload"]["timestamp"] = event.timestamp;
            pushEvent["payload"]["channel"] = event.channel;
            pushEvent["payload"]["level"] = levelToString(event.level);
            pushEvent["payload"]["message"] = event.message;
            broadcastToChannel(event.channel, pushEvent.dump());

            response["type"] = "ack";
            response["payload"]["resource_id"] = event.id;
            response["payload"]["message"] = "Event created";
            client->sendText(response.dump());
            return;
        }

        // Handle incident creation
        if (type == "incident_create") {
            if (enableAuth_ && client->getRole() == UserRole::VIEWER) {
                response["type"] = "error";
                response["payload"]["code"] = 403;
                response["payload"]["message"] = "Insufficient permissions";
                client->sendText(response.dump());
                return;
            }

            std::string channel = payload.value("channel", "general");
            std::string title = payload.value("title", "");
            std::string description = payload.value("description", "");
            Severity sev = stringToSeverity(payload.value("sev", 3));

            std::string incidentId = stateStore_.createIncident(sev, title, channel, description);
            auto incident = stateStore_.getIncident(incidentId);

            // Broadcast to subscribers
            if (incident) {
                nlohmann::json pushIncident;
                pushIncident["type"] = "push_incident";
                pushIncident["payload"]["id"] = incident->id;
                pushIncident["payload"]["created_at"] = incident->created_at;
                pushIncident["payload"]["severity"] = static_cast<int>(incident->severity);
                pushIncident["payload"]["status"] = statusToString(incident->status);
                pushIncident["payload"]["title"] = incident->title;
                pushIncident["payload"]["channel"] = incident->channel;
                broadcastToChannel(incident->channel, pushIncident.dump());
            }

            response["type"] = "ack";
            response["payload"]["resource_id"] = incidentId;
            response["payload"]["message"] = "Incident created";
            client->sendText(response.dump());
            return;
        }

        // Handle incident updates
        if (type == "incident_update") {
            if (enableAuth_ && client->getRole() == UserRole::VIEWER) {
                response["type"] = "error";
                response["payload"]["code"] = 403;
                response["payload"]["message"] = "Insufficient permissions";
                client->sendText(response.dump());
                return;
            }

            std::string id = payload.value("id", "");
            auto incident = stateStore_.getIncident(id);
            if (!incident) {
                response["type"] = "error";
                response["payload"]["code"] = 404;
                response["payload"]["message"] = "Incident not found";
                client->sendText(response.dump());
                return;
            }

            if (payload.contains("status")) {
                stateStore_.updateIncidentStatus(id, stringToStatus(payload["status"]), client->getUser());
            }
            if (payload.contains("owner")) {
                stateStore_.updateIncidentOwner(id, payload["owner"], client->getUser());
            }
            if (payload.contains("sev")) {
                stateStore_.updateIncidentSeverity(id, stringToSeverity(payload["sev"]), client->getUser());
            }

            response["type"] = "ack";
            response["payload"]["resource_id"] = id;
            response["payload"]["message"] = "Incident updated";
            client->sendText(response.dump());
            return;
        }

        // Handle list events
        if (type == "list_events") {
            std::string channel = payload.value("channel", "");
            int limit = payload.value("limit", 50);

            auto events = channel.empty() ? stateStore_.getAllEvents(static_cast<size_t>(limit))
                                          : stateStore_.getEventsByChannel(channel, static_cast<size_t>(limit));

            nlohmann::json eventsJson = nlohmann::json::array();
            for (const auto& e : events) {
                nlohmann::json ej;
                ej["id"] = e.id;
                ej["timestamp"] = e.timestamp;
                ej["channel"] = e.channel;
                ej["level"] = levelToString(e.level);
                ej["message"] = e.message;
                eventsJson.push_back(ej);
            }

            response["type"] = "events";
            response["payload"]["events"] = eventsJson;
            response["payload"]["count"] = events.size();
            client->sendText(response.dump());
            return;
        }

        // Handle list incidents
        if (type == "list_incidents") {
            std::string channel = payload.value("channel", "");
            std::string statusFilter = payload.value("status", "");

            std::optional<std::string> channelOpt = channel.empty() ? std::nullopt : std::make_optional(channel);
            std::optional<IncidentStatus> statusOpt = statusFilter.empty() ? std::nullopt 
                                                        : std::make_optional(stringToStatus(statusFilter));

            auto incidents = stateStore_.getIncidents(channelOpt, statusOpt, 100);

            nlohmann::json incidentsJson = nlohmann::json::array();
            for (const auto& i : incidents) {
                nlohmann::json ij;
                ij["id"] = i.id;
                ij["created_at"] = i.created_at;
                ij["updated_at"] = i.updated_at;
                ij["severity"] = static_cast<int>(i.severity);
                ij["status"] = statusToString(i.status);
                ij["title"] = i.title;
                ij["channel"] = i.channel;
                ij["owner"] = i.owner;
                incidentsJson.push_back(ij);
            }

            response["type"] = "incidents";
            response["payload"]["incidents"] = incidentsJson;
            response["payload"]["count"] = incidents.size();
            client->sendText(response.dump());
            return;
        }

        // Unknown message type
        response["type"] = "error";
        response["payload"]["code"] = 400;
        response["payload"]["message"] = "Unknown message type: " + type;
        client->sendText(response.dump());

    } catch (const std::exception& e) {
        nlohmann::json error;
        error["type"] = "error";
        error["payload"]["code"] = 500;
        error["payload"]["message"] = std::string("Error processing message: ") + e.what();
        client->sendText(error.dump());
    }
}

void WebSocketServer::broadcastToChannel(const std::string& channel, const std::string& jsonMessage) {
    auto clients = getAllClients();
    for (const auto& client : clients) {
        if (client->isOpen() && client->isAuthenticated() && client->isSubscribed(channel)) {
            client->sendText(jsonMessage);
        }
    }
}

void WebSocketServer::broadcastToAll(const std::string& jsonMessage) {
    auto clients = getAllClients();
    for (const auto& client : clients) {
        if (client->isOpen() && client->isAuthenticated()) {
            client->sendText(jsonMessage);
        }
    }
}

} // namespace opspulse

