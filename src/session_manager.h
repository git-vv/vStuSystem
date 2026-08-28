#ifndef __SESSION_MANAGER_H__
#define __SESSION_MANAGER_H__

#include <string>
#include <map>
#include <mutex>
#include "user_types.h"

struct SessionInfo {
    std::string session_id;
    int32_t user_id;
    std::string username;
    UserRoleType role;
    std::string create_time;
};

class SessionManager {
public:
    SessionManager();
    ~SessionManager();

    /**
     * @brief 创建会话
     * @param user_id 用户ID
     * @param username 用户名
     * @param role 用户角色
     * @return session_id
     */
    std::string CreateSession(int32_t user_id, const std::string& username, UserRoleType role);

    /**
     * @brief 校验会话
     * @param session_id 会话ID
     * @param info 输出会话信息
     * @return 0=有效, ERR_AUTH_SESSION_EXPIRED=无效
     */
    int ValidateSession(const std::string& session_id, SessionInfo& info);

    /**
     * @brief 销毁会话
     * @param session_id 会话ID
     */
    void DestroySession(const std::string& session_id);

private:
    std::map<std::string, SessionInfo> sessions_;
    std::mutex mutex_;
};

#endif /* __SESSION_MANAGER_H__ */
