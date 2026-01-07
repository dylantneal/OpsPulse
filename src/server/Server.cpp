#include "server/Server.hpp"
#include "server/RequestHandler.hpp"
#include <iostream>
#include <cstring>
#include <csignal>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <poll.h>
#endif

namespace opspulse {

// Global for signal handling
static Server* g_serverInstance = nullptr;

static void signalHandler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        std::cout << "\n[Server] Received shutdown signal" << std::endl;
        if (g_serverInstance) {
            g_serverInstance->stop();
        }
    }
}

Server::Server(const ServerConfig& config)
    : config_(config)
{
    g_serverInstance = this;
}

Server::~Server() {
    shutdown();
    g_serverInstance = nullptr;
}

bool Server::initialize() {
    std::cout << "[Server] Initializing OpsPulse Server v1.0" << std::endl;

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[Server] WSAStartup failed" << std::endl;
        return false;
    }
#endif

    // Create components
    workerPool_ = std::make_unique<ThreadPool>(config_.workerThreads);
    stateStore_ = std::make_unique<StateStore>();
    broadcaster_ = std::make_unique<Broadcaster>();
    eventLog_ = std::make_unique<EventLog>(config_.eventLogPath);
    authManager_ = std::make_unique<AuthManager>();

    // Load users from file (if it exists)
    authManager_->loadUsers(config_.usersFilePath);

    // Open event log
    if (!eventLog_->open()) {
        std::cerr << "[Server] Warning: Could not open event log" << std::endl;
    }

    // Replay event log to restore state
    replayEventLog();

    // Setup state change callbacks
    setupCallbacks();

    return true;
}

void Server::setupCallbacks() {
    // Wire up callbacks for broadcasting state changes
    stateStore_->setOnEventAdded([this](const Event& event) {
        // Event already broadcast in RequestHandler
    });

    stateStore_->setOnIncidentCreated([this](const Incident& incident) {
        // Incident already broadcast in RequestHandler
    });

    stateStore_->setOnIncidentUpdated([this](const std::string& id, 
                                              const std::string& field,
                                              const std::string& oldVal,
                                              const std::string& newVal,
                                              const std::string& updatedBy) {
        // Get incident to find channel
        auto incident = stateStore_->getIncident(id);
        if (incident) {
            // Log the update
            eventLog_->logIncidentUpdate(id, field, oldVal, newVal, updatedBy);
            
            // Broadcast update to subscribers
            auto msg = MessageBuilder::pushIncidentUpdate(id, field, oldVal, newVal, updatedBy);
            broadcaster_->broadcastToChannel(incident->channel, msg);
        }
    });
}

bool Server::setupListenSocket() {
    listenSocket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket_ == INVALID_SOCK) {
        std::cerr << "[Server] Failed to create socket" << std::endl;
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
    addr.sin_port = htons(static_cast<uint16_t>(config_.port));

    if (bind(listenSocket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[Server] Failed to bind to port " << config_.port << std::endl;
#ifdef _WIN32
        closesocket(listenSocket_);
#else
        close(listenSocket_);
#endif
        return false;
    }

    // Listen
    if (listen(listenSocket_, SOMAXCONN) < 0) {
        std::cerr << "[Server] Failed to listen" << std::endl;
#ifdef _WIN32
        closesocket(listenSocket_);
#else
        close(listenSocket_);
#endif
        return false;
    }

    std::cout << "[Server] Listening on port " << config_.port << std::endl;
    return true;
}

void Server::run() {
    if (!initialize()) {
        std::cerr << "[Server] Initialization failed" << std::endl;
        return;
    }

    if (!setupListenSocket()) {
        return;
    }

    // Install signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Start broadcaster
    broadcaster_->start();

    // Start I/O threads
    running_.store(true);
    for (int i = 0; i < config::IO_THREADS; ++i) {
        ioThreads_.emplace_back(&Server::ioLoop, this);
    }

    std::cout << "[Server] Ready to accept connections" << std::endl;
    std::cout << "[Server] Stats: " << stateStore_->eventCount() << " events, "
              << stateStore_->incidentCount() << " incidents loaded" << std::endl;

    // Accept loop (main thread)
    acceptLoop();
}

void Server::stop() {
    running_.store(false);
}

void Server::acceptLoop() {
    while (running_.load()) {
        // Use poll/select with timeout to allow checking running_ flag
#ifdef _WIN32
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listenSocket_, &readSet);
        
        timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        int ready = select(0, &readSet, nullptr, nullptr, &timeout);
#else
        pollfd pfd;
        pfd.fd = listenSocket_;
        pfd.events = POLLIN;
        int ready = poll(&pfd, 1, 1000);  // 1 second timeout
#endif

        if (ready <= 0) {
            continue;  // Timeout or error, check running_ flag
        }

        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);

        SocketType clientSocket = accept(listenSocket_, 
                                          reinterpret_cast<sockaddr*>(&clientAddr),
                                          &clientLen);
        if (clientSocket == INVALID_SOCK) {
            continue;
        }

        // Get client address string
        char addrBuf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, addrBuf, sizeof(addrBuf));
        std::string remoteAddr = std::string(addrBuf) + ":" + 
                                  std::to_string(ntohs(clientAddr.sin_port));

        handleNewConnection(clientSocket, remoteAddr);
    }

    shutdown();
}

void Server::handleNewConnection(SocketType clientSocket, const std::string& remoteAddr) {
    std::cout << "[Server] New connection from " << remoteAddr << std::endl;

    // Create client session
    auto client = std::make_shared<ClientSession>(clientSocket, remoteAddr);
    
    // Register with broadcaster
    broadcaster_->registerClient(client);
}

void Server::ioLoop() {
    while (running_.load()) {
        auto clients = broadcaster_->getAllClients();

        for (const auto& client : clients) {
            if (client->isClosed()) {
                continue;
            }

            // Try to read from client
            int bytesRead = client->readIntoBuffer();
            
            if (bytesRead == 0) {
                // Client disconnected
                std::cout << "[Server] Client disconnected: " << client->getId() << std::endl;
                cleanupClient(client->getId());
                continue;
            }
            
            if (bytesRead < 0) {
                // Error
                cleanupClient(client->getId());
                continue;
            }

            // Process any complete messages
            std::string rawMsg;
            while (client->tryGetMessage(rawMsg)) {
                // Submit to worker pool
                ClientSessionPtr clientCopy = client;
                std::string msgCopy = rawMsg;
                
                workerPool_->execute([this, clientCopy, msgCopy]() {
                    handleMessage(clientCopy, msgCopy);
                });
            }
        }

        // Brief sleep to avoid busy-waiting when no data
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void Server::handleMessage(ClientSessionPtr client, const std::string& rawMsg) {
    try {
        Message msg = Message::parse(rawMsg);

        if (!msg.validate()) {
            auto errorResp = MessageBuilder::error(msg.requestId, 400, "Invalid message");
            client->queueMessage(errorResp);
            return;
        }

        // Create handler and process
        RequestHandler handler(*stateStore_, *broadcaster_, *eventLog_, 
                                *authManager_, config_.enableAuth);
        
        auto response = handler.handleMessage(client, msg);
        if (response) {
            client->queueMessage(*response);
        }
    }
    catch (const std::exception& e) {
        std::cerr << "[Server] Error handling message: " << e.what() << std::endl;
        auto errorResp = MessageBuilder::error("", 500, "Internal error");
        client->queueMessage(errorResp);
    }
}

void Server::cleanupClient(const std::string& clientId) {
    broadcaster_->unregisterClient(clientId);
}

void Server::replayEventLog() {
    eventLog_->replay(
        // On event
        [this](const Event& event) {
            stateStore_->loadEvent(event);
        },
        // On incident
        [this](const Incident& incident) {
            stateStore_->loadIncident(incident);
        },
        // On incident update
        [this](const std::string& id, const std::string& field,
                const std::string& oldVal, const std::string& newVal,
                const std::string& by) {
            (void)oldVal;
            (void)by;
            
            if (field == "status") {
                stateStore_->updateIncidentStatus(id, stringToStatus(newVal), "replay");
            } else if (field == "owner") {
                stateStore_->updateIncidentOwner(id, newVal, "replay");
            } else if (field == "severity") {
                stateStore_->updateIncidentSeverity(id, stringToSeverity(std::stoi(newVal)), "replay");
            }
        }
    );
}

void Server::shutdown() {
    std::cout << "[Server] Shutting down..." << std::endl;

    // Stop accepting new connections
    if (listenSocket_ != INVALID_SOCK) {
#ifdef _WIN32
        closesocket(listenSocket_);
#else
        close(listenSocket_);
#endif
        listenSocket_ = INVALID_SOCK;
    }

    // Wait for I/O threads
    for (auto& t : ioThreads_) {
        if (t.joinable()) {
            t.join();
        }
    }
    ioThreads_.clear();

    // Stop broadcaster
    if (broadcaster_) {
        broadcaster_->stop();
    }

    // Shutdown worker pool
    if (workerPool_) {
        workerPool_->shutdown();
    }

    // Flush and close event log
    if (eventLog_) {
        eventLog_->flush();
        eventLog_->close();
    }

#ifdef _WIN32
    WSACleanup();
#endif

    std::cout << "[Server] Shutdown complete" << std::endl;
}

size_t Server::connectedClients() const {
    return broadcaster_ ? broadcaster_->clientCount() : 0;
}

size_t Server::eventCount() const {
    return stateStore_ ? stateStore_->eventCount() : 0;
}

size_t Server::incidentCount() const {
    return stateStore_ ? stateStore_->incidentCount() : 0;
}

} // namespace opspulse

