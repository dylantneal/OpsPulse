#pragma once

#include "common/Types.hpp"
#include "common/Message.hpp"
#include <string>
#include <fstream>
#include <mutex>
#include <functional>
#include <atomic>

namespace opspulse {

/**
 * Append-only event log for persistence.
 * Writes JSON lines to a file for replay on restart.
 * 
 * Log format (JSON lines):
 * {"type":"event","data":{...},"ts":1234567890}
 * {"type":"incident","data":{...},"ts":1234567890}
 * {"type":"incident_update","id":"INC-1001","field":"status","old":"OPEN","new":"ACKED","by":"user","ts":123}
 */
class EventLog {
public:
    explicit EventLog(const std::string& filepath);
    ~EventLog();

    // Open/create the log file
    bool open();

    // Close the log file
    void close();

    // Check if log is open
    bool isOpen() const { return file_.is_open(); }

    // ============ Write Operations ============

    // Log an event
    void logEvent(const Event& event);

    // Log a new incident
    void logIncident(const Incident& incident);

    // Log an incident update
    void logIncidentUpdate(const std::string& incidentId, 
                            const std::string& field,
                            const std::string& oldValue,
                            const std::string& newValue,
                            const std::string& updatedBy);

    // Flush to disk
    void flush();

    // ============ Replay Operations ============

    // Replay callback types
    using EventCallback = std::function<void(const Event&)>;
    using IncidentCallback = std::function<void(const Incident&)>;
    using IncidentUpdateCallback = std::function<void(const std::string& id,
                                                       const std::string& field,
                                                       const std::string& oldValue,
                                                       const std::string& newValue,
                                                       const std::string& updatedBy)>;

    // Replay the log file and invoke callbacks
    bool replay(EventCallback onEvent,
                IncidentCallback onIncident,
                IncidentUpdateCallback onUpdate);

    // Get stats
    size_t recordCount() const { return recordCount_.load(); }
    size_t byteCount() const { return byteCount_.load(); }

private:
    std::string filepath_;
    std::ofstream file_;
    mutable std::mutex mutex_;
    std::atomic<size_t> recordCount_{0};
    std::atomic<size_t> byteCount_{0};

    void writeRecord(const std::string& json);
};

} // namespace opspulse

