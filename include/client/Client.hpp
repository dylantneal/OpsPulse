#pragma once

#include "common/Types.hpp"
#include "common/Protocol.hpp"
#include "common/Message.hpp"

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using SocketType = SOCKET;
    constexpr SocketType INVALID_SOCK = INVALID_SOCKET;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <netdb.h>
    using SocketType = int;
    constexpr SocketType INVALID_SOCK = -1;
#endif

namespace opspulse {

/**
 * OpsPulse client for connecting to the server.
 * Provides both sync and async APIs.
 */
class Client {
public:
    Client();
    ~Client();

    // Connect to server
    bool connect(const std::string& host, int port);

    // Disconnect from server
    void disconnect();

    // Check connection status
    bool isConnected() const { return connected_.load(); }

    // ============ Authentication ============

    bool authenticate(const std::string& username, const std::string& token);

    // ============ Subscriptions ============

    bool subscribe(const std::vector<std::string>& channels);
    bool unsubscribe(const std::vector<std::string>& channels);

    // ============ Events ============

    bool publishEvent(const std::string& channel, const std::string& level,
                      const std::string& message, const std::vector<std::string>& tags = {});

    std::vector<Event> listEvents(const std::string& channel = "", int limit = 50);

    // ============ Incidents ============

    std::optional<std::string> createIncident(int severity, const std::string& title,
                                               const std::string& channel,
                                               const std::string& description = "");

    bool updateIncident(const std::string& incidentId,
                        const std::optional<std::string>& status = std::nullopt,
                        const std::optional<std::string>& owner = std::nullopt,
                        const std::optional<int>& severity = std::nullopt,
                        const std::optional<std::string>& comment = std::nullopt);

    std::vector<Incident> listIncidents(const std::string& channel = "",
                                         const std::string& status = "",
                                         int limit = 50);

    // ============ Push Callbacks ============

    using EventCallback = std::function<void(const Event&)>;
    using IncidentCallback = std::function<void(const Incident&)>;
    using IncidentUpdateCallback = std::function<void(const std::string& id,
                                                       const std::string& field,
                                                       const std::string& newValue)>;
    using ErrorCallback = std::function<void(const std::string& msg)>;
    using DisconnectCallback = std::function<void()>;

    void onEvent(EventCallback cb) { eventCallback_ = std::move(cb); }
    void onIncident(IncidentCallback cb) { incidentCallback_ = std::move(cb); }
    void onIncidentUpdate(IncidentUpdateCallback cb) { updateCallback_ = std::move(cb); }
    void onError(ErrorCallback cb) { errorCallback_ = std::move(cb); }
    void onDisconnect(DisconnectCallback cb) { disconnectCallback_ = std::move(cb); }

    // Start receiving push messages (starts background thread)
    void startReceiving();

    // Stop receiving
    void stopReceiving();

    // ============ Sync Utilities ============

    // Send raw message and wait for response
    std::optional<Message> sendAndWait(const Message& msg, int timeoutMs = 5000);

private:
    SocketType socket_ = INVALID_SOCK;
    std::atomic<bool> connected_{false};
    std::atomic<bool> authenticated_{false};
    std::string sessionId_;

    // Receiver thread
    std::thread receiverThread_;
    std::atomic<bool> receiving_{false};

    // Callbacks
    EventCallback eventCallback_;
    IncidentCallback incidentCallback_;
    IncidentUpdateCallback updateCallback_;
    ErrorCallback errorCallback_;
    DisconnectCallback disconnectCallback_;

    // Response waiting
    std::mutex responseMutex_;
    std::condition_variable responseCV_;
    std::queue<Message> responseQueue_;

    // Send a message
    bool sendMessage(const Message& msg);

    // Receive a message (blocking)
    std::optional<Message> receiveMessage();

    // Receiver thread loop
    void receiverLoop();

    // Handle a received message
    void handleMessage(const Message& msg);

    // Frame parser for receiving
    Protocol::FrameParser frameParser_;
    std::mutex recvMutex_;

    // Generate request ID
    static std::string generateRequestId();
};

} // namespace opspulse

