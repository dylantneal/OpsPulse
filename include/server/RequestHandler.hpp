#pragma once

#include "ClientSession.hpp"
#include "StateStore.hpp"
#include "Broadcaster.hpp"
#include "EventLog.hpp"
#include "Auth.hpp"
#include "common/Message.hpp"

namespace opspulse {

/**
 * Handles incoming requests from clients.
 * Validates, processes, and generates responses.
 */
class RequestHandler {
public:
    RequestHandler(StateStore& store, Broadcaster& broadcaster, 
                   EventLog& eventLog, AuthManager& auth, bool authEnabled);

    // Process a message from a client
    // Returns a response message (or empty if no response needed)
    std::optional<Message> handleMessage(ClientSessionPtr client, const Message& msg);

private:
    StateStore& store_;
    Broadcaster& broadcaster_;
    EventLog& eventLog_;
    AuthManager& auth_;
    bool authEnabled_;

    // Handlers for each message type
    Message handleAuth(ClientSessionPtr client, const Message& msg);
    Message handleSubscribe(ClientSessionPtr client, const Message& msg);
    Message handleUnsubscribe(ClientSessionPtr client, const Message& msg);
    Message handleEventPublish(ClientSessionPtr client, const Message& msg);
    Message handleIncidentCreate(ClientSessionPtr client, const Message& msg);
    Message handleIncidentUpdate(ClientSessionPtr client, const Message& msg);
    Message handleIncidentList(ClientSessionPtr client, const Message& msg);
    Message handleEventList(ClientSessionPtr client, const Message& msg);
    Message handlePing(ClientSessionPtr client, const Message& msg);

    // Check if client is authenticated (generates error response if not)
    bool checkAuth(ClientSessionPtr client, const std::string& requestId, Message& errorOut);

    // Check if client has permission (generates error response if not)
    bool checkPermission(ClientSessionPtr client, const std::string& action, 
                         const std::string& requestId, Message& errorOut);
};

} // namespace opspulse

