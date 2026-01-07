#pragma once

#include "ClientSession.hpp"
#include "common/Message.hpp"

#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <thread>
#include <atomic>
#include <functional>

namespace opspulse {

/**
 * Fan-out broadcaster that pushes events to subscribed clients.
 * Runs its own thread to avoid blocking worker threads on slow clients.
 */
class Broadcaster {
public:
    Broadcaster();
    ~Broadcaster();

    // Start the broadcaster thread
    void start();

    // Stop the broadcaster (waits for thread to finish)
    void stop();

    // ============ Client Management ============

    void registerClient(ClientSessionPtr client);
    void unregisterClient(const std::string& clientId);

    // Get a client by ID
    ClientSessionPtr getClient(const std::string& clientId) const;

    // Get all connected clients
    std::vector<ClientSessionPtr> getAllClients() const;

    // Get count of connected clients
    size_t clientCount() const;

    // ============ Broadcasting ============

    // Broadcast to all clients subscribed to a channel
    void broadcastToChannel(const std::string& channel, const Message& msg);

    // Broadcast to all authenticated clients
    void broadcastToAll(const Message& msg);

    // Send to a specific client
    void sendToClient(const std::string& clientId, const Message& msg);

    // ============ Subscription Index ============

    // Called when client subscribes/unsubscribes
    void updateSubscription(const std::string& clientId, const std::string& channel, bool subscribe);

    // Get clients subscribed to a channel
    std::vector<std::string> getSubscribers(const std::string& channel) const;

private:
    mutable std::shared_mutex clientMutex_;
    std::unordered_map<std::string, ClientSessionPtr> clients_;

    mutable std::shared_mutex subMutex_;
    // channel -> set of client IDs
    std::unordered_map<std::string, std::unordered_set<std::string>> subscriptions_;

    // Broadcaster thread for flushing send queues
    std::thread writerThread_;
    std::atomic<bool> running_{false};

    void writerLoop();
};

} // namespace opspulse

