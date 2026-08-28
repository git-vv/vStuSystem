#include "class_manage_handler.h"
#include "utils.h"
#include "error_codes.h"
#include "log_types.h"
#include "resource_types.h"
#include "registration_types.h"
#include "refund_types.h"

#include <vector>
#include <string>
#include <map>
#include <cstdlib>
#include <sstream>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <set>

ClassManageHandler::ClassManageHandler(IClassDao* class_dao, IRegistrationDao* reg_dao,
                                       IResourceDao* resource_dao, IAttendanceDao* attendance_dao,
                                       IRefundDao* refund_dao, IOperationLogDao* log_dao,
                                       SessionManager* session_mgr)
    : class_dao_(class_dao)
    , reg_dao_(reg_dao)
    , resource_dao_(resource_dao)
    , attendance_dao_(attendance_dao)
    , refund_dao_(refund_dao)
    , log_dao_(log_dao)
    , session_mgr_(session_mgr) {}

ClassManageHandler::~ClassManageHandler() {}

void ClassManageHandler::RegisterRoutes(crow::SimpleApp& app) {
    CROW_ROUTE(app, "/api/class/list").methods("GET"_method)
    ([this](const crow::request& req) {
        return HandleListClasses(req);
    });

    CROW_ROUTE(app, "/api/class/detail").methods("GET"_method)
    ([this](const crow::request& req) {
        return HandleGetClassDetail(req);
    });

    CROW_ROUTE(app, "/api/class/enrollment").methods("PUT"_method)
    ([this](const crow::request& req) {
        return HandleUpdateEnrollment(req);
    });

    CROW_ROUTE(app, "/api/class/students").methods("GET"_method)
    ([this](const crow::request& req) {
        return HandleGetStudents(req);
    });

    CROW_ROUTE(app, "/api/class/student").methods("GET"_method)
    ([this](const crow::request& req) {
        return HandleGetStudentDetail(req);
    });

    CROW_ROUTE(app, "/api/class/student/update").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleUpdateStudent(req);
    });

    CROW_ROUTE(app, "/api/class/attendance").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleSubmitAttendance(req);
    });

    CROW_ROUTE(app, "/api/class/attendance/query").methods("GET"_method)
    ([this](const crow::request& req) {
        return HandleGetAttendance(req);
    });

    CROW_ROUTE(app, "/api/class/allocate-resource").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleAllocateResource(req);
    });

    CROW_ROUTE(app, "/api/class/allocations").methods("GET"_method)
    ([this](const crow::request& req) {
        return HandleGetClassAllocations(req);
    });

    CROW_ROUTE(app, "/api/class/students/query").methods("GET"_method)
    ([this](const crow::request& req) {
        return HandleQueryStudentsByTime(req);
    });

    CROW_ROUTE(app, "/api/class/allocations/query").methods("GET"_method)
    ([this](const crow::request& req) {
        return HandleQueryAllocationsByTime(req);
    });

    CROW_ROUTE(app, "/api/class/prices/update").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleUpdateClassPrices(req);
    });

    CROW_ROUTE(app, "/api/class/students/refund").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleRefund(req);
    });

    CROW_ROUTE(app, "/api/class/students/refund/cancel").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleCancelRefund(req);
    });

    CROW_ROUTE(app, "/api/class/students/supplement").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleSupplement(req);
    });

    CROW_ROUTE(app, "/api/class/student/delete").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleDeleteStudent(req);
    });

    CROW_ROUTE(app, "/api/class/calculate-amount").methods("GET"_method)
    ([this](const crow::request& req) {
        return HandleCalculateAmount(req);
    });

    CROW_ROUTE(app, "/api/class/students/renew").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleRenew(req);
    });
}

std::string ClassManageHandler::GetSessionIdFromCookie(const crow::request& req) {
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

int ClassManageHandler::CheckTeacherPermission(const crow::request& req, SessionInfo& info) {
    if (!session_mgr_) {
        LOG_ERROR << "ClassManageHandler: session_mgr is null";
        return ERR_HANDLER_NULL_DAO;
    }

    std::string session_id = GetSessionIdFromCookie(req);
    if (session_id.empty()) {
        LOG_ERROR << "ClassManageHandler: no session_id in cookie";
        return ERR_AUTH_SESSION_EXPIRED;
    }

    int ret = session_mgr_->ValidateSession(session_id, info);
    if (ret != DB_OK) {
        LOG_ERROR << "ClassManageHandler: session validation failed, ret=" << ret;
        return ERR_AUTH_SESSION_EXPIRED;
    }

    if (info.role != UserRole_Teacher && info.role != UserRole_Admin) {
        LOG_ERROR << "ClassManageHandler: permission denied for user: " << info.username;
        return ERR_AUTH_PERMISSION_DENIED;
    }

    return DB_OK;
}

int ClassManageHandler::CheckAdminPermission(const crow::request& req, SessionInfo& info) {
    if (!session_mgr_) {
        LOG_ERROR << "ClassManageHandler: session_mgr is null";
        return ERR_HANDLER_NULL_DAO;
    }

    std::string session_id = GetSessionIdFromCookie(req);
    if (session_id.empty()) {
        LOG_ERROR << "ClassManageHandler: no session_id in cookie";
        return ERR_AUTH_SESSION_EXPIRED;
    }

    int ret = session_mgr_->ValidateSession(session_id, info);
    if (ret != DB_OK) {
        LOG_ERROR << "ClassManageHandler: session validation failed, ret=" << ret;
        return ERR_AUTH_SESSION_EXPIRED;
    }

    if (info.role != UserRole_Admin) {
        LOG_ERROR << "ClassManageHandler: admin permission denied for user: " << info.username;
        return ERR_AUTH_PERMISSION_DENIED;
    }

    return DB_OK;
}

int32_t ClassManageHandler::ExtractIntParam(const crow::request& req, const std::string& key, int32_t default_val) {
    char* val = const_cast<crow::request&>(req).url_params.get(key);
    if (val && val[0] != '\0') {
        return static_cast<int32_t>(std::atoi(val));
    }
    return default_val;
}

crow::response ClassManageHandler::HandleListClasses(const crow::request& req) {
    LOG_INFO << "ClassManageHandler: list classes request";

    if (!class_dao_) {
        LOG_ERROR << "ClassManageHandler: class_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "class_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    /* 解析查询参数 */
    std::string filter = "active";
    std::string keyword;

    char* p = nullptr;
    p = const_cast<crow::request&>(req).url_params.get("filter");
    if (p && p[0] != '\0') { filter = p; }
    p = const_cast<crow::request&>(req).url_params.get("keyword");
    if (p && p[0] != '\0') { keyword = p; }

    /* 查询班级列表 */
    std::vector<ClassInfo> classes;
    int ret = DB_OK;

    if (filter == "all") {
        if (!keyword.empty()) {
            ret = class_dao_->SearchClassesByName(keyword, classes);
        } else {
            ret = class_dao_->QueryAllClasses(classes);
        }
    } else {
        /* 默认active */
        if (!keyword.empty()) {
            ret = class_dao_->SearchActiveClassesByName(keyword, classes);
        } else {
            ret = class_dao_->QueryActiveClasses(classes);
        }
    }

    if (ret != DB_OK) {
        LOG_ERROR << "ClassManageHandler: query classes failed, ret=" << ret;
        crow::json::wvalue err;
        err["code"] = ret;
        err["message"] = "query classes failed";
        return crow::response(500, crow::json::dump(err));
    }

    /* 构建响应 */
    std::vector<crow::json::wvalue> class_items;
    for (size_t i = 0; i < classes.size(); ++i) {
        int active_count = reg_dao_->CountActiveStudentsByClassId(classes[i].id);
        if (active_count < 0) { active_count = 0; }

        crow::json::wvalue item;
        item["id"] = classes[i].id;
        item["class_name"] = classes[i].class_name;
        item["start_time"] = classes[i].start_time;
        item["end_time"] = classes[i].end_time;
        item["description"] = classes[i].description;
        item["enrollment_capacity"] = classes[i].enrollment_capacity;
        item["enrollment_used"] = active_count;
        item["class_type"] = classes[i].class_type;
        item["create_time"] = classes[i].create_time;
        class_items.push_back(std::move(item));
    }

    crow::json::wvalue data;
    for (size_t i = 0; i < class_items.size(); ++i) {
        data["classes"][i] = std::move(class_items[i]);
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    LOG_INFO << "ClassManageHandler: list classes success, count=" << classes.size();
    return crow::response(result);
}

crow::response ClassManageHandler::HandleGetClassDetail(const crow::request& req) {
    LOG_INFO << "ClassManageHandler: get class detail request";

    if (!class_dao_) {
        LOG_ERROR << "ClassManageHandler: class_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "class_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    /* 解析class_id参数 */
    int32_t class_id = ExtractIntParam(req, "id", 0);
    if (class_id <= 0) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "id is required";
        return crow::response(400, crow::json::dump(err));
    }

    /* 查询班级信息 */
    ClassInfo class_info;
    int ret = class_dao_->QueryClassById(class_id, class_info);
    if (ret != DB_OK) {
        LOG_ERROR << "ClassManageHandler: query class by id failed, ret=" << ret;
        crow::json::wvalue err;
        err["code"] = ret;
        err["message"] = "class not found";
        return crow::response(404, crow::json::dump(err));
    }

    /* 查询价位信息 */
    std::vector<PriceInfo> prices;
    ret = class_dao_->QueryPricesByClassId(class_id, prices);
    if (ret != DB_OK) {
        LOG_ERROR << "ClassManageHandler: query prices by class id failed, ret=" << ret;
        /* 价位查询失败不阻断，返回空数组 */
    }

    /* 构建价位数组 */
    std::vector<crow::json::wvalue> price_items;
    for (size_t i = 0; i < prices.size(); ++i) {
        crow::json::wvalue price_item;
        price_item["id"] = prices[i].id;
        price_item["class_id"] = prices[i].class_id;
        price_item["preset_id"] = prices[i].preset_id;
        price_item["price"] = prices[i].price;
        price_item["activity_name"] = prices[i].activity_name;
        price_item["expected_headcount"] = prices[i].snapshot_headcount;

        /* 构建二维码路径数组 */
        for (size_t j = 0; j < prices[i].qrcode_paths.size(); ++j) {
            price_item["qrcode_paths"][j] = prices[i].qrcode_paths[j];
        }

        price_items.push_back(std::move(price_item));
    }

    int active_count = reg_dao_->CountActiveStudentsByClassId(class_id);
    if (active_count < 0) { active_count = 0; }

    /* 构建响应 */
    crow::json::wvalue data;
    data["id"] = class_info.id;
    data["class_name"] = class_info.class_name;
    data["start_time"] = class_info.start_time;
    data["end_time"] = class_info.end_time;
    data["description"] = class_info.description;
    data["enrollment_capacity"] = class_info.enrollment_capacity;
    data["enrollment_used"] = active_count;
    data["class_type"] = class_info.class_type;
    data["create_time"] = class_info.create_time;
    for (size_t i = 0; i < price_items.size(); ++i) {
        data["prices"][i] = std::move(price_items[i]);
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    LOG_INFO << "ClassManageHandler: get class detail success, id=" << class_id
             << ", active_students=" << active_count
             << ", enrollment_capacity=" << class_info.enrollment_capacity;
    return crow::response(result);
}

crow::response ClassManageHandler::HandleUpdateEnrollment(const crow::request& req) {
    LOG_INFO << "ClassManageHandler: update enrollment request";

    if (!class_dao_) {
        LOG_ERROR << "ClassManageHandler: class_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "class_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    if (!log_dao_) {
        LOG_ERROR << "ClassManageHandler: log_dao is null";
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

    if (!body.has("class_id")) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "class_id is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (!body.has("capacity") || body["capacity"].i() <= 0) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "capacity must be positive";
        return crow::response(400, crow::json::dump(err_resp));
    }

    int32_t class_id = body["class_id"].i();
    int32_t capacity = body["capacity"].i();

    /* 查询班级名称用于日志 */
    ClassInfo class_info;
    std::string class_name;
    ret = class_dao_->QueryClassById(class_id, class_info);
    if (ret == DB_OK) {
        class_name = class_info.class_name;
    }

    /* 更新招生名额 */
    ret = class_dao_->UpdateEnrollment(class_id, capacity);
    if (ret != DB_OK) {
        LOG_ERROR << "ClassManageHandler: update enrollment failed, ret=" << ret;
        err_resp["code"] = ret;
        err_resp["message"] = "update enrollment failed";
        return crow::response(500, crow::json::dump(err_resp));
    }

    /* 记录操作日志 */
    OperationLog op_log;
    op_log.id = 0;
    op_log.op_type = OpType_ModifyEnrollment;
    op_log.operator_name = session_info.username;
    op_log.target_class = class_name;
    op_log.target_student = "";
    op_log.target_resource = "";
    op_log.detail = "modified enrollment capacity to " + std::to_string(capacity);
    op_log.op_time = register_student::GetCurrentTimeString();

    log_dao_->InsertLog(op_log);

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";

    LOG_INFO << "ClassManageHandler: update enrollment success, class_id=" << class_id << " capacity=" << capacity;
    return crow::response(result);
}

crow::response ClassManageHandler::HandleGetStudents(const crow::request& req) {
    LOG_INFO << "ClassManageHandler: get students request";

    if (!reg_dao_) {
        LOG_ERROR << "ClassManageHandler: reg_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "reg_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    /* 校验教师权限 */
    SessionInfo session_info;
    int ret = CheckTeacherPermission(req, session_info);
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

    /* 解析class_id参数 */
    int32_t class_id = ExtractIntParam(req, "class_id", 0);
    if (class_id <= 0) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "class_id is required";
        return crow::response(400, crow::json::dump(err));
    }

    /* 查询报名记录 */
    std::vector<RegistrationInfo> regs;
    ret = reg_dao_->QueryRegistrationsByClassId(class_id, regs);
    if (ret != DB_OK) {
        LOG_ERROR << "ClassManageHandler: query registrations by class id failed, ret=" << ret;
        crow::json::wvalue err;
        err["code"] = ret;
        err["message"] = "query students failed";
        return crow::response(500, crow::json::dump(err));
    }

    /* 查询床位资源分配，构建 registration_id → resource_code 映射 */
    std::map<int32_t, int32_t> bed_alloc_map;
    if (resource_dao_) {
        ResourceInfo bed_res;
        if (resource_dao_->QueryResourceByType(ResourceType_Bed, bed_res) == DB_OK) {
            std::vector<ResourceAllocation> allocs;
            if (resource_dao_->QueryAllocationsByClassId(class_id, allocs) == DB_OK) {
                for (size_t j = 0; j < allocs.size(); ++j) {
                    if (allocs[j].resource_id == bed_res.id) {
                        bed_alloc_map[allocs[j].registration_id] = allocs[j].resource_code;
                    }
                }
            }
        }
    }

    /* 查询班级信息（用于判断部分时段） */
    ClassInfo class_info_for_period;
    bool has_class_info = (class_dao_->QueryClassById(class_id, class_info_for_period) == DB_OK);

    /* 构建响应 */
    std::vector<crow::json::wvalue> student_items;
    for (size_t i = 0; i < regs.size(); ++i) {
        crow::json::wvalue item;
        item["id"] = regs[i].id;
        item["class_id"] = regs[i].class_id;
        item["student_name"] = regs[i].student_name;
        item["student_gender"] = regs[i].student_gender;
        item["parent_phone"] = regs[i].parent_phone;
        item["has_allergy"] = regs[i].has_allergy;
        item["allergy_desc"] = regs[i].allergy_desc;
        item["price_id"] = regs[i].price_id;
        item["need_bed"] = regs[i].need_bed;
        item["teacher_name"] = regs[i].teacher_name;
        item["other_info"] = regs[i].other_info;
        item["register_time"] = regs[i].register_time;
        item["refund_amount"] = regs[i].refund_amount;
        /* paid_amount = paid_amount_snapshot - 累计未撤销退费（统一取 registration.paid_amount_snapshot） */
        item["paid_amount"] = regs[i].paid_amount_snapshot - regs[i].refund_amount;
        item["is_deposit"] = regs[i].is_deposit;
        item["paid_amount_snapshot"] = regs[i].paid_amount_snapshot;
        /* fully_refunded: 有退费记录且实缴<=0 表示已全额退费，考勤/打印时排除
           定金为0但无退费记录的学生不算全额退费 */
        item["fully_refunded"] = (regs[i].refund_amount > 0.001 && regs[i].paid_amount_snapshot - regs[i].refund_amount <= 0.001);
        /* 部分时段信息（FR-10, FR-15, FR-16） */
        item["student_start_date"] = regs[i].student_start_date;
        item["student_end_date"] = regs[i].student_end_date;
        if (has_class_info) {
            std::string stu_end = regs[i].student_end_date.empty()
                ? class_info_for_period.end_time : regs[i].student_end_date;
            std::string stu_start = regs[i].student_start_date.empty()
                ? class_info_for_period.start_time : regs[i].student_start_date;
            item["is_partial_period"] = (stu_start > class_info_for_period.start_time || stu_end < class_info_for_period.end_time);
        } else {
            item["is_partial_period"] = false;
        }
        /* 床位分配编号：need_bed=1 且已分配床位资源时返回 resource_code */
        auto bed_it = bed_alloc_map.find(regs[i].id);
        if (regs[i].need_bed == 1 && bed_it != bed_alloc_map.end()) {
            item["bed_resource_code"] = bed_it->second;
        } else {
            item["bed_resource_code"] = -1;
        }
        student_items.push_back(std::move(item));
    }

    crow::json::wvalue data;
    for (size_t i = 0; i < student_items.size(); ++i) {
        data["students"][i] = std::move(student_items[i]);
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    LOG_INFO << "ClassManageHandler: get students success, class_id=" << class_id << " count=" << regs.size();
    return crow::response(result);
}

crow::response ClassManageHandler::HandleQueryStudentsByTime(const crow::request& req) {
    LOG_INFO << "ClassManageHandler: query students by time request";

    if (!reg_dao_) {
        LOG_ERROR << "ClassManageHandler: reg_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "reg_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    /* 校验教师权限 */
    SessionInfo session_info;
    int ret = CheckTeacherPermission(req, session_info);
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

    /* 解析时间范围参数 */
    std::string start_time;
    std::string end_time;
    char* s = const_cast<crow::request&>(req).url_params.get("start_time");
    if (s && s[0] != '\0') { start_time = s; }
    char* e = const_cast<crow::request&>(req).url_params.get("end_time");
    if (e && e[0] != '\0') { end_time = e; }

    if (start_time.empty() || end_time.empty()) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "start_time and end_time are required";
        return crow::response(400, crow::json::dump(err));
    }

    /* 查询时间范围内的报名记录 */
    std::vector<RegistrationInfo> regs;
    ret = reg_dao_->QueryRegistrationsByTimeRange(start_time, end_time + " 23:59:59", regs);
    if (ret != DB_OK) {
        LOG_ERROR << "ClassManageHandler: query registrations by time failed, ret=" << ret;
        crow::json::wvalue err;
        err["code"] = ret;
        err["message"] = "query students failed";
        return crow::response(500, crow::json::dump(err));
    }

    /* 构建响应，包含价格信息 */
    std::vector<crow::json::wvalue> student_items;
    for (size_t i = 0; i < regs.size(); ++i) {
        crow::json::wvalue item;
        item["id"] = regs[i].id;
        item["class_id"] = regs[i].class_id;
        item["student_name"] = regs[i].student_name;
        item["student_gender"] = regs[i].student_gender;
        item["parent_phone"] = regs[i].parent_phone;
        item["price_id"] = regs[i].price_id;
        item["register_time"] = regs[i].register_time;

        /* 查询价格信息 */
        std::string activity_name;
        double price_value = 0;
        if (class_dao_ && regs[i].price_id > 0) {
            PriceInfo price;
            if (class_dao_->QueryPriceById(regs[i].price_id, price) == DB_OK) {
                activity_name = price.activity_name;
                price_value = price.price;
            }
        }
        item["activity_name"] = activity_name;
        item["price"] = price_value;
        item["refund_amount"] = regs[i].refund_amount;
        /* paid_amount 统一取 registration.paid_amount_snapshot - 累计退费（不再依赖 class_price.snapshot_amount） */
        item["paid_amount"] = regs[i].paid_amount_snapshot - regs[i].refund_amount;
        item["is_deposit"] = regs[i].is_deposit;
        item["paid_amount_snapshot"] = regs[i].paid_amount_snapshot;
        item["teacher_name"] = regs[i].teacher_name;
        item["other_info"] = regs[i].other_info;
        item["has_allergy"] = regs[i].has_allergy;
        item["allergy_desc"] = regs[i].allergy_desc;
        item["student_start_date"] = regs[i].student_start_date;
        item["student_end_date"] = regs[i].student_end_date;

        /* 查询班级名称 */
        std::string class_name;
        if (class_dao_) {
            ClassInfo class_info;
            if (class_dao_->QueryClassById(regs[i].class_id, class_info) == DB_OK) {
                class_name = class_info.class_name;
            }
        }
        item["class_name"] = class_name;

        student_items.push_back(std::move(item));
    }

    crow::json::wvalue data;
    for (size_t i = 0; i < student_items.size(); ++i) {
        data["students"][i] = std::move(student_items[i]);
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    LOG_INFO << "ClassManageHandler: query students by time success, count=" << regs.size();
    return crow::response(result);
}

crow::response ClassManageHandler::HandleQueryAllocationsByTime(const crow::request& req) {
    LOG_INFO << "ClassManageHandler: query allocations by time request";

    if (!resource_dao_) {
        LOG_ERROR << "ClassManageHandler: resource_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "resource_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    /* 校验教师权限 */
    SessionInfo session_info;
    int ret = CheckTeacherPermission(req, session_info);
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

    /* 解析时间范围参数 */
    std::string start_time;
    std::string end_time;
    char* s = const_cast<crow::request&>(req).url_params.get("start_time");
    if (s && s[0] != '\0') { start_time = s; }
    char* e = const_cast<crow::request&>(req).url_params.get("end_time");
    if (e && e[0] != '\0') { end_time = e; }

    if (start_time.empty() || end_time.empty()) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "start_time and end_time are required";
        return crow::response(400, crow::json::dump(err));
    }

    /* 查询时间范围内的分配记录 */
    std::vector<ResourceAllocation> allocs;
    ret = resource_dao_->QueryAllocationsByTimeRange(start_time, end_time + " 23:59:59", allocs);
    if (ret != DB_OK) {
        LOG_ERROR << "ClassManageHandler: query allocations by time failed, ret=" << ret;
        crow::json::wvalue err;
        err["code"] = ret;
        err["message"] = "query allocations failed";
        return crow::response(500, crow::json::dump(err));
    }

    /* 收集所有资源ID以查询资源名称 */
    std::vector<int32_t> resource_ids;
    for (size_t i = 0; i < allocs.size(); ++i) {
        bool found = false;
        for (size_t j = 0; j < resource_ids.size(); ++j) {
            if (resource_ids[j] == allocs[i].resource_id) { found = true; break; }
        }
        if (!found && allocs[i].resource_id > 0) {
            resource_ids.push_back(allocs[i].resource_id);
        }
    }

    /* 构建响应 */
    crow::json::wvalue data;

    /* 资源列表（用于前端动态列） */
    for (size_t i = 0; i < resource_ids.size(); ++i) {
        ResourceInfo res_info;
        std::string res_name;
        if (resource_dao_->QueryResourceById(resource_ids[i], res_info) == DB_OK) {
            res_name = res_info.name;
        }
        crow::json::wvalue ritem;
        ritem["id"] = resource_ids[i];
        ritem["name"] = res_name;
        data["resources"][i] = std::move(ritem);
    }

    /* 分配记录 */
    for (size_t i = 0; i < allocs.size(); ++i) {
        crow::json::wvalue item;
        item["id"] = allocs[i].id;
        item["resource_id"] = allocs[i].resource_id;
        item["registration_id"] = allocs[i].registration_id;
        item["student_name"] = allocs[i].student_name;
        item["student_gender"] = allocs[i].student_gender;
        item["class_name"] = allocs[i].class_name;
        item["resource_code"] = allocs[i].resource_code;
        item["allocate_time"] = allocs[i].allocate_time;
        data["allocations"][i] = std::move(item);
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    LOG_INFO << "ClassManageHandler: query allocations by time success, count=" << allocs.size();
    return crow::response(result);
}

crow::response ClassManageHandler::HandleGetStudentDetail(const crow::request& req) {
    LOG_INFO << "ClassManageHandler: get student detail request";

    if (!reg_dao_) {
        LOG_ERROR << "ClassManageHandler: reg_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "reg_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    /* 校验教师权限 */
    SessionInfo session_info;
    int ret = CheckTeacherPermission(req, session_info);
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
    int32_t class_id = ExtractIntParam(req, "class_id", 0);
    int32_t student_id = ExtractIntParam(req, "student_id", 0);

    if (class_id <= 0) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "class_id is required";
        return crow::response(400, crow::json::dump(err));
    }

    if (student_id <= 0) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "student_id is required";
        return crow::response(400, crow::json::dump(err));
    }

    /* 查询报名记录 */
    RegistrationInfo reg_info;
    ret = reg_dao_->QueryRegistrationById(student_id, reg_info);
    if (ret != DB_OK) {
        LOG_ERROR << "ClassManageHandler: query registration by id failed, ret=" << ret;
        crow::json::wvalue err;
        err["code"] = ret;
        err["message"] = "student not found";
        return crow::response(404, crow::json::dump(err));
    }

    /* 构建响应 */
    crow::json::wvalue data;
    data["id"] = reg_info.id;
    data["class_id"] = reg_info.class_id;
    data["student_name"] = reg_info.student_name;
    data["student_gender"] = reg_info.student_gender;
    data["parent_phone"] = reg_info.parent_phone;
    data["has_allergy"] = reg_info.has_allergy;
    data["allergy_desc"] = reg_info.allergy_desc;
    data["price_id"] = reg_info.price_id;
    data["need_bed"] = reg_info.need_bed;
    data["teacher_name"] = reg_info.teacher_name;
    data["other_info"] = reg_info.other_info;
    data["register_time"] = reg_info.register_time;
    data["is_deposit"] = reg_info.is_deposit;
    data["paid_amount_snapshot"] = reg_info.paid_amount_snapshot;
    data["supplement_amount"] = reg_info.supplement_amount;
    data["supplement_preset_id"] = reg_info.supplement_preset_id;

    /* 部分时段信息 */
    data["student_start_date"] = reg_info.student_start_date;
    data["student_end_date"] = reg_info.student_end_date;
    data["fully_refunded"] = (reg_info.refund_amount > 0.001 && reg_info.paid_amount_snapshot - reg_info.refund_amount <= 0.001);

    ClassInfo class_info_for_period;
    if (class_dao_->QueryClassById(reg_info.class_id, class_info_for_period) == DB_OK) {
        std::string stu_start = reg_info.student_start_date.empty()
            ? class_info_for_period.start_time : reg_info.student_start_date;
        std::string stu_end = reg_info.student_end_date.empty()
            ? class_info_for_period.end_time : reg_info.student_end_date;
        data["is_partial_period"] = (stu_start > class_info_for_period.start_time || stu_end < class_info_for_period.end_time);
        data["can_renew"] = (stu_end < class_info_for_period.end_time);
    } else {
        data["is_partial_period"] = false;
        data["can_renew"] = false;
    }

    /* 床位分配编号：need_bed=1 且已分配床位资源时返回 resource_code */
    if (reg_info.need_bed == 1 && resource_dao_) {
        ResourceInfo bed_res;
        if (resource_dao_->QueryResourceByType(ResourceType_Bed, bed_res) == DB_OK) {
            std::vector<ResourceAllocation> allocs;
            if (resource_dao_->QueryAllocationsByClassId(reg_info.class_id, allocs) == DB_OK) {
                int32_t bed_code = -1;
                for (size_t j = 0; j < allocs.size(); ++j) {
                    if (allocs[j].resource_id == bed_res.id && allocs[j].registration_id == reg_info.id) {
                        bed_code = allocs[j].resource_code;
                        break;
                    }
                }
                data["bed_resource_code"] = bed_code;
            } else {
                data["bed_resource_code"] = -1;
            }
        } else {
            data["bed_resource_code"] = -1;
        }
    } else {
        data["bed_resource_code"] = -1;
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    LOG_INFO << "ClassManageHandler: get student detail success, student_id=" << student_id;
    return crow::response(result);
}

crow::response ClassManageHandler::HandleUpdateStudent(const crow::request& req) {
    LOG_INFO << "ClassManageHandler: update student request";

    if (!class_dao_ || !reg_dao_ || !log_dao_ || !session_mgr_) {
        LOG_ERROR << "ClassManageHandler: dao or session_mgr is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "dao or session_mgr is null";
        return crow::response(500, crow::json::dump(err));
    }

    /* 权限校验：Teacher 或 Admin */
    SessionInfo session_info;
    int ret = CheckTeacherPermission(req, session_info);
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
        if (!body.has("registration_id") || body["registration_id"].i() <= 0) {
            err_resp["code"] = ERR_INVALID_PARAM;
            err_resp["message"] = "registration_id is required";
            return crow::response(400, crow::json::dump(err_resp));
        }
        if (!body.has("student_name") || std::string(body["student_name"].s()).empty() ||
            std::string(body["student_name"].s()).size() > 64) {
            err_resp["code"] = ERR_INVALID_PARAM;
            err_resp["message"] = "student_name invalid or too long";
            return crow::response(400, crow::json::dump(err_resp));
        }
        if (!body.has("student_gender")) {
            err_resp["code"] = ERR_INVALID_PARAM;
            err_resp["message"] = "student_gender is required";
            return crow::response(400, crow::json::dump(err_resp));
        }
        std::string gender = std::string(body["student_gender"].s());
        if (gender != "male" && gender != "female") {
            err_resp["code"] = ERR_INVALID_PARAM;
            err_resp["message"] = "student_gender must be male or female";
            return crow::response(400, crow::json::dump(err_resp));
        }
        if (body.has("parent_phone") && !std::string(body["parent_phone"].s()).empty()) {
            std::string phone_val = std::string(body["parent_phone"].s());
            if (phone_val.size() < 7 || phone_val.size() > 15) {
                err_resp["code"] = ERR_INVALID_PARAM;
                err_resp["message"] = "parent_phone length must be 7-15 digits";
                return crow::response(400, crow::json::dump(err_resp));
            }
            for (size_t k = 0; k < phone_val.size(); ++k) {
                if (phone_val[k] < '0' || phone_val[k] > '9') {
                    err_resp["code"] = ERR_INVALID_PARAM;
                    err_resp["message"] = "parent_phone must contain only digits";
                    return crow::response(400, crow::json::dump(err_resp));
                }
            }
        }
        if (!body.has("class_id") || body["class_id"].i() <= 0) {
            err_resp["code"] = ERR_INVALID_PARAM;
            err_resp["message"] = "class_id is required";
            return crow::response(400, crow::json::dump(err_resp));
        }
    } catch (...) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }

    int32_t registration_id = body["registration_id"].i();
    std::string student_name = std::string(body["student_name"].s());
    std::string student_gender = std::string(body["student_gender"].s());
    std::string parent_phone = body.has("parent_phone") ? std::string(body["parent_phone"].s()) : "";
    int32_t has_allergy = body.has("has_allergy") ? body["has_allergy"].i() : 0;
    if (has_allergy != 0 && has_allergy != 1) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "has_allergy must be 0 or 1";
        return crow::response(400, crow::json::dump(err_resp));
    }
    std::string allergy_desc;
    if (body.has("allergy_desc")) {
        allergy_desc = std::string(body["allergy_desc"].s());
    }
    if (has_allergy == 1 && allergy_desc.empty()) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "allergy_desc required when has_allergy=1";
        return crow::response(400, crow::json::dump(err_resp));
    }
    if (allergy_desc.size() > 256) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "allergy_desc too long";
        return crow::response(400, crow::json::dump(err_resp));
    }
    std::string other_info;
    if (body.has("other_info")) {
        other_info = std::string(body["other_info"].s());
    }
    if (other_info.size() > 1024) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "other_info too long";
        return crow::response(400, crow::json::dump(err_resp));
    }
    std::string teacher_name;
    if (body.has("teacher_name")) {
        teacher_name = std::string(body["teacher_name"].s());
    }
    if (teacher_name.size() > 64) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "teacher_name too long";
        return crow::response(400, crow::json::dump(err_resp));
    }
    int32_t new_class_id = body["class_id"].i();

    /* 查询原报名信息 */
    RegistrationInfo old_info;
    ret = reg_dao_->QueryRegistrationById(registration_id, old_info);
    if (ret == ERR_REGISTRATION_NOT_FOUND) {
        err_resp["code"] = ret;
        err_resp["message"] = "registration not found";
        return crow::response(200, crow::json::dump(err_resp));
    }
    if (ret != DB_OK) {
        LOG_ERROR << "ClassManageHandler: query registration failed, ret=" << ret;
        err_resp["code"] = ret;
        err_resp["message"] = "query registration failed";
        return crow::response(500, crow::json::dump(err_resp));
    }

    /* 查询原班级名（用于 diff 与日志） */
    ClassInfo old_class_info;
    std::string old_class_name;
    ret = class_dao_->QueryClassById(old_info.class_id, old_class_info);
    if (ret == DB_OK) {
        old_class_name = old_class_info.class_name;
    }

    /* 查询新班级信息 */
    ClassInfo new_class_info;
    ret = class_dao_->QueryClassById(new_class_id, new_class_info);
    if (ret != DB_OK) {
        LOG_ERROR << "ClassManageHandler: target class not found, class_id=" << new_class_id;
        err_resp["code"] = ERR_DB_EXEC_FAILED;
        err_resp["message"] = "target class not found";
        return crow::response(200, crow::json::dump(err_resp));
    }
    std::string new_class_name = new_class_info.class_name;

    /* 构造 new_info：保留 price_id/need_bed/register_time/id 不变，覆盖其他字段 */
    RegistrationInfo new_info = old_info;
    new_info.student_name = student_name;
    new_info.student_gender = student_gender;
    new_info.parent_phone = parent_phone;
    new_info.has_allergy = has_allergy;
    new_info.allergy_desc = allergy_desc;
    new_info.other_info = other_info;
    new_info.teacher_name = teacher_name;
    new_info.class_id = new_class_id;

    /* 生成字段级 diff */
    std::string diff = register_student::BuildStudentDiff(old_info, new_info, old_class_name, new_class_name);

    /* 无变更：不调用 Dao、不写日志 */
    if (diff.empty()) {
        crow::json::wvalue result;
        result["code"] = DB_OK;
        result["message"] = "success";
        result["data"]["registration_id"] = registration_id;
        result["data"]["changed"] = false;
        LOG_INFO << "ClassManageHandler: update student no change, id=" << registration_id;
        return crow::response(result);
    }

    /* Dao 调用分支 */
    bool is_transfer = (new_class_id != old_info.class_id);
    if (is_transfer) {
        /* 转班时段校验（FR-13） */
        std::string stu_start = old_info.student_start_date;
        std::string stu_end = old_info.student_end_date;
        bool is_full_period = (stu_start.empty() || stu_end.empty() ||
                               (stu_start == old_class_info.start_time && stu_end == old_class_info.end_time));

        if (!is_full_period) {
            /* 部分时段学生转班：检查新班级时段是否覆盖原上课时段 */
            if (new_class_info.start_time > stu_start || new_class_info.end_time < stu_end) {
                err_resp["code"] = ERR_TRANSFER_PERIOD_NOT_COVERED;
                err_resp["message"] = "new class period does not cover student attendance period";
                return crow::response(200, crow::json::dump(err_resp));
            }
        }

        ret = reg_dao_->TransferClassAtomic(registration_id, old_info.class_id,
                                            new_class_id, new_class_info.enrollment_capacity);
        if (ret != DB_OK) {
            LOG_ERROR << "ClassManageHandler: transfer class failed, ret=" << ret;
            err_resp["code"] = ret;
            if (ret == ERR_CLASS_ENROLLMENT_FULL) {
                err_resp["message"] = "target class is full";
            } else if (ret == ERR_REGISTRATION_NOT_FOUND) {
                err_resp["message"] = "registration not found";
            } else {
                err_resp["message"] = "transfer class failed";
            }
            return crow::response(200, crow::json::dump(err_resp));
        }

        /* 转班后若还有基础信息字段变更（diff 含除"班级"外的字段），再更新基础字段 */
        bool has_basic_change = (old_info.student_name != new_info.student_name) ||
                                (old_info.student_gender != new_info.student_gender) ||
                                (old_info.parent_phone != new_info.parent_phone) ||
                                (old_info.has_allergy != new_info.has_allergy) ||
                                (old_info.allergy_desc != new_info.allergy_desc) ||
                                (old_info.other_info != new_info.other_info) ||
                                (old_info.teacher_name != new_info.teacher_name);
        if (has_basic_change) {
            ret = reg_dao_->UpdateStudentBasicInfo(new_info);
            if (ret != DB_OK) {
                LOG_ERROR << "ClassManageHandler: update basic info after transfer failed, ret=" << ret
                          << " (transfer already committed)";
                err_resp["code"] = ret;
                err_resp["message"] = "update basic info failed after transfer";
                return crow::response(500, crow::json::dump(err_resp));
            }
        }
    } else {
        ret = reg_dao_->UpdateStudentBasicInfo(new_info);
        if (ret != DB_OK) {
            LOG_ERROR << "ClassManageHandler: update basic info failed, ret=" << ret;
            err_resp["code"] = ret;
            if (ret == ERR_REGISTRATION_NOT_FOUND) {
                err_resp["message"] = "registration not found";
            } else {
                err_resp["message"] = "update basic info failed";
            }
            return crow::response(200, crow::json::dump(err_resp));
        }
    }

    /* 写入操作日志 */
    OperationLog op_log;
    op_log.id = 0;
    op_log.op_type = OpType_ModifyStudent;
    op_log.operator_name = session_info.username;
    op_log.target_student = new_info.student_name;
    op_log.target_resource = "";
    if (is_transfer) {
        op_log.target_class = old_class_name + " \xE2\x86\x92 " + new_class_name;  /* 原班级 → 新班级 */
    } else {
        op_log.target_class = old_class_name;
    }
    op_log.detail = diff;
    op_log.op_time = register_student::GetCurrentTimeString();

    int log_ret = log_dao_->InsertLog(op_log);

    /* 日志写入失败：回滚主操作（D-1） */
    if (log_ret != DB_OK) {
        LOG_ERROR << "ClassManageHandler: log write failed, ret=" << log_ret << " rolling back main op";

        int rollback_ret = DB_OK;
        if (is_transfer) {
            /* 反向转回 */
            rollback_ret = reg_dao_->TransferClassAtomic(registration_id, new_class_id,
                                                         old_info.class_id,
                                                         old_class_info.enrollment_capacity);
            if (rollback_ret == DB_OK) {
                /* 还需要恢复基础字段 */
                reg_dao_->UpdateStudentBasicInfo(old_info);
            }
        } else {
            rollback_ret = reg_dao_->UpdateStudentBasicInfo(old_info);
        }

        LOG_ERROR << "ClassManageHandler: rollback ret=" << rollback_ret;

        err_resp["code"] = (log_ret == ERR_LOG_DB_NOT_OPEN) ? ERR_LOG_DB_NOT_OPEN : ERR_DB_EXEC_FAILED;
        err_resp["message"] = "log write failed, modification rolled back, please retry";
        return crow::response(200, crow::json::dump(err_resp));
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"]["registration_id"] = registration_id;
    result["data"]["changed"] = true;

    LOG_INFO << "ClassManageHandler: update student success, id=" << registration_id
             << " transfer=" << (is_transfer ? 1 : 0);
    return crow::response(result);
}

/* 校验 HH:MM 格式（24小时制），早退时间必须严格5字符 */
static bool IsValidHHMM(const std::string& s) {
    if (s.size() != 5 || s[2] != ':') { return false; }
    for (int i = 0; i < 5; ++i) {
        if (i == 2) { continue; }
        if (s[i] < '0' || s[i] > '9') { return false; }
    }
    int hh = (s[0] - '0') * 10 + (s[1] - '0');
    int mm = (s[3] - '0') * 10 + (s[4] - '0');
    return hh >= 0 && hh <= 23 && mm >= 0 && mm <= 59;
}

crow::response ClassManageHandler::HandleSubmitAttendance(const crow::request& req) {
    LOG_INFO << "ClassManageHandler: submit attendance request";

    if (!attendance_dao_) {
        LOG_ERROR << "ClassManageHandler: attendance_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "attendance_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    if (!log_dao_) {
        LOG_ERROR << "ClassManageHandler: log_dao is null";
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

    if (!body.has("class_id")) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "class_id is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (!body.has("date") || std::string(body["date"].s()).empty()) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "date is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (!body.has("records") || body["records"].t() != crow::json::type::List) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "records array is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    int32_t class_id = body["class_id"].i();
    std::string date = body["date"].s();

    /* 日期分档权限校验（FR-10）：未来日期禁止、历史日期管理员、当天日期教师 */
    std::string today = register_student::GetCurrentDateString();
    bool is_future = (date > today);
    bool is_history = (date < today);
    if (is_future) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "不允许提交未来日期的考勤";
        return crow::response(200, crow::json::dump(err_resp));
    }

    SessionInfo session_info;
    if (is_history) {
        /* 历史考勤必须管理员 */
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
    } else {
        /* 当天考勤：教师 + 管理员均可 */
        int ret = CheckTeacherPermission(req, session_info);
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
    }

    /* 早退支持反复修改：移除 CheckAttendanceExists 阻断，InsertAttendance 内部走 upsert */

    /* 先将records数组提取到C++结构体，避免Crow v0.3 rvalue重复访问崩溃 */
    size_t record_count = 0;
    try {
        record_count = body["records"].size();
    } catch (...) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "failed to parse records array";
        return crow::response(400, crow::json::dump(err_resp));
    }

    std::vector<AttendanceRecord> attendance_records;
    attendance_records.reserve(record_count);
    for (size_t i = 0; i < record_count; ++i) {
        try {
            AttendanceRecord record;
            record.id = 0;
            record.class_id = class_id;
            if (body["records"][i].has("registration_id")) {
                record.registration_id = body["records"][i]["registration_id"].i();
            }
            if (body["records"][i].has("student_name")) {
                record.student_name = std::string(body["records"][i]["student_name"].s());
            }
            if (body["records"][i].has("student_gender")) {
                record.student_gender = std::string(body["records"][i]["student_gender"].s());
            }
            record.attendance_date = date;
            if (body["records"][i].has("status")) {
                record.status = static_cast<AttendanceStatusType>(body["records"][i]["status"].i());
            } else {
                record.status = AttendanceStatus_Present;
            }
            /* 解析 leave_time（status==EarlyLeave 或 Late 时必填 HH:MM，其他状态强制清空） */
            std::string leave_time;
            if (body["records"][i].has("leave_time")) {
                leave_time = std::string(body["records"][i]["leave_time"].s());
            }
            if (record.status == AttendanceStatus_EarlyLeave || record.status == AttendanceStatus_Late) {
                if (!IsValidHHMM(leave_time)) {
                    err_resp["code"] = ERR_INVALID_PARAM;
                    err_resp["message"] = (record.status == AttendanceStatus_Late)
                        ? "invalid leave_time for late, expect HH:MM"
                        : "invalid leave_time for early leave, expect HH:MM";
                    return crow::response(400, crow::json::dump(err_resp));
                }
            } else {
                leave_time = "";
            }
            record.leave_time = leave_time;
            record.teacher_name = session_info.username;
            record.record_time = register_student::GetCurrentTimeString();
            attendance_records.push_back(record);
        } catch (...) {
            LOG_ERROR << "ClassManageHandler: parse attendance record[" << i << "] failed, skipping";
        }
    }

    if (attendance_records.empty()) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "no valid attendance records";
        return crow::response(400, crow::json::dump(err_resp));
    }

    /* 过滤已全额退费学生：实缴<=0 的学生不再参与考勤 */
    /* 同时构建 registration_id -> (student_start_date, student_end_date) 映射，用于部分时段防御性校验 */
    std::set<int32_t> fully_refunded_reg_ids;
    std::map<int32_t, std::pair<std::string, std::string> > reg_period_map;
    if (reg_dao_) {
        std::vector<RegistrationInfo> class_regs;
        if (reg_dao_->QueryRegistrationsByClassId(class_id, class_regs) == DB_OK) {
            for (size_t i = 0; i < class_regs.size(); ++i) {
                if (class_regs[i].refund_amount > 0.001 && class_regs[i].paid_amount_snapshot - class_regs[i].refund_amount <= 0.001) {
                    fully_refunded_reg_ids.insert(class_regs[i].id);
                }
                if (!class_regs[i].student_start_date.empty() && !class_regs[i].student_end_date.empty()) {
                    reg_period_map[class_regs[i].id] = std::make_pair(class_regs[i].student_start_date,
                                                                       class_regs[i].student_end_date);
                }
            }
        }
    }

    /* 逐条插入考勤记录（跳过已全额退费学生 + 超出学生上课时段的记录静默跳过） */
    int32_t success_count = 0;
    int32_t skipped_count = 0;
    for (size_t i = 0; i < attendance_records.size(); ++i) {
        if (fully_refunded_reg_ids.count(attendance_records[i].registration_id) > 0) {
            ++skipped_count;
            continue;
        }
        /* 防御性校验：超出学生上课时段的考勤记录静默跳过（FR-9） */
        std::map<int32_t, std::pair<std::string, std::string> >::iterator period_it =
            reg_period_map.find(attendance_records[i].registration_id);
        if (period_it != reg_period_map.end()) {
            if (date < period_it->second.first || date > period_it->second.second) {
                ++skipped_count;
                continue;
            }
        }
        int insert_ret = attendance_dao_->InsertAttendance(attendance_records[i]);
        if (insert_ret == DB_OK) {
            ++success_count;
        } else {
            LOG_ERROR << "ClassManageHandler: insert attendance record failed, ret=" << insert_ret;
        }
    }

    /* 查询班级名称用于日志 */
    std::string class_name;
    if (class_dao_) {
        ClassInfo class_info;
        int query_ret = class_dao_->QueryClassById(class_id, class_info);
        if (query_ret == DB_OK) {
            class_name = class_info.class_name;
        }
    }

    /* 记录操作日志 */
    OperationLog op_log;
    op_log.id = 0;
    op_log.op_type = OpType_RegisterStudent;
    op_log.operator_name = session_info.username;
    op_log.target_class = class_name;
    op_log.target_student = "";
    op_log.target_resource = "";
    op_log.detail = "submitted attendance (upsert) for date " + date + ", records=" + std::to_string(success_count);
    op_log.op_time = register_student::GetCurrentTimeString();

    log_dao_->InsertLog(op_log);

    crow::json::wvalue data;
    data["success_count"] = success_count;

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    LOG_INFO << "ClassManageHandler: submit attendance success, class_id=" << class_id << " date=" << date << " count=" << success_count;
    return crow::response(result);
}

crow::response ClassManageHandler::HandleGetAttendance(const crow::request& req) {
    LOG_INFO << "ClassManageHandler: get attendance request";

    if (!attendance_dao_) {
        LOG_ERROR << "ClassManageHandler: attendance_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "attendance_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    /* 校验教师权限 */
    SessionInfo session_info;
    int ret = CheckTeacherPermission(req, session_info);
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
    int32_t class_id = ExtractIntParam(req, "class_id", 0);
    if (class_id <= 0) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "class_id is required";
        return crow::response(400, crow::json::dump(err));
    }

    /* 提取date和start_date/end_date参数 */
    std::string date;
    std::string start_date;
    std::string end_date;

    char* p = nullptr;
    p = const_cast<crow::request&>(req).url_params.get("date");
    if (p && p[0] != '\0') { date = p; }
    p = const_cast<crow::request&>(req).url_params.get("start_date");
    if (p && p[0] != '\0') { start_date = p; }
    p = const_cast<crow::request&>(req).url_params.get("end_date");
    if (p && p[0] != '\0') { end_date = p; }

    /* 查询考勤记录 */
    std::vector<AttendanceRecord> records;

    if (!date.empty()) {
        ret = attendance_dao_->QueryAttendanceByClassAndDate(class_id, date, records);
    } else if (!start_date.empty() && !end_date.empty()) {
        ret = attendance_dao_->QueryAttendanceByClassAndDateRange(class_id, start_date, end_date, records);
    } else {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "date or start_date+end_date is required";
        return crow::response(400, crow::json::dump(err));
    }

    if (ret != DB_OK) {
        LOG_ERROR << "ClassManageHandler: query attendance failed, ret=" << ret;
        crow::json::wvalue err;
        err["code"] = ret;
        err["message"] = "query attendance failed";
        return crow::response(500, crow::json::dump(err));
    }

    /* 构建响应 */
    std::vector<crow::json::wvalue> record_items;
    for (size_t i = 0; i < records.size(); ++i) {
        crow::json::wvalue item;
        item["id"] = records[i].id;
        item["class_id"] = records[i].class_id;
        item["registration_id"] = records[i].registration_id;
        item["student_name"] = records[i].student_name;
        item["student_gender"] = records[i].student_gender;
        item["attendance_date"] = records[i].attendance_date;
        item["status"] = static_cast<int32_t>(records[i].status);
        item["leave_time"] = records[i].leave_time;
        item["teacher_name"] = records[i].teacher_name;
        item["record_time"] = records[i].record_time;
        record_items.push_back(std::move(item));
    }

    crow::json::wvalue data;
    for (size_t i = 0; i < record_items.size(); ++i) {
        data["records"][i] = std::move(record_items[i]);
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    LOG_INFO << "ClassManageHandler: get attendance success, class_id=" << class_id << " count=" << records.size();
    return crow::response(result);
}

crow::response ClassManageHandler::HandleGetClassAllocations(const crow::request& req) {
    LOG_INFO << "ClassManageHandler: get class allocations request";

    if (!resource_dao_) {
        LOG_ERROR << "ClassManageHandler: resource_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "resource_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    int32_t class_id = ExtractIntParam(req, "class_id", 0);
    int32_t resource_id = ExtractIntParam(req, "resource_id", 0);

    if (class_id <= 0) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "class_id is required";
        return crow::response(400, crow::json::dump(err));
    }

    std::vector<ResourceAllocation> allocs;
    int ret = resource_dao_->QueryAllocationsByClassId(class_id, allocs);
    if (ret != DB_OK) {
        crow::json::wvalue err;
        err["code"] = ret;
        err["message"] = "failed to query allocations";
        return crow::response(500, crow::json::dump(err));
    }

    crow::json::wvalue data;
    for (size_t i = 0; i < allocs.size(); ++i) {
        /* Filter by resource_id if specified */
        if (resource_id > 0 && allocs[i].resource_id != resource_id) {
            continue;
        }
        crow::json::wvalue item;
        item["id"] = allocs[i].id;
        item["resource_id"] = allocs[i].resource_id;
        item["registration_id"] = allocs[i].registration_id;
        item["student_name"] = allocs[i].student_name;
        item["student_gender"] = allocs[i].student_gender;
        item["teacher_name"] = allocs[i].teacher_name;
        item["class_name"] = allocs[i].class_name;
        item["resource_code"] = allocs[i].resource_code;
        item["allocate_time"] = allocs[i].allocate_time;

        /* Look up resource name */
        if (allocs[i].resource_id > 0) {
            ResourceInfo res_info;
            if (resource_dao_->QueryResourceById(allocs[i].resource_id, res_info) == DB_OK) {
                item["resource_name"] = res_info.name;
            }
        }

        data["allocations"][i] = std::move(item);
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);
    return crow::response(result);
}

crow::response ClassManageHandler::HandleAllocateResource(const crow::request& req) {
    LOG_INFO << "ClassManageHandler: allocate resource request";

    if (!resource_dao_) {
        LOG_ERROR << "ClassManageHandler: resource_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "resource_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    if (!log_dao_) {
        LOG_ERROR << "ClassManageHandler: log_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "log_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    /* 校验教师权限 */
    SessionInfo session_info;
    int ret = CheckTeacherPermission(req, session_info);
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

    if (!body.has("class_id")) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "class_id is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (!body.has("resource_id")) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "resource_id is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (!body.has("resource_code")) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "resource_code is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    int32_t resource_id = 0;
    int32_t resource_code = 0;
    int32_t registration_id = 0;
    std::string student_name;
    std::string student_gender;
    std::string teacher_name;
    std::string class_name;

    try {
        resource_id = body["resource_id"].i();
        resource_code = body["resource_code"].i();
        if (body.has("registration_id")) { registration_id = body["registration_id"].i(); }
        if (body.has("student_name")) { student_name = std::string(body["student_name"].s()); }
        if (body.has("student_gender")) { student_gender = std::string(body["student_gender"].s()); }
        if (body.has("teacher_name")) { teacher_name = std::string(body["teacher_name"].s()); }
        if (body.has("class_name")) { class_name = std::string(body["class_name"].s()); }
    } catch (...) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid parameter type";
        return crow::response(400, crow::json::dump(err_resp));
    }

    /* 若前端未传 teacher_name（如旧前端或写死空串），用当前登录用户兜底，
       避免 resource_allocation.teacher_name 入库为空导致分配列表显示"教师:"后无名字 */
    if (teacher_name.empty()) {
        teacher_name = session_info.username;
    }

    /* 检查床位资源分配限制：学生报名时未选需要床位的，不能分配床位资源 */
    if (registration_id > 0) {
        ResourceInfo res_info;
        if (resource_dao_->QueryResourceById(resource_id, res_info) == DB_OK) {
            if (res_info.resource_type == ResourceType_Bed) {
                RegistrationInfo reg_info;
                if (reg_dao_->QueryRegistrationById(registration_id, reg_info) == DB_OK) {
                    if (reg_info.need_bed == 0) {
                        err_resp["code"] = ERR_INVALID_PARAM;
                        err_resp["message"] = "该学生报名时未选择需要床位，不能分配床位资源";
                        return crow::response(200, crow::json::dump(err_resp));
                    }
                }
            }
        }
    }

    /* 检查资源编号是否已被占用 */
    ret = resource_dao_->CheckResourceCodeOccupied(resource_id, resource_code);
    if (ret == 1) {
        LOG_ERROR << "ClassManageHandler: resource code already occupied, resource_id=" << resource_id << " code=" << resource_code;
        err_resp["code"] = ERR_RESOURCE_CODE_OCCUPIED;
        err_resp["message"] = "该资源编号已被占用，请更换编号";
        return crow::response(200, crow::json::dump(err_resp));
    }
    if (ret < 0) {
        LOG_ERROR << "ClassManageHandler: check resource code failed, ret=" << ret;
        err_resp["code"] = ret;
        err_resp["message"] = "检查资源编号失败";
        return crow::response(500, crow::json::dump(err_resp));
    }

    /* 检查同一学生是否已分配过该资源 */
    if (registration_id > 0) {
        ret = resource_dao_->CheckStudentResourceAllocated(resource_id, registration_id);
        if (ret == 1) {
            LOG_ERROR << "ClassManageHandler: student already allocated, resource_id=" << resource_id << " reg_id=" << registration_id;
            err_resp["code"] = ERR_RESOURCE_ALREADY_ALLOCATED;
            err_resp["message"] = "学生 " + student_name + " 已分配过该资源，不能重复分配";
            return crow::response(200, crow::json::dump(err_resp));
        }
        if (ret < 0) {
            LOG_ERROR << "ClassManageHandler: check student allocated failed, ret=" << ret;
            err_resp["code"] = ret;
            err_resp["message"] = "检查学生分配记录失败";
            return crow::response(500, crow::json::dump(err_resp));
        }
    }

    /* 构建分配记录 */
    ResourceAllocation alloc;
    alloc.id = 0;
    alloc.resource_id = resource_id;
    alloc.registration_id = registration_id;
    alloc.student_name = student_name;
    alloc.student_gender = student_gender;
    alloc.teacher_name = teacher_name;
    alloc.class_name = class_name;
    alloc.resource_code = resource_code;
    alloc.allocate_time = register_student::GetCurrentTimeString();

    /* 原子分配：检查编号占用+插入分配+增加已用数量 */
    ret = resource_dao_->AllocateResourceAtomic(alloc);
    if (ret != DB_OK) {
        LOG_ERROR << "ClassManageHandler: atomic allocate resource failed, ret=" << ret;
        if (ret == ERR_RESOURCE_CODE_OCCUPIED) {
            err_resp["code"] = ret;
            err_resp["message"] = "该资源编号已被占用，请更换编号";
            return crow::response(200, crow::json::dump(err_resp));
        }
        err_resp["code"] = ret;
        err_resp["message"] = "分配资源失败";
        return crow::response(500, crow::json::dump(err_resp));
    }

    /* 记录操作日志 */
    OperationLog op_log;
    op_log.id = 0;
    op_log.op_type = OpType_AllocateResource;
    op_log.operator_name = session_info.username;
    op_log.target_class = class_name;
    op_log.target_student = student_name;
    op_log.target_resource = std::to_string(resource_id);
    op_log.detail = "allocated resource code " + std::to_string(resource_code);
    op_log.op_time = register_student::GetCurrentTimeString();

    log_dao_->InsertLog(op_log);

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";

    LOG_INFO << "ClassManageHandler: allocate resource success, resource_id=" << resource_id << " code=" << resource_code;
    return crow::response(result);
}

/* ==================== 编辑班级价位 ==================== */

crow::response ClassManageHandler::HandleUpdateClassPrices(const crow::request& req) {
    LOG_INFO << "ClassManageHandler: update class prices request";

    if (!class_dao_) {
        LOG_ERROR << "ClassManageHandler: class_dao is null";
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

    if (!body.has("class_id") || !body.has("prices")) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "class_id and prices are required";
        return crow::response(400, crow::json::dump(err));
    }

    int32_t class_id = static_cast<int32_t>(body["class_id"].i());
    if (class_id <= 0) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "invalid class_id";
        return crow::response(400, crow::json::dump(err));
    }

    if (body["prices"].t() != crow::json::type::List) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "prices must be a list";
        return crow::response(400, crow::json::dump(err));
    }

    /* 收集价位项指针 */
    std::vector<PriceUpdateItem> price_items;
    size_t price_count = body["prices"].size();
    for (size_t i = 0; i < price_count; ++i) {
        const crow::json::rvalue& item = body["prices"][i];
        PriceUpdateItem ui;
        if (item.has("price_id")) {
            ui.price_id = static_cast<int32_t>(item["price_id"].i());
        }
        if (item.has("preset_id")) {
            ui.preset_id = static_cast<int32_t>(item["preset_id"].i());
        }
        if (item.has("activity_name")) {
            ui.activity_name = std::string(item["activity_name"].s());
        }
        price_items.push_back(ui);
    }

    /* Handler 层预校验：已存在项的 preset_id 不可变（Dao 层二次校验） */
    for (size_t i = 0; i < price_items.size(); ++i) {
        const PriceUpdateItem& item = price_items[i];
        if (item.activity_name.empty()) {
            crow::json::wvalue err;
            err["code"] = ERR_INVALID_PARAM;
            err["message"] = "activity_name is required";
            return crow::response(200, crow::json::dump(err));
        }
        if (item.preset_id <= 0) {
            crow::json::wvalue err;
            err["code"] = ERR_INVALID_PARAM;
            err["message"] = "preset_id is required";
            return crow::response(200, crow::json::dump(err));
        }
    }

    int ret = class_dao_->UpdateClassPricesAtomic(class_id, price_items);
    if (ret != DB_OK) {
        LOG_ERROR << "ClassManageHandler: update class prices failed, ret=" << ret;
        crow::json::wvalue err;
        err["code"] = ret;
        if (ret == ERR_PRICE_PRESET_IMMUTABLE) {
            err["message"] = "cannot change preset of existing price item";
        } else if (ret == ERR_CLASS_ACTIVITY_DUPLICATE) {
            err["message"] = "duplicate price amount in same class";
        } else if (ret == ERR_PRICE_PRESET_NOT_FOUND) {
            err["message"] = "price preset not found";
        } else if (ret == ERR_INVALID_PARAM) {
            err["message"] = "invalid params";
        } else {
            err["message"] = "update class prices failed";
        }
        return crow::response(200, crow::json::dump(err));
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
        detail_ss << "updated class prices, class_id=" << class_id;
        op_log.detail = detail_ss.str();
        op_log.op_time = register_student::GetCurrentTimeString();
        log_dao_->InsertLog(op_log);
    }

    LOG_INFO << "ClassManageHandler: update class prices success, class_id=" << class_id;
    return crow::response(result);
}

/* 静态工具：金额格式化为 2 位小数字符串（用于错误消息和日志） */
static std::string FormatMoney(double v) {
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss << std::setprecision(2) << v;
    return oss.str();
}

/* @brief 计算退费上限（实缴上限 vs 考勤折损上限取较小者 + 0.01 容差）
 * @param registration_id 报名记录 ID
 * @param original_amount 输出：原始报名金额（snapshot_amount）
 * @param result 输出：上限计算结果
 * @return 0=成功, ERR_REGISTRATION_NOT_FOUND, ERR_INVALID_PARAM, ERR_DB_*=失败
 */
int ClassManageHandler::ComputeRefundCap(int32_t registration_id, double& original_amount,
                                         RefundCapResult& result) {
    RegistrationInfo reg;
    int ret = reg_dao_->QueryRegistrationById(registration_id, reg);
    if (ret != DB_OK) {
        LOG_ERROR << "ClassManageHandler: query registration failed, reg_id=" << registration_id
                  << " ret=" << ret;
        return (ret == ERR_REGISTRATION_NOT_FOUND) ? ERR_REGISTRATION_NOT_FOUND : ERR_DB_EXEC_FAILED;
    }

    /* original_amount 统一取 registration.paid_amount_snapshot（定金=定金金额, 全额=预设全额,
       补缴后=目标全额）。定金学生因 paid_amount_snapshot=定金金额，退费上限天然=已付定金（D-4）。
       不再查 class_price.snapshot_amount，降低耦合且无需依赖 price_id 对应 class_price 是否存在。 */
    original_amount = reg.paid_amount_snapshot;

    std::vector<PriceInfo> prices;
    ret = class_dao_->QueryPricesByClassId(reg.class_id, prices);
    if (ret != DB_OK || prices.empty()) {
        LOG_ERROR << "ClassManageHandler: query prices failed, class_id=" << reg.class_id
                  << " ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    double max_single_price = 0.0;
    bool found_single = false;
    for (size_t i = 0; i < prices.size(); ++i) {
        int32_t hc = prices[i].snapshot_headcount;
        if (hc <= 0) { hc = 1; }
        if (hc == 1) {
            if (!found_single || prices[i].snapshot_amount > max_single_price) {
                max_single_price = prices[i].snapshot_amount;
                found_single = true;
            }
        }
    }
    if (!found_single) {
        for (size_t i = 0; i < prices.size(); ++i) {
            int32_t hc = prices[i].snapshot_headcount;
            if (hc <= 0) { hc = 1; }
            double per = prices[i].snapshot_amount / hc;
            if (!found_single || per > max_single_price) {
                max_single_price = per;
                found_single = true;
            }
        }
    }

    ClassInfo class_info;
    ret = class_dao_->QueryClassById(reg.class_id, class_info);
    if (ret != DB_OK) {
        LOG_ERROR << "ClassManageHandler: query class failed, class_id=" << reg.class_id
                  << " ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    int32_t total_class_days = register_student::CountWeekdaysInRange(class_info.start_time,
                                                                      class_info.end_time);
    if (total_class_days <= 0) { total_class_days = 1; }

    /* 部分时段判断：student_start_date/student_end_date 非空且与班级时段不同 */
    bool is_partial_period = false;
    int32_t student_class_days = total_class_days;  /* 默认=班级总天数 */
    if (!reg.student_start_date.empty() && !reg.student_end_date.empty()) {
        if (reg.student_start_date != class_info.start_time || reg.student_end_date != class_info.end_time) {
            is_partial_period = true;
            student_class_days = register_student::CountWeekdaysInRange(reg.student_start_date,
                                                                        reg.student_end_date);
            if (student_class_days <= 0) { student_class_days = 1; }
        }
    }

    double unit_price;
    if (reg.is_deposit == 1) {
        /* 定金学生 unit_price 按定金金额折算 */
        unit_price = original_amount / static_cast<double>(total_class_days);
    } else if (is_partial_period) {
        /* 部分时段全额学生 unit_price = paid_amount_snapshot / 学生上课天数 */
        unit_price = original_amount / static_cast<double>(student_class_days);
    } else {
        /* 全额完整时段学生 unit_price = max_single_price / 班级总天数（不变） */
        unit_price = max_single_price / static_cast<double>(total_class_days);
    }

    std::vector<AttendanceRecord> attendance;
    ret = attendance_dao_->QueryAttendanceByRegId(registration_id, attendance);
    if (ret != DB_OK) {
        LOG_ERROR << "ClassManageHandler: query attendance failed, reg_id=" << registration_id
                  << " ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }
    int32_t attended_days = 0;
    for (size_t i = 0; i < attendance.size(); ++i) {
        if (attendance[i].status == AttendanceStatus_Present ||
            attendance[i].status == AttendanceStatus_EarlyLeave ||
            attendance[i].status == AttendanceStatus_Late) {
            ++attended_days;
        }
    }
    bool zero_attendance = (attended_days == 0);
    int32_t deduction_days = zero_attendance ? 1 : attended_days;
    double attendance_limit = original_amount - unit_price * static_cast<double>(deduction_days);

    double accumulated_refund = 0.0;
    ret = refund_dao_->QueryActiveRefundSumByRegId(registration_id, accumulated_refund);
    if (ret != DB_OK) {
        LOG_ERROR << "ClassManageHandler: query refund sum failed, reg_id=" << registration_id
                  << " ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }
    double paid_limit = original_amount - accumulated_refund;

    /* 定金学生退费上限限定为已付定金（FR-8.1/D-4）：考勤折损不适用，因定金学生未付全额学费，
       attendance_limit 不应低于 paid_limit。全额/已补缴学生正常计算 attendance_limit。 */
    if (reg.is_deposit == 1) {
        attendance_limit = paid_limit;
    }

    result.paid_limit = paid_limit;
    result.attendance_limit = attendance_limit;
    result.final_limit = std::min(paid_limit, attendance_limit) + 0.01;
    result.unit_price = unit_price;
    result.total_class_days = total_class_days;
    result.attended_days = attended_days;
    result.deduction_days = deduction_days;
    result.original_amount = original_amount;
    result.accumulated_refund = accumulated_refund;
    result.zero_attendance = zero_attendance;

    LOG_INFO << "ClassManageHandler: refund cap, reg_id=" << registration_id
             << " original=" << original_amount
             << " paid_limit=" << paid_limit
             << " attendance_limit=" << attendance_limit
             << " final_limit=" << result.final_limit
             << " attended=" << attended_days
             << " deduction=" << deduction_days
             << " unit_price=" << unit_price;
    return DB_OK;
}

/* @brief 发起退费 */
crow::response ClassManageHandler::HandleRefund(const crow::request& req) {
    LOG_INFO << "ClassManageHandler: refund request";

    if (!class_dao_ || !reg_dao_ || !attendance_dao_ || !refund_dao_ || !log_dao_ || !session_mgr_) {
        LOG_ERROR << "ClassManageHandler: dao or session_mgr is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "dao or session_mgr is null";
        return crow::response(500, crow::json::dump(err));
    }

    /* 权限校验：仅 Admin */
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
    int32_t registration_id = 0;
    double refund_amount = 0.0;
    try {
        body = crow::json::load(req.body);
        if (!body.has("registration_id") || body["registration_id"].i() <= 0) {
            err_resp["code"] = ERR_INVALID_PARAM;
            err_resp["message"] = "registration_id is required";
            return crow::response(400, crow::json::dump(err_resp));
        }
        registration_id = body["registration_id"].i();
        if (!body.has("refund_amount")) {
            err_resp["code"] = ERR_INVALID_PARAM;
            err_resp["message"] = "refund_amount is required";
            return crow::response(400, crow::json::dump(err_resp));
        }
        refund_amount = body["refund_amount"].d();
    } catch (...) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (refund_amount < 0.0) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "refund_amount must be >= 0";
        return crow::response(200, crow::json::dump(err_resp));
    }

    /* 0 元退费 = 取消，等价直接返回当前实缴 */
    if (refund_amount == 0.0) {
        double accumulated = 0.0;
        refund_dao_->QueryActiveRefundSumByRegId(registration_id, accumulated);
        RegistrationInfo reg;
        double original = 0.0;
        int rret = reg_dao_->QueryRegistrationById(registration_id, reg);
        if (rret == DB_OK) {
            original = reg.paid_amount_snapshot;  /* 统一取 registration.paid_amount_snapshot */
        }
        crow::json::wvalue data;
        data["paid_amount"] = original - accumulated;
        data["refundable_max"] = original - accumulated;
        crow::json::wvalue resp;
        resp["code"] = DB_OK;
        resp["message"] = "success";
        resp["data"] = std::move(data);
        return crow::response(crow::json::dump(resp));
    }

    /* 计算退费上限 */
    double original_amount = 0.0;
    RefundCapResult cap;
    int cret = ComputeRefundCap(registration_id, original_amount, cap);
    if (cret != DB_OK) {
        err_resp["code"] = cret;
        if (cret == ERR_REGISTRATION_NOT_FOUND) {
            err_resp["message"] = "registration not found";
        } else {
            err_resp["message"] = "compute refund cap failed";
        }
        return crow::response(200, crow::json::dump(err_resp));
    }

    /* 上限校验：分支 1=超实缴上限（所有角色均受此限制），分支 2=超考勤折损上限（仅非管理员受此限制）
       管理员可退全部费用（不受考勤折损限制），但仍不可超过已付金额 */
    if (refund_amount > cap.paid_limit + 0.0001) {
        err_resp["code"] = ERR_REFUND_EXCEEDS_PAID;
        err_resp["message"] = "不可退费超过报名金额的钱";
        return crow::response(200, crow::json::dump(err_resp));
    }
    if (session_info.role != UserRole_Admin && refund_amount > cap.final_limit) {
        err_resp["code"] = ERR_REFUND_EXCEEDS_ATTENDANCE_CAP;
        if (cap.zero_attendance) {
            err_resp["message"] = "前期已投入教材配套资料等资源，0 天出勤折损上限为 "
                                  + FormatMoney(cap.attendance_limit) + " 元";
        } else {
            err_resp["message"] = "考勤折损上限为 " + FormatMoney(cap.attendance_limit)
                                  + " 元（已出勤 " + std::to_string(cap.attended_days)
                                  + " 天 / 总 " + std::to_string(cap.total_class_days)
                                  + " 天，单位价 " + FormatMoney(cap.unit_price) + " 元/天）";
        }
        return crow::response(200, crow::json::dump(err_resp));
    }

    /* 构造退款记录并执行原子插入 */
    RefundRecordInfo info;
    info.id = 0;
    info.registration_id = registration_id;
    info.refund_amount = refund_amount;
    info.operator_name = session_info.username;
    info.refund_time = register_student::GetCurrentTimeString();
    info.status = RefundStatus_Active;
    info.cancel_operator_name = "";
    info.cancel_time = "";
    info.unit_price = cap.unit_price;
    info.total_class_days = cap.total_class_days;
    info.attended_days = cap.deduction_days;
    info.original_amount = original_amount;
    info.tolerance_used = 0.01;

    ret = refund_dao_->InsertRefundAtomic(info, original_amount, 0.01,
                                          session_info.role == UserRole_Admin);
    if (ret != DB_OK) {
        LOG_ERROR << "ClassManageHandler: insert refund failed, reg_id=" << registration_id
                  << " ret=" << ret;
        err_resp["code"] = ret;
        if (ret == ERR_REFUND_EXCEEDS_PAID) {
            err_resp["message"] = "不可退费超过报名金额的钱";
        } else {
            err_resp["message"] = "refund failed";
        }
        return crow::response(200, crow::json::dump(err_resp));
    }

    /* 操作日志 */
    if (log_dao_) {
        OperationLog op_log;
        op_log.id = 0;
        op_log.op_type = OpType_ModifyStudent;
        op_log.operator_name = session_info.username;
        op_log.target_class = "";
        op_log.target_student = "";
        op_log.target_resource = "";
        std::ostringstream detail_ss;
        detail_ss << "refund " << FormatMoney(refund_amount)
                  << " for reg_id=" << registration_id
                  << ", paid=" << FormatMoney(original_amount - cap.accumulated_refund - refund_amount);
        op_log.detail = detail_ss.str();
        op_log.op_time = register_student::GetCurrentTimeString();
        log_dao_->InsertLog(op_log);
    }

    double new_paid = original_amount - cap.accumulated_refund - refund_amount;
    double new_refundable = cap.final_limit - refund_amount;
    if (new_refundable < 0.0) { new_refundable = 0.0; }

    crow::json::wvalue data;
    data["paid_amount"] = new_paid;
    data["refundable_max"] = new_refundable;
    crow::json::wvalue resp;
    resp["code"] = DB_OK;
    resp["message"] = "success";
    resp["data"] = std::move(data);

    LOG_INFO << "ClassManageHandler: refund success, reg_id=" << registration_id
             << " amount=" << refund_amount << " new_paid=" << new_paid;
    return crow::response(crow::json::dump(resp));
}

/* @brief 撤销退费 */
crow::response ClassManageHandler::HandleCancelRefund(const crow::request& req) {
    LOG_INFO << "ClassManageHandler: cancel refund request";

    if (!refund_dao_ || !log_dao_ || !session_mgr_) {
        LOG_ERROR << "ClassManageHandler: dao or session_mgr is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "dao or session_mgr is null";
        return crow::response(500, crow::json::dump(err));
    }

    /* 权限校验：仅 Admin */
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
    int32_t registration_id = 0;
    try {
        body = crow::json::load(req.body);
        if (!body.has("registration_id") || body["registration_id"].i() <= 0) {
            err_resp["code"] = ERR_INVALID_PARAM;
            err_resp["message"] = "registration_id is required";
            return crow::response(400, crow::json::dump(err_resp));
        }
        registration_id = body["registration_id"].i();
    } catch (...) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }

    std::string cancel_time = register_student::GetCurrentTimeString();
    double restored_paid = 0.0;
    ret = refund_dao_->CancelRefundAtomic(registration_id, session_info.username,
                                          cancel_time, restored_paid);
    if (ret != DB_OK) {
        LOG_ERROR << "ClassManageHandler: cancel refund failed, reg_id=" << registration_id
                  << " ret=" << ret;
        err_resp["code"] = ret;
        if (ret == ERR_REFUND_NOT_FOUND) {
            err_resp["message"] = "no active refund to cancel";
        } else {
            err_resp["message"] = "cancel refund failed";
        }
        return crow::response(200, crow::json::dump(err_resp));
    }

    /* 操作日志 */
    if (log_dao_) {
        OperationLog op_log;
        op_log.id = 0;
        op_log.op_type = OpType_ModifyStudent;
        op_log.operator_name = session_info.username;
        op_log.target_class = "";
        op_log.target_student = "";
        op_log.target_resource = "";
        std::ostringstream detail_ss;
        detail_ss << "cancel refund for reg_id=" << registration_id
                  << ", restored paid=" << FormatMoney(restored_paid);
        op_log.detail = detail_ss.str();
        op_log.op_time = cancel_time;
        log_dao_->InsertLog(op_log);
    }

    crow::json::wvalue data;
    data["paid_amount"] = restored_paid;
    crow::json::wvalue resp;
    resp["code"] = DB_OK;
    resp["message"] = "success";
    resp["data"] = std::move(data);

    LOG_INFO << "ClassManageHandler: cancel refund success, reg_id=" << registration_id
             << " restored_paid=" << restored_paid;
    return crow::response(crow::json::dump(resp));
}

crow::response ClassManageHandler::HandleSupplement(const crow::request& req) {
    LOG_INFO << "ClassManageHandler: supplement request";

    if (!reg_dao_ || !class_dao_ || !log_dao_ || !session_mgr_) {
        LOG_ERROR << "ClassManageHandler: dao or session_mgr is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "dao or session_mgr is null";
        return crow::response(500, crow::json::dump(err));
    }

    /* 权限校验：教师权限即可（FR-6.2，非仅 Admin） */
    SessionInfo session_info;
    int ret = CheckTeacherPermission(req, session_info);
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

    /* 解析请求体：registration_id + target_preset_id */
    crow::json::wvalue err_resp;
    crow::json::rvalue body;
    int32_t registration_id = 0;
    int32_t target_preset_id = 0;
    try {
        body = crow::json::load(req.body);
        if (!body.has("registration_id") || body["registration_id"].i() <= 0) {
            err_resp["code"] = ERR_INVALID_PARAM;
            err_resp["message"] = "registration_id is required";
            return crow::response(400, crow::json::dump(err_resp));
        }
        if (!body.has("target_preset_id") || body["target_preset_id"].i() <= 0) {
            err_resp["code"] = ERR_INVALID_PARAM;
            err_resp["message"] = "target_preset_id is required";
            return crow::response(400, crow::json::dump(err_resp));
        }
        registration_id = body["registration_id"].i();
        target_preset_id = body["target_preset_id"].i();
    } catch (...) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }

    /* 查报名记录所属班级 */
    RegistrationInfo reg;
    ret = reg_dao_->QueryRegistrationById(registration_id, reg);
    if (ret != DB_OK) {
        LOG_ERROR << "ClassManageHandler: supplement query reg failed, reg_id=" << registration_id
                  << " ret=" << ret;
        err_resp["code"] = (ret == ERR_REGISTRATION_NOT_FOUND) ? ERR_REGISTRATION_NOT_FOUND : ret;
        err_resp["message"] = "registration not found";
        return crow::response(200, crow::json::dump(err_resp));
    }

    /* 查班级预设列表，解析 target_class_price_id 与 target_amount（= class_price.snapshot_amount） */
    std::vector<PriceInfo> prices;
    ret = class_dao_->QueryPricesByClassId(reg.class_id, prices);
    if (ret != DB_OK) {
        LOG_ERROR << "ClassManageHandler: supplement query prices failed, class_id=" << reg.class_id
                  << " ret=" << ret;
        err_resp["code"] = ret;
        err_resp["message"] = "query class prices failed";
        return crow::response(500, crow::json::dump(err_resp));
    }
    int32_t target_class_price_id = -1;
    double target_amount = 0.0;
    bool found = false;
    for (size_t i = 0; i < prices.size(); ++i) {
        if (prices[i].preset_id == target_preset_id) {
            target_class_price_id = prices[i].id;
            target_amount = prices[i].snapshot_amount;
            found = true;
            break;
        }
    }
    if (!found) {
        err_resp["code"] = ERR_SUPPLEMENT_PRESET_NOT_IN_CLASS;
        err_resp["message"] = "目标预设不属于该班级";
        return crow::response(200, crow::json::dump(err_resp));
    }

    /* 调原子事务：事务内重读定金状态防并发重复补缴 + 校验目标全额 > 已付定金 + 更新 registration */
    std::string operate_time = register_student::GetCurrentTimeString();
    double supplement_amount = 0.0;
    ret = reg_dao_->SupplementDepositAtomic(registration_id, target_class_price_id,
                                            target_preset_id, target_amount,
                                            session_info.username, operate_time,
                                            supplement_amount);
    if (ret != DB_OK) {
        LOG_ERROR << "ClassManageHandler: supplement failed, reg_id=" << registration_id
                  << " ret=" << ret;
        err_resp["code"] = ret;
        if (ret == ERR_SUPPLEMENT_AMOUNT_INVALID) {
            err_resp["message"] = "补缴金额不符合班级要求";
        } else if (ret == ERR_SUPPLEMENT_ALREADY_DONE) {
            err_resp["message"] = "该学生已补缴，不可重复补缴";
        } else if (ret == ERR_SUPPLEMENT_PRESET_NOT_IN_CLASS) {
            err_resp["message"] = "目标预设不属于该班级";
        } else if (ret == ERR_REGISTRATION_NOT_FOUND) {
            err_resp["message"] = "registration not found";
        } else {
            err_resp["message"] = "supplement failed";
        }
        return crow::response(200, crow::json::dump(err_resp));
    }

    /* 写操作日志（OpType_SupplementDeposit=13） */
    if (log_dao_) {
        OperationLog op_log;
        op_log.id = 0;
        op_log.op_type = OpType_SupplementDeposit;
        op_log.operator_name = session_info.username;
        op_log.target_class = "";
        op_log.target_student = reg.student_name;
        op_log.target_resource = "";
        std::ostringstream detail_ss;
        detail_ss << "supplement " << FormatMoney(supplement_amount)
                  << ", target=" << FormatMoney(target_amount);
        op_log.detail = detail_ss.str();
        op_log.op_time = operate_time;
        log_dao_->InsertLog(op_log);
    }

    /* 返回补缴金额与新实缴（补缴后 paid_amount_snapshot=target_amount，无退费则实缴=目标全额） */
    crow::json::wvalue data;
    data["supplement_amount"] = supplement_amount;
    data["paid_amount"] = target_amount;
    crow::json::wvalue resp;
    resp["code"] = DB_OK;
    resp["message"] = "success";
    resp["data"] = std::move(data);

    LOG_INFO << "ClassManageHandler: supplement success, reg_id=" << registration_id
             << " supplement=" << supplement_amount << " target=" << target_amount;
    return crow::response(crow::json::dump(resp));
}

crow::response ClassManageHandler::HandleDeleteStudent(const crow::request& req) {
    LOG_INFO << "ClassManageHandler: delete student request";

    if (!reg_dao_ || !class_dao_ || !resource_dao_ || !log_dao_ || !session_mgr_) {
        LOG_ERROR << "ClassManageHandler: dao or session_mgr is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "dao or session_mgr is null";
        return crow::response(500, crow::json::dump(err));
    }

    /* admin permission check */
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

    /* parse request body */
    crow::json::wvalue err_resp;
    crow::json::rvalue body;
    int32_t registration_id = 0;
    try {
        body = crow::json::load(req.body);
        if (!body.has("registration_id") || body["registration_id"].i() <= 0) {
            err_resp["code"] = ERR_INVALID_PARAM;
            err_resp["message"] = "registration_id is required";
            return crow::response(200, crow::json::dump(err_resp));
        }
        registration_id = body["registration_id"].i();
    } catch (...) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(200, crow::json::dump(err_resp));
    }

    /* query registration for student name, class_id, need_bed */
    RegistrationInfo reg;
    ret = reg_dao_->QueryRegistrationById(registration_id, reg);
    if (ret != DB_OK) {
        LOG_ERROR << "ClassManageHandler: delete student query reg failed, reg_id=" << registration_id
                  << " ret=" << ret;
        err_resp["code"] = (ret == ERR_REGISTRATION_NOT_FOUND) ? ERR_REGISTRATION_NOT_FOUND : ret;
        err_resp["message"] = "registration not found";
        return crow::response(200, crow::json::dump(err_resp));
    }

    /* find bed resource id if need_bed=1 */
    int32_t bed_resource_id = -1;
    if (reg.need_bed == 1) {
        ResourceInfo bed_info;
        if (resource_dao_->QueryResourceByType(ResourceType_Bed, bed_info) == DB_OK) {
            bed_resource_id = bed_info.id;
        }
    }

    /* call atomic delete */
    ret = reg_dao_->DeleteRegistrationAtomic(registration_id, bed_resource_id);
    if (ret != DB_OK) {
        LOG_ERROR << "ClassManageHandler: delete student failed, reg_id=" << registration_id
                  << " ret=" << ret;
        err_resp["code"] = ret;
        if (ret == ERR_REGISTRATION_NOT_FOUND) {
            err_resp["message"] = "registration not found";
        } else {
            err_resp["message"] = "delete student failed";
        }
        return crow::response(200, crow::json::dump(err_resp));
    }

    /* write operation log */
    if (log_dao_) {
        ClassInfo class_info;
        class_dao_->QueryClassById(reg.class_id, class_info);

        OperationLog op_log;
        op_log.id = 0;
        op_log.op_type = OpType_DeleteStudent;
        op_log.operator_name = session_info.username;
        op_log.target_class = class_info.class_name;
        op_log.target_student = reg.student_name;
        op_log.target_resource = "";
        op_log.detail = "deleted student " + reg.student_name;
        op_log.op_time = register_student::GetCurrentTimeString();
        log_dao_->InsertLog(op_log);
    }

    crow::json::wvalue resp;
    resp["code"] = DB_OK;
    resp["message"] = "success";
    resp["data"] = crow::json::wvalue();

    LOG_INFO << "ClassManageHandler: delete student success, reg_id=" << registration_id;
    return crow::response(crow::json::dump(resp));
}

crow::response ClassManageHandler::HandleCalculateAmount(const crow::request& req) {
    /* 解析参数：class_id, start, end, price_id */
    int32_t class_id = ExtractIntParam(req, "class_id", 0);
    int32_t price_id = ExtractIntParam(req, "price_id", 0);
    std::string start_date;
    std::string end_date;
    char* p = nullptr;
    p = const_cast<crow::request&>(req).url_params.get("start");
    if (p && p[0] != '\0') { start_date = p; }
    p = const_cast<crow::request&>(req).url_params.get("end");
    if (p && p[0] != '\0') { end_date = p; }

    if (class_id <= 0 || start_date.empty() || end_date.empty() || price_id <= 0) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "class_id, start, end, price_id are required";
        return crow::response(200, crow::json::dump(err));
    }

    /* 查班级信息 */
    ClassInfo class_info;
    int ret = class_dao_->QueryClassById(class_id, class_info);
    if (ret != DB_OK) {
        crow::json::wvalue err;
        err["code"] = ret;
        err["message"] = "class not found";
        return crow::response(200, crow::json::dump(err));
    }

    /* 查价位信息取 snapshot_amount */
    std::vector<PriceInfo> prices;
    ret = class_dao_->QueryPricesByClassId(class_id, prices);
    if (ret != DB_OK) {
        crow::json::wvalue err;
        err["code"] = ret;
        err["message"] = "query prices failed";
        return crow::response(200, crow::json::dump(err));
    }
    double snapshot_amount = 0.0;
    bool found_price = false;
    for (size_t i = 0; i < prices.size(); ++i) {
        if (prices[i].id == price_id) {
            snapshot_amount = prices[i].snapshot_amount;
            found_price = true;
            break;
        }
    }
    if (!found_price) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "price_id not found in class";
        return crow::response(200, crow::json::dump(err));
    }

    /* 计算折算金额 */
    int32_t total_class_days = register_student::CountWeekdaysInRange(class_info.start_time,
                                                                      class_info.end_time);
    if (total_class_days <= 0) { total_class_days = 1; }
    int32_t student_days = register_student::CountWeekdaysInRange(start_date, end_date);
    if (student_days <= 0) { student_days = 1; }
    double suggested_amount = snapshot_amount * static_cast<double>(student_days)
                              / static_cast<double>(total_class_days);

    crow::json::wvalue data;
    data["suggested_amount"] = suggested_amount;
    data["student_days"] = student_days;
    data["total_class_days"] = total_class_days;

    crow::json::wvalue resp;
    resp["code"] = DB_OK;
    resp["message"] = "success";
    resp["data"] = std::move(data);
    return crow::response(crow::json::dump(resp));
}

crow::response ClassManageHandler::HandleRenew(const crow::request& req) {
    /* 权限校验 */
    SessionInfo session_info;
    int perm = CheckTeacherPermission(req, session_info);
    if (perm != 0) {
        crow::json::wvalue err;
        err["code"] = ERR_AUTH_PERMISSION_DENIED;
        err["message"] = "permission denied";
        return crow::response(403, crow::json::dump(err));
    }

    /* 解析请求 */
    crow::json::wvalue err_resp;
    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (!body.has("registration_id") || !body.has("new_end_date") || !body.has("renew_amount")) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "registration_id, new_end_date, renew_amount are required";
        return crow::response(200, crow::json::dump(err_resp));
    }

    int32_t registration_id = body["registration_id"].i();
    std::string new_end_date = std::string(body["new_end_date"].s());
    double renew_amount = body["renew_amount"].d();

    /* 校验续费金额 */
    if (renew_amount < 0.0) {
        err_resp["code"] = ERR_RENEWAL_AMOUNT_INVALID;
        err_resp["message"] = "renew_amount must be >= 0";
        return crow::response(200, crow::json::dump(err_resp));
    }

    /* 查报名记录 */
    RegistrationInfo reg;
    int ret = reg_dao_->QueryRegistrationById(registration_id, reg);
    if (ret != DB_OK) {
        err_resp["code"] = ERR_REGISTRATION_NOT_FOUND;
        err_resp["message"] = "registration not found";
        return crow::response(200, crow::json::dump(err_resp));
    }

    /* 查班级信息 */
    ClassInfo class_info;
    ret = class_dao_->QueryClassById(reg.class_id, class_info);
    if (ret != DB_OK) {
        err_resp["code"] = ret;
        err_resp["message"] = "class not found";
        return crow::response(200, crow::json::dump(err_resp));
    }

    /* 校验续费条件 */
    /* 空日期视为班级完整时段 */
    std::string stu_end = reg.student_end_date.empty() ? class_info.end_time : reg.student_end_date;
    std::string stu_start = reg.student_start_date.empty() ? class_info.start_time : reg.student_start_date;

    /* 全额报名学生（上课时段=班级完整时段）不允许续费 */
    bool is_full_period = (stu_start == class_info.start_time && stu_end == class_info.end_time);
    if (is_full_period) {
        err_resp["code"] = ERR_RENEWAL_NOT_ALLOWED;
        err_resp["message"] = "full period student cannot renew";
        return crow::response(200, crow::json::dump(err_resp));
    }

    /* 已全额退费学生不允许续费 */
    if (reg.refund_amount > 0.001 && reg.paid_amount_snapshot - reg.refund_amount <= 0.001) {
        err_resp["code"] = ERR_RENEWAL_NOT_ALLOWED;
        err_resp["message"] = "fully refunded student cannot renew";
        return crow::response(200, crow::json::dump(err_resp));
    }

    /* new_end_date 必须 > 原 student_end_date */
    if (new_end_date <= stu_end) {
        err_resp["code"] = ERR_RENEWAL_DATE_INVALID;
        err_resp["message"] = "new_end_date must be after current student_end_date";
        return crow::response(200, crow::json::dump(err_resp));
    }

    /* new_end_date 不得超出班级 end_time */
    if (new_end_date > class_info.end_time) {
        err_resp["code"] = ERR_RENEWAL_DATE_INVALID;
        err_resp["message"] = "new_end_date must not exceed class end_time";
        return crow::response(200, crow::json::dump(err_resp));
    }

    /* 计算名额增量 */
    int32_t total_class_days = register_student::CountWeekdaysInRange(class_info.start_time,
                                                                      class_info.end_time);
    if (total_class_days <= 0) { total_class_days = 1; }
    int32_t old_days = register_student::CountWeekdaysInRange(stu_start, stu_end);
    if (old_days <= 0) { old_days = 1; }
    int32_t new_days = register_student::CountWeekdaysInRange(stu_start, new_end_date);
    if (new_days <= 0) { new_days = 1; }
    double enrollment_delta = static_cast<double>(new_days - old_days)
                              / static_cast<double>(total_class_days);

    /* 检查剩余名额 */
    double used = reg_dao_->QueryEnrollmentUsedByClassId(reg.class_id);
    if (used < -0.001) {
        err_resp["code"] = ERR_DB_EXEC_FAILED;
        err_resp["message"] = "query enrollment failed";
        return crow::response(200, crow::json::dump(err_resp));
    }
    double remaining = static_cast<double>(class_info.enrollment_capacity) - used;
    if (remaining < enrollment_delta - 0.001) {
        err_resp["code"] = ERR_CLASS_ENROLLMENT_FULL;
        err_resp["message"] = "enrollment capacity insufficient for renewal";
        return crow::response(200, crow::json::dump(err_resp));
    }

    /* 调用续费原子事务 */
    ret = reg_dao_->RenewRegistrationAtomic(registration_id, new_end_date, renew_amount,
                                            enrollment_delta, session_info.username,
                                            register_student::GetCurrentTimeString());
    if (ret != DB_OK) {
        LOG_ERROR << "ClassManageHandler: renew failed, reg_id=" << registration_id
                  << " ret=" << ret;
        err_resp["code"] = ret;
        err_resp["message"] = "renew failed";
        return crow::response(200, crow::json::dump(err_resp));
    }

    /* 写操作日志 */
    if (log_dao_) {
        OperationLog op_log;
        op_log.id = 0;
        op_log.op_type = OpType_RenewRegistration;
        op_log.operator_name = session_info.username;
        op_log.target_class = class_info.class_name;
        op_log.target_student = reg.student_name;
        op_log.target_resource = "";
        op_log.detail = "renewed period to " + new_end_date + ", amount=" + FormatMoney(renew_amount);
        op_log.op_time = register_student::GetCurrentTimeString();
        log_dao_->InsertLog(op_log);
    }

    crow::json::wvalue resp;
    resp["code"] = DB_OK;
    resp["message"] = "success";
    resp["data"] = crow::json::wvalue();

    LOG_INFO << "ClassManageHandler: renew success, reg_id=" << registration_id
             << " new_end=" << new_end_date << " amount=" << renew_amount;
    return crow::response(crow::json::dump(resp));
}
