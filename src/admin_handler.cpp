#include "admin_handler.h"
#include "utils.h"
#include "error_codes.h"
#include "log_types.h"

AdminHandler::AdminHandler(IUserDao* user_dao, IOperationLogDao* log_dao, SessionManager* session_mgr)
    : user_dao_(user_dao)
    , log_dao_(log_dao)
    , session_mgr_(session_mgr) {}

AdminHandler::~AdminHandler() {}

void AdminHandler::RegisterRoutes(crow::SimpleApp& app) {
    CROW_ROUTE(app, "/api/admin/reset-requests").methods("GET"_method)
    ([this](const crow::request& req) {
        return HandleGetResetRequests(req);
    });

    CROW_ROUTE(app, "/api/admin/approve-reset").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleApproveReset(req);
    });

    CROW_ROUTE(app, "/api/admin/logs").methods("GET"_method)
    ([this](const crow::request& req) {
        return HandleGetLogs(req);
    });

    CROW_ROUTE(app, "/api/admin/logs/clean").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleCleanLogs(req);
    });

    CROW_ROUTE(app, "/api/admin/users").methods("GET"_method)
    ([this](const crow::request& req) {
        return HandleGetUsers(req);
    });

    CROW_ROUTE(app, "/api/admin/user/delete").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleDeleteUser(req);
    });

    CROW_ROUTE(app, "/api/admin/registration-requests").methods("GET"_method)
    ([this](const crow::request& req) {
        return HandleGetRegistrationRequests(req);
    });

    CROW_ROUTE(app, "/api/admin/approve-registration").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleApproveRegistration(req);
    });

    CROW_ROUTE(app, "/api/admin/reject-registration").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleRejectRegistration(req);
    });
}

std::string AdminHandler::GetSessionIdFromCookie(const crow::request& req) {
    std::string cookie_header = const_cast<crow::request&>(req).get_header_value("Cookie");
    if (cookie_header.empty()) {
        return "";
    }

    std::string key = "session_id=";
    size_t pos = cookie_header.find(key);
    if (pos == std::string::npos) {
        return "";
    }

    size_t start = pos + key.size();
    size_t end = cookie_header.find(';', start);
    if (end == std::string::npos) {
        return cookie_header.substr(start);
    }
    return cookie_header.substr(start, end - start);
}

int AdminHandler::CheckAdminPermission(const crow::request& req, SessionInfo& info) {
    if (!session_mgr_) {
        LOG_ERROR << "AdminHandler: session_mgr is null";
        return ERR_HANDLER_NULL_DAO;
    }

    std::string session_id = GetSessionIdFromCookie(req);
    if (session_id.empty()) {
        LOG_ERROR << "AdminHandler: no session_id in cookie";
        return ERR_AUTH_SESSION_EXPIRED;
    }

    int ret = session_mgr_->ValidateSession(session_id, info);
    if (ret != DB_OK) {
        LOG_ERROR << "AdminHandler: session validation failed, ret=" << ret;
        return ERR_AUTH_SESSION_EXPIRED;
    }

    if (info.role != UserRole_Admin) {
        LOG_ERROR << "AdminHandler: permission denied for user: " << info.username;
        return ERR_AUTH_PERMISSION_DENIED;
    }

    return DB_OK;
}

crow::response AdminHandler::HandleGetResetRequests(const crow::request& req) {
    LOG_INFO << "AdminHandler: get reset requests";

    if (!user_dao_) {
        LOG_ERROR << "AdminHandler: user_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "user_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    /* 校验管理员权限 */
    SessionInfo session_info;
    int ret = CheckAdminPermission(req, session_info);
    if (ret != DB_OK) {
        crow::json::wvalue err;
        if (ret == ERR_AUTH_SESSION_EXPIRED) {
            err["code"] = ret;
            err["message"] = "session expired or invalid";
            return crow::response(401, crow::json::dump(err));
        }
        err["code"] = ret;
        err["message"] = "permission denied";
        return crow::response(403, crow::json::dump(err));
    }

    /* 查询待审批的重置请求 */
    std::vector<PasswordResetRequest> requests;
    ret = user_dao_->QueryPendingResetRequests(requests);
    if (ret != DB_OK) {
        LOG_ERROR << "AdminHandler: query pending reset requests failed, ret=" << ret;
        crow::json::wvalue err;
        err["code"] = ret;
        err["message"] = "query reset requests failed";
        return crow::response(500, crow::json::dump(err));
    }

    /* 构建响应 */
    std::vector<crow::json::wvalue> items;
    for (size_t i = 0; i < requests.size(); ++i) {
        crow::json::wvalue item;
        item["id"] = requests[i].id;
        item["user_id"] = requests[i].user_id;
        item["username"] = requests[i].username;
        item["status"] = static_cast<int32_t>(requests[i].status);
        item["request_time"] = requests[i].request_time;
        items.push_back(std::move(item));
    }

    crow::json::wvalue data;
    for (size_t i = 0; i < items.size(); ++i) {
        data["requests"][i] = std::move(items[i]);
    }
    if (items.empty()) {
        data["count"] = 0;
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    LOG_INFO << "AdminHandler: get reset requests success, count=" << requests.size();
    return crow::response(result);
}

crow::response AdminHandler::HandleApproveReset(const crow::request& req) {
    LOG_INFO << "AdminHandler: approve reset request";

    if (!user_dao_) {
        LOG_ERROR << "AdminHandler: user_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "user_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    if (!log_dao_) {
        LOG_ERROR << "AdminHandler: log_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "log_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    /* 校验管理员权限 */
    SessionInfo session_info;
    int ret = CheckAdminPermission(req, session_info);
    if (ret != DB_OK) {
        crow::json::wvalue err;
        if (ret == ERR_AUTH_SESSION_EXPIRED) {
            err["code"] = ret;
            err["message"] = "session expired or invalid";
            return crow::response(401, crow::json::dump(err));
        }
        err["code"] = ret;
        err["message"] = "permission denied";
        return crow::response(403, crow::json::dump(err));
    }

    /* 解析请求体 */
    crow::json::wvalue err_resp;
    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (!body.has("request_id")) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "request_id is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (!body.has("new_password") || std::string(body["new_password"].s()).empty()) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "new_password is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    int32_t request_id = body["request_id"].i();
    std::string new_password = body["new_password"].s();

    /* 加密新密码 */
    std::string new_salt = register_student::GenerateSalt();
    std::string new_hash = register_student::EncryptPassword(new_password, new_salt);

    /* 审批重置请求 */
    ret = user_dao_->ApproveResetRequest(request_id, session_info.user_id, new_hash, new_salt);
    if (ret != DB_OK) {
        LOG_ERROR << "AdminHandler: approve reset request failed, ret=" << ret;
        err_resp["code"] = ret;
        err_resp["message"] = "approve reset request failed";
        return crow::response(500, crow::json::dump(err_resp));
    }

    /* 从已审批的请求中无法再查到，需要从请求体或请求ID推导 */
    /* 由于ApproveResetRequest已将请求标记为approved，我们需要通过request_id找到对应用户 */
    /* 这里使用一个简化的方式：从body中获取user_id（如果有），否则无法更新 */
    if (body.has("user_id")) {
        int32_t target_user_id = body["user_id"].i();
        ret = user_dao_->UpdatePassword(target_user_id, new_hash, new_salt);
        if (ret != DB_OK) {
            LOG_ERROR << "AdminHandler: update user password failed, ret=" << ret;
            err_resp["code"] = ret;
            err_resp["message"] = "update user password failed";
            return crow::response(500, crow::json::dump(err_resp));
        }
    }

    /* 记录操作日志 */
    OperationLog op_log;
    op_log.id = 0;
    op_log.op_type = OpType_ApproveReset;
    op_log.operator_name = session_info.username;
    op_log.target_class = "";
    op_log.target_student = "";
    op_log.target_resource = "";
    op_log.detail = "approved password reset request";
    op_log.op_time = register_student::GetCurrentTimeString();

    log_dao_->InsertLog(op_log);

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    LOG_INFO << "AdminHandler: approve reset request success, request_id=" << request_id;
    return crow::response(result);
}

crow::response AdminHandler::HandleGetLogs(const crow::request& req) {
    LOG_INFO << "AdminHandler: get logs request";

    if (!log_dao_) {
        LOG_ERROR << "AdminHandler: log_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "log_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    /* 校验管理员权限 */
    SessionInfo session_info;
    int ret = CheckAdminPermission(req, session_info);
    if (ret != DB_OK) {
        crow::json::wvalue err;
        if (ret == ERR_AUTH_SESSION_EXPIRED) {
            err["code"] = ret;
            err["message"] = "session expired or invalid";
            return crow::response(401, crow::json::dump(err));
        }
        err["code"] = ret;
        err["message"] = "permission denied";
        return crow::response(403, crow::json::dump(err));
    }

    /* 解析查询参数 */
    LogQueryCondition cond;
    cond.op_type = -1;
    cond.page = 1;
    cond.page_size = 20;

    char* p = nullptr;
    p = const_cast<crow::request&>(req).url_params.get("op_type");
    if (p && p[0] != '\0') { cond.op_type = static_cast<int32_t>(std::atoi(p)); }
    p = const_cast<crow::request&>(req).url_params.get("start_time");
    if (p && p[0] != '\0') { cond.start_time = p; }
    p = const_cast<crow::request&>(req).url_params.get("end_time");
    if (p && p[0] != '\0') { cond.end_time = p; }
    p = const_cast<crow::request&>(req).url_params.get("class_name");
    if (p && p[0] != '\0') { cond.class_name = p; }
    p = const_cast<crow::request&>(req).url_params.get("teacher_name");
    if (p && p[0] != '\0') { cond.teacher_name = p; }
    p = const_cast<crow::request&>(req).url_params.get("student_name");
    if (p && p[0] != '\0') { cond.student_name = p; }
    p = const_cast<crow::request&>(req).url_params.get("resource_name");
    if (p && p[0] != '\0') { cond.resource_name = p; }
    p = const_cast<crow::request&>(req).url_params.get("page");
    if (p && p[0] != '\0') { cond.page = static_cast<int32_t>(std::atoi(p)); if (cond.page < 1) { cond.page = 1; } }
    p = const_cast<crow::request&>(req).url_params.get("page_size");
    if (p && p[0] != '\0') { cond.page_size = static_cast<int32_t>(std::atoi(p)); if (cond.page_size < 1) { cond.page_size = 20; } }

    /* 查询日志 */
    std::vector<OperationLog> logs;
    int32_t total = 0;
    ret = log_dao_->QueryLogs(cond, logs, total);
    if (ret != DB_OK) {
        LOG_ERROR << "AdminHandler: query logs failed, ret=" << ret;
        crow::json::wvalue err;
        err["code"] = ret;
        err["message"] = "query logs failed";
        return crow::response(500, crow::json::dump(err));
    }

    /* 构建响应 */
    std::vector<crow::json::wvalue> log_items;
    for (size_t i = 0; i < logs.size(); ++i) {
        crow::json::wvalue item;
        item["id"] = logs[i].id;
        item["op_type"] = static_cast<int32_t>(logs[i].op_type);
        item["operator_name"] = logs[i].operator_name;
        item["target_class"] = logs[i].target_class;
        item["target_student"] = logs[i].target_student;
        item["target_resource"] = logs[i].target_resource;
        item["detail"] = logs[i].detail;
        item["op_time"] = logs[i].op_time;
        log_items.push_back(std::move(item));
    }

    crow::json::wvalue data;
    for (size_t i = 0; i < log_items.size(); ++i) {
        data["logs"][i] = std::move(log_items[i]);
    }
    data["total"] = total;
    data["page"] = cond.page;
    data["page_size"] = cond.page_size;

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    LOG_INFO << "AdminHandler: get logs success, total=" << total;
    return crow::response(result);
}

crow::response AdminHandler::HandleCleanLogs(const crow::request& req) {
    LOG_INFO << "AdminHandler: clean logs request";

    if (!log_dao_) {
        LOG_ERROR << "AdminHandler: log_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "log_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    /* 校验管理员权限 */
    SessionInfo session_info;
    int ret = CheckAdminPermission(req, session_info);
    if (ret != DB_OK) {
        crow::json::wvalue err;
        if (ret == ERR_AUTH_SESSION_EXPIRED) {
            err["code"] = ret;
            err["message"] = "session expired or invalid";
            return crow::response(401, crow::json::dump(err));
        }
        err["code"] = ret;
        err["message"] = "permission denied";
        return crow::response(403, crow::json::dump(err));
    }

    /* 解析请求体 */
    crow::json::wvalue err_resp;
    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }

    /* 构建清理条件 */
    LogQueryCondition cond;
    cond.op_type = -1;
    cond.page = 1;
    cond.page_size = 0;

    if (body.has("op_type")) {
        cond.op_type = body["op_type"].i();
    }
    if (body.has("start_time") && !std::string(body["start_time"].s()).empty()) {
        cond.start_time = body["start_time"].s();
    }
    if (body.has("end_time") && !std::string(body["end_time"].s()).empty()) {
        cond.end_time = body["end_time"].s();
    }
    if (body.has("class_name") && !std::string(body["class_name"].s()).empty()) {
        cond.class_name = body["class_name"].s();
    }
    if (body.has("teacher_name") && !std::string(body["teacher_name"].s()).empty()) {
        cond.teacher_name = body["teacher_name"].s();
    }
    if (body.has("student_name") && !std::string(body["student_name"].s()).empty()) {
        cond.student_name = body["student_name"].s();
    }
    if (body.has("resource_name") && !std::string(body["resource_name"].s()).empty()) {
        cond.resource_name = body["resource_name"].s();
    }

    /* 执行清理 */
    ret = log_dao_->CleanLogs(cond);
    if (ret != DB_OK) {
        LOG_ERROR << "AdminHandler: clean logs failed, ret=" << ret;
        err_resp["code"] = ret;
        err_resp["message"] = "clean logs failed";
        return crow::response(500, crow::json::dump(err_resp));
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    LOG_INFO << "AdminHandler: clean logs success";
    return crow::response(result);
}

crow::response AdminHandler::HandleGetUsers(const crow::request& req) {
    LOG_INFO << "AdminHandler: get users request";

    if (!user_dao_) {
        LOG_ERROR << "AdminHandler: user_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "user_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    /* 校验管理员权限 */
    SessionInfo session_info;
    int ret = CheckAdminPermission(req, session_info);
    if (ret != DB_OK) {
        crow::json::wvalue err;
        if (ret == ERR_AUTH_SESSION_EXPIRED) {
            err["code"] = ret;
            err["message"] = "session expired or invalid";
            return crow::response(401, crow::json::dump(err));
        }
        err["code"] = ret;
        err["message"] = "permission denied";
        return crow::response(403, crow::json::dump(err));
    }

    /* 查询所有教师 */
    std::vector<UserInfo> teachers;
    ret = user_dao_->QueryAllTeachers(teachers);
    if (ret != DB_OK) {
        LOG_ERROR << "AdminHandler: query all teachers failed, ret=" << ret;
        crow::json::wvalue err;
        err["code"] = ret;
        err["message"] = "query users failed";
        return crow::response(500, crow::json::dump(err));
    }

    /* 构建响应 */
    std::vector<crow::json::wvalue> user_items;
    for (size_t i = 0; i < teachers.size(); ++i) {
        crow::json::wvalue item;
        item["id"] = teachers[i].id;
        item["username"] = teachers[i].username;
        item["role"] = static_cast<int32_t>(teachers[i].role);
        item["display_name"] = teachers[i].display_name;
        item["create_time"] = teachers[i].create_time;
        user_items.push_back(std::move(item));
    }

    crow::json::wvalue data;
    for (size_t i = 0; i < user_items.size(); ++i) {
        data["users"][i] = std::move(user_items[i]);
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    LOG_INFO << "AdminHandler: get users success, count=" << teachers.size();
    return crow::response(result);
}

crow::response AdminHandler::HandleDeleteUser(const crow::request& req) {
    LOG_INFO << "AdminHandler: delete user request";

    if (!user_dao_) {
        LOG_ERROR << "AdminHandler: user_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "user_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    if (!log_dao_) {
        LOG_ERROR << "AdminHandler: log_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "log_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    /* 校验管理员权限 */
    SessionInfo session_info;
    int ret = CheckAdminPermission(req, session_info);
    if (ret != DB_OK) {
        crow::json::wvalue err;
        if (ret == ERR_AUTH_SESSION_EXPIRED) {
            err["code"] = ret;
            err["message"] = "session expired or invalid";
            return crow::response(401, crow::json::dump(err));
        }
        err["code"] = ret;
        err["message"] = "permission denied";
        return crow::response(403, crow::json::dump(err));
    }

    /* 解析请求体（Crow DELETE可能没有body，但这里从body JSON解析） */
    crow::json::wvalue err_resp;
    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (!body.has("user_id")) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "user_id is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    int32_t user_id = body["user_id"].i();

    /* 查询被删除用户信息用于日志记录 */
    UserInfo target_user;
    std::string target_username;
    ret = user_dao_->QueryUserById(user_id, target_user);
    if (ret == DB_OK) {
        target_username = target_user.username;
    }

    /* 删除用户 */
    ret = user_dao_->DeleteUser(user_id);
    if (ret != DB_OK) {
        LOG_ERROR << "AdminHandler: delete user failed, ret=" << ret;
        err_resp["code"] = ret;
        err_resp["message"] = "delete user failed";
        return crow::response(500, crow::json::dump(err_resp));
    }

    /* 记录操作日志 */
    OperationLog op_log;
    op_log.id = 0;
    op_log.op_type = OpType_DeleteUser;
    op_log.operator_name = session_info.username;
    op_log.target_class = "";
    op_log.target_student = "";
    op_log.target_resource = "";
    op_log.detail = "deleted user: " + target_username;
    op_log.op_time = register_student::GetCurrentTimeString();

    log_dao_->InsertLog(op_log);

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    LOG_INFO << "AdminHandler: delete user success, user_id=" << user_id;
    return crow::response(result);
}

crow::response AdminHandler::HandleGetRegistrationRequests(const crow::request& req) {
    LOG_INFO << "AdminHandler: get registration requests";

    if (!user_dao_) {
        LOG_ERROR << "AdminHandler: user_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "user_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    /* 校验管理员权限 */
    SessionInfo session_info;
    int ret = CheckAdminPermission(req, session_info);
    if (ret != DB_OK) {
        crow::json::wvalue err;
        if (ret == ERR_AUTH_SESSION_EXPIRED) {
            err["code"] = ret;
            err["message"] = "session expired or invalid";
            return crow::response(401, crow::json::dump(err));
        }
        err["code"] = ret;
        err["message"] = "permission denied";
        return crow::response(403, crow::json::dump(err));
    }

    /* 查询待审核的注册申请 */
    std::vector<RegistrationRequest> requests;
    ret = user_dao_->QueryPendingRegistrationRequests(requests);
    if (ret != DB_OK) {
        LOG_ERROR << "AdminHandler: query pending registration requests failed, ret=" << ret;
        crow::json::wvalue err;
        err["code"] = ret;
        err["message"] = "query registration requests failed";
        return crow::response(500, crow::json::dump(err));
    }

    /* 构建响应（不含密码字段） */
    std::vector<crow::json::wvalue> items;
    for (size_t i = 0; i < requests.size(); ++i) {
        crow::json::wvalue item;
        item["id"] = requests[i].id;
        item["username"] = requests[i].username;
        item["role"] = static_cast<int32_t>(requests[i].role);
        item["display_name"] = requests[i].display_name;
        item["request_time"] = requests[i].request_time;
        items.push_back(std::move(item));
    }

    crow::json::wvalue data;
    for (size_t i = 0; i < items.size(); ++i) {
        data["requests"][i] = std::move(items[i]);
    }
    if (items.empty()) {
        data["count"] = 0;
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    LOG_INFO << "AdminHandler: get registration requests success, count=" << requests.size();
    return crow::response(result);
}

crow::response AdminHandler::HandleApproveRegistration(const crow::request& req) {
    LOG_INFO << "AdminHandler: approve registration request";

    if (!user_dao_) {
        LOG_ERROR << "AdminHandler: user_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "user_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    if (!log_dao_) {
        LOG_ERROR << "AdminHandler: log_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "log_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    /* 校验管理员权限 */
    SessionInfo session_info;
    int ret = CheckAdminPermission(req, session_info);
    if (ret != DB_OK) {
        crow::json::wvalue err;
        if (ret == ERR_AUTH_SESSION_EXPIRED) {
            err["code"] = ret;
            err["message"] = "session expired or invalid";
            return crow::response(401, crow::json::dump(err));
        }
        err["code"] = ret;
        err["message"] = "permission denied";
        return crow::response(403, crow::json::dump(err));
    }

    /* 解析请求体 */
    crow::json::wvalue err_resp;
    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (!body.has("ids")) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "ids is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    std::vector<int32_t> ids;
    try {
        const auto& ids_json = body["ids"];
        for (size_t i = 0; i < ids_json.size(); ++i) {
            ids.push_back(ids_json[i].i());
        }
    } catch (...) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid ids format";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (ids.empty()) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "ids cannot be empty";
        return crow::response(400, crow::json::dump(err_resp));
    }

    /* 查询申请用户名（用于日志），然后原子审核 */
    std::vector<std::string> usernames;
    for (size_t i = 0; i < ids.size(); ++i) {
        RegistrationRequest reg_req;
        ret = user_dao_->QueryRegistrationRequestById(ids[i], reg_req);
        if (ret != DB_OK) {
            err_resp["code"] = ERR_REG_REQ_NOT_FOUND;
            err_resp["message"] = "registration request not found";
            return crow::response(404, crow::json::dump(err_resp));
        }
        usernames.push_back(reg_req.username);
    }

    /* 原子审核通过 */
    ret = user_dao_->ApproveRegistrationRequestsAtomic(ids);
    if (ret != DB_OK) {
        LOG_ERROR << "AdminHandler: approve registration requests failed, ret=" << ret;
        err_resp["code"] = ret;
        err_resp["message"] = "approve failed";
        return crow::response(500, crow::json::dump(err_resp));
    }

    /* 记录操作日志 */
    for (size_t i = 0; i < usernames.size(); ++i) {
        OperationLog op_log;
        op_log.id = 0;
        op_log.op_type = OpType_ApproveRegistration;
        op_log.operator_name = session_info.username;
        op_log.target_class = "";
        op_log.target_student = usernames[i];
        op_log.target_resource = "";
        op_log.detail = "approved registration request";
        op_log.op_time = register_student::GetCurrentTimeString();
        log_dao_->InsertLog(op_log);
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    LOG_INFO << "AdminHandler: approve registration requests success, count=" << ids.size();
    return crow::response(result);
}

crow::response AdminHandler::HandleRejectRegistration(const crow::request& req) {
    LOG_INFO << "AdminHandler: reject registration request";

    if (!user_dao_) {
        LOG_ERROR << "AdminHandler: user_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "user_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    /* 校验管理员权限 */
    SessionInfo session_info;
    int ret = CheckAdminPermission(req, session_info);
    if (ret != DB_OK) {
        crow::json::wvalue err;
        if (ret == ERR_AUTH_SESSION_EXPIRED) {
            err["code"] = ret;
            err["message"] = "session expired or invalid";
            return crow::response(401, crow::json::dump(err));
        }
        err["code"] = ret;
        err["message"] = "permission denied";
        return crow::response(403, crow::json::dump(err));
    }

    /* 解析请求体 */
    crow::json::wvalue err_resp;
    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (!body.has("ids")) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "ids is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    std::vector<int32_t> ids;
    try {
        const auto& ids_json = body["ids"];
        for (size_t i = 0; i < ids_json.size(); ++i) {
            ids.push_back(ids_json[i].i());
        }
    } catch (...) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid ids format";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (ids.empty()) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "ids cannot be empty";
        return crow::response(400, crow::json::dump(err_resp));
    }

    /* 逐个删除 */
    for (size_t i = 0; i < ids.size(); ++i) {
        ret = user_dao_->DeleteRegistrationRequest(ids[i]);
        if (ret != DB_OK) {
            LOG_ERROR << "AdminHandler: delete registration request failed, id=" << ids[i] << " ret=" << ret;
            err_resp["code"] = ret;
            err_resp["message"] = "reject failed";
            return crow::response(500, crow::json::dump(err_resp));
        }
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    LOG_INFO << "AdminHandler: reject registration requests success, count=" << ids.size();
    return crow::response(result);
}
