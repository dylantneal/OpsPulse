#include "common/Message.hpp"
#include <stdexcept>

namespace opspulse {

std::string messageTypeToString(MessageType type) {
    switch (type) {
        case MessageType::AUTH: return "auth";
        case MessageType::SUBSCRIBE: return "subscribe";
        case MessageType::UNSUBSCRIBE: return "unsubscribe";
        case MessageType::EVENT_PUBLISH: return "event";
        case MessageType::INCIDENT_CREATE: return "incident_create";
        case MessageType::INCIDENT_UPDATE: return "incident_update";
        case MessageType::INCIDENT_LIST: return "incident_list";
        case MessageType::EVENT_LIST: return "event_list";
        case MessageType::PING: return "ping";
        case MessageType::AUTH_RESPONSE: return "auth_response";
        case MessageType::ACK: return "ack";
        case MessageType::ERROR: return "error";
        case MessageType::PUSH_EVENT: return "push_event";
        case MessageType::PUSH_INCIDENT: return "push_incident";
        case MessageType::PUSH_INCIDENT_UPDATE: return "push_incident_update";
        case MessageType::INCIDENT_LIST_RESPONSE: return "incident_list_response";
        case MessageType::EVENT_LIST_RESPONSE: return "event_list_response";
        case MessageType::PONG: return "pong";
        default: return "unknown";
    }
}

MessageType stringToMessageType(const std::string& s) {
    if (s == "auth") return MessageType::AUTH;
    if (s == "subscribe") return MessageType::SUBSCRIBE;
    if (s == "unsubscribe") return MessageType::UNSUBSCRIBE;
    if (s == "event") return MessageType::EVENT_PUBLISH;
    if (s == "incident_create") return MessageType::INCIDENT_CREATE;
    if (s == "incident_update") return MessageType::INCIDENT_UPDATE;
    if (s == "incident_list") return MessageType::INCIDENT_LIST;
    if (s == "event_list") return MessageType::EVENT_LIST;
    if (s == "ping") return MessageType::PING;
    if (s == "auth_response") return MessageType::AUTH_RESPONSE;
    if (s == "ack") return MessageType::ACK;
    if (s == "error") return MessageType::ERROR;
    if (s == "push_event") return MessageType::PUSH_EVENT;
    if (s == "push_incident") return MessageType::PUSH_INCIDENT;
    if (s == "push_incident_update") return MessageType::PUSH_INCIDENT_UPDATE;
    if (s == "incident_list_response") return MessageType::INCIDENT_LIST_RESPONSE;
    if (s == "event_list_response") return MessageType::EVENT_LIST_RESPONSE;
    if (s == "pong") return MessageType::PONG;
    return MessageType::UNKNOWN;
}

std::string Message::serialize() const {
    json j;
    j["type"] = messageTypeToString(type);
    if (!requestId.empty()) {
        j["request_id"] = requestId;
    }
    if (!payload.is_null()) {
        j["payload"] = payload;
    }
    return j.dump();
}

Message Message::parse(const std::string& data) {
    try {
        json j = json::parse(data);
        Message msg;

        if (!j.contains("type")) {
            throw std::runtime_error("Missing 'type' field");
        }

        msg.type = stringToMessageType(j["type"].get<std::string>());

        if (j.contains("request_id")) {
            msg.requestId = j["request_id"].get<std::string>();
        }

        if (j.contains("payload")) {
            msg.payload = j["payload"];
        }

        return msg;
    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("JSON parse error: ") + e.what());
    }
}

bool Message::validate() const {
    // Basic validation - ensure type is known
    return type != MessageType::UNKNOWN;
}

// ============ Request Message Implementations ============

AuthRequest AuthRequest::fromJson(const json& j) {
    AuthRequest req;
    req.username = j.value("user", "");
    req.token = j.value("token", "");
    return req;
}

json AuthRequest::toJson() const {
    return {{"user", username}, {"token", token}};
}

SubscribeRequest SubscribeRequest::fromJson(const json& j) {
    SubscribeRequest req;
    if (j.contains("channels") && j["channels"].is_array()) {
        req.channels = j["channels"].get<std::vector<std::string>>();
    } else if (j.contains("channel")) {
        req.channels.push_back(j["channel"].get<std::string>());
    }
    return req;
}

json SubscribeRequest::toJson() const {
    return {{"channels", channels}};
}

UnsubscribeRequest UnsubscribeRequest::fromJson(const json& j) {
    UnsubscribeRequest req;
    if (j.contains("channels") && j["channels"].is_array()) {
        req.channels = j["channels"].get<std::vector<std::string>>();
    } else if (j.contains("channel")) {
        req.channels.push_back(j["channel"].get<std::string>());
    }
    return req;
}

json UnsubscribeRequest::toJson() const {
    return {{"channels", channels}};
}

EventPublishRequest EventPublishRequest::fromJson(const json& j) {
    EventPublishRequest req;
    req.channel = j.value("channel", "");
    req.level = j.value("level", "info");
    req.message = j.value("msg", j.value("message", ""));
    if (j.contains("tags") && j["tags"].is_array()) {
        req.tags = j["tags"].get<std::vector<std::string>>();
    }
    return req;
}

json EventPublishRequest::toJson() const {
    return {
        {"channel", channel},
        {"level", level},
        {"msg", message},
        {"tags", tags}
    };
}

IncidentCreateRequest IncidentCreateRequest::fromJson(const json& j) {
    IncidentCreateRequest req;
    req.severity = j.value("sev", j.value("severity", 3));
    req.title = j.value("title", "");
    req.channel = j.value("channel", "");
    req.description = j.value("description", "");
    return req;
}

json IncidentCreateRequest::toJson() const {
    return {
        {"sev", severity},
        {"title", title},
        {"channel", channel},
        {"description", description}
    };
}

IncidentUpdateRequest IncidentUpdateRequest::fromJson(const json& j) {
    IncidentUpdateRequest req;
    req.incidentId = j.value("id", "");
    if (j.contains("status")) req.status = j["status"].get<std::string>();
    if (j.contains("owner")) req.owner = j["owner"].get<std::string>();
    if (j.contains("sev") || j.contains("severity")) {
        req.severity = j.value("sev", j.value("severity", 0));
    }
    if (j.contains("comment")) req.comment = j["comment"].get<std::string>();
    return req;
}

json IncidentUpdateRequest::toJson() const {
    json j = {{"id", incidentId}};
    if (status) j["status"] = *status;
    if (owner) j["owner"] = *owner;
    if (severity) j["sev"] = *severity;
    if (comment) j["comment"] = *comment;
    return j;
}

IncidentListRequest IncidentListRequest::fromJson(const json& j) {
    IncidentListRequest req;
    if (j.contains("channel")) req.channel = j["channel"].get<std::string>();
    if (j.contains("status")) req.status = j["status"].get<std::string>();
    if (j.contains("limit")) req.limit = j["limit"].get<int>();
    return req;
}

json IncidentListRequest::toJson() const {
    json j;
    if (channel) j["channel"] = *channel;
    if (status) j["status"] = *status;
    if (limit) j["limit"] = *limit;
    return j;
}

EventListRequest EventListRequest::fromJson(const json& j) {
    EventListRequest req;
    if (j.contains("channel")) req.channel = j["channel"].get<std::string>();
    if (j.contains("incident_id")) req.incidentId = j["incident_id"].get<std::string>();
    if (j.contains("limit")) req.limit = j["limit"].get<int>();
    return req;
}

json EventListRequest::toJson() const {
    json j;
    if (channel) j["channel"] = *channel;
    if (incidentId) j["incident_id"] = *incidentId;
    if (limit) j["limit"] = *limit;
    return j;
}

// ============ Response Message Implementations ============

json AuthResponse::toJson() const {
    json j = {
        {"success", success},
        {"message", message}
    };
    if (!sessionId.empty()) {
        j["session_id"] = sessionId;
    }
    return j;
}

json AckResponse::toJson() const {
    json j = {{"message", message}};
    if (!requestId.empty()) j["request_id"] = requestId;
    if (!resourceId.empty()) j["resource_id"] = resourceId;
    return j;
}

json ErrorResponse::toJson() const {
    return {
        {"request_id", requestId},
        {"code", code},
        {"message", message}
    };
}

// ============ Push Message Implementations ============

json PushEvent::toJson() const {
    json j;
    to_json(j, event);
    return j;
}

json PushIncident::toJson() const {
    json j;
    to_json(j, incident);
    return j;
}

json PushIncidentUpdate::toJson() const {
    return {
        {"id", incidentId},
        {"field", field},
        {"old_value", oldValue},
        {"new_value", newValue},
        {"updated_by", updatedBy},
        {"timestamp", timestamp}
    };
}

// ============ List Response Implementations ============

json IncidentListResponse::toJson() const {
    json arr = json::array();
    for (const auto& inc : incidents) {
        json j;
        to_json(j, inc);
        arr.push_back(j);
    }
    return {{"incidents", arr}, {"total", total}};
}

json EventListResponse::toJson() const {
    json arr = json::array();
    for (const auto& ev : events) {
        json j;
        to_json(j, ev);
        arr.push_back(j);
    }
    return {{"events", arr}, {"total", total}};
}

// ============ Message Builder Implementations ============

Message MessageBuilder::authSuccess(const std::string& sessionId) {
    Message msg;
    msg.type = MessageType::AUTH_RESPONSE;
    msg.payload = AuthResponse{true, "Authenticated successfully", sessionId}.toJson();
    return msg;
}

Message MessageBuilder::authFailure(const std::string& reason) {
    Message msg;
    msg.type = MessageType::AUTH_RESPONSE;
    msg.payload = AuthResponse{false, reason, ""}.toJson();
    return msg;
}

Message MessageBuilder::ack(const std::string& requestId, const std::string& resourceId, const std::string& msgText) {
    Message msg;
    msg.type = MessageType::ACK;
    msg.requestId = requestId;
    msg.payload = AckResponse{requestId, resourceId, msgText}.toJson();
    return msg;
}

Message MessageBuilder::error(const std::string& requestId, int code, const std::string& msgText) {
    Message msg;
    msg.type = MessageType::ERROR;
    msg.requestId = requestId;
    msg.payload = ErrorResponse{requestId, code, msgText}.toJson();
    return msg;
}

Message MessageBuilder::pushEvent(const Event& event) {
    Message msg;
    msg.type = MessageType::PUSH_EVENT;
    msg.payload = PushEvent{event}.toJson();
    return msg;
}

Message MessageBuilder::pushIncident(const Incident& incident) {
    Message msg;
    msg.type = MessageType::PUSH_INCIDENT;
    msg.payload = PushIncident{incident}.toJson();
    return msg;
}

Message MessageBuilder::pushIncidentUpdate(const std::string& incidentId, const std::string& field,
                                            const std::string& oldVal, const std::string& newVal,
                                            const std::string& updatedBy) {
    Message msg;
    msg.type = MessageType::PUSH_INCIDENT_UPDATE;
    msg.payload = PushIncidentUpdate{incidentId, field, oldVal, newVal, updatedBy, nowMs()}.toJson();
    return msg;
}

Message MessageBuilder::incidentList(const std::vector<Incident>& incidents, int total) {
    Message msg;
    msg.type = MessageType::INCIDENT_LIST_RESPONSE;
    msg.payload = IncidentListResponse{incidents, total}.toJson();
    return msg;
}

Message MessageBuilder::eventList(const std::vector<Event>& events, int total) {
    Message msg;
    msg.type = MessageType::EVENT_LIST_RESPONSE;
    msg.payload = EventListResponse{events, total}.toJson();
    return msg;
}

Message MessageBuilder::pong() {
    Message msg;
    msg.type = MessageType::PONG;
    return msg;
}

// ============ JSON Conversion for Types ============

void to_json(json& j, const Event& e) {
    j = {
        {"id", e.id},
        {"timestamp", e.timestamp},
        {"channel", e.channel},
        {"level", levelToString(e.level)},
        {"message", e.message},
        {"tags", e.tags}
    };
    if (e.incident_id) {
        j["incident_id"] = *e.incident_id;
    }
}

void from_json(const json& j, Event& e) {
    e.id = j.value("id", "");
    e.timestamp = j.value("timestamp", static_cast<uint64_t>(0));
    e.channel = j.value("channel", "");
    e.level = stringToLevel(j.value("level", "info"));
    e.message = j.value("message", "");
    if (j.contains("tags") && j["tags"].is_array()) {
        e.tags = j["tags"].get<std::vector<std::string>>();
    }
    if (j.contains("incident_id")) {
        e.incident_id = j["incident_id"].get<std::string>();
    }
}

void to_json(json& j, const Incident& i) {
    j = {
        {"id", i.id},
        {"created_at", i.created_at},
        {"updated_at", i.updated_at},
        {"severity", static_cast<int>(i.severity)},
        {"status", statusToString(i.status)},
        {"owner", i.owner},
        {"title", i.title},
        {"channel", i.channel},
        {"description", i.description},
        {"timeline_event_ids", i.timeline_event_ids}
    };
}

void from_json(const json& j, Incident& i) {
    i.id = j.value("id", "");
    i.created_at = j.value("created_at", static_cast<uint64_t>(0));
    i.updated_at = j.value("updated_at", static_cast<uint64_t>(0));
    i.severity = stringToSeverity(j.value("severity", 3));
    i.status = stringToStatus(j.value("status", "OPEN"));
    i.owner = j.value("owner", "");
    i.title = j.value("title", "");
    i.channel = j.value("channel", "");
    i.description = j.value("description", "");
    if (j.contains("timeline_event_ids") && j["timeline_event_ids"].is_array()) {
        i.timeline_event_ids = j["timeline_event_ids"].get<std::vector<std::string>>();
    }
}

} // namespace opspulse

