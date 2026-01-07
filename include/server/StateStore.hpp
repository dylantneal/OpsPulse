#pragma once

#include "common/Types.hpp"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <shared_mutex>
#include <optional>
#include <functional>

namespace opspulse {

/**
 * Thread-safe in-memory state store for events and incidents.
 * Uses reader-writer locks for concurrent read access.
 */
class StateStore {
public:
    StateStore() = default;

    // ============ Event Operations ============

    // Add a new event
    void addEvent(const Event& event);

    // Get event by ID
    std::optional<Event> getEvent(const std::string& id) const;

    // Get events by channel (most recent first)
    std::vector<Event> getEventsByChannel(const std::string& channel, size_t limit = 100) const;

    // Get events linked to an incident
    std::vector<Event> getEventsByIncident(const std::string& incidentId) const;

    // Get all events (limited)
    std::vector<Event> getAllEvents(size_t limit = 100) const;

    // ============ Incident Operations ============

    // Create a new incident
    std::string createIncident(Severity sev, const std::string& title,
                                const std::string& channel, const std::string& description);

    // Get incident by ID
    std::optional<Incident> getIncident(const std::string& id) const;

    // Update incident status
    bool updateIncidentStatus(const std::string& id, IncidentStatus status, 
                               const std::string& updatedBy);

    // Update incident owner
    bool updateIncidentOwner(const std::string& id, const std::string& owner,
                              const std::string& updatedBy);

    // Update incident severity
    bool updateIncidentSeverity(const std::string& id, Severity sev,
                                 const std::string& updatedBy);

    // Add event to incident timeline
    bool addEventToIncident(const std::string& incidentId, const std::string& eventId);

    // Get incidents by filter
    std::vector<Incident> getIncidents(
        const std::optional<std::string>& channel = std::nullopt,
        const std::optional<IncidentStatus>& status = std::nullopt,
        size_t limit = 100) const;

    // Get all open incidents
    std::vector<Incident> getOpenIncidents() const;

    // ============ Stats ============

    size_t eventCount() const;
    size_t incidentCount() const;
    size_t openIncidentCount() const;

    // ============ Callbacks for State Changes ============

    using EventCallback = std::function<void(const Event&)>;
    using IncidentCallback = std::function<void(const Incident&)>;
    using IncidentUpdateCallback = std::function<void(const std::string& id, 
                                                       const std::string& field,
                                                       const std::string& oldValue,
                                                       const std::string& newValue,
                                                       const std::string& updatedBy)>;

    void setOnEventAdded(EventCallback cb) { onEventAdded_ = std::move(cb); }
    void setOnIncidentCreated(IncidentCallback cb) { onIncidentCreated_ = std::move(cb); }
    void setOnIncidentUpdated(IncidentUpdateCallback cb) { onIncidentUpdated_ = std::move(cb); }

    // ============ Bulk Operations (for replay) ============

    void loadEvent(const Event& event);
    void loadIncident(const Incident& incident);
    void setIncidentCounter(uint64_t counter) { incidentCounter_ = counter; }

private:
    mutable std::shared_mutex eventMutex_;
    mutable std::shared_mutex incidentMutex_;

    // Events indexed by ID
    std::unordered_map<std::string, Event> events_;
    // Events indexed by channel (just IDs for quick lookup)
    std::unordered_map<std::string, std::vector<std::string>> eventsByChannel_;
    // Events indexed by incident
    std::unordered_map<std::string, std::vector<std::string>> eventsByIncident_;

    // Incidents indexed by ID
    std::unordered_map<std::string, Incident> incidents_;
    // Incident IDs by status
    std::unordered_map<IncidentStatus, std::unordered_set<std::string>> incidentsByStatus_;
    // Incident IDs by channel
    std::unordered_map<std::string, std::vector<std::string>> incidentsByChannel_;

    // Incident ID counter
    std::atomic<uint64_t> incidentCounter_{1000};

    // Callbacks
    EventCallback onEventAdded_;
    IncidentCallback onIncidentCreated_;
    IncidentUpdateCallback onIncidentUpdated_;

    std::string generateIncidentId();
};

} // namespace opspulse

