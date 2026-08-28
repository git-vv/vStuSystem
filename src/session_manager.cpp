#include "session_manager.h"
#include "utils.h"
#include "error_codes.h"

#include <cstdlib>
#include <openssl/rand.h>

namespace {

std::string GenerateSessionId() {
    uint8_t bytes[32];
    if (RAND_bytes(bytes, sizeof(bytes)) == 1) {
        static const char hex_chars[] = "0123456789abcdef";
        std::string result;
        result.reserve(sizeof(bytes) * 2);
        for (size_t i = 0; i < sizeof(bytes); ++i) {
            result.push_back(hex_chars[(bytes[i] >> 4) & 0x0f]);
            result.push_back(hex_chars[bytes[i] & 0x0f]);
        }
        return result;
    }

    /* 回退方案 */
    std::string result;
    result.reserve(64);
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
    static const char hex_chars[] = "0123456789abcdef";
    for (int32_t i = 0; i < 64; ++i) {
        result.push_back(hex_chars[std::rand() % 16]);
    }
    return result;
}

} /* anonymous namespace */

SessionManager::SessionManager() {}

SessionManager::~SessionManager() {}

std::string SessionManager::CreateSession(int32_t user_id, const std::string& username, UserRoleType role) {
    SessionInfo info;
    info.session_id = GenerateSessionId();
    info.user_id = user_id;
    info.username = username;
    info.role = role;
    info.create_time = register_student::GetCurrentTimeString();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_[info.session_id] = info;
    }

    LOG_INFO << "Session created for user: " << username;
    return info.session_id;
}

int SessionManager::ValidateSession(const std::string& session_id, SessionInfo& info) {
    if (session_id.empty()) {
        return ERR_AUTH_SESSION_EXPIRED;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    std::map<std::string, SessionInfo>::iterator it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        return ERR_AUTH_SESSION_EXPIRED;
    }

    info = it->second;
    return DB_OK;
}

void SessionManager::DestroySession(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::map<std::string, SessionInfo>::iterator it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        LOG_INFO << "Session destroyed for user: " << it->second.username;
        sessions_.erase(it);
    }
}
