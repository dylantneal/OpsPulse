#pragma once

#include "common/Types.hpp"
#include "common/Protocol.hpp"
#include "common/Message.hpp"
#include "ThreadPool.hpp"

#include <string>
#include <vector>
#include <unordered_set>
#include <mutex>
#include <atomic>
#include <memory>
#include <functional>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using SocketType = SOCKET;
    constexpr SocketType INVALID_SOCK = INVALID_SOCKET;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <unistd.h>
    using SocketType = int;
    constexpr SocketType INVALID_SOCK = -1;
#endif

namespace opspulse {

class ClientSession;
using ClientSessionPtr = std::shared_ptr<ClientSession>;

/**
 * Represents a connected client session.
 * Handles buffered I/O and manages subscriptions.
 */
class ClientSession : public std::enable_shared_from_this<ClientSession> {
public:
    ClientSession(SocketType socket, const std::string& remoteAddr);
    ~ClientSession();

    // Non-copyable
    ClientSession(const ClientSession&) = delete;
    ClientSession& operator=(const ClientSession&) = delete;

    // ============ Identity ============

    std::string getId() const { return id_; }
    std::string getRemoteAddr() const { return remoteAddr_; }
    std::string getUsername() const { 
        std::lock_guard<std::mutex> lock(mutex_);
        return username_; 
    }

    void setAuthenticated(const std::string& username, const std::string& sessionId);
    bool isAuthenticated() const { return authenticated_.load(); }

    // ============ Subscriptions ============

    void subscribe(const std::string& channel);
    void unsubscribe(const std::string& channel);
    bool isSubscribed(const std::string& channel) const;
    std::vector<std::string> getSubscriptions() const;

    // ============ I/O Operations ============

    // Read from socket into internal buffer
    // Returns number of bytes read, 0 on disconnect, -1 on error
    int readIntoBuffer();

    // Try to parse a complete message from buffer
    // Returns true if a message is available, sets msg parameter
    bool tryGetMessage(std::string& msg);

    // Queue a message for sending
    void queueMessage(const std::string& msg);
    void queueMessage(const Message& msg);

    // Send queued messages (called by sender thread)
    // Returns false if socket is closed or error
    bool flushSendQueue();

    // Check if there are pending outbound messages
    bool hasPendingWrites() const;

    // ============ Lifecycle ============

    void close();
    bool isClosed() const { return closed_.load(); }

    SocketType getSocket() const { return socket_; }

    // Backpressure: check if outbound queue is too large
    bool isBackpressured() const;

private:
    std::string id_;
    SocketType socket_;
    std::string remoteAddr_;

    mutable std::mutex mutex_;
    std::string username_;
    std::string sessionId_;
    std::atomic<bool> authenticated_{false};
    std::atomic<bool> closed_{false};

    // Subscriptions
    std::unordered_set<std::string> subscriptions_;
    mutable std::mutex subMutex_;

    // Receive buffer and frame parser
    std::vector<uint8_t> recvBuffer_;
    Protocol::FrameParser frameParser_;
    mutable std::mutex recvMutex_;

    // Send queue
    ThreadSafeQueue<std::string> sendQueue_{1000};  // Max 1000 pending messages
    std::vector<uint8_t> sendBuffer_;
    size_t sendOffset_ = 0;
    mutable std::mutex sendMutex_;

    static std::atomic<uint64_t> sessionCounter_;
    static std::string generateSessionId();
};

} // namespace opspulse

