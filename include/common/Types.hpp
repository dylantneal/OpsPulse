#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <cstdint>
#include <atomic>
#include <optional>

namespace opspulse {

// Timestamp utilities
using Timestamp = std::chrono::system_clock::time_point;
using Duration = std::chrono::milliseconds;

inline uint64_t timestampToMs(Timestamp ts) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        ts.time_since_epoch()).count();
}

inline Timestamp msToTimestamp(uint64_t ms) {
    return Timestamp(std::chrono::milliseconds(ms));
}

inline uint64_t nowMs() {
    return timestampToMs(std::chrono::system_clock::now());
}

// Severity levels for incidents (1 = critical, 5 = minor)
enum class Severity : uint8_t {
    SEV1 = 1,  // Critical - immediate response required
    SEV2 = 2,  // High - major impact
    SEV3 = 3,  // Medium - moderate impact
    SEV4 = 4,  // Low - minor impact
    SEV5 = 5   // Informational
};

inline std::string severityToString(Severity sev) {
    switch (sev) {
        case Severity::SEV1: return "SEV1";
        case Severity::SEV2: return "SEV2";
        case Severity::SEV3: return "SEV3";
        case Severity::SEV4: return "SEV4";
        case Severity::SEV5: return "SEV5";
        default: return "UNKNOWN";
    }
}

inline Severity stringToSeverity(int sev) {
    if (sev < 1) sev = 1;
    if (sev > 5) sev = 5;
    return static_cast<Severity>(sev);
}

// Incident status
enum class IncidentStatus : uint8_t {
    OPEN,
    ACKNOWLEDGED,
    INVESTIGATING,
    MITIGATED,
    RESOLVED,
    CLOSED
};

inline std::string statusToString(IncidentStatus status) {
    switch (status) {
        case IncidentStatus::OPEN: return "OPEN";
        case IncidentStatus::ACKNOWLEDGED: return "ACKED";
        case IncidentStatus::INVESTIGATING: return "INVESTIGATING";
        case IncidentStatus::MITIGATED: return "MITIGATED";
        case IncidentStatus::RESOLVED: return "RESOLVED";
        case IncidentStatus::CLOSED: return "CLOSED";
        default: return "UNKNOWN";
    }
}

inline IncidentStatus stringToStatus(const std::string& s) {
    if (s == "OPEN") return IncidentStatus::OPEN;
    if (s == "ACKED" || s == "ACKNOWLEDGED") return IncidentStatus::ACKNOWLEDGED;
    if (s == "INVESTIGATING") return IncidentStatus::INVESTIGATING;
    if (s == "MITIGATED") return IncidentStatus::MITIGATED;
    if (s == "RESOLVED") return IncidentStatus::RESOLVED;
    if (s == "CLOSED") return IncidentStatus::CLOSED;
    return IncidentStatus::OPEN;
}

// Event log levels
enum class LogLevel : uint8_t {
    DEBUG,
    INFO,
    WARN,
    ERROR,
    CRITICAL
};

inline std::string levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "debug";
        case LogLevel::INFO: return "info";
        case LogLevel::WARN: return "warn";
        case LogLevel::ERROR: return "error";
        case LogLevel::CRITICAL: return "critical";
        default: return "info";
    }
}

inline LogLevel stringToLevel(const std::string& s) {
    if (s == "debug") return LogLevel::DEBUG;
    if (s == "info") return LogLevel::INFO;
    if (s == "warn") return LogLevel::WARN;
    if (s == "error") return LogLevel::ERROR;
    if (s == "critical") return LogLevel::CRITICAL;
    return LogLevel::INFO;
}

// Event data structure
struct Event {
    std::string id;
    uint64_t timestamp;
    std::string channel;
    LogLevel level;
    std::string message;
    std::vector<std::string> tags;
    std::optional<std::string> incident_id;  // If linked to an incident
};

// Incident data structure
struct Incident {
    std::string id;              // INC-XXXXX format
    uint64_t created_at;
    uint64_t updated_at;
    Severity severity;
    IncidentStatus status;
    std::string owner;
    std::string title;
    std::string channel;
    std::string description;
    std::vector<std::string> timeline_event_ids;
};

// ID generation
class IdGenerator {
public:
    static std::string generateEventId() {
        static std::atomic<uint64_t> counter{0};
        return "EVT-" + std::to_string(nowMs()) + "-" + std::to_string(++counter);
    }

    static std::string generateIncidentId() {
        static std::atomic<uint64_t> counter{1000};
        return "INC-" + std::to_string(++counter);
    }
};

// Common channels
namespace channels {
    constexpr const char* TRADING = "trading";
    constexpr const char* INFRA = "infra";
    constexpr const char* RISK = "risk";
    constexpr const char* MARKET_DATA = "market-data";
    constexpr const char* ALL = "*";
}

// Configuration constants
namespace config {
    constexpr size_t MAX_MESSAGE_SIZE = 1024 * 1024;  // 1MB max message
    constexpr size_t MAX_QUEUE_SIZE = 10000;
    constexpr size_t CLIENT_BUFFER_SIZE = 4096;
    constexpr int DEFAULT_PORT = 9090;
    constexpr int WORKER_THREADS = 4;
    constexpr int IO_THREADS = 2;
}

} // namespace opspulse

