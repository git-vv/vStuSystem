#ifndef __ADMIN_HANDLER_H__
#define __ADMIN_HANDLER_H__

#include "crow_safe.h"
#include "i_user_dao.h"
#include "i_operation_log_dao.h"
#include "session_manager.h"

class AdminHandler {
public:
    explicit AdminHandler(IUserDao* user_dao, IOperationLogDao* log_dao, SessionManager* session_mgr);
    ~AdminHandler();

    /* @brief 注册Crow路由 */
    void RegisterRoutes(crow::SimpleApp& app);

private:
    /* @brief 获取待审批的密码重置请求列表 */
    crow::response HandleGetResetRequests(const crow::request& req);

    /* @brief 审批密码重置请求 */
    crow::response HandleApproveReset(const crow::request& req);

    /* @brief 查询操作日志 */
    crow::response HandleGetLogs(const crow::request& req);

    /* @brief 清理操作日志 */
    crow::response HandleCleanLogs(const crow::request& req);

    /* @brief 获取所有教师用户列表 */
    crow::response HandleGetUsers(const crow::request& req);

    /* @brief 删除用户 */
    crow::response HandleDeleteUser(const crow::request& req);

    /* @brief 获取待审核的注册申请列表 */
    crow::response HandleGetRegistrationRequests(const crow::request& req);

    /* @brief 批量审核通过注册申请 */
    crow::response HandleApproveRegistration(const crow::request& req);

    /* @brief 批量拒绝注册申请 */
    crow::response HandleRejectRegistration(const crow::request& req);

    /* @brief 校验管理员权限，成功返回0 */
    int CheckAdminPermission(const crow::request& req, SessionInfo& info);

    /* @brief 从Cookie中解析session_id */
    std::string GetSessionIdFromCookie(const crow::request& req);

    IUserDao* user_dao_;
    IOperationLogDao* log_dao_;
    SessionManager* session_mgr_;
};

#endif /* __ADMIN_HANDLER_H__ */
