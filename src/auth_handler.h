#ifndef __AUTH_HANDLER_H__
#define __AUTH_HANDLER_H__

#include "crow_safe.h"
#include "i_user_dao.h"
#include "i_operation_log_dao.h"
#include "session_manager.h"

class AuthHandler {
public:
    explicit AuthHandler(IUserDao* user_dao, IOperationLogDao* log_dao, SessionManager* session_mgr);
    ~AuthHandler();

    /* @brief 注册Crow路由 */
    void RegisterRoutes(crow::SimpleApp& app);

private:
    /* @brief 用户登录 */
    crow::response HandleLogin(const crow::request& req);

    /* @brief 用户注册 */
    crow::response HandleRegister(const crow::request& req);

    /* @brief 用户登出 */
    crow::response HandleLogout(const crow::request& req);

    /* @brief 修改密码 */
    crow::response HandleChangePassword(const crow::request& req);

    /* @brief 密码重置请求 */
    crow::response HandleResetRequest(const crow::request& req);

    /* @brief 检查管理员是否存在 */
    crow::response HandleCheckAdmin();

    /* @brief 获取当前会话信息 */
    crow::response HandleGetSession(const crow::request& req);

    /* @brief 从Cookie中解析session_id */
    std::string GetSessionIdFromCookie(const crow::request& req);

    IUserDao* user_dao_;
    IOperationLogDao* log_dao_;
    SessionManager* session_mgr_;
};

#endif /* AUTH_HANDLER_H__ */
