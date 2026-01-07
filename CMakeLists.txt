cmake_minimum_required(VERSION 3.16)
project(OpsPulse VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Platform detection
if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0601)
    set(PLATFORM_LIBS ws2_32)
else()
    set(PLATFORM_LIBS pthread)
endif()

# Options
option(BUILD_TESTS "Build unit tests" OFF)
option(ENABLE_SANITIZERS "Enable address/thread sanitizers" OFF)

if(ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# Compiler warnings
if(MSVC)
    add_compile_options(/W4 /WX-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Third-party: nlohmann/json (header-only)
include(FetchContent)
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

# Common library (shared between server and client)
add_library(opspulse_common STATIC
    src/common/Protocol.cpp
    src/common/Message.cpp
)
target_link_libraries(opspulse_common PUBLIC nlohmann_json::nlohmann_json)

# Server executable
add_executable(opspulse_server
    src/server/main.cpp
    src/server/Server.cpp
    src/server/ThreadPool.cpp
    src/server/ClientSession.cpp
    src/server/StateStore.cpp
    src/server/Broadcaster.cpp
    src/server/EventLog.cpp
    src/server/Auth.cpp
    src/server/RequestHandler.cpp
)
target_link_libraries(opspulse_server PRIVATE opspulse_common ${PLATFORM_LIBS})

# CLI Client executable
add_executable(opspulse_client
    src/client/main.cpp
    src/client/Client.cpp
)
target_link_libraries(opspulse_client PRIVATE opspulse_common ${PLATFORM_LIBS})

# Create data directory for event logs
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/data)

# Copy config files
configure_file(${CMAKE_SOURCE_DIR}/config/users.json ${CMAKE_BINARY_DIR}/config/users.json COPYONLY)

# Install targets
install(TARGETS opspulse_server opspulse_client
    RUNTIME DESTINATION bin
)
install(DIRECTORY config/ DESTINATION config)

