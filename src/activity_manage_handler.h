#ifndef __ACTIVITY_MANAGE_HANDLER_H__
#define __ACTIVITY_MANAGE_HANDLER_H__

#include "crow_safe.h"
#include "i_activity_dao.h"
#include "i_activity_signup_dao.h"
#include "i_activity_group_dao.h"
#include "session_manager.h"
#include <string>

class GroupSessionManager;

class ActivityManageHandler {
public:
    explicit ActivityManageHandler(IActivityDao* activity_dao,
                                   IActivitySignupDao* signup_dao,
                                   IActivityGroupDao* group_dao,
                                   SessionManager* session_mgr,
                                   GroupSessionManager* group_session_mgr,
                                   const std::string& upload_path);
    ~ActivityManageHandler();

    void RegisterRoutes(crow::SimpleApp& app);

private:
    /* @brief 活动 CRUD */
    crow::response HandleCreateActivity(const crow::request& req);
    crow::response HandleUpdateActivity(const crow::request& req);
    crow::response HandleDeleteActivity(const crow::request& req);
    crow::response HandleListActivities(const crow::request& req);
    crow::response HandlePublishActivity(const crow::request& req);
    crow::response HandleUpdateSort(const crow::request& req);

    /* @brief 报名记录 */
    crow::response HandleGetSignups(const crow::request& req);

    /* @brief 图片上传 */
    crow::response HandleUploadImage(const crow::request& req);
    crow::response HandleDeleteCoverImage(const crow::request& req);

    /* @brief 宣传彩页 */
    crow::response HandleUploadPromotionImage(const crow::request& req);
    crow::response HandleDeletePromotionImage(const crow::request& req);
    crow::response HandleSortPromotionImages(const crow::request& req);
    crow::response HandleUpdatePromotionText(const crow::request& req);
    crow::response HandleGetPromotion(const crow::request& req);

    /* @brief 活动须知 */
    crow::response HandleUpdateActivityNotice(const crow::request& req);
    crow::response HandleGetActivityNotice(const crow::request& req);

    /* @brief 关于我们 */
    crow::response HandleUploadAboutUsCardImage(const crow::request& req);
    crow::response HandleUpdateAboutUsCard(const crow::request& req);
    crow::response HandleDeleteAboutUsCard(const crow::request& req);
    crow::response HandleSortAboutUsCards(const crow::request& req);
    crow::response HandleGetAboutUsCards(const crow::request& req);

    /* @brief 校验会话有效性（教师+管理员均可），成功返回0 */
    int CheckSession(const crow::request& req, SessionInfo& info);

    /* @brief 从Cookie中解析session_id */
    std::string GetSessionIdFromCookie(const crow::request& req);

    IActivityDao* activity_dao_;
    IActivitySignupDao* signup_dao_;
    IActivityGroupDao* group_dao_;
    SessionManager* session_mgr_;
    GroupSessionManager* group_session_mgr_;
    std::string upload_path_;
};

#endif /* __ACTIVITY_MANAGE_HANDLER_H__ */
