#include "auth_handler.h"
#include "utils.h"
#include "error_codes.h"
#include "log_types.h"

AuthHandler::AuthHandler(IUserDao* user_dao, IOperationLogDao* log_dao, SessionManager* session_mgr)
    : user_dao_(user_dao)
    , log_dao_(log_dao)
    , session_mgr_(session_mgr) {}

AuthHandler::~AuthHandler() {}

void AuthHandler::RegisterRoutes(crow::SimpleApp& app) {
    CROW_ROUTE(app, "/api/auth/login").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleLogin(req);
    });

    CROW_ROUTE(app, "/api/auth/register").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleRegister(req);
    });

    CROW_ROUTE(app, "/api/auth/logout").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleLogout(req);
    });

    CROW_ROUTE(app, "/api/auth/change-password").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleChangePassword(req);
    });

    CROW_ROUTE(app, "/api/auth/reset-request").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleResetRequest(req);
    });

    CROW_ROUTE(app, "/api/auth/check-admin").methods("GET"_method)
    ([this]() {
        return HandleCheckAdmin();
    });

    CROW_ROUTE(app, "/api/auth/session").methods("GET"_method)
    ([this](const crow::request& req) {
        return HandleGetSession(req);
    });
}

std::string AuthHandler::GetSessionIdFromCookie(const crow::request& req) {
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

crow::response AuthHandler::HandleLogin(const crow::request& req) {
    LOG_INFO << "AuthHandler: login request received";

    if (!user_dao_) {
        LOG_ERROR << "AuthHandler: user_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "user_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    if (!session_mgr_) {
        LOG_ERROR << "AuthHandler: session_mgr is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "session_mgr is null";
        return crow::response(500, crow::json::dump(err));
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

    if (!body.has("username") || std::string(body["username"].s()).empty()) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "username is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (!body.has("password") || std::string(body["password"].s()).empty()) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "password is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    std::string username = body["username"].s();
    std::string password = body["password"].s();

    /* 查询用户 */
    UserInfo user_info;
    int ret = user_dao_->QueryUserByUsername(username, user_info);
    if (ret != DB_OK) {
        /* 检查是否在注册审核表中 */
        int reg_status = -1;
        int reg_ret = user_dao_->QueryRegistrationRequestStatus(username, reg_status);
        if (reg_ret == DB_OK && reg_status == RegStatus_Pending) {
            LOG_ERROR << "AuthHandler: account pending review: " << username;
            err_resp["code"] = ERR_AUTH_ACCOUNT_PENDING;
            err_resp["message"] = "account is pending review";
            return crow::response(403, crow::json::dump(err_resp));
        }
        LOG_ERROR << "AuthHandler: user not found: " << username;
        err_resp["code"] = ERR_AUTH_USER_NOT_FOUND;
        err_resp["message"] = "user not found";
        return crow::response(401, crow::json::dump(err_resp));
    }

    /* 校验密码 */
    bool valid = register_student::VerifyPassword(password, user_info.salt, user_info.password_hash);
    if (!valid) {
        LOG_ERROR << "AuthHandler: password verification failed for user: " << username;
        err_resp["code"] = ERR_AUTH_INVALID_CREDENTIALS;
        err_resp["message"] = "invalid password";
        return crow::response(401, crow::json::dump(err_resp));
    }

    /* 校验角色权限（前端指定需要登录的角色） */
    if (body.has("role")) {
        UserRoleType required_role = static_cast<UserRoleType>(body["role"].i());
        if (user_info.role != required_role) {
            LOG_ERROR << "AuthHandler: role mismatch, required=" << static_cast<int>(required_role)
                      << " actual=" << static_cast<int>(user_info.role);
            err_resp["code"] = ERR_AUTH_PERMISSION_DENIED;
            err_resp["message"] = "permission denied";
            return crow::response(403, crow::json::dump(err_resp));
        }
    }

    /* 创建会话 */
    std::string session_id = session_mgr_->CreateSession(user_info.id, user_info.username, user_info.role);

    /* 构建响应 */
    crow::json::wvalue data;
    data["user_id"] = user_info.id;
    data["username"] = user_info.username;
    data["role"] = static_cast<int32_t>(user_info.role);
    if (!user_info.display_name.empty()) {
        data["display_name"] = user_info.display_name;
    } else {
        data["display_name"] = user_info.username;
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    crow::response resp(crow::json::dump(result));
    resp.set_header("Set-Cookie", "session_id=" + session_id + "; HttpOnly; Path=/");
    LOG_INFO << "AuthHandler: login success for user: " << username;
    return resp;
}

crow::response AuthHandler::HandleRegister(const crow::request& req) {
    LOG_INFO << "AuthHandler: register request received";

    if (!user_dao_) {
        LOG_ERROR << "AuthHandler: user_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "user_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    if (!session_mgr_) {
        LOG_ERROR << "AuthHandler: session_mgr is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "session_mgr is null";
        return crow::response(500, crow::json::dump(err));
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

    if (!body.has("username") || std::string(body["username"].s()).empty()) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "username is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (!body.has("password") || std::string(body["password"].s()).empty()) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "password is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    std::string username = body["username"].s();
    std::string password = body["password"].s();
    UserRoleType role = UserRole_Teacher;
    if (body.has("role")) {
        role = static_cast<UserRoleType>(body["role"].i());
    }

    /* 如果注册管理员，检查是否已存在 */
    if (role == UserRole_Admin) {
        int admin_exists = user_dao_->CheckAdminExists();
        if (admin_exists != 0) {
            LOG_ERROR << "AuthHandler: admin already exists";
            err_resp["code"] = ERR_AUTH_ADMIN_EXISTS;
            err_resp["message"] = "admin already exists";
            return crow::response(403, crow::json::dump(err_resp));
        }
    }

    /* 教师注册需管理员已存在 */
    if (role == UserRole_Teacher) {
        int admin_exists = user_dao_->CheckAdminExists();
        if (admin_exists == 0) {
            LOG_ERROR << "AuthHandler: admin not exists, cannot register teacher";
            err_resp["code"] = ERR_AUTH_ADMIN_NOT_EXISTS;
            err_resp["message"] = "please create admin account first";
            return crow::response(403, crow::json::dump(err_resp));
        }
    }

    /* 加密密码 */
    std::string salt = register_student::GenerateSalt();
    std::string password_hash = register_student::EncryptPassword(password, salt);

    /* 教师注册：写入注册审核申请 */
    if (role == UserRole_Teacher) {
        /* 检查用户名是否已存在于 users 表 */
        UserInfo existing_user;
        int check_ret = user_dao_->QueryUserByUsername(username, existing_user);
        if (check_ret == DB_OK) {
            err_resp["code"] = ERR_REG_REQ_DUPLICATE;
            err_resp["message"] = "username already exists";
            return crow::response(409, crow::json::dump(err_resp));
        }

        /* 检查用户名是否在审核中 */
        int req_exists = user_dao_->CheckRegistrationRequestExists(username);
        if (req_exists == 1) {
            err_resp["code"] = ERR_REG_REQ_DUPLICATE;
            err_resp["message"] = "username is pending review";
            return crow::response(409, crow::json::dump(err_resp));
        }

        RegistrationRequest reg_req;
        reg_req.id = 0;
        reg_req.username = username;
        reg_req.password_hash = password_hash;
        reg_req.salt = salt;
        reg_req.role = role;
        reg_req.display_name = username;
        reg_req.status = RegStatus_Pending;
        reg_req.request_time = register_student::GetCurrentTimeString();

        int ret = user_dao_->InsertRegistrationRequest(reg_req);
        if (ret != DB_OK) {
            LOG_ERROR << "AuthHandler: insert registration request failed, ret=" << ret;
            err_resp["code"] = ret;
            err_resp["message"] = "register failed";
            return crow::response(500, crow::json::dump(err_resp));
        }

        crow::json::wvalue data;
        data["pending_review"] = true;

        crow::json::wvalue result;
        result["code"] = DB_OK;
        result["message"] = "success";
        result["data"] = std::move(data);

        LOG_INFO << "AuthHandler: registration request submitted for user: " << username;
        return crow::response(crow::json::dump(result));
    }

    /* 管理员注册：直接创建用户 */
    /* 构建用户信息 */
    UserInfo user_info;
    user_info.id = 0;
    user_info.username = username;
    user_info.password_hash = password_hash;
    user_info.salt = salt;
    user_info.role = role;
    user_info.display_name = username;
    user_info.create_time = register_student::GetCurrentTimeString();

    /* 插入用户 */
    int ret = user_dao_->InsertUser(user_info);
    if (ret != DB_OK) {
        LOG_ERROR << "AuthHandler: insert user failed, ret=" << ret;
        err_resp["code"] = ret;
        err_resp["message"] = "register failed";
        return crow::response(500, crow::json::dump(err_resp));
    }

    /* 查询刚插入的用户以获取自增ID */
    UserInfo inserted_user;
    ret = user_dao_->QueryUserByUsername(username, inserted_user);
    if (ret != DB_OK) {
        LOG_ERROR << "AuthHandler: query inserted user failed, ret=" << ret;
        err_resp["code"] = ret;
        err_resp["message"] = "register failed";
        return crow::response(500, crow::json::dump(err_resp));
    }

    /* 构建响应 */
    crow::json::wvalue data;
    data["user_id"] = inserted_user.id;
    data["username"] = inserted_user.username;
    data["role"] = static_cast<int32_t>(inserted_user.role);
    if (!inserted_user.display_name.empty()) {
        data["display_name"] = inserted_user.display_name;
    } else {
        data["display_name"] = inserted_user.username;
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    crow::response resp(crow::json::dump(result));
    LOG_INFO << "AuthHandler: register success for user: " << username;
    return resp;
}

crow::response AuthHandler::HandleLogout(const crow::request& req) {
    LOG_INFO << "AuthHandler: logout request received";

    if (!session_mgr_) {
        LOG_ERROR << "AuthHandler: session_mgr is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "session_mgr is null";
        return crow::response(500, crow::json::dump(err));
    }

    std::string session_id = GetSessionIdFromCookie(req);
    if (!session_id.empty()) {
        session_mgr_->DestroySession(session_id);
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";

    crow::response resp(crow::json::dump(result));
    resp.set_header("Set-Cookie", "session_id=; HttpOnly; Path=/; Max-Age=0");
    LOG_INFO << "AuthHandler: logout success";
    return resp;
}

crow::response AuthHandler::HandleChangePassword(const crow::request& req) {
    LOG_INFO << "AuthHandler: change password request received";

    if (!user_dao_) {
        LOG_ERROR << "AuthHandler: user_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "user_dao is null";
        return crow::response(500, crow::json::dump(err));
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

    if (!body.has("old_password") || std::string(body["old_password"].s()).empty()) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "old_password is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (!body.has("new_password") || std::string(body["new_password"].s()).empty()) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "new_password is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    std::string old_password = body["old_password"].s();
    std::string new_password = body["new_password"].s();

    /* 确定用户：优先用 username 字段，否则用 session */
    UserInfo user_info;
    int32_t target_user_id = -1;
    std::string target_username;

    if (body.has("username") && !std::string(body["username"].s()).empty()) {
        /* 基于用户名修改密码（无需登录） */
        target_username = body["username"].s();
        int ret = user_dao_->QueryUserByUsername(target_username, user_info);
        if (ret != DB_OK) {
            LOG_DEBUG << "AuthHandler: username not found: " << target_username;
            err_resp["code"] = ERR_AUTH_USER_NOT_FOUND;
            err_resp["message"] = "username not found";
            return crow::response(crow::json::dump(err_resp));
        }
        target_user_id = user_info.id;
    } else {
        /* 基于会话修改密码（需登录） */
        if (!session_mgr_) {
            LOG_ERROR << "AuthHandler: session_mgr is null";
            crow::json::wvalue err;
            err["code"] = ERR_HANDLER_NULL_DAO;
            err["message"] = "session_mgr is null";
            return crow::response(500, crow::json::dump(err));
        }

        std::string session_id = GetSessionIdFromCookie(req);
        SessionInfo session_info;
        int ret = session_mgr_->ValidateSession(session_id, session_info);
        if (ret != DB_OK) {
            LOG_DEBUG << "AuthHandler: no valid session";
            crow::json::wvalue err;
            err["code"] = ERR_AUTH_SESSION_EXPIRED;
            err["message"] = "session expired or invalid";
            return crow::response(crow::json::dump(err));
        }

        ret = user_dao_->QueryUserById(session_info.user_id, user_info);
        if (ret != DB_OK) {
            LOG_ERROR << "AuthHandler: query user failed, ret=" << ret;
            err_resp["code"] = ret;
            err_resp["message"] = "query user failed";
            return crow::response(500, crow::json::dump(err_resp));
        }
        target_user_id = session_info.user_id;
        target_username = session_info.username;
    }

    /* 校验旧密码 */
    bool valid = register_student::VerifyPassword(old_password, user_info.salt, user_info.password_hash);
    if (!valid) {
        LOG_ERROR << "AuthHandler: old password verification failed for user: " << target_username;
        err_resp["code"] = ERR_AUTH_INVALID_CREDENTIALS;
        err_resp["message"] = "old password is incorrect";
        return crow::response(401, crow::json::dump(err_resp));
    }

    /* 加密新密码 */
    std::string new_salt = register_student::GenerateSalt();
    std::string new_hash = register_student::EncryptPassword(new_password, new_salt);

    /* 更新密码 */
    int ret = user_dao_->UpdatePassword(target_user_id, new_hash, new_salt);
    if (ret != DB_OK) {
        LOG_ERROR << "AuthHandler: update password failed, ret=" << ret;
        err_resp["code"] = ret;
        err_resp["message"] = "update password failed";
        return crow::response(500, crow::json::dump(err_resp));
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    LOG_INFO << "AuthHandler: change password success for user: " << target_username;
    return crow::response(result);
}

crow::response AuthHandler::HandleResetRequest(const crow::request& req) {
    LOG_INFO << "AuthHandler: reset request received";

    if (!user_dao_) {
        LOG_ERROR << "AuthHandler: user_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "user_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    if (!log_dao_) {
        LOG_ERROR << "AuthHandler: log_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "log_dao is null";
        return crow::response(500, crow::json::dump(err));
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

    if (!body.has("username") || std::string(body["username"].s()).empty()) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "username is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    std::string username = body["username"].s();

    /* 查询用户 */
    UserInfo user_info;
    int ret = user_dao_->QueryUserByUsername(username, user_info);
    if (ret != DB_OK) {
        LOG_ERROR << "AuthHandler: query user by username failed, ret=" << ret;
        err_resp["code"] = ret;
        err_resp["message"] = "user not found";
        return crow::response(404, crow::json::dump(err_resp));
    }

    /* 检查是否已有待审批的重置请求 */
    int pending = user_dao_->CheckResetPending(user_info.id);
    if (pending != 0) {
        LOG_ERROR << "AuthHandler: reset request already pending for user: " << username;
        err_resp["code"] = ERR_AUTH_RESET_PENDING;
        err_resp["message"] = "reset request already pending";
        return crow::response(200, crow::json::dump(err_resp));
    }

    /* 构建重置请求 */
    PasswordResetRequest reset_req;
    reset_req.id = 0;
    reset_req.user_id = user_info.id;
    reset_req.username = user_info.username;
    reset_req.status = ResetStatus_Pending;
    reset_req.approver_id = 0;
    reset_req.new_password_hash = "";
    reset_req.new_salt = "";
    reset_req.request_time = register_student::GetCurrentTimeString();
    reset_req.approve_time = "";

    ret = user_dao_->InsertResetRequest(reset_req);
    if (ret != DB_OK) {
        LOG_ERROR << "AuthHandler: insert reset request failed, ret=" << ret;
        err_resp["code"] = ret;
        err_resp["message"] = "submit reset request failed";
        return crow::response(500, crow::json::dump(err_resp));
    }

    /* 记录操作日志 */
    OperationLog op_log;
    op_log.id = 0;
    op_log.op_type = OpType_ResetPassword;
    op_log.operator_name = username;
    op_log.target_class = "";
    op_log.target_student = "";
    op_log.target_resource = "";
    op_log.detail = "password reset request submitted";
    op_log.op_time = register_student::GetCurrentTimeString();

    log_dao_->InsertLog(op_log);

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    LOG_INFO << "AuthHandler: reset request submitted for user: " << username;
    return crow::response(result);
}

crow::response AuthHandler::HandleCheckAdmin() {
    LOG_INFO << "AuthHandler: check admin request received";

    if (!user_dao_) {
        LOG_ERROR << "AuthHandler: user_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "user_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    int admin_exists = user_dao_->CheckAdminExists();

    crow::json::wvalue data;
    data["exists"] = (admin_exists != 0);

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);
    LOG_INFO << "AuthHandler: check admin, exists=" << (admin_exists != 0);
    return crow::response(result);
}

crow::response AuthHandler::HandleGetSession(const crow::request& req) {
    LOG_INFO << "AuthHandler: get session request received";

    if (!session_mgr_) {
        LOG_ERROR << "AuthHandler: session_mgr is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "session_mgr is null";
        return crow::response(500, crow::json::dump(err));
    }

    /* 校验会话 */
    std::string session_id = GetSessionIdFromCookie(req);
    SessionInfo session_info;
    int ret = session_mgr_->ValidateSession(session_id, session_info);
    if (ret != DB_OK) {
        LOG_DEBUG << "AuthHandler: no valid session";
        crow::json::wvalue err;
        err["code"] = ERR_AUTH_SESSION_EXPIRED;
        err["message"] = "session expired or invalid";
        return crow::response(crow::json::dump(err));
    }

    /* 构建响应 */
    crow::json::wvalue data;
    data["user_id"] = session_info.user_id;
    data["username"] = session_info.username;
    data["role"] = static_cast<int32_t>(session_info.role);

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);
    LOG_INFO << "AuthHandler: get session success for user: " << session_info.username;
    return crow::response(result);
}
