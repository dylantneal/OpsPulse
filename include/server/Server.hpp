#pragma once

#include "ThreadPool.hpp"
#include "StateStore.hpp"
#include "ClientSession.hpp"
#include "Broadcaster.hpp"
#include "EventLog.hpp"
#include "Auth.hpp"
#include "common/Types.hpp"

#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <vector>

namespace opspulse {

// Forward declaration
class WebSocketServer;

struct ServerConfig {
    int port = config::DEFAULT_PORT;
    int wsPort = 8080;  // WebSocket port for browser connections
    int workerThreads = config::WORKER_THREADS;
    std::string eventLogPath = "data/events.log";
    std::string usersFilePath = "config/users.json";
    bool enableAuth = true;
};

/**
 * Main OpsPulse server.
 * Orchestrates all components: accepting connections, handling requests,
 * broadcasting updates, and persisting state.
 */
class Server {
public:
    explicit Server(const ServerConfig& config = {});
    ~Server();

    // Non-copyable
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Start the server (blocking)
    void run();

    // Stop the server gracefully
    void stop();

    // Check if server is running
    bool isRunning() const { return running_.load(); }

    // Get stats
    size_t connectedClients() const;
    size_t eventCount() const;
    size_t incidentCount() const;

private:
    ServerConfig config_;

    // Core components
    std::unique_ptr<ThreadPool> workerPool_;
    std::unique_ptr<StateStore> stateStore_;
    std::unique_ptr<Broadcaster> broadcaster_;
    std::unique_ptr<EventLog> eventLog_;
    std::unique_ptr<AuthManager> authManager_;
    std::unique_ptr<WebSocketServer> wsServer_;  // WebSocket server for browsers

    // Networking
    SocketType listenSocket_ = INVALID_SOCK;
    std::atomic<bool> running_{false};

    // I/O threads for reading from clients
    std::vector<std::thread> ioThreads_;

    // Initialize components
    bool initialize();

    // Setup listening socket
    bool setupListenSocket();

    // Accept thread loop
    void acceptLoop();

    // I/O thread loop (reads from clients)
    void ioLoop();

    // Handle a new connection
    void handleNewConnection(SocketType clientSocket, const std::string& remoteAddr);

    // Handle a message from a client
    void handleMessage(ClientSessionPtr client, const std::string& rawMsg);

    // Cleanup a disconnected client
    void cleanupClient(const std::string& clientId);

    // Replay event log on startup
    void replayEventLog();

    // Wire up state change callbacks
    void setupCallbacks();

    // Graceful shutdown
    void shutdown();
};

} // namespace opspulse

