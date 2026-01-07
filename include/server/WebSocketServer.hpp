#pragma once

#include "StateStore.hpp"
#include "Broadcaster.hpp"
#include "Auth.hpp"
#include "common/Types.hpp"
#include "common/Message.hpp"

#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <functional>
#include <set>

namespace opspulse {

/**
 * WebSocket frame opcode
 */
enum class WsOpcode : uint8_t {
    CONTINUATION = 0x0,
    TEXT = 0x1,
    BINARY = 0x2,
    CLOSE = 0x8,
    PING = 0x9,
    PONG = 0xA
};

/**
 * WebSocket client connection
 */
class WebSocketClient {
public:
    WebSocketClient(SocketType socket, const std::string& remoteAddr);
    ~WebSocketClient();

    // Non-copyable
    WebSocketClient(const WebSocketClient&) = delete;
    WebSocketClient& operator=(const WebSocketClient&) = delete;

    // Send a text message
    bool sendText(const std::string& message);

    // Send a close frame
    void sendClose(uint16_t code = 1000);

    // Receive and decode a WebSocket frame
    // Returns empty optional if no complete frame available or connection closed
    std::optional<std::pair<WsOpcode, std::string>> receiveFrame();

    // Handle WebSocket handshake (returns false if invalid)
    bool performHandshake();

    // Connection state
    bool isOpen() const { return !closed_.load(); }
    void close();

    // Identifiers
    const std::string& getId() const { return id_; }
    const std::string& getRemoteAddr() const { return remoteAddr_; }

    // User info
    void setUser(const std::string& user, UserRole role) { user_ = user; role_ = role; authenticated_ = true; }
    bool isAuthenticated() const { return authenticated_; }
    const std::string& getUser() const { return user_; }
    UserRole getRole() const { return role_; }

    // Subscriptions
    void subscribe(const std::string& channel) { 
        std::lock_guard<std::mutex> lock(subMutex_);
        subscriptions_.insert(channel);
    }
    void unsubscribe(const std::string& channel) {
        std::lock_guard<std::mutex> lock(subMutex_);
        subscriptions_.erase(channel);
    }
    bool isSubscribed(const std::string& channel) const {
        std::lock_guard<std::mutex> lock(subMutex_);
        return subscriptions_.count(channel) > 0 || subscriptions_.count("*") > 0;
    }
    std::set<std::string> getSubscriptions() const {
        std::lock_guard<std::mutex> lock(subMutex_);
        return subscriptions_;
    }

private:
    SocketType socket_;
    std::string id_;
    std::string remoteAddr_;
    std::atomic<bool> closed_{false};
    
    // Authentication
    bool authenticated_ = false;
    std::string user_;
    UserRole role_ = UserRole::VIEWER;

    // Subscriptions
    mutable std::mutex subMutex_;
    std::set<std::string> subscriptions_;

    // Buffer for reading
    std::vector<uint8_t> readBuffer_;

    // Helper to encode WebSocket frame
    std::vector<uint8_t> encodeFrame(WsOpcode opcode, const std::string& payload);

    // Helper to read exactly n bytes
    bool readExact(std::vector<uint8_t>& buffer, size_t n);

    // Generate WebSocket accept key
    static std::string computeAcceptKey(const std::string& clientKey);
    static std::string base64Encode(const unsigned char* data, size_t len);
    static void sha1(const std::string& input, unsigned char output[20]);
};

using WebSocketClientPtr = std::shared_ptr<WebSocketClient>;

/**
 * WebSocket server for browser connections.
 * Provides a WebSocket interface to OpsPulse functionality.
 */
class WebSocketServer {
public:
    WebSocketServer(int port, StateStore& stateStore, AuthManager& authManager, bool enableAuth);
    ~WebSocketServer();

    // Non-copyable
    WebSocketServer(const WebSocketServer&) = delete;
    WebSocketServer& operator=(const WebSocketServer&) = delete;

    // Start/stop the server
    bool start();
    void stop();

    // Broadcast to all WebSocket clients subscribed to a channel
    void broadcastToChannel(const std::string& channel, const std::string& jsonMessage);

    // Broadcast to all authenticated clients
    void broadcastToAll(const std::string& jsonMessage);

    // Get client count
    size_t clientCount() const;

private:
    int port_;
    StateStore& stateStore_;
    AuthManager& authManager_;
    bool enableAuth_;

    SocketType listenSocket_ = INVALID_SOCK;
    std::atomic<bool> running_{false};

    // Client management
    mutable std::mutex clientsMutex_;
    std::unordered_map<std::string, WebSocketClientPtr> clients_;

    // Threads
    std::thread acceptThread_;
    std::thread ioThread_;

    // Accept loop
    void acceptLoop();

    // I/O loop for handling client messages
    void ioLoop();

    // Handle a new WebSocket connection
    void handleNewConnection(SocketType clientSocket, const std::string& remoteAddr);

    // Process a message from a WebSocket client
    void processMessage(WebSocketClientPtr client, const std::string& message);

    // Register/unregister clients
    void registerClient(WebSocketClientPtr client);
    void unregisterClient(const std::string& clientId);

    // Get all clients (thread-safe copy)
    std::vector<WebSocketClientPtr> getAllClients() const;
};

} // namespace opspulse

