#include "server/Auth.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

namespace opspulse {

using json = nlohmann::json;

AuthManager::AuthManager() {
    // Add a default admin user for testing
    addUser("admin", "admin-secret", UserRole::ADMIN);
    addUser("operator", "operator-secret", UserRole::OPERATOR);
    addUser("viewer", "viewer-secret", UserRole::VIEWER);
}

bool AuthManager::loadUsers(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[Auth] Could not open users file: " << filepath << std::endl;
        return false;
    }

    try {
        json j;
        file >> j;

        std::unique_lock<std::shared_mutex> lock(mutex_);
        users_.clear();

        if (!j.contains("users") || !j["users"].is_array()) {
            std::cerr << "[Auth] Invalid users file format" << std::endl;
            return false;
        }

        for (const auto& userJson : j["users"]) {
            User user;
            user.username = userJson.value("username", "");
            user.hashedToken = userJson.value("token", "");
            user.role = stringToRole(userJson.value("role", "viewer"));

            if (!user.username.empty() && !user.hashedToken.empty()) {
                users_[user.username] = user;
            }
        }

        std::cout << "[Auth] Loaded " << users_.size() << " users from " << filepath << std::endl;
        return true;
    }
    catch (const json::exception& e) {
        std::cerr << "[Auth] JSON error loading users: " << e.what() << std::endl;
        return false;
    }
}

std::optional<std::string> AuthManager::authenticate(const std::string& username,
                                                       const std::string& token) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    auto it = users_.find(username);
    if (it == users_.end()) {
        return std::nullopt;
    }

    if (!verifyToken(token, it->second.hashedToken)) {
        return std::nullopt;
    }

    return username;
}

bool AuthManager::verifyToken(const std::string& provided, const std::string& stored) {
    // Simple string comparison for now
    // In production: use constant-time comparison and proper hashing
    return provided == stored;
}

bool AuthManager::canCreateIncident(const std::string& username) const {
    auto role = getUserRole(username);
    if (!role) return false;
    return *role == UserRole::ADMIN || *role == UserRole::OPERATOR;
}

bool AuthManager::canUpdateIncident(const std::string& username) const {
    auto role = getUserRole(username);
    if (!role) return false;
    return *role == UserRole::ADMIN || *role == UserRole::OPERATOR;
}

bool AuthManager::canPublishEvent(const std::string& username) const {
    auto role = getUserRole(username);
    if (!role) return false;
    return *role == UserRole::ADMIN || *role == UserRole::OPERATOR;
}

std::optional<UserRole> AuthManager::getUserRole(const std::string& username) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    auto it = users_.find(username);
    if (it != users_.end()) {
        return it->second.role;
    }
    return std::nullopt;
}

void AuthManager::addUser(const std::string& username, const std::string& token, UserRole role) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    User user;
    user.username = username;
    user.hashedToken = token;  // Should hash this in production
    user.role = role;
    
    users_[username] = user;
}

bool AuthManager::removeUser(const std::string& username) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    return users_.erase(username) > 0;
}

size_t AuthManager::userCount() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return users_.size();
}

} // namespace opspulse

