#include "server/RequestHandler.hpp"
#include <iostream>

namespace opspulse {

RequestHandler::RequestHandler(StateStore& store, Broadcaster& broadcaster,
                                EventLog& eventLog, AuthManager& auth, bool authEnabled)
    : store_(store)
    , broadcaster_(broadcaster)
    , eventLog_(eventLog)
    , auth_(auth)
    , authEnabled_(authEnabled)
{
}

std::optional<Message> RequestHandler::handleMessage(ClientSessionPtr client, const Message& msg) {
    switch (msg.type) {
        case MessageType::AUTH:
            return handleAuth(client, msg);
        case MessageType::SUBSCRIBE:
            return handleSubscribe(client, msg);
        case MessageType::UNSUBSCRIBE:
            return handleUnsubscribe(client, msg);
        case MessageType::EVENT_PUBLISH:
            return handleEventPublish(client, msg);
        case MessageType::INCIDENT_CREATE:
            return handleIncidentCreate(client, msg);
        case MessageType::INCIDENT_UPDATE:
            return handleIncidentUpdate(client, msg);
        case MessageType::INCIDENT_LIST:
            return handleIncidentList(client, msg);
        case MessageType::EVENT_LIST:
            return handleEventList(client, msg);
        case MessageType::PING:
            return handlePing(client, msg);
        default:
            return MessageBuilder::error(msg.requestId, 400, "Unknown message type");
    }
}

bool RequestHandler::checkAuth(ClientSessionPtr client, const std::string& requestId, Message& errorOut) {
    if (!authEnabled_) {
        return true;
    }
    
    if (!client->isAuthenticated()) {
        errorOut = MessageBuilder::error(requestId, 401, "Not authenticated");
        return false;
    }
    return true;
}

bool RequestHandler::checkPermission(ClientSessionPtr client, const std::string& action,
                                      const std::string& requestId, Message& errorOut) {
    if (!authEnabled_) {
        return true;
    }

    std::string username = client->getUsername();
    bool allowed = false;

    if (action == "publish_event") {
        allowed = auth_.canPublishEvent(username);
    } else if (action == "create_incident") {
        allowed = auth_.canCreateIncident(username);
    } else if (action == "update_incident") {
        allowed = auth_.canUpdateIncident(username);
    } else {
        allowed = true;  // Read operations allowed for all
    }

    if (!allowed) {
        errorOut = MessageBuilder::error(requestId, 403, "Permission denied for: " + action);
        return false;
    }
    return true;
}

Message RequestHandler::handleAuth(ClientSessionPtr client, const Message& msg) {
    try {
        AuthRequest req = AuthRequest::fromJson(msg.payload);

        if (req.username.empty() || req.token.empty()) {
            return MessageBuilder::authFailure("Username and token required");
        }

        auto result = auth_.authenticate(req.username, req.token);
        if (!result) {
            return MessageBuilder::authFailure("Invalid credentials");
        }

        // Generate session ID and mark client authenticated
        std::string sessionId = std::to_string(nowMs());
        client->setAuthenticated(req.username, sessionId);

        std::cout << "[Auth] User authenticated: " << req.username 
                  << " from " << client->getRemoteAddr() << std::endl;

        return MessageBuilder::authSuccess(sessionId);
    }
    catch (const std::exception& e) {
        return MessageBuilder::authFailure(std::string("Auth error: ") + e.what());
    }
}

Message RequestHandler::handleSubscribe(ClientSessionPtr client, const Message& msg) {
    Message errorResp;
    if (!checkAuth(client, msg.requestId, errorResp)) {
        return errorResp;
    }

    try {
        SubscribeRequest req = SubscribeRequest::fromJson(msg.payload);

        for (const auto& channel : req.channels) {
            client->subscribe(channel);
            broadcaster_.updateSubscription(client->getId(), channel, true);
            std::cout << "[Subscribe] " << client->getUsername() 
                      << " subscribed to: " << channel << std::endl;
        }

        return MessageBuilder::ack(msg.requestId, "", "Subscribed to " + 
                                    std::to_string(req.channels.size()) + " channel(s)");
    }
    catch (const std::exception& e) {
        return MessageBuilder::error(msg.requestId, 400, e.what());
    }
}

Message RequestHandler::handleUnsubscribe(ClientSessionPtr client, const Message& msg) {
    Message errorResp;
    if (!checkAuth(client, msg.requestId, errorResp)) {
        return errorResp;
    }

    try {
        UnsubscribeRequest req = UnsubscribeRequest::fromJson(msg.payload);

        for (const auto& channel : req.channels) {
            client->unsubscribe(channel);
            broadcaster_.updateSubscription(client->getId(), channel, false);
        }

        return MessageBuilder::ack(msg.requestId, "", "Unsubscribed from " +
                                    std::to_string(req.channels.size()) + " channel(s)");
    }
    catch (const std::exception& e) {
        return MessageBuilder::error(msg.requestId, 400, e.what());
    }
}

Message RequestHandler::handleEventPublish(ClientSessionPtr client, const Message& msg) {
    Message errorResp;
    if (!checkAuth(client, msg.requestId, errorResp)) {
        return errorResp;
    }
    if (!checkPermission(client, "publish_event", msg.requestId, errorResp)) {
        return errorResp;
    }

    try {
        EventPublishRequest req = EventPublishRequest::fromJson(msg.payload);

        if (req.channel.empty()) {
            return MessageBuilder::error(msg.requestId, 400, "Channel required");
        }

        // Create event
        Event event;
        event.id = IdGenerator::generateEventId();
        event.timestamp = nowMs();
        event.channel = req.channel;
        event.level = stringToLevel(req.level);
        event.message = req.message;
        event.tags = req.tags;

        // Store and log
        store_.addEvent(event);
        eventLog_.logEvent(event);

        // Broadcast to subscribers
        broadcaster_.broadcastToChannel(req.channel, MessageBuilder::pushEvent(event));

        std::cout << "[Event] " << levelToString(event.level) << " on " << req.channel 
                  << ": " << req.message << std::endl;

        return MessageBuilder::ack(msg.requestId, event.id, "Event published");
    }
    catch (const std::exception& e) {
        return MessageBuilder::error(msg.requestId, 400, e.what());
    }
}

Message RequestHandler::handleIncidentCreate(ClientSessionPtr client, const Message& msg) {
    Message errorResp;
    if (!checkAuth(client, msg.requestId, errorResp)) {
        return errorResp;
    }
    if (!checkPermission(client, "create_incident", msg.requestId, errorResp)) {
        return errorResp;
    }

    try {
        IncidentCreateRequest req = IncidentCreateRequest::fromJson(msg.payload);

        if (req.title.empty()) {
            return MessageBuilder::error(msg.requestId, 400, "Title required");
        }
        if (req.channel.empty()) {
            return MessageBuilder::error(msg.requestId, 400, "Channel required");
        }

        // Create incident
        Severity sev = stringToSeverity(req.severity);
        std::string incidentId = store_.createIncident(sev, req.title, req.channel, req.description);

        // Get the created incident for logging and broadcast
        auto incident = store_.getIncident(incidentId);
        if (incident) {
            eventLog_.logIncident(*incident);
            broadcaster_.broadcastToChannel(req.channel, MessageBuilder::pushIncident(*incident));
        }

        std::cout << "[Incident] Created " << incidentId << " (" << severityToString(sev) 
                  << "): " << req.title << std::endl;

        return MessageBuilder::ack(msg.requestId, incidentId, "Incident created");
    }
    catch (const std::exception& e) {
        return MessageBuilder::error(msg.requestId, 400, e.what());
    }
}

Message RequestHandler::handleIncidentUpdate(ClientSessionPtr client, const Message& msg) {
    Message errorResp;
    if (!checkAuth(client, msg.requestId, errorResp)) {
        return errorResp;
    }
    if (!checkPermission(client, "update_incident", msg.requestId, errorResp)) {
        return errorResp;
    }

    try {
        IncidentUpdateRequest req = IncidentUpdateRequest::fromJson(msg.payload);

        if (req.incidentId.empty()) {
            return MessageBuilder::error(msg.requestId, 400, "Incident ID required");
        }

        // Check incident exists
        auto incident = store_.getIncident(req.incidentId);
        if (!incident) {
            return MessageBuilder::error(msg.requestId, 404, "Incident not found");
        }

        std::string username = client->getUsername();
        std::string channel = incident->channel;

        // Apply updates
        if (req.status) {
            IncidentStatus newStatus = stringToStatus(*req.status);
            store_.updateIncidentStatus(req.incidentId, newStatus, username);
        }

        if (req.owner) {
            store_.updateIncidentOwner(req.incidentId, *req.owner, username);
        }

        if (req.severity) {
            Severity newSev = stringToSeverity(*req.severity);
            store_.updateIncidentSeverity(req.incidentId, newSev, username);
        }

        // If there's a comment, add it as an event linked to the incident
        if (req.comment && !req.comment->empty()) {
            Event commentEvent;
            commentEvent.id = IdGenerator::generateEventId();
            commentEvent.timestamp = nowMs();
            commentEvent.channel = channel;
            commentEvent.level = LogLevel::INFO;
            commentEvent.message = "Comment from " + username + ": " + *req.comment;
            commentEvent.incident_id = req.incidentId;

            store_.addEvent(commentEvent);
            store_.addEventToIncident(req.incidentId, commentEvent.id);
            eventLog_.logEvent(commentEvent);
        }

        std::cout << "[Incident] Updated " << req.incidentId << " by " << username << std::endl;

        return MessageBuilder::ack(msg.requestId, req.incidentId, "Incident updated");
    }
    catch (const std::exception& e) {
        return MessageBuilder::error(msg.requestId, 400, e.what());
    }
}

Message RequestHandler::handleIncidentList(ClientSessionPtr client, const Message& msg) {
    Message errorResp;
    if (!checkAuth(client, msg.requestId, errorResp)) {
        return errorResp;
    }

    try {
        IncidentListRequest req = IncidentListRequest::fromJson(msg.payload);

        std::optional<IncidentStatus> statusFilter;
        if (req.status) {
            statusFilter = stringToStatus(*req.status);
        }

        size_t limit = req.limit.value_or(50);
        auto incidents = store_.getIncidents(req.channel, statusFilter, limit);

        return MessageBuilder::incidentList(incidents, static_cast<int>(incidents.size()));
    }
    catch (const std::exception& e) {
        return MessageBuilder::error(msg.requestId, 400, e.what());
    }
}

Message RequestHandler::handleEventList(ClientSessionPtr client, const Message& msg) {
    Message errorResp;
    if (!checkAuth(client, msg.requestId, errorResp)) {
        return errorResp;
    }

    try {
        EventListRequest req = EventListRequest::fromJson(msg.payload);

        std::vector<Event> events;
        size_t limit = req.limit.value_or(50);

        if (req.incidentId) {
            events = store_.getEventsByIncident(*req.incidentId);
        } else if (req.channel) {
            events = store_.getEventsByChannel(*req.channel, limit);
        } else {
            events = store_.getAllEvents(limit);
        }

        return MessageBuilder::eventList(events, static_cast<int>(events.size()));
    }
    catch (const std::exception& e) {
        return MessageBuilder::error(msg.requestId, 400, e.what());
    }
}

Message RequestHandler::handlePing(ClientSessionPtr client, const Message& msg) {
    (void)client;  // Unused
    (void)msg;     // Unused
    return MessageBuilder::pong();
}

} // namespace opspulse

