#include "resource_handler.h"
#include "utils.h"
#include "error_codes.h"
#include "resource_types.h"

ResourceHandler::ResourceHandler(IResourceDao* resource_dao, IOperationLogDao* log_dao, SessionManager* session_mgr)
    : resource_dao_(resource_dao)
    , log_dao_(log_dao)
    , session_mgr_(session_mgr) {}

ResourceHandler::~ResourceHandler() {}

void ResourceHandler::RegisterRoutes(crow::SimpleApp& app) {
    CROW_ROUTE(app, "/api/resource/list").methods("GET"_method)
    ([this](const crow::request&) {
        return HandleListResources();
    });

    CROW_ROUTE(app, "/api/resource/create").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleCreateResource(req);
    });

    CROW_ROUTE(app, "/api/resource/update").methods("PUT"_method)
    ([this](const crow::request& req) {
        return HandleUpdateResource(req);
    });

    CROW_ROUTE(app, "/api/resource/delete").methods("DELETE"_method)
    ([this](const crow::request& req) {
        return HandleDeleteResource(req);
    });

    CROW_ROUTE(app, "/api/resource/usage").methods("GET"_method)
    ([this](const crow::request& req) {
        return HandleGetResourceUsage(req);
    });
}

std::string ResourceHandler::GetSessionIdFromCookie(const crow::request& req) {
    std::string cookie = const_cast<crow::request&>(req).get_header_value("Cookie");
    if (cookie.empty()) {
        return "";
    }

    std::string key = "session_id=";
    size_t pos = cookie.find(key);
    if (pos == std::string::npos) {
        return "";
    }

    pos += key.size();
    size_t end = cookie.find(';', pos);
    if (end == std::string::npos) {
        return cookie.substr(pos);
    }
    return cookie.substr(pos, end - pos);
}

int ResourceHandler::CheckAdminPermission(const crow::request& req, SessionInfo& info) {
    std::string session_id = GetSessionIdFromCookie(req);
    int ret = session_mgr_->ValidateSession(session_id, info);
    if (ret != DB_OK) {
        return ERR_AUTH_SESSION_EXPIRED;
    }
    if (info.role != UserRole_Admin) {
        return ERR_AUTH_PERMISSION_DENIED;
    }
    return DB_OK;
}

int32_t ResourceHandler::ExtractIntParam(const crow::request& req, const std::string& key, int32_t default_val) {
    char* val = const_cast<crow::request&>(req).url_params.get(key);
    if (val && val[0] != '\0') {
        return static_cast<int32_t>(std::atoi(val));
    }
    return default_val;
}

crow::response ResourceHandler::HandleListResources() {
    LOG_INFO << "ResourceHandler: list resources request";

    std::vector<ResourceInfo> resources;
    int ret = resource_dao_->QueryAllResources(resources);
    if (ret != DB_OK) {
        crow::json::wvalue err;
        err["code"] = ret;
        err["message"] = "failed to query resources";
        return crow::response(500, crow::json::dump(err));
    }

    crow::json::wvalue data;
    for (size_t i = 0; i < resources.size(); ++i) {
        crow::json::wvalue item;
        item["id"] = resources[i].id;
        item["name"] = resources[i].name;
        item["total_count"] = resources[i].total_count;
        item["used_count"] = resources[i].used_count;
        item["remain_count"] = resources[i].remain_count;
        item["resource_type"] = resources[i].resource_type;
        item["bed_reserved_count"] = resources[i].bed_reserved_count;
        data["list"][i] = std::move(item);
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);
    return crow::response(result);
}

crow::response ResourceHandler::HandleCreateResource(const crow::request& req) {
    LOG_INFO << "ResourceHandler: create resource request";

    SessionInfo info;
    int perm = CheckAdminPermission(req, info);
    if (perm != DB_OK) {
        crow::json::wvalue err;
        err["code"] = perm;
        err["message"] = (perm == ERR_AUTH_SESSION_EXPIRED ? "session expired" : "permission denied");
        return crow::response(perm == ERR_AUTH_SESSION_EXPIRED ? 401 : 403, crow::json::dump(err));
    }

    crow::json::rvalue body = crow::json::load(req.body);
    if (!body) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err));
    }

    std::string name = body["name"].s();
    int32_t total_count = static_cast<int32_t>(body["total_count"].i());
    int32_t resource_type = body.has("resource_type") ? static_cast<int32_t>(body["resource_type"].i()) : ResourceType_Other;

    /* 资源类型校验：床位类型固定名称为"床位"，其他类型用户自定义名称 */
    if (resource_type == ResourceType_Bed) {
        name = "\xe5\xba\x8a\xe4\xbd\x8d"; /* "床位" */
    } else {
        resource_type = ResourceType_Other;
    }

    if (name.empty() || total_count <= 0) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "invalid name or total_count";
        return crow::response(400, crow::json::dump(err));
    }

    /* 床位类型只能存在一个 */
    if (resource_type == ResourceType_Bed) {
        ResourceInfo existing_bed;
        if (resource_dao_->QueryResourceByType(ResourceType_Bed, existing_bed) == DB_OK) {
            crow::json::wvalue err;
            err["code"] = ERR_INVALID_PARAM;
            err["message"] = "床位资源已存在，不能重复创建";
            return crow::response(400, crow::json::dump(err));
        }
    }

    ResourceInfo res;
    res.name = name;
    res.total_count = total_count;
    res.used_count = 0;
    res.remain_count = total_count;
    res.resource_type = resource_type;

    int ret = resource_dao_->InsertResource(res);
    if (ret != DB_OK) {
        crow::json::wvalue err;
        err["code"] = ret;
        err["message"] = "failed to insert resource";
        return crow::response(500, crow::json::dump(err));
    }

    /* 记录操作日志 */
    OperationLog log;
    log.op_type = OpType_CreateResource;
    log.operator_name = info.username;
    log.target_resource = name;
    log.detail = "create resource";
    log.op_time = register_student::GetCurrentTimeString();
    log_dao_->InsertLog(log);

    crow::json::wvalue data;
    data["code"] = DB_OK;
    data["message"] = "success";
    return crow::response(data);
}

crow::response ResourceHandler::HandleUpdateResource(const crow::request& req) {
    LOG_INFO << "ResourceHandler: update resource request";

    SessionInfo info;
    int perm = CheckAdminPermission(req, info);
    if (perm != DB_OK) {
        crow::json::wvalue err;
        err["code"] = perm;
        err["message"] = (perm == ERR_AUTH_SESSION_EXPIRED ? "session expired" : "permission denied");
        return crow::response(perm == ERR_AUTH_SESSION_EXPIRED ? 401 : 403, crow::json::dump(err));
    }

    crow::json::rvalue body = crow::json::load(req.body);
    if (!body) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err));
    }

    int32_t id = static_cast<int32_t>(body["id"].i());
    int32_t total_count = static_cast<int32_t>(body["total_count"].i());

    if (id <= 0 || total_count <= 0) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "invalid id or total_count";
        return crow::response(400, crow::json::dump(err));
    }

    int ret = resource_dao_->UpdateResourceTotal(id, total_count);
    if (ret != DB_OK) {
        crow::json::wvalue err;
        err["code"] = ret;
        err["message"] = "failed to update resource";
        return crow::response(500, crow::json::dump(err));
    }

    /* 记录操作日志 */
    OperationLog log;
    log.op_type = OpType_ModifyResource;
    log.operator_name = info.username;
    log.detail = "update resource total to " + std::to_string(total_count);
    log.op_time = register_student::GetCurrentTimeString();
    log_dao_->InsertLog(log);

    crow::json::wvalue data;
    data["code"] = DB_OK;
    data["message"] = "success";
    return crow::response(data);
}

crow::response ResourceHandler::HandleDeleteResource(const crow::request& req) {
    LOG_INFO << "ResourceHandler: delete resource request";

    SessionInfo info;
    int perm = CheckAdminPermission(req, info);
    if (perm != DB_OK) {
        crow::json::wvalue err;
        err["code"] = perm;
        err["message"] = (perm == ERR_AUTH_SESSION_EXPIRED ? "session expired" : "permission denied");
        return crow::response(perm == ERR_AUTH_SESSION_EXPIRED ? 401 : 403, crow::json::dump(err));
    }

    crow::json::rvalue body = crow::json::load(req.body);
    if (!body) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err));
    }

    int32_t id = static_cast<int32_t>(body["id"].i());

    if (id <= 0) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "invalid id";
        return crow::response(400, crow::json::dump(err));
    }

    /* 检查资源是否被未结束班级使用 */
    std::vector<std::string> using_classes;
    int ret = resource_dao_->CheckResourceInUse(id, using_classes);
    if (ret != DB_OK) {
        crow::json::wvalue err;
        err["code"] = ret;
        err["message"] = "failed to check resource in use";
        return crow::response(500, crow::json::dump(err));
    }

    if (!using_classes.empty()) {
        std::string class_list;
        for (size_t i = 0; i < using_classes.size(); ++i) {
            if (i > 0) {
                class_list += ", ";
            }
            class_list += using_classes[i];
        }

        crow::json::wvalue err;
        err["code"] = ERR_RESOURCE_IN_USE;
        err["message"] = "resource in use by classes: " + class_list;
        return crow::response(400, crow::json::dump(err));
    }

    /* 查询资源名称用于日志 */
    ResourceInfo res;
    resource_dao_->QueryResourceById(id, res);

    ret = resource_dao_->DeleteResource(id);
    if (ret != DB_OK) {
        crow::json::wvalue err;
        err["code"] = ret;
        err["message"] = "failed to delete resource";
        return crow::response(500, crow::json::dump(err));
    }

    /* 记录操作日志 */
    OperationLog log;
    log.op_type = OpType_DeleteResource;
    log.operator_name = info.username;
    log.target_resource = res.name;
    log.detail = "delete resource";
    log.op_time = register_student::GetCurrentTimeString();
    log_dao_->InsertLog(log);

    crow::json::wvalue data;
    data["code"] = DB_OK;
    data["message"] = "success";
    return crow::response(data);
}

crow::response ResourceHandler::HandleGetResourceUsage(const crow::request& req) {
    LOG_INFO << "ResourceHandler: get resource usage request";

    int32_t id = ExtractIntParam(req, "id", 0);
    if (id <= 0) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "invalid id";
        return crow::response(400, crow::json::dump(err));
    }

    ResourceInfo res;
    int ret = resource_dao_->QueryResourceById(id, res);
    if (ret != DB_OK) {
        crow::json::wvalue err;
        err["code"] = ret;
        err["message"] = "failed to query resource";
        return crow::response(500, crow::json::dump(err));
    }

    std::vector<ResourceAllocation> allocs;
    ret = resource_dao_->QueryAllocationsByResourceId(id, allocs);
    if (ret != DB_OK) {
        crow::json::wvalue err;
        err["code"] = ret;
        err["message"] = "failed to query allocations";
        return crow::response(500, crow::json::dump(err));
    }

    crow::json::wvalue data;
    data["id"] = res.id;
    data["name"] = res.name;
    data["total_count"] = res.total_count;
    data["used_count"] = res.used_count;
    data["remain_count"] = res.remain_count;

    for (size_t i = 0; i < allocs.size(); ++i) {
        crow::json::wvalue item;
        item["id"] = allocs[i].id;
        item["student_name"] = allocs[i].student_name;
        item["student_gender"] = allocs[i].student_gender;
        item["teacher_name"] = allocs[i].teacher_name;
        item["class_name"] = allocs[i].class_name;
        item["resource_code"] = allocs[i].resource_code;
        item["allocate_time"] = allocs[i].allocate_time;
        data["allocations"][i] = std::move(item);
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);
    return crow::response(result);
}
