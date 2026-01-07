#include "server/Server.hpp"
#include <iostream>
#include <cstdlib>
#include <cstring>

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "Options:\n"
              << "  -p, --port PORT        TCP server port (default: 9090)\n"
              << "  --ws-port PORT         WebSocket server port (default: 8080)\n"
              << "  -w, --workers N        Worker thread count (default: 4)\n"
              << "  -l, --log PATH         Event log file path (default: data/events.log)\n"
              << "  -u, --users PATH       Users config file (default: config/users.json)\n"
              << "  --no-auth              Disable authentication\n"
              << "  -h, --help             Show this help\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    opspulse::ServerConfig config;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
        else if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            config.port = std::atoi(argv[++i]);
        }
        else if (arg == "--ws-port" && i + 1 < argc) {
            config.wsPort = std::atoi(argv[++i]);
        }
        else if ((arg == "-w" || arg == "--workers") && i + 1 < argc) {
            config.workerThreads = std::atoi(argv[++i]);
        }
        else if ((arg == "-l" || arg == "--log") && i + 1 < argc) {
            config.eventLogPath = argv[++i];
        }
        else if ((arg == "-u" || arg == "--users") && i + 1 < argc) {
            config.usersFilePath = argv[++i];
        }
        else if (arg == "--no-auth") {
            config.enableAuth = false;
        }
        else {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }

    std::cout << R"(
   ____            ____        __          
  / __ \____  ____/ __ \__  __/ /___ ___  
 / / / / __ \/ __/ /_/ / / / / / __ `__ \ 
/ /_/ / /_/ (__  ) ____/ /_/ / / / / / / / 
\____/ .___/____/_/    \__,_/_/_/ /_/ /_/  
    /_/                                    
         Real-Time Incident & Ops Console
)" << std::endl;

    std::cout << "Configuration:" << std::endl;
    std::cout << "  TCP Port:     " << config.port << std::endl;
    std::cout << "  WS Port:      " << config.wsPort << " (WebSocket for browsers)" << std::endl;
    std::cout << "  Workers:      " << config.workerThreads << std::endl;
    std::cout << "  Event Log:    " << config.eventLogPath << std::endl;
    std::cout << "  Users File:   " << config.usersFilePath << std::endl;
    std::cout << "  Auth:         " << (config.enableAuth ? "enabled" : "disabled") << std::endl;
    std::cout << std::endl;

    try {
        opspulse::Server server(config);
        server.run();
    }
    catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

