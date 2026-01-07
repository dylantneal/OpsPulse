#include "server/Broadcaster.hpp"
#include <iostream>
#include <chrono>

namespace opspulse {

Broadcaster::Broadcaster() = default;

Broadcaster::~Broadcaster() {
    stop();
}

void Broadcaster::start() {
    if (running_.exchange(true)) {
        return;  // Already running
    }
    writerThread_ = std::thread(&Broadcaster::writerLoop, this);
}

void Broadcaster::stop() {
    if (!running_.exchange(false)) {
        return;  // Not running
    }
    
    if (writerThread_.joinable()) {
        writerThread_.join();
    }
}

// ============ Client Management ============

void Broadcaster::registerClient(ClientSessionPtr client) {
    std::unique_lock<std::shared_mutex> lock(clientMutex_);
    clients_[client->getId()] = client;
}

void Broadcaster::unregisterClient(const std::string& clientId) {
    // Remove from clients map
    {
        std::unique_lock<std::shared_mutex> lock(clientMutex_);
        clients_.erase(clientId);
    }

    // Remove from all subscription lists
    {
        std::unique_lock<std::shared_mutex> lock(subMutex_);
        for (auto& [channel, subscribers] : subscriptions_) {
            subscribers.erase(clientId);
        }
    }
}

ClientSessionPtr Broadcaster::getClient(const std::string& clientId) const {
    std::shared_lock<std::shared_mutex> lock(clientMutex_);
    auto it = clients_.find(clientId);
    if (it != clients_.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<ClientSessionPtr> Broadcaster::getAllClients() const {
    std::shared_lock<std::shared_mutex> lock(clientMutex_);
    std::vector<ClientSessionPtr> result;
    result.reserve(clients_.size());
    for (const auto& [id, client] : clients_) {
        result.push_back(client);
    }
    return result;
}

size_t Broadcaster::clientCount() const {
    std::shared_lock<std::shared_mutex> lock(clientMutex_);
    return clients_.size();
}

// ============ Broadcasting ============

void Broadcaster::broadcastToChannel(const std::string& channel, const Message& msg) {
    std::vector<ClientSessionPtr> targets;

    {
        std::shared_lock<std::shared_mutex> subLock(subMutex_);
        std::shared_lock<std::shared_mutex> clientLock(clientMutex_);

        // Get subscribers for this specific channel
        auto it = subscriptions_.find(channel);
        if (it != subscriptions_.end()) {
            for (const auto& clientId : it->second) {
                auto clientIt = clients_.find(clientId);
                if (clientIt != clients_.end() && !clientIt->second->isClosed()) {
                    targets.push_back(clientIt->second);
                }
            }
        }

        // Also check wildcard subscribers
        auto wildIt = subscriptions_.find(channels::ALL);
        if (wildIt != subscriptions_.end()) {
            for (const auto& clientId : wildIt->second) {
                auto clientIt = clients_.find(clientId);
                if (clientIt != clients_.end() && !clientIt->second->isClosed()) {
                    // Avoid duplicates
                    bool found = false;
                    for (const auto& t : targets) {
                        if (t->getId() == clientId) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        targets.push_back(clientIt->second);
                    }
                }
            }
        }
    }

    // Queue message to all targets (outside lock)
    for (const auto& client : targets) {
        if (!client->isBackpressured()) {
            client->queueMessage(msg);
        }
    }
}

void Broadcaster::broadcastToAll(const Message& msg) {
    std::vector<ClientSessionPtr> targets;

    {
        std::shared_lock<std::shared_mutex> lock(clientMutex_);
        for (const auto& [id, client] : clients_) {
            if (client->isAuthenticated() && !client->isClosed()) {
                targets.push_back(client);
            }
        }
    }

    for (const auto& client : targets) {
        if (!client->isBackpressured()) {
            client->queueMessage(msg);
        }
    }
}

void Broadcaster::sendToClient(const std::string& clientId, const Message& msg) {
    ClientSessionPtr client;

    {
        std::shared_lock<std::shared_mutex> lock(clientMutex_);
        auto it = clients_.find(clientId);
        if (it != clients_.end()) {
            client = it->second;
        }
    }

    if (client && !client->isClosed()) {
        client->queueMessage(msg);
    }
}

// ============ Subscription Index ============

void Broadcaster::updateSubscription(const std::string& clientId, const std::string& channel, bool subscribe) {
    std::unique_lock<std::shared_mutex> lock(subMutex_);
    
    if (subscribe) {
        subscriptions_[channel].insert(clientId);
    } else {
        auto it = subscriptions_.find(channel);
        if (it != subscriptions_.end()) {
            it->second.erase(clientId);
        }
    }
}

std::vector<std::string> Broadcaster::getSubscribers(const std::string& channel) const {
    std::shared_lock<std::shared_mutex> lock(subMutex_);
    std::vector<std::string> result;
    
    auto it = subscriptions_.find(channel);
    if (it != subscriptions_.end()) {
        result.assign(it->second.begin(), it->second.end());
    }
    
    return result;
}

// ============ Writer Thread ============

void Broadcaster::writerLoop() {
    while (running_.load()) {
        std::vector<ClientSessionPtr> clients;
        std::vector<std::string> closedClients;

        // Get snapshot of clients
        {
            std::shared_lock<std::shared_mutex> lock(clientMutex_);
            clients.reserve(clients_.size());
            for (const auto& [id, client] : clients_) {
                clients.push_back(client);
            }
        }

        // Flush send queues for all clients
        for (const auto& client : clients) {
            if (client->isClosed()) {
                closedClients.push_back(client->getId());
                continue;
            }

            if (client->hasPendingWrites()) {
                if (!client->flushSendQueue()) {
                    // Error writing - mark for cleanup
                    closedClients.push_back(client->getId());
                }
            }
        }

        // Clean up closed clients
        for (const auto& id : closedClients) {
            unregisterClient(id);
        }

        // Sleep briefly to avoid busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

} // namespace opspulse

