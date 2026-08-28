#ifndef __ACTIVITY_HANDLER_H__
#define __ACTIVITY_HANDLER_H__

#include "crow_safe.h"
#include "i_activity_dao.h"
#include "i_activity_signup_dao.h"
#include "i_activity_group_dao.h"

class GroupSessionManager;

class ActivityHandler {
public:
    explicit ActivityHandler(IActivityDao* activity_dao,
                             IActivitySignupDao* signup_dao,
                             IActivityGroupDao* group_dao,
                             GroupSessionManager* session_mgr);
    ~ActivityHandler();

    void RegisterRoutes(crow::SimpleApp& app);

    /* @brief GET /api/public/activity/list */
    crow::response HandleListActivities(const crow::request& req);

    /* @brief POST /api/public/activity/signup */
    crow::response HandleSignup(const crow::request& req);

    /* @brief GET /api/public/activity/promotion */
    crow::response HandleGetPromotion();

    /* @brief GET /api/public/activity/notice */
    crow::response HandleGetNotice();

    /* @brief GET /api/public/activity/about_us */
    crow::response HandleGetAboutUs();

    /* @brief POST /api/public/activity/group_signup */
    crow::response HandleGroupSignup(const crow::request& req);

    /* @brief GET /api/public/activity/group_status */
    crow::response HandleGetGroupStatus(const crow::request& req);

    /* @brief POST /api/public/activity/confirm_group */
    crow::response HandleConfirmGroup(const crow::request& req);

    /* @brief POST /api/public/activity/leave_group */
    crow::response HandleLeaveGroup(const crow::request& req);

    /* @brief POST /api/public/activity/cancel_group */
    crow::response HandleCancelGroup(const crow::request& req);

    /* @brief POST /api/public/activity/batch_group_signup */
    crow::response HandleBatchGroupSignup(const crow::request& req);

private:
    IActivityDao* activity_dao_;
    IActivitySignupDao* signup_dao_;
    IActivityGroupDao* group_dao_;
    GroupSessionManager* session_mgr_;
};

#endif /* __ACTIVITY_HANDLER_H__ */
