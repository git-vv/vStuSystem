#include "class_create_handler.h"
#include "utils.h"
#include "error_codes.h"
#include "log_types.h"
#include "upload_util.h"
#include "config.h"

#include <vector>
#include <string>

/* base64解码辅助函数 */
static int Base64CharValue(char c) {
    if (c >= 'A' && c <= 'Z') { return c - 'A'; }
    if (c >= 'a' && c <= 'z') { return c - 'a' + 26; }
    if (c >= '0' && c <= '9') { return c - '0' + 52; }
    if (c == '+') { return 62; }
    if (c == '/') { return 63; }
    return -1;
}

static std::vector<uint8_t> Base64Decode(const std::string& encoded) {
    std::vector<uint8_t> result;
    if (encoded.empty()) {
        return result;
    }

    size_t i = 0;
    size_t len = encoded.size();

    while (i < len) {
        /* 跳过空白字符 */
        while (i < len && (encoded[i] == ' ' || encoded[i] == '\t' || encoded[i] == '\n' || encoded[i] == '\r')) {
            ++i;
        }
        if (i >= len) { break; }

        int a = Base64CharValue(encoded[i]);
        ++i;
        while (i < len && (encoded[i] == ' ' || encoded[i] == '\t')) { ++i; }
        int b = (i < len && encoded[i] != '=') ? Base64CharValue(encoded[i]) : 0;
        if (i < len) { ++i; }
        while (i < len && (encoded[i] == ' ' || encoded[i] == '\t')) { ++i; }
        int c = (i < len && encoded[i] != '=') ? Base64CharValue(encoded[i]) : 0;
        if (i < len) { ++i; }
        while (i < len && (encoded[i] == ' ' || encoded[i] == '\t')) { ++i; }
        int d = (i < len && encoded[i] != '=') ? Base64CharValue(encoded[i]) : 0;
        if (i < len) { ++i; }

        if (a < 0 || b < 0 || c < 0 || d < 0) { break; }

        uint32_t triple = (static_cast<uint32_t>(a) << 18) |
                          (static_cast<uint32_t>(b) << 12) |
                          (static_cast<uint32_t>(c) << 6) |
                          static_cast<uint32_t>(d);

        result.push_back(static_cast<uint8_t>((triple >> 16) & 0xFF));
        result.push_back(static_cast<uint8_t>((triple >> 8) & 0xFF));
        result.push_back(static_cast<uint8_t>(triple & 0xFF));
    }

    /* 去除padding对应的额外字节 */
    if (len >= 1 && encoded[len - 1] == '=') {
        result.pop_back();
        if (len >= 2 && encoded[len - 2] == '=') {
            result.pop_back();
        }
    }

    return result;
}

ClassCreateHandler::ClassCreateHandler(IClassDao* class_dao, IOperationLogDao* log_dao, SessionManager* session_mgr)
    : class_dao_(class_dao)
    , log_dao_(log_dao)
    , session_mgr_(session_mgr) {}

ClassCreateHandler::~ClassCreateHandler() {}

void ClassCreateHandler::RegisterRoutes(crow::SimpleApp& app) {
    CROW_ROUTE(app, "/api/class/create").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleCreateClass(req);
    });

    CROW_ROUTE(app, "/api/class/types").methods("GET"_method)
    ([this](const crow::request&) {
        return HandleGetClassTypes();
    });

    CROW_ROUTE(app, "/api/class/types/add").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleAddClassType(req);
    });

    CROW_ROUTE(app, "/api/class/types/delete").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleDeleteClassType(req);
    });

    CROW_ROUTE(app, "/api/class/delete").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleDeleteClass(req);
    });

    CROW_ROUTE(app, "/api/class/upload-qrcode").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleUploadQrcode(req);
    });

    CROW_ROUTE(app, "/api/class/price-presets").methods("GET"_method)
    ([this](const crow::request&) {
        return HandleGetPricePresets();
    });

    CROW_ROUTE(app, "/api/class/price-presets/add").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleAddPricePreset(req);
    });

    CROW_ROUTE(app, "/api/class/price-presets/delete").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleDeletePricePreset(req);
    });

    CROW_ROUTE(app, "/api/class/price-presets/qrcodes/add").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleAddPresetQrcode(req);
    });

    CROW_ROUTE(app, "/api/class/price-presets/qrcodes/delete").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleDeletePresetQrcode(req);
    });
}

std::string ClassCreateHandler::GetSessionIdFromCookie(const crow::request& req) {
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

int ClassCreateHandler::CheckAdminPermission(const crow::request& req, SessionInfo& info) {
    if (!session_mgr_) {
        LOG_ERROR << "ClassCreateHandler: session_mgr is null";
        return ERR_HANDLER_NULL_DAO;
    }

    std::string session_id = GetSessionIdFromCookie(req);
    if (session_id.empty()) {
        LOG_ERROR << "ClassCreateHandler: no session_id in cookie";
        return ERR_AUTH_SESSION_EXPIRED;
    }

    int ret = session_mgr_->ValidateSession(session_id, info);
    if (ret != DB_OK) {
        LOG_ERROR << "ClassCreateHandler: session validation failed, ret=" << ret;
        return ERR_AUTH_SESSION_EXPIRED;
    }

    if (info.role != UserRole_Admin) {
        LOG_ERROR << "ClassCreateHandler: permission denied for user: " << info.username;
        return ERR_AUTH_PERMISSION_DENIED;
    }

    return DB_OK;
}

std::string ClassCreateHandler::GenerateClassName(const std::string& start_time, const std::string& end_time,
                                                   const std::string& class_type, const std::string& description) {
    /* 输入格式: YYYY-MM-DD, 输出格式: YYMMDD-YYMMDD-<type>-<desc> */
    /* 提取start_time的YY MM DD */
    std::string start_yymmdd;
    if (start_time.size() >= 10) {
        /* YYYY-MM-DD: 取YY=year[2..3], MM=month[5..6], DD=day[8..9] */
        std::string yy = start_time.substr(2, 2);
        std::string mm = start_time.substr(5, 2);
        std::string dd = start_time.substr(8, 2);
        start_yymmdd = yy + mm + dd;
    } else {
        start_yymmdd = start_time;
    }

    /* 提取end_time的YY MM DD */
    std::string end_yymmdd;
    if (end_time.size() >= 10) {
        std::string yy = end_time.substr(2, 2);
        std::string mm = end_time.substr(5, 2);
        std::string dd = end_time.substr(8, 2);
        end_yymmdd = yy + mm + dd;
    } else {
        end_yymmdd = end_time;
    }

    std::string name = start_yymmdd + "-" + end_yymmdd + "-" + class_type;
    if (!description.empty()) {
        name += "-" + description;
    }
    return name;
}

crow::response ClassCreateHandler::HandleCreateClass(const crow::request& req) {
    LOG_INFO << "ClassCreateHandler: create class request received";

    if (!class_dao_) {
        LOG_ERROR << "ClassCreateHandler: class_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "class_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    if (!log_dao_) {
        LOG_ERROR << "ClassCreateHandler: log_dao is null";
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

    /* 参数校验 */
    if (!body.has("start_time") || std::string(body["start_time"].s()).empty()) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "start_time is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (!body.has("end_time") || std::string(body["end_time"].s()).empty()) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "end_time is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (!body.has("class_type") || std::string(body["class_type"].s()).empty()) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "class_type is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (!body.has("enrollment_capacity") || body["enrollment_capacity"].i() <= 0) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "enrollment_capacity must be positive";
        return crow::response(400, crow::json::dump(err_resp));
    }

    std::string start_time = body["start_time"].s();
    std::string end_time = body["end_time"].s();
    std::string class_type = body["class_type"].s();
    int32_t enrollment_capacity = body["enrollment_capacity"].i();
    std::string description;
    if (body.has("description") && !std::string(body["description"].s()).empty()) {
        description = body["description"].s();
    }

    /* 生成班级名称 */
    std::string class_name = GenerateClassName(start_time, end_time, class_type, description);

    /* 检查班级名称唯一性 */
    ClassInfo existing_class;
    ret = class_dao_->QueryClassByName(class_name, existing_class);
    if (ret == DB_OK) {
        LOG_ERROR << "ClassCreateHandler: class name already exists: " << class_name;
        err_resp["code"] = ERR_CLASS_NAME_DUPLICATE;
        err_resp["message"] = "class name already exists";
        return crow::response(200, crow::json::dump(err_resp));
    }

    /* 构建ClassInfo */
    ClassInfo class_info;
    class_info.id = 0;
    class_info.class_name = class_name;
    class_info.start_time = start_time;
    class_info.end_time = end_time;
    class_info.description = description;
    class_info.enrollment_capacity = enrollment_capacity;
    class_info.enrollment_used = 0.0;
    class_info.class_type = class_type;
    class_info.create_time = register_student::GetCurrentTimeString();

    /* price_library 改造：解析 prices 数组（activity_name + preset_id），原子插入。
       班级名唯一性预校验已由上面 QueryClassByName 完成；实际插入由 CreateClassWithPricesAtomic 事务内完成。 */

    /* 收集 prices 为 (activity_name, preset_id) 列表 */
    std::vector<std::pair<std::string, int32_t> > prices_vec;
    if (body.has("prices") && body["prices"].t() == crow::json::type::List) {
        size_t price_count = body["prices"].size();
        for (size_t i = 0; i < price_count; ++i) {
            std::string activity_name = body["prices"][i].has("activity_name")
                ? std::string(body["prices"][i]["activity_name"].s()) : "";
            int32_t preset_id = body["prices"][i].has("preset_id")
                ? static_cast<int32_t>(body["prices"][i]["preset_id"].i()) : 0;
            prices_vec.push_back(std::make_pair(activity_name, preset_id));
        }
    }

    /* 校验：至少一个价位项 */
    if (prices_vec.empty()) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "prices must not be empty";
        return crow::response(200, crow::json::dump(err_resp));
    }

    /* 校验：每项 activity_name 非空、preset_id > 0 */
    for (size_t i = 0; i < prices_vec.size(); ++i) {
        if (prices_vec[i].first.empty()) {
            err_resp["code"] = ERR_INVALID_PARAM;
            err_resp["message"] = "activity_name is required";
            return crow::response(200, crow::json::dump(err_resp));
        }
        if (prices_vec[i].second <= 0) {
            err_resp["code"] = ERR_INVALID_PARAM;
            err_resp["message"] = "preset_id is required";
            return crow::response(200, crow::json::dump(err_resp));
        }
    }

    /* 原子创建班级 + 关联预设（事务内完成 class_info 插入 + class_price 关联） */
    int32_t generated_class_id = 0;
    int atomic_ret = class_dao_->CreateClassWithPricesAtomic(class_info, prices_vec, generated_class_id);
    if (atomic_ret != DB_OK) {
        LOG_ERROR << "ClassCreateHandler: create class with prices failed, ret=" << atomic_ret;
        err_resp["code"] = atomic_ret;
        if (atomic_ret == ERR_CLASS_NAME_DUPLICATE) {
            err_resp["message"] = "class name already exists";
        } else if (atomic_ret == ERR_CLASS_ACTIVITY_DUPLICATE) {
            err_resp["message"] = "duplicate price amount in same class";
        } else if (atomic_ret == ERR_PRICE_PRESET_NOT_FOUND) {
            err_resp["message"] = "price preset not found";
        } else if (atomic_ret == ERR_INVALID_PARAM) {
            err_resp["message"] = "invalid params";
        } else {
            err_resp["message"] = "create class failed";
        }
        return crow::response(200, crow::json::dump(err_resp));
    }
    class_info.id = generated_class_id;

    /* 记录操作日志 */
    OperationLog op_log;
    op_log.id = 0;
    op_log.op_type = OpType_CreateClass;
    op_log.operator_name = session_info.username;
    op_log.target_class = class_name;
    op_log.target_student = "";
    op_log.target_resource = "";
    op_log.detail = "created class";
    op_log.op_time = register_student::GetCurrentTimeString();

    log_dao_->InsertLog(op_log);

    /* 构建成功响应 */
    crow::json::wvalue data;
    data["class_name"] = class_name;

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    LOG_INFO << "ClassCreateHandler: create class success, class_name=" << class_name;
    return crow::response(result);
}

crow::response ClassCreateHandler::HandleGetClassTypes() {
    LOG_INFO << "ClassCreateHandler: get class types request";

    if (!class_dao_) {
        LOG_ERROR << "ClassCreateHandler: class_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "class_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    std::vector<ClassType> types;
    int ret = class_dao_->QueryAllClassTypes(types);
    if (ret != DB_OK) {
        LOG_ERROR << "ClassCreateHandler: query class types failed, ret=" << ret;
        crow::json::wvalue err;
        err["code"] = ret;
        err["message"] = "query class types failed";
        return crow::response(500, crow::json::dump(err));
    }

    /* 构建响应 */
    std::vector<crow::json::wvalue> type_items;
    for (size_t i = 0; i < types.size(); ++i) {
        crow::json::wvalue item;
        item["id"] = types[i].id;
        item["name"] = types[i].name;
        item["is_builtin"] = types[i].is_builtin;
        type_items.push_back(std::move(item));
    }

    crow::json::wvalue data;
    for (size_t i = 0; i < type_items.size(); ++i) {
        data["types"][i] = std::move(type_items[i]);
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    LOG_INFO << "ClassCreateHandler: get class types success, count=" << types.size();
    return crow::response(result);
}

crow::response ClassCreateHandler::HandleAddClassType(const crow::request& req) {
    LOG_INFO << "ClassCreateHandler: add class type request";

    if (!class_dao_) {
        LOG_ERROR << "ClassCreateHandler: class_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "class_dao is null";
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

    if (!body.has("name") || std::string(body["name"].s()).empty()) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "name is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    std::string name = body["name"].s();

    /* 构建ClassType */
    ClassType class_type;
    class_type.id = 0;
    class_type.name = name;
    class_type.is_builtin = 0; /* 自定义类型，可删除 */

    ret = class_dao_->InsertClassType(class_type);
    if (ret != DB_OK) {
        LOG_ERROR << "ClassCreateHandler: insert class type failed, ret=" << ret;
        err_resp["code"] = ret;
        err_resp["message"] = "insert class type failed";
        return crow::response(500, crow::json::dump(err_resp));
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";

    LOG_INFO << "ClassCreateHandler: add class type success, name=" << name;
    return crow::response(result);
}

crow::response ClassCreateHandler::HandleDeleteClassType(const crow::request& req) {
    LOG_INFO << "ClassCreateHandler: delete class type request";

    if (!class_dao_) {
        LOG_ERROR << "ClassCreateHandler: class_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "class_dao is null";
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

    if (!body.has("id")) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "id is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    int32_t type_id = body["id"].i();

    /* 查询类型信息，检查是否为内置类型 */
    ClassType class_type;
    ret = class_dao_->QueryClassTypeById(type_id, class_type);
    if (ret != DB_OK) {
        LOG_ERROR << "ClassCreateHandler: query class type failed, ret=" << ret;
        err_resp["code"] = ret;
        err_resp["message"] = "class type not found";
        return crow::response(404, crow::json::dump(err_resp));
    }

    if (class_type.is_builtin == 1) {
        LOG_ERROR << "ClassCreateHandler: cannot delete builtin class type, id=" << type_id;
        err_resp["code"] = ERR_CLASS_TYPE_BUILTIN;
        err_resp["message"] = "cannot delete builtin class type";
        return crow::response(403, crow::json::dump(err_resp));
    }

    /* 执行删除 */
    ret = class_dao_->DeleteClassType(type_id);
    if (ret != DB_OK) {
        LOG_ERROR << "ClassCreateHandler: delete class type failed, ret=" << ret;
        err_resp["code"] = ret;
        err_resp["message"] = "delete class type failed";
        return crow::response(500, crow::json::dump(err_resp));
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";

    LOG_INFO << "ClassCreateHandler: delete class type success, id=" << type_id;
    return crow::response(result);
}

crow::response ClassCreateHandler::HandleUploadQrcode(const crow::request& req) {
    LOG_INFO << "ClassCreateHandler: upload qrcode request";

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

    /* 解析请求体: {"filename": "xxx.jpg", "data": "base64..."} */
    crow::json::wvalue err_resp;
    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (!body.has("filename") || std::string(body["filename"].s()).empty()) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "filename is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (!body.has("data") || std::string(body["data"].s()).empty()) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "data is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    std::string filename = body["filename"].s();
    std::string base64_data = body["data"].s();

    /* 校验文件格式 */
    ret = UploadUtil::ValidateFormat(filename);
    if (ret != DB_OK) {
        LOG_ERROR << "ClassCreateHandler: invalid file format, filename=" << filename;
        err_resp["code"] = ERR_UPLOAD_FORMAT_INVALID;
        err_resp["message"] = "invalid file format";
        return crow::response(400, crow::json::dump(err_resp));
    }

    /* base64解码 */
    std::vector<uint8_t> file_data = Base64Decode(base64_data);
    if (file_data.empty()) {
        LOG_ERROR << "ClassCreateHandler: base64 decode failed";
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "base64 decode failed";
        return crow::response(400, crow::json::dump(err_resp));
    }

    /* 校验文件大小 */
    ret = UploadUtil::ValidateSize(file_data.size());
    if (ret != DB_OK) {
        LOG_ERROR << "ClassCreateHandler: file size exceeded, size=" << file_data.size();
        err_resp["code"] = ERR_UPLOAD_SIZE_EXCEEDED;
        err_resp["message"] = "file size exceeded";
        return crow::response(400, crow::json::dump(err_resp));
    }

    /* 获取上传路径配置 */
    register_student::Config config = register_student::LoadConfig("conf/register_student.conf");
    if (config.upload_path.empty()) {
        LOG_ERROR << "ClassCreateHandler: upload path not configured";
        err_resp["code"] = ERR_UPLOAD_PATH_NOT_CONFIGURED;
        err_resp["message"] = "upload path not configured";
        return crow::response(500, crow::json::dump(err_resp));
    }

    /* 保存文件 */
    std::string saved_path;
    ret = UploadUtil::SaveFile(config.upload_path, filename,
                               reinterpret_cast<const char*>(file_data.data()),
                               file_data.size(), saved_path);
    if (ret != DB_OK) {
        LOG_ERROR << "ClassCreateHandler: save file failed, ret=" << ret;
        err_resp["code"] = ret;
        err_resp["message"] = "save file failed";
        return crow::response(500, crow::json::dump(err_resp));
    }

    /* 构建成功响应 */
    crow::json::wvalue data;
    data["path"] = saved_path;

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    LOG_INFO << "ClassCreateHandler: upload qrcode success, path=" << saved_path;
    return crow::response(result);
}

crow::response ClassCreateHandler::HandleDeleteClass(const crow::request& req) {
    LOG_INFO << "ClassCreateHandler: delete class request";

    SessionInfo session_info;
    int perm_ret = CheckAdminPermission(req, session_info);
    if (perm_ret != DB_OK) {
        crow::json::wvalue err;
        err["code"] = perm_ret;
        err["message"] = "permission denied";
        return crow::response(403, crow::json::dump(err));
    }

    if (!class_dao_) {
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "class_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err));
    }

    if (!body.has("id")) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "id is required";
        return crow::response(400, crow::json::dump(err));
    }

    int32_t class_id = body["id"].i();
    if (class_id <= 0) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "invalid id";
        return crow::response(400, crow::json::dump(err));
    }

    /* query class name for log */
    ClassInfo class_info;
    int ret = class_dao_->QueryClassById(class_id, class_info);
    if (ret != DB_OK) {
        crow::json::wvalue err;
        err["code"] = ret;
        err["message"] = "class not found";
        return crow::response(404, crow::json::dump(err));
    }

    ret = class_dao_->DeleteClass(class_id);
    if (ret != DB_OK) {
        LOG_ERROR << "ClassCreateHandler: delete class failed, ret=" << ret;
        crow::json::wvalue err;
        err["code"] = ret;
        err["message"] = "delete class failed";
        return crow::response(500, crow::json::dump(err));
    }

    /* log */
    if (log_dao_) {
        OperationLog op_log;
        op_log.id = 0;
        op_log.op_type = OpType_CreateClass;
        op_log.operator_name = session_info.username;
        op_log.target_class = class_info.class_name;
        op_log.target_student = "";
        op_log.target_resource = "";
        op_log.detail = "deleted class";
        op_log.op_time = register_student::GetCurrentTimeString();
        log_dao_->InsertLog(op_log);
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    LOG_INFO << "ClassCreateHandler: delete class success, id=" << class_id;
    return crow::response(result);
}

/* ==================== 价位预设管理 ==================== */

crow::response ClassCreateHandler::HandleGetPricePresets() {
    LOG_INFO << "ClassCreateHandler: get price presets request";

    if (!class_dao_) {
        LOG_ERROR << "ClassCreateHandler: class_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "class_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    /* 预设查询不要求管理员权限（创建班级页加载下拉框需要），但删除/新增要求 */

    std::vector<PricePresetInfo> presets;
    int ret = class_dao_->QueryAllPricePresets(presets);
    if (ret != DB_OK) {
        LOG_ERROR << "ClassCreateHandler: query price presets failed, ret=" << ret;
        crow::json::wvalue err;
        err["code"] = ret;
        err["message"] = "query price presets failed";
        return crow::response(500, crow::json::dump(err));
    }

    std::vector<crow::json::wvalue> preset_items;
    for (size_t i = 0; i < presets.size(); ++i) {
        crow::json::wvalue item;
        item["id"] = presets[i].id;
        item["amount"] = presets[i].amount;
        item["expected_headcount"] = presets[i].expected_headcount;
        item["create_time"] = presets[i].create_time;
        for (size_t j = 0; j < presets[i].qrcode_paths.size(); ++j) {
            item["qrcode_paths"][j] = presets[i].qrcode_paths[j];
        }
        preset_items.push_back(std::move(item));
    }

    crow::json::wvalue data;
    for (size_t i = 0; i < preset_items.size(); ++i) {
        data["presets"][i] = std::move(preset_items[i]);
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    LOG_INFO << "ClassCreateHandler: get price presets success, count=" << presets.size();
    return crow::response(result);
}

crow::response ClassCreateHandler::HandleAddPricePreset(const crow::request& req) {
    LOG_INFO << "ClassCreateHandler: add price preset request";

    if (!class_dao_) {
        LOG_ERROR << "ClassCreateHandler: class_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "class_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    SessionInfo session_info;
    int perm_ret = CheckAdminPermission(req, session_info);
    if (perm_ret != DB_OK) {
        crow::json::wvalue err;
        if (perm_ret == ERR_AUTH_SESSION_EXPIRED) {
            err["code"] = perm_ret;
            err["message"] = "session expired or invalid";
            return crow::response(401, crow::json::dump(err));
        }
        err["code"] = perm_ret;
        err["message"] = "permission denied";
        return crow::response(403, crow::json::dump(err));
    }

    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err));
    }

    if (!body.has("amount")) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "amount is required";
        return crow::response(200, crow::json::dump(err));
    }

    double amount = body["amount"].d();
    if (amount < 0) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "amount must be non-negative";
        return crow::response(200, crow::json::dump(err));
    }

    /* 0元预设全局唯一约束：只能有一个0元预设（headcount强制为1） */
    if (amount < 0.001) {
        std::vector<PricePresetInfo> existing;
        if (class_dao_->QueryAllPricePresets(existing) == DB_OK) {
            for (size_t i = 0; i < existing.size(); ++i) {
                if (existing[i].amount < 0.001) {
                    crow::json::wvalue err;
                    err["code"] = ERR_PRICE_DUPLICATE;
                    err["message"] = "zero-amount preset already exists";
                    return crow::response(200, crow::json::dump(err));
                }
            }
        }
    }

    /* expected_headcount: 必须 >=1, 默认 1；0元预设强制为 1 */
    int32_t expected_headcount = 1;
    if (amount >= 0.001 && body.has("expected_headcount")) {
        expected_headcount = static_cast<int32_t>(body["expected_headcount"].i());
    }
    if (expected_headcount < 1) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "expected_headcount must be >= 1";
        return crow::response(200, crow::json::dump(err));
    }

    /* 解析 qrcode_paths */
    std::vector<std::string> qrcode_paths;
    if (body.has("qrcode_paths") && body["qrcode_paths"].t() == crow::json::type::List) {
        size_t count = body["qrcode_paths"].size();
        for (size_t i = 0; i < count; ++i) {
            qrcode_paths.push_back(std::string(body["qrcode_paths"][i].s()));
        }
    }

    if (qrcode_paths.empty() || qrcode_paths.size() > 10) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "qrcode_paths must have 1-10 items";
        return crow::response(200, crow::json::dump(err));
    }

    PricePresetInfo preset_info;
    preset_info.id = 0;
    preset_info.amount = amount;
    preset_info.expected_headcount = expected_headcount;
    preset_info.create_time = register_student::GetCurrentTimeString();
    preset_info.qrcode_paths = qrcode_paths;

    int ret = class_dao_->InsertPricePreset(preset_info);
    if (ret != DB_OK) {
        LOG_ERROR << "ClassCreateHandler: insert price preset failed, ret=" << ret;
        crow::json::wvalue err;
        err["code"] = ret;
        if (ret == ERR_PRICE_DUPLICATE) {
            err["message"] = "amount and headcount combination already exists";
        } else {
            err["message"] = "insert price preset failed";
        }
        return crow::response(200, crow::json::dump(err));
    }

    crow::json::wvalue data;
    data["id"] = preset_info.id;

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    /* 操作日志 */
    if (log_dao_) {
        OperationLog op_log;
        op_log.id = 0;
        op_log.op_type = OpType_CreateClass;
        op_log.operator_name = session_info.username;
        op_log.target_class = "";
        op_log.target_student = "";
        op_log.target_resource = "";
        std::ostringstream detail_ss;
        detail_ss << "added price preset, amount=" << amount << ", headcount=" << expected_headcount;
        op_log.detail = detail_ss.str();
        op_log.op_time = register_student::GetCurrentTimeString();
        log_dao_->InsertLog(op_log);
    }

    LOG_INFO << "ClassCreateHandler: add price preset success, id=" << preset_info.id;
    return crow::response(result);
}

crow::response ClassCreateHandler::HandleDeletePricePreset(const crow::request& req) {
    LOG_INFO << "ClassCreateHandler: delete price preset request";

    if (!class_dao_) {
        LOG_ERROR << "ClassCreateHandler: class_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "class_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    SessionInfo session_info;
    int perm_ret = CheckAdminPermission(req, session_info);
    if (perm_ret != DB_OK) {
        crow::json::wvalue err;
        if (perm_ret == ERR_AUTH_SESSION_EXPIRED) {
            err["code"] = perm_ret;
            err["message"] = "session expired or invalid";
            return crow::response(401, crow::json::dump(err));
        }
        err["code"] = perm_ret;
        err["message"] = "permission denied";
        return crow::response(403, crow::json::dump(err));
    }

    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err));
    }

    if (!body.has("preset_id")) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "preset_id is required";
        return crow::response(400, crow::json::dump(err));
    }

    int32_t preset_id = static_cast<int32_t>(body["preset_id"].i());
    if (preset_id <= 0) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "invalid preset_id";
        return crow::response(400, crow::json::dump(err));
    }

    /* 原子删除预设，收集待删物理文件 */
    std::vector<std::string> deleted_files;
    int ret = class_dao_->DeletePricePresetAtomic(preset_id, deleted_files);
    if (ret != DB_OK) {
        LOG_ERROR << "ClassCreateHandler: delete price preset failed, ret=" << ret;
        crow::json::wvalue err;
        err["code"] = ret;
        if (ret == ERR_PRICE_PRESET_IN_USE) {
            /* 查询引用的班级名，填入提示文案 */
            std::string class_name = class_dao_->QueryClassNameByPresetId(preset_id);
            if (!class_name.empty()) {
                err["message"] = std::string("该价位已被 " + class_name + " 班级引用，请先解除关联");
            } else {
                err["message"] = "该价位已有报名记录，不可删除";
            }
        } else {
            err["message"] = "delete price preset failed";
        }
        return crow::response(200, crow::json::dump(err));
    }

    /* 事务 COMMIT 后联动删除物理文件 */
    register_student::Config config = register_student::LoadConfig("conf/register_student.conf");
    std::string upload_dir = config.upload_path;
    for (size_t i = 0; i < deleted_files.size(); ++i) {
        UploadUtil::DeleteUploadedFile(upload_dir, deleted_files[i]);
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";

    /* 操作日志 */
    if (log_dao_) {
        OperationLog op_log;
        op_log.id = 0;
        op_log.op_type = OpType_CreateClass;
        op_log.operator_name = session_info.username;
        op_log.target_class = "";
        op_log.target_student = "";
        op_log.target_resource = "";
        std::ostringstream detail_ss;
        detail_ss << "deleted price preset, id=" << preset_id
                  << " files=" << deleted_files.size();
        op_log.detail = detail_ss.str();
        op_log.op_time = register_student::GetCurrentTimeString();
        log_dao_->InsertLog(op_log);
    }

    LOG_INFO << "ClassCreateHandler: delete price preset success, id=" << preset_id;
    return crow::response(result);
}

crow::response ClassCreateHandler::HandleAddPresetQrcode(const crow::request& req) {
    LOG_INFO << "ClassCreateHandler: add preset qrcode request";

    if (!class_dao_) {
        LOG_ERROR << "ClassCreateHandler: class_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "class_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    SessionInfo session_info;
    int perm_ret = CheckAdminPermission(req, session_info);
    if (perm_ret != DB_OK) {
        crow::json::wvalue err;
        if (perm_ret == ERR_AUTH_SESSION_EXPIRED) {
            err["code"] = perm_ret;
            err["message"] = "session expired or invalid";
            return crow::response(401, crow::json::dump(err));
        }
        err["code"] = perm_ret;
        err["message"] = "permission denied";
        return crow::response(403, crow::json::dump(err));
    }

    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err));
    }

    if (!body.has("preset_id") || !body.has("qrcode_path")) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "preset_id and qrcode_path are required";
        return crow::response(400, crow::json::dump(err));
    }

    int32_t preset_id = static_cast<int32_t>(body["preset_id"].i());
    std::string qrcode_path = std::string(body["qrcode_path"].s());

    if (preset_id <= 0 || qrcode_path.empty()) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "invalid preset_id or qrcode_path";
        return crow::response(400, crow::json::dump(err));
    }

    int ret = class_dao_->AddPresetQrcode(preset_id, qrcode_path);
    if (ret != DB_OK) {
        LOG_ERROR << "ClassCreateHandler: add preset qrcode failed, ret=" << ret;
        crow::json::wvalue err;
        err["code"] = ret;
        if (ret == ERR_PRICE_PRESET_NOT_FOUND) {
            err["message"] = "price preset not found";
        } else if (ret == ERR_INVALID_PARAM) {
            err["message"] = "qrcode count exceeds 10 limit";
        } else {
            err["message"] = "add preset qrcode failed";
        }
        return crow::response(200, crow::json::dump(err));
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";

    LOG_INFO << "ClassCreateHandler: add preset qrcode success, preset_id=" << preset_id;
    return crow::response(result);
}

crow::response ClassCreateHandler::HandleDeletePresetQrcode(const crow::request& req) {
    LOG_INFO << "ClassCreateHandler: delete preset qrcode request";

    if (!class_dao_) {
        LOG_ERROR << "ClassCreateHandler: class_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "class_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    SessionInfo session_info;
    int perm_ret = CheckAdminPermission(req, session_info);
    if (perm_ret != DB_OK) {
        crow::json::wvalue err;
        if (perm_ret == ERR_AUTH_SESSION_EXPIRED) {
            err["code"] = perm_ret;
            err["message"] = "session expired or invalid";
            return crow::response(401, crow::json::dump(err));
        }
        err["code"] = perm_ret;
        err["message"] = "permission denied";
        return crow::response(403, crow::json::dump(err));
    }

    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err));
    }

    if (!body.has("preset_id") || !body.has("qrcode_path")) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "preset_id and qrcode_path are required";
        return crow::response(400, crow::json::dump(err));
    }

    int32_t preset_id = static_cast<int32_t>(body["preset_id"].i());
    std::string qrcode_path = std::string(body["qrcode_path"].s());

    if (preset_id <= 0 || qrcode_path.empty()) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "invalid preset_id or qrcode_path";
        return crow::response(400, crow::json::dump(err));
    }

    std::string deleted_file;
    int ret = class_dao_->DeletePresetQrcode(preset_id, qrcode_path, deleted_file);
    if (ret != DB_OK) {
        LOG_ERROR << "ClassCreateHandler: delete preset qrcode failed, ret=" << ret;
        crow::json::wvalue err;
        err["code"] = ret;
        err["message"] = "delete preset qrcode failed";
        return crow::response(200, crow::json::dump(err));
    }

    /* 物理文件联动删除 */
    if (!deleted_file.empty()) {
        register_student::Config config = register_student::LoadConfig("conf/register_student.conf");
        std::string upload_dir = config.upload_path;
        UploadUtil::DeleteUploadedFile(upload_dir, deleted_file);
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";

    LOG_INFO << "ClassCreateHandler: delete preset qrcode success, preset_id=" << preset_id;
    return crow::response(result);
}
