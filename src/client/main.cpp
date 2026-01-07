#include "client/Client.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <algorithm>

using namespace opspulse;

// ANSI color codes
namespace color {
    const char* RESET = "\033[0m";
    const char* RED = "\033[31m";
    const char* GREEN = "\033[32m";
    const char* YELLOW = "\033[33m";
    const char* BLUE = "\033[34m";
    const char* MAGENTA = "\033[35m";
    const char* CYAN = "\033[36m";
    const char* BOLD = "\033[1m";
    const char* DIM = "\033[2m";
}

std::string formatTimestamp(uint64_t ts) {
    time_t t = static_cast<time_t>(ts / 1000);
    struct tm* tm_info = localtime(&t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info);
    return std::string(buf);
}

std::string getSeverityColor(int sev) {
    switch (sev) {
        case 1: return color::RED;
        case 2: return color::YELLOW;
        case 3: return color::BLUE;
        default: return color::DIM;
    }
}

std::string getStatusColor(const std::string& status) {
    if (status == "OPEN") return color::RED;
    if (status == "ACKED") return color::YELLOW;
    if (status == "INVESTIGATING") return color::MAGENTA;
    if (status == "MITIGATED") return color::CYAN;
    if (status == "RESOLVED") return color::GREEN;
    return color::DIM;
}

std::string getLevelColor(LogLevel level) {
    switch (level) {
        case LogLevel::CRITICAL: return color::RED;
        case LogLevel::ERROR: return color::RED;
        case LogLevel::WARN: return color::YELLOW;
        case LogLevel::INFO: return color::GREEN;
        case LogLevel::DEBUG: return color::DIM;
        default: return color::RESET;
    }
}

void printEvent(const Event& e) {
    std::cout << color::DIM << "[" << formatTimestamp(e.timestamp) << "]" << color::RESET
              << " " << getLevelColor(e.level) << "[" << levelToString(e.level) << "]" << color::RESET
              << " " << color::CYAN << "[" << e.channel << "]" << color::RESET
              << " " << e.message;
    if (!e.tags.empty()) {
        std::cout << " " << color::DIM;
        for (const auto& tag : e.tags) {
            std::cout << "#" << tag << " ";
        }
        std::cout << color::RESET;
    }
    std::cout << std::endl;
}

void printIncident(const Incident& inc) {
    int sev = static_cast<int>(inc.severity);
    std::string status = statusToString(inc.status);

    std::cout << "\n" << color::BOLD << inc.id << color::RESET
              << " " << getSeverityColor(sev) << "SEV" << sev << color::RESET
              << " " << getStatusColor(status) << "[" << status << "]" << color::RESET
              << "\n  " << color::BOLD << inc.title << color::RESET
              << "\n  " << color::DIM << "Channel: " << color::RESET << inc.channel
              << "  " << color::DIM << "Owner: " << color::RESET << (inc.owner.empty() ? "unassigned" : inc.owner)
              << "\n  " << color::DIM << "Created: " << color::RESET << formatTimestamp(inc.created_at)
              << "  " << color::DIM << "Updated: " << color::RESET << formatTimestamp(inc.updated_at)
              << std::endl;
}

void printHelp() {
    std::cout << "\n" << color::BOLD << "OpsPulse CLI Commands:" << color::RESET << "\n\n"
              << color::CYAN << "Connection:" << color::RESET << "\n"
              << "  connect <host> <port>    Connect to server\n"
              << "  auth <user> <token>      Authenticate\n"
              << "  disconnect               Disconnect from server\n"
              << "\n" << color::CYAN << "Subscriptions:" << color::RESET << "\n"
              << "  sub <channel>            Subscribe to channel\n"
              << "  unsub <channel>          Unsubscribe from channel\n"
              << "\n" << color::CYAN << "Events:" << color::RESET << "\n"
              << "  event <channel> <level> <msg>  Publish event (levels: debug,info,warn,error,critical)\n"
              << "  events [channel] [limit]       List recent events\n"
              << "\n" << color::CYAN << "Incidents:" << color::RESET << "\n"
              << "  inc create <sev> <channel> <title>  Create incident (sev 1-5)\n"
              << "  inc list [channel] [status]         List incidents\n"
              << "  inc ack <id>                        Acknowledge incident\n"
              << "  inc assign <id> <owner>             Assign owner\n"
              << "  inc resolve <id>                    Resolve incident\n"
              << "  inc comment <id> <text>             Add comment\n"
              << "\n" << color::CYAN << "Other:" << color::RESET << "\n"
              << "  help                     Show this help\n"
              << "  quit / exit              Exit client\n"
              << std::endl;
}

class CLI {
public:
    CLI() {
        // Setup callbacks for push messages
        client_.onEvent([](const Event& e) {
            std::cout << "\n" << color::GREEN << ">>> EVENT:" << color::RESET << " ";
            printEvent(e);
            std::cout << "> " << std::flush;
        });

        client_.onIncident([](const Incident& inc) {
            std::cout << "\n" << color::MAGENTA << ">>> NEW INCIDENT:" << color::RESET;
            printIncident(inc);
            std::cout << "> " << std::flush;
        });

        client_.onIncidentUpdate([](const std::string& id, const std::string& field, const std::string& newVal) {
            std::cout << "\n" << color::YELLOW << ">>> INCIDENT UPDATE:" << color::RESET
                      << " " << id << " " << field << " -> " << newVal << "\n"
                      << "> " << std::flush;
        });

        client_.onError([](const std::string& msg) {
            std::cout << "\n" << color::RED << ">>> ERROR:" << color::RESET << " " << msg << "\n"
                      << "> " << std::flush;
        });

        client_.onDisconnect([]() {
            std::cout << "\n" << color::RED << ">>> DISCONNECTED" << color::RESET << "\n"
                      << "> " << std::flush;
        });
    }

    void run() {
        std::cout << R"(
   ____            ____        __          
  / __ \____  ____/ __ \__  __/ /___ ___  
 / / / / __ \/ __/ /_/ / / / / / __ `__ \ 
/ /_/ / /_/ (__  ) ____/ /_/ / / / / / / / 
\____/ .___/____/_/    \__,_/_/_/ /_/ /_/  
    /_/           CLI Client v1.0
)" << std::endl;

        printHelp();

        std::string line;
        while (true) {
            std::cout << "> ";
            if (!std::getline(std::cin, line)) {
                break;
            }

            // Trim whitespace
            size_t start = line.find_first_not_of(" \t");
            if (start == std::string::npos) continue;
            line = line.substr(start);

            if (line.empty()) continue;

            if (line == "quit" || line == "exit") {
                break;
            }

            processCommand(line);
        }

        client_.disconnect();
    }

private:
    Client client_;

    std::vector<std::string> tokenize(const std::string& line) {
        std::vector<std::string> tokens;
        std::istringstream iss(line);
        std::string token;
        while (iss >> token) {
            tokens.push_back(token);
        }
        return tokens;
    }

    std::string getRest(const std::string& line, size_t skipWords) {
        std::istringstream iss(line);
        std::string word;
        for (size_t i = 0; i < skipWords && iss >> word; ++i) {}
        std::string rest;
        std::getline(iss >> std::ws, rest);
        return rest;
    }

    void processCommand(const std::string& line) {
        auto tokens = tokenize(line);
        if (tokens.empty()) return;

        const std::string& cmd = tokens[0];

        if (cmd == "help") {
            printHelp();
        }
        else if (cmd == "connect") {
            if (tokens.size() < 3) {
                std::cout << "Usage: connect <host> <port>\n";
                return;
            }
            std::string host = tokens[1];
            int port = std::stoi(tokens[2]);
            if (client_.connect(host, port)) {
                client_.startReceiving();
            }
        }
        else if (cmd == "disconnect") {
            client_.disconnect();
            std::cout << "Disconnected\n";
        }
        else if (cmd == "auth") {
            if (tokens.size() < 3) {
                std::cout << "Usage: auth <username> <token>\n";
                return;
            }
            client_.authenticate(tokens[1], tokens[2]);
        }
        else if (cmd == "sub") {
            if (tokens.size() < 2) {
                std::cout << "Usage: sub <channel>\n";
                return;
            }
            if (client_.subscribe({tokens[1]})) {
                std::cout << "Subscribed to " << tokens[1] << "\n";
            }
        }
        else if (cmd == "unsub") {
            if (tokens.size() < 2) {
                std::cout << "Usage: unsub <channel>\n";
                return;
            }
            if (client_.unsubscribe({tokens[1]})) {
                std::cout << "Unsubscribed from " << tokens[1] << "\n";
            }
        }
        else if (cmd == "event") {
            if (tokens.size() < 4) {
                std::cout << "Usage: event <channel> <level> <message>\n";
                return;
            }
            std::string channel = tokens[1];
            std::string level = tokens[2];
            std::string msg = getRest(line, 3);
            
            if (client_.publishEvent(channel, level, msg)) {
                std::cout << "Event published\n";
            }
        }
        else if (cmd == "events") {
            std::string channel = tokens.size() > 1 ? tokens[1] : "";
            int limit = tokens.size() > 2 ? std::stoi(tokens[2]) : 20;
            
            auto events = client_.listEvents(channel, limit);
            std::cout << "\n" << color::BOLD << "Recent Events (" << events.size() << "):" << color::RESET << "\n";
            for (const auto& e : events) {
                printEvent(e);
            }
        }
        else if (cmd == "inc") {
            if (tokens.size() < 2) {
                std::cout << "Usage: inc <create|list|ack|assign|resolve|comment> ...\n";
                return;
            }
            handleIncidentCommand(tokens);
        }
        else {
            std::cout << color::RED << "Unknown command: " << cmd << color::RESET << "\n";
            std::cout << "Type 'help' for available commands\n";
        }
    }

    void handleIncidentCommand(const std::vector<std::string>& tokens) {
        const std::string& subcmd = tokens[1];

        if (subcmd == "create") {
            if (tokens.size() < 5) {
                std::cout << "Usage: inc create <sev> <channel> <title>\n";
                return;
            }
            int sev = std::stoi(tokens[2]);
            std::string channel = tokens[3];
            std::string title;
            for (size_t i = 4; i < tokens.size(); ++i) {
                if (i > 4) title += " ";
                title += tokens[i];
            }

            auto id = client_.createIncident(sev, title, channel);
            if (id) {
                std::cout << color::GREEN << "Created incident: " << *id << color::RESET << "\n";
            }
        }
        else if (subcmd == "list") {
            std::string channel = tokens.size() > 2 ? tokens[2] : "";
            std::string status = tokens.size() > 3 ? tokens[3] : "";

            auto incidents = client_.listIncidents(channel, status);
            std::cout << "\n" << color::BOLD << "Incidents (" << incidents.size() << "):" << color::RESET;
            for (const auto& inc : incidents) {
                printIncident(inc);
            }
            std::cout << std::endl;
        }
        else if (subcmd == "ack") {
            if (tokens.size() < 3) {
                std::cout << "Usage: inc ack <incident-id>\n";
                return;
            }
            if (client_.updateIncident(tokens[2], "ACKED")) {
                std::cout << "Incident acknowledged\n";
            }
        }
        else if (subcmd == "assign") {
            if (tokens.size() < 4) {
                std::cout << "Usage: inc assign <incident-id> <owner>\n";
                return;
            }
            if (client_.updateIncident(tokens[2], std::nullopt, tokens[3])) {
                std::cout << "Incident assigned to " << tokens[3] << "\n";
            }
        }
        else if (subcmd == "resolve") {
            if (tokens.size() < 3) {
                std::cout << "Usage: inc resolve <incident-id>\n";
                return;
            }
            if (client_.updateIncident(tokens[2], "RESOLVED")) {
                std::cout << "Incident resolved\n";
            }
        }
        else if (subcmd == "comment") {
            if (tokens.size() < 4) {
                std::cout << "Usage: inc comment <incident-id> <text>\n";
                return;
            }
            std::string comment;
            for (size_t i = 3; i < tokens.size(); ++i) {
                if (i > 3) comment += " ";
                comment += tokens[i];
            }
            if (client_.updateIncident(tokens[2], std::nullopt, std::nullopt, std::nullopt, comment)) {
                std::cout << "Comment added\n";
            }
        }
        else {
            std::cout << "Unknown incident command: " << subcmd << "\n";
        }
    }
};

int main(int argc, char* argv[]) {
    // Quick connect mode: opspulse_client host port user token
    if (argc >= 5) {
        Client client;
        if (!client.connect(argv[1], std::stoi(argv[2]))) {
            return 1;
        }
        if (!client.authenticate(argv[3], argv[4])) {
            return 1;
        }
        
        // If additional args, treat as command
        if (argc > 5) {
            std::string cmd;
            for (int i = 5; i < argc; ++i) {
                if (i > 5) cmd += " ";
                cmd += argv[i];
            }
            // Execute command...
        }
        
        client.disconnect();
        return 0;
    }

    // Interactive mode
    CLI cli;
    cli.run();

    return 0;
}

