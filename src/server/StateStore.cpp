#include "server/StateStore.hpp"
#include <algorithm>

namespace opspulse {

std::string StateStore::generateIncidentId() {
    return "INC-" + std::to_string(++incidentCounter_);
}

// ============ Event Operations ============

void StateStore::addEvent(const Event& event) {
    {
        std::unique_lock<std::shared_mutex> lock(eventMutex_);
        events_[event.id] = event;
        eventsByChannel_[event.channel].push_back(event.id);
        if (event.incident_id) {
            eventsByIncident_[*event.incident_id].push_back(event.id);
        }
    }

    // Invoke callback outside lock
    if (onEventAdded_) {
        onEventAdded_(event);
    }
}

std::optional<Event> StateStore::getEvent(const std::string& id) const {
    std::shared_lock<std::shared_mutex> lock(eventMutex_);
    auto it = events_.find(id);
    if (it != events_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<Event> StateStore::getEventsByChannel(const std::string& channel, size_t limit) const {
    std::shared_lock<std::shared_mutex> lock(eventMutex_);
    std::vector<Event> result;

    auto it = eventsByChannel_.find(channel);
    if (it == eventsByChannel_.end()) {
        return result;
    }

    const auto& ids = it->second;
    size_t count = std::min(limit, ids.size());
    result.reserve(count);

    // Return most recent first (reverse order)
    for (auto rit = ids.rbegin(); rit != ids.rend() && result.size() < limit; ++rit) {
        auto eventIt = events_.find(*rit);
        if (eventIt != events_.end()) {
            result.push_back(eventIt->second);
        }
    }

    return result;
}

std::vector<Event> StateStore::getEventsByIncident(const std::string& incidentId) const {
    std::shared_lock<std::shared_mutex> lock(eventMutex_);
    std::vector<Event> result;

    auto it = eventsByIncident_.find(incidentId);
    if (it == eventsByIncident_.end()) {
        return result;
    }

    const auto& ids = it->second;
    result.reserve(ids.size());

    for (const auto& id : ids) {
        auto eventIt = events_.find(id);
        if (eventIt != events_.end()) {
            result.push_back(eventIt->second);
        }
    }

    return result;
}

std::vector<Event> StateStore::getAllEvents(size_t limit) const {
    std::shared_lock<std::shared_mutex> lock(eventMutex_);
    std::vector<Event> result;
    result.reserve(std::min(limit, events_.size()));

    // Collect all events and sort by timestamp (most recent first)
    for (const auto& [id, event] : events_) {
        result.push_back(event);
    }

    std::sort(result.begin(), result.end(), [](const Event& a, const Event& b) {
        return a.timestamp > b.timestamp;
    });

    if (result.size() > limit) {
        result.resize(limit);
    }

    return result;
}

// ============ Incident Operations ============

std::string StateStore::createIncident(Severity sev, const std::string& title,
                                        const std::string& channel, const std::string& description) {
    Incident incident;
    incident.id = generateIncidentId();
    incident.created_at = nowMs();
    incident.updated_at = incident.created_at;
    incident.severity = sev;
    incident.status = IncidentStatus::OPEN;
    incident.title = title;
    incident.channel = channel;
    incident.description = description;

    {
        std::unique_lock<std::shared_mutex> lock(incidentMutex_);
        incidents_[incident.id] = incident;
        incidentsByStatus_[IncidentStatus::OPEN].insert(incident.id);
        incidentsByChannel_[channel].push_back(incident.id);
    }

    if (onIncidentCreated_) {
        onIncidentCreated_(incident);
    }

    return incident.id;
}

std::optional<Incident> StateStore::getIncident(const std::string& id) const {
    std::shared_lock<std::shared_mutex> lock(incidentMutex_);
    auto it = incidents_.find(id);
    if (it != incidents_.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool StateStore::updateIncidentStatus(const std::string& id, IncidentStatus status,
                                       const std::string& updatedBy) {
    std::string oldStatus, newStatus;
    
    {
        std::unique_lock<std::shared_mutex> lock(incidentMutex_);
        auto it = incidents_.find(id);
        if (it == incidents_.end()) {
            return false;
        }

        Incident& incident = it->second;
        IncidentStatus oldStatusEnum = incident.status;
        oldStatus = statusToString(oldStatusEnum);
        newStatus = statusToString(status);

        if (oldStatusEnum == status) {
            return true;  // No change needed
        }

        // Update status indexes
        incidentsByStatus_[oldStatusEnum].erase(id);
        incidentsByStatus_[status].insert(id);

        incident.status = status;
        incident.updated_at = nowMs();
    }

    if (onIncidentUpdated_) {
        onIncidentUpdated_(id, "status", oldStatus, newStatus, updatedBy);
    }

    return true;
}

bool StateStore::updateIncidentOwner(const std::string& id, const std::string& owner,
                                      const std::string& updatedBy) {
    std::string oldOwner;
    
    {
        std::unique_lock<std::shared_mutex> lock(incidentMutex_);
        auto it = incidents_.find(id);
        if (it == incidents_.end()) {
            return false;
        }

        oldOwner = it->second.owner;
        it->second.owner = owner;
        it->second.updated_at = nowMs();
    }

    if (onIncidentUpdated_) {
        onIncidentUpdated_(id, "owner", oldOwner, owner, updatedBy);
    }

    return true;
}

bool StateStore::updateIncidentSeverity(const std::string& id, Severity sev,
                                         const std::string& updatedBy) {
    std::string oldSev, newSev;
    
    {
        std::unique_lock<std::shared_mutex> lock(incidentMutex_);
        auto it = incidents_.find(id);
        if (it == incidents_.end()) {
            return false;
        }

        oldSev = severityToString(it->second.severity);
        newSev = severityToString(sev);
        it->second.severity = sev;
        it->second.updated_at = nowMs();
    }

    if (onIncidentUpdated_) {
        onIncidentUpdated_(id, "severity", oldSev, newSev, updatedBy);
    }

    return true;
}

bool StateStore::addEventToIncident(const std::string& incidentId, const std::string& eventId) {
    std::unique_lock<std::shared_mutex> lock(incidentMutex_);
    auto it = incidents_.find(incidentId);
    if (it == incidents_.end()) {
        return false;
    }

    it->second.timeline_event_ids.push_back(eventId);
    it->second.updated_at = nowMs();
    return true;
}

std::vector<Incident> StateStore::getIncidents(
    const std::optional<std::string>& channel,
    const std::optional<IncidentStatus>& status,
    size_t limit) const {

    std::shared_lock<std::shared_mutex> lock(incidentMutex_);
    std::vector<Incident> result;

    // If filtering by status, start from that index
    if (status) {
        auto statusIt = incidentsByStatus_.find(*status);
        if (statusIt == incidentsByStatus_.end()) {
            return result;
        }

        for (const auto& id : statusIt->second) {
            auto incIt = incidents_.find(id);
            if (incIt != incidents_.end()) {
                if (!channel || incIt->second.channel == *channel) {
                    result.push_back(incIt->second);
                }
            }
            if (result.size() >= limit) break;
        }
    }
    // If filtering by channel
    else if (channel) {
        auto chanIt = incidentsByChannel_.find(*channel);
        if (chanIt == incidentsByChannel_.end()) {
            return result;
        }

        for (auto rit = chanIt->second.rbegin(); rit != chanIt->second.rend(); ++rit) {
            auto incIt = incidents_.find(*rit);
            if (incIt != incidents_.end()) {
                result.push_back(incIt->second);
            }
            if (result.size() >= limit) break;
        }
    }
    // No filter - return all (most recent first)
    else {
        result.reserve(std::min(limit, incidents_.size()));
        for (const auto& [id, incident] : incidents_) {
            result.push_back(incident);
        }

        std::sort(result.begin(), result.end(), [](const Incident& a, const Incident& b) {
            return a.updated_at > b.updated_at;
        });

        if (result.size() > limit) {
            result.resize(limit);
        }
    }

    return result;
}

std::vector<Incident> StateStore::getOpenIncidents() const {
    return getIncidents(std::nullopt, IncidentStatus::OPEN, 1000);
}

// ============ Stats ============

size_t StateStore::eventCount() const {
    std::shared_lock<std::shared_mutex> lock(eventMutex_);
    return events_.size();
}

size_t StateStore::incidentCount() const {
    std::shared_lock<std::shared_mutex> lock(incidentMutex_);
    return incidents_.size();
}

size_t StateStore::openIncidentCount() const {
    std::shared_lock<std::shared_mutex> lock(incidentMutex_);
    auto it = incidentsByStatus_.find(IncidentStatus::OPEN);
    if (it != incidentsByStatus_.end()) {
        return it->second.size();
    }
    return 0;
}

// ============ Bulk Operations ============

void StateStore::loadEvent(const Event& event) {
    std::unique_lock<std::shared_mutex> lock(eventMutex_);
    events_[event.id] = event;
    eventsByChannel_[event.channel].push_back(event.id);
    if (event.incident_id) {
        eventsByIncident_[*event.incident_id].push_back(event.id);
    }
}

void StateStore::loadIncident(const Incident& incident) {
    std::unique_lock<std::shared_mutex> lock(incidentMutex_);
    incidents_[incident.id] = incident;
    incidentsByStatus_[incident.status].insert(incident.id);
    incidentsByChannel_[incident.channel].push_back(incident.id);

    // Update counter to be higher than loaded ID
    if (incident.id.substr(0, 4) == "INC-") {
        try {
            uint64_t num = std::stoull(incident.id.substr(4));
            uint64_t expected = incidentCounter_.load();
            while (num >= expected && !incidentCounter_.compare_exchange_weak(expected, num + 1)) {
                // Keep trying
            }
        } catch (...) {
            // Ignore parse errors
        }
    }
}

} // namespace opspulse

