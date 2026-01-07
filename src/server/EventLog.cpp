#include "server/EventLog.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <sstream>

namespace opspulse {

using json = nlohmann::json;

EventLog::EventLog(const std::string& filepath)
    : filepath_(filepath)
{
}

EventLog::~EventLog() {
    close();
}

bool EventLog::open() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (file_.is_open()) {
        return true;
    }

    // Open in append mode
    file_.open(filepath_, std::ios::out | std::ios::app);
    if (!file_.is_open()) {
        std::cerr << "[EventLog] Failed to open file: " << filepath_ << std::endl;
        return false;
    }

    std::cout << "[EventLog] Opened: " << filepath_ << std::endl;
    return true;
}

void EventLog::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
}

void EventLog::writeRecord(const std::string& jsonStr) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!file_.is_open()) {
        return;
    }

    file_ << jsonStr << "\n";
    ++recordCount_;
    byteCount_ += jsonStr.size() + 1;

    // Flush periodically for durability
    if (recordCount_ % 10 == 0) {
        file_.flush();
    }
}

void EventLog::logEvent(const Event& event) {
    json j;
    j["type"] = "event";
    j["ts"] = nowMs();
    
    json data;
    to_json(data, event);
    j["data"] = data;

    writeRecord(j.dump());
}

void EventLog::logIncident(const Incident& incident) {
    json j;
    j["type"] = "incident";
    j["ts"] = nowMs();
    
    json data;
    to_json(data, incident);
    j["data"] = data;

    writeRecord(j.dump());
}

void EventLog::logIncidentUpdate(const std::string& incidentId,
                                  const std::string& field,
                                  const std::string& oldValue,
                                  const std::string& newValue,
                                  const std::string& updatedBy) {
    json j;
    j["type"] = "incident_update";
    j["ts"] = nowMs();
    j["id"] = incidentId;
    j["field"] = field;
    j["old"] = oldValue;
    j["new"] = newValue;
    j["by"] = updatedBy;

    writeRecord(j.dump());
}

void EventLog::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) {
        file_.flush();
    }
}

bool EventLog::replay(EventCallback onEvent,
                       IncidentCallback onIncident,
                       IncidentUpdateCallback onUpdate) {
    std::ifstream infile(filepath_);
    if (!infile.is_open()) {
        std::cout << "[EventLog] No existing log file to replay" << std::endl;
        return true;  // Not an error - just no history
    }

    std::cout << "[EventLog] Replaying log: " << filepath_ << std::endl;

    std::string line;
    size_t lineNum = 0;
    size_t eventCount = 0, incidentCount = 0, updateCount = 0;

    while (std::getline(infile, line)) {
        ++lineNum;
        
        if (line.empty()) {
            continue;
        }

        try {
            json j = json::parse(line);
            std::string type = j.value("type", "");

            if (type == "event") {
                Event event;
                from_json(j["data"], event);
                if (onEvent) {
                    onEvent(event);
                }
                ++eventCount;
            }
            else if (type == "incident") {
                Incident incident;
                from_json(j["data"], incident);
                if (onIncident) {
                    onIncident(incident);
                }
                ++incidentCount;
            }
            else if (type == "incident_update") {
                std::string id = j.value("id", "");
                std::string field = j.value("field", "");
                std::string oldVal = j.value("old", "");
                std::string newVal = j.value("new", "");
                std::string by = j.value("by", "");
                
                if (onUpdate) {
                    onUpdate(id, field, oldVal, newVal, by);
                }
                ++updateCount;
            }
        }
        catch (const json::exception& e) {
            std::cerr << "[EventLog] Parse error at line " << lineNum 
                      << ": " << e.what() << std::endl;
            // Continue with next line
        }
    }

    std::cout << "[EventLog] Replay complete: " 
              << eventCount << " events, "
              << incidentCount << " incidents, "
              << updateCount << " updates" << std::endl;

    return true;
}

} // namespace opspulse

