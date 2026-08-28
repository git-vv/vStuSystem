#ifndef __PAGE_HANDLER_H__
#define __PAGE_HANDLER_H__

#include "crow_safe.h"

class PageHandler {
public:
    PageHandler();
    ~PageHandler();

    /* @brief 注册页面路由 */
    void RegisterRoutes(crow::SimpleApp& app);

    /* @brief 凭证页面（public for unit test access） */
    crow::response HandleCredential();

    /* @brief 账号管理页面（public for unit test access） */
    crow::response HandleAccount();

    /* @brief 公开活动页（public for unit test access） */
    crow::response HandleActivity();

    /* @brief 活动管理页（public for unit test access） */
    crow::response HandleActivityManage();

    /* @brief 数据管理页（仅管理员，前端校验） */
    crow::response HandleDataTransfer();

    /* @brief API管理页（仅管理员，前端校验） */
    crow::response HandleApiManage();

private:
    /* @brief 门户首页 */
    crow::response HandleIndex();

    /* @brief 报名缴费页面 */
    crow::response HandleRegistration();

    /* @brief 创建班级页面 */
    crow::response HandleClassCreate();

    /* @brief 班级管理页面 */
    crow::response HandleClassManage();

    /* @brief 资源管理页面 */
    crow::response HandleResource();

    /* @brief 超级管理员页面 */
    crow::response HandleAdmin();

    /* @brief 登录页面 */
    crow::response HandleLogin();

    /* @brief 注册页面 */
    crow::response HandleRegisterPage();

    /* @brief 价位预设管理页面 */
    crow::response HandlePreset();
};

#endif /* __PAGE_HANDLER_H__ */
