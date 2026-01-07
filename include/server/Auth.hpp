#pragma once

#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <optional>

namespace opspulse {

/**
 * Simple authentication manager.
 * Loads users/tokens from a JSON file.
 * 
 * users.json format:
 * {
 *   "users": [
 *     {"username": "admin", "token": "admin-token-123", "role": "admin"},
 *     {"username": "operator", "token": "op-token-456", "role": "operator"},
 *     {"username": "viewer", "token": "view-token-789", "role": "viewer"}
 *   ]
 * }
 */

enum class UserRole {
    ADMIN,      // Full access
    OPERATOR,   // Can create/update incidents and events
    VIEWER      // Read-only access
};

struct User {
    std::string username;
    std::string hashedToken;  // In production, this would be hashed
    UserRole role;
};

class AuthManager {
public:
    AuthManager();

    // Load users from file
    bool loadUsers(const std::string& filepath);

    // Authenticate a user with token
    // Returns username if successful, nullopt if failed
    std::optional<std::string> authenticate(const std::string& username, 
                                             const std::string& token) const;

    // Check if user has permission for an action
    bool canCreateIncident(const std::string& username) const;
    bool canUpdateIncident(const std::string& username) const;
    bool canPublishEvent(const std::string& username) const;

    // Get user role
    std::optional<UserRole> getUserRole(const std::string& username) const;

    // Add user (for testing or admin API)
    void addUser(const std::string& username, const std::string& token, UserRole role);

    // Remove user
    bool removeUser(const std::string& username);

    // Get user count
    size_t userCount() const;

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, User> users_;

    // Simple token verification (in production, use proper hashing)
    static bool verifyToken(const std::string& provided, const std::string& stored);
};

inline std::string roleToString(UserRole role) {
    switch (role) {
        case UserRole::ADMIN: return "admin";
        case UserRole::OPERATOR: return "operator";
        case UserRole::VIEWER: return "viewer";
        default: return "unknown";
    }
}

inline UserRole stringToRole(const std::string& s) {
    if (s == "admin") return UserRole::ADMIN;
    if (s == "operator") return UserRole::OPERATOR;
    return UserRole::VIEWER;
}

} // namespace opspulse

