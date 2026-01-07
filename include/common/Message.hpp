#pragma once

#include "Types.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <optional>
#include <variant>
#include <vector>

namespace opspulse {

using json = nlohmann::json;

// Message types
enum class MessageType {
    // Client -> Server
    AUTH,
    SUBSCRIBE,
    UNSUBSCRIBE,
    EVENT_PUBLISH,
    INCIDENT_CREATE,
    INCIDENT_UPDATE,
    INCIDENT_LIST,
    EVENT_LIST,
    PING,

    // Server -> Client
    AUTH_RESPONSE,
    ACK,
    ERROR,
    PUSH_EVENT,
    PUSH_INCIDENT,
    PUSH_INCIDENT_UPDATE,
    INCIDENT_LIST_RESPONSE,
    EVENT_LIST_RESPONSE,
    PONG,

    UNKNOWN
};

std::string messageTypeToString(MessageType type);
MessageType stringToMessageType(const std::string& s);

// Base message structure
struct Message {
    MessageType type = MessageType::UNKNOWN;
    std::string requestId;  // Optional correlation ID
    json payload;

    // Serialize to JSON string
    std::string serialize() const;

    // Parse from JSON string (throws on error)
    static Message parse(const std::string& data);

    // Validate message has required fields for its type
    bool validate() const;
};

// ============ Request Messages ============

struct AuthRequest {
    std::string username;
    std::string token;

    static AuthRequest fromJson(const json& j);
    json toJson() const;
};

struct SubscribeRequest {
    std::vector<std::string> channels;

    static SubscribeRequest fromJson(const json& j);
    json toJson() const;
};

struct UnsubscribeRequest {
    std::vector<std::string> channels;

    static UnsubscribeRequest fromJson(const json& j);
    json toJson() const;
};

struct EventPublishRequest {
    std::string channel;
    std::string level;
    std::string message;
    std::vector<std::string> tags;

    static EventPublishRequest fromJson(const json& j);
    json toJson() const;
};

struct IncidentCreateRequest {
    int severity;
    std::string title;
    std::string channel;
    std::string description;

    static IncidentCreateRequest fromJson(const json& j);
    json toJson() const;
};

struct IncidentUpdateRequest {
    std::string incidentId;
    std::optional<std::string> status;
    std::optional<std::string> owner;
    std::optional<int> severity;
    std::optional<std::string> comment;

    static IncidentUpdateRequest fromJson(const json& j);
    json toJson() const;
};

struct IncidentListRequest {
    std::optional<std::string> channel;
    std::optional<std::string> status;
    std::optional<int> limit;

    static IncidentListRequest fromJson(const json& j);
    json toJson() const;
};

struct EventListRequest {
    std::optional<std::string> channel;
    std::optional<std::string> incidentId;
    std::optional<int> limit;

    static EventListRequest fromJson(const json& j);
    json toJson() const;
};

// ============ Response Messages ============

struct AuthResponse {
    bool success;
    std::string message;
    std::string sessionId;

    json toJson() const;
};

struct AckResponse {
    std::string requestId;
    std::string resourceId;  // ID of created/updated resource
    std::string message;

    json toJson() const;
};

struct ErrorResponse {
    std::string requestId;
    int code;
    std::string message;

    json toJson() const;
};

// ============ Push Messages ============

struct PushEvent {
    Event event;

    json toJson() const;
};

struct PushIncident {
    Incident incident;

    json toJson() const;
};

struct PushIncidentUpdate {
    std::string incidentId;
    std::string field;
    std::string oldValue;
    std::string newValue;
    std::string updatedBy;
    uint64_t timestamp;

    json toJson() const;
};

// ============ List Responses ============

struct IncidentListResponse {
    std::vector<Incident> incidents;
    int total;

    json toJson() const;
};

struct EventListResponse {
    std::vector<Event> events;
    int total;

    json toJson() const;
};

// ============ Message Builder Utilities ============

class MessageBuilder {
public:
    // Server response builders
    static Message authSuccess(const std::string& sessionId);
    static Message authFailure(const std::string& reason);
    static Message ack(const std::string& requestId, const std::string& resourceId, const std::string& msg = "OK");
    static Message error(const std::string& requestId, int code, const std::string& msg);

    // Push message builders
    static Message pushEvent(const Event& event);
    static Message pushIncident(const Incident& incident);
    static Message pushIncidentUpdate(const std::string& incidentId, const std::string& field,
                                       const std::string& oldVal, const std::string& newVal,
                                       const std::string& updatedBy);

    // List response builders
    static Message incidentList(const std::vector<Incident>& incidents, int total);
    static Message eventList(const std::vector<Event>& events, int total);

    static Message pong();
};

// JSON conversion helpers for types
void to_json(json& j, const Event& e);
void from_json(const json& j, Event& e);
void to_json(json& j, const Incident& i);
void from_json(const json& j, Incident& i);

} // namespace opspulse

