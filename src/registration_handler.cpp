#include "registration_handler.h"
#include "utils.h"
#include "error_codes.h"
#include "log_types.h"

#include <vector>
#include <string>
#include <cstdlib>
#include <cmath>
#include <sstream>
#include <iomanip>

/* @brief 金额格式化为两位小数字符串（与 class_manage_handler.cpp 静态 helper 一致，用于操作日志详情） */
static std::string FormatMoney(double v) {
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss << std::setprecision(2) << v;
    return oss.str();
}

RegistrationHandler::RegistrationHandler(IClassDao* class_dao, IRegistrationDao* reg_dao,
                                         IResourceDao* resource_dao, IOperationLogDao* log_dao,
                                         SessionManager* session_mgr)
    : class_dao_(class_dao)
    , reg_dao_(reg_dao)
    , resource_dao_(resource_dao)
    , log_dao_(log_dao)
    , session_mgr_(session_mgr) {}

RegistrationHandler::~RegistrationHandler() {}

void RegistrationHandler::RegisterRoutes(crow::SimpleApp& app) {
    CROW_ROUTE(app, "/api/registration/classes").methods("GET"_method)
    ([this]() {
        return HandleListClasses();
    });

    CROW_ROUTE(app, "/api/registration/class").methods("GET"_method)
    ([this](const crow::request& req) {
        return HandleGetClassDetail(req);
    });

    CROW_ROUTE(app, "/api/registration/register").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleRegister(req);
    });
}

crow::response RegistrationHandler::HandleListClasses() {
    LOG_INFO << "RegistrationHandler: list classes request";

    if (!class_dao_) {
        LOG_ERROR << "RegistrationHandler: class_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "class_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    /* query active classes */
    std::vector<ClassInfo> classes;
    int ret = class_dao_->QueryActiveClasses(classes);
    if (ret != DB_OK) {
        LOG_ERROR << "RegistrationHandler: query active classes failed, ret=" << ret;
        crow::json::wvalue err;
        err["code"] = ret;
        err["message"] = "query active classes failed";
        return crow::response(500, crow::json::dump(err));
    }

    /* build response */
    std::vector<crow::json::wvalue> class_items;
    for (size_t i = 0; i < classes.size(); ++i) {
        int active_count = reg_dao_->CountActiveStudentsByClassId(classes[i].id);
        if (active_count < 0) { active_count = 0; }

        crow::json::wvalue item;
        item["class_id"] = classes[i].id;
        item["class_name"] = classes[i].class_name;
        item["class_type"] = classes[i].class_type;
        item["start_time"] = classes[i].start_time;
        item["end_time"] = classes[i].end_time;
        item["enrollment_capacity"] = classes[i].enrollment_capacity;
        item["enrollment_used"] = active_count;
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

    LOG_INFO << "RegistrationHandler: list classes success, count=" << classes.size();
    return crow::response(result);
}

crow::response RegistrationHandler::HandleGetClassDetail(const crow::request& req) {
    LOG_INFO << "RegistrationHandler: get class detail request";

    if (!class_dao_) {
        LOG_ERROR << "RegistrationHandler: class_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "class_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    if (!reg_dao_) {
        LOG_ERROR << "RegistrationHandler: reg_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "reg_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    /* parse class_id from query params */
    int32_t class_id = 0;
    char* id_str = const_cast<crow::request&>(req).url_params.get("id");
    if (id_str && id_str[0] != '\0') {
        class_id = static_cast<int32_t>(std::atoi(id_str));
    }

    if (class_id <= 0) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "id is required";
        return crow::response(400, crow::json::dump(err));
    }

    /* query class info */
    ClassInfo class_info;
    int ret = class_dao_->QueryClassById(class_id, class_info);
    if (ret != DB_OK) {
        LOG_ERROR << "RegistrationHandler: query class by id failed, ret=" << ret;
        crow::json::wvalue err;
        err["code"] = ret;
        err["message"] = "class not found";
        return crow::response(404, crow::json::dump(err));
    }

    /* query prices */
    std::vector<PriceInfo> prices;
    ret = class_dao_->QueryPricesByClassId(class_id, prices);
    if (ret != DB_OK) {
        LOG_ERROR << "RegistrationHandler: query prices by class id failed, ret=" << ret;
        /* price query failure does not block, return empty array */
    }

    /* build price array with qrcode paths */
    std::vector<crow::json::wvalue> price_items;
    for (size_t i = 0; i < prices.size(); ++i) {
        crow::json::wvalue price_item;
        price_item["id"] = prices[i].id;
        price_item["class_id"] = prices[i].class_id;
        price_item["price"] = prices[i].price;
        price_item["expected_headcount"] = prices[i].snapshot_headcount;
        price_item["activity_name"] = prices[i].activity_name;

        /* query qrcode paths for each price */
        std::vector<std::string> qrcode_paths;
        int qr_ret = class_dao_->QueryQrcodesByPriceId(prices[i].id, qrcode_paths);
        if (qr_ret != DB_OK) {
            LOG_ERROR << "RegistrationHandler: query qrcodes by price id failed, ret=" << qr_ret;
        }

        for (size_t j = 0; j < qrcode_paths.size(); ++j) {
            price_item["qrcode_paths"][j] = qrcode_paths[j];
        }

        price_items.push_back(std::move(price_item));
    }

    int active_count = reg_dao_->CountActiveStudentsByClassId(class_id);
    if (active_count < 0) { active_count = 0; }

    LOG_INFO << "RegistrationHandler: get class detail, id=" << class_id
             << " name=" << class_info.class_name
             << " active_students=" << active_count
             << " enrollment_capacity=" << class_info.enrollment_capacity
             << " class_period=" << class_info.start_time << "~" << class_info.end_time;

    /* check bed resource */
    int32_t bed_remain = -1;
    if (resource_dao_) {
        ResourceInfo bed_resource;
        int bed_ret = resource_dao_->QueryResourceByType(ResourceType_Bed, bed_resource);
        if (bed_ret == DB_OK) {
            bed_remain = resource_dao_->QueryBedResourceRemain(bed_resource.id);
        }
    }

    /* build response */
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

    /* query deposit qrcode paths from zero-amount preset (for deposit registration) */
    std::vector<PricePresetInfo> all_presets;
    if (class_dao_->QueryAllPricePresets(all_presets) == DB_OK) {
        for (size_t i = 0; i < all_presets.size(); ++i) {
            if (all_presets[i].amount < 0.001 && !all_presets[i].qrcode_paths.empty()) {
                for (size_t j = 0; j < all_presets[i].qrcode_paths.size(); ++j) {
                    data["deposit_qrcode_paths"][j] = all_presets[i].qrcode_paths[j];
                }
                break;
            }
        }
    }

    if (bed_remain >= 0) {
        data["bed_remain"] = bed_remain;
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    LOG_INFO << "RegistrationHandler: get class detail success, id=" << class_id;
    return crow::response(result);
}

std::string RegistrationHandler::GetSessionIdFromCookie(const crow::request& req) {
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

crow::response RegistrationHandler::HandleRegister(const crow::request& req) {
    LOG_INFO << "RegistrationHandler: register request";

    /* validate session - registration requires login */
    if (!session_mgr_) {
        LOG_ERROR << "RegistrationHandler: session_mgr is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "session_mgr is null";
        return crow::response(500, crow::json::dump(err));
    }

    std::string session_id = GetSessionIdFromCookie(req);
    SessionInfo session_info;
    int session_ret = session_mgr_->ValidateSession(session_id, session_info);
    if (session_ret != DB_OK) {
        LOG_DEBUG << "RegistrationHandler: no valid session";
        crow::json::wvalue err;
        err["code"] = ERR_AUTH_SESSION_EXPIRED;
        err["message"] = "session expired or invalid";
        return crow::response(401, crow::json::dump(err));
    }

    if (!class_dao_) {
        LOG_ERROR << "RegistrationHandler: class_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "class_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    if (!reg_dao_) {
        LOG_ERROR << "RegistrationHandler: reg_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "reg_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    /* parse request body */
    crow::json::wvalue err_resp;
    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }

    /* validate required fields */
    if (!body.has("class_id")) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "class_id is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (!body.has("teacher_name") || std::string(body["teacher_name"].s()).empty()) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "teacher_name is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    int32_t class_id = body["class_id"].i();
    std::string teacher_name = body["teacher_name"].s();
    int32_t price_id = body.has("price_id") ? body["price_id"].i() : 0;
    std::string other_info = body.has("other_info") ? std::string(body["other_info"].s()) : "";
    std::string register_time = register_student::GetCurrentTimeString();

    /* 定金报名参数解析：is_deposit=1 表示定金方式，需附带 deposit_amount（>=0） */
    int32_t is_deposit = body.has("is_deposit") ? body["is_deposit"].i() : 0;
    if (is_deposit != 0 && is_deposit != 1) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "is_deposit must be 0 or 1";
        return crow::response(200, crow::json::dump(err_resp));
    }
    double deposit_amount = 0.0;
    if (is_deposit == 1) {
        if (!body.has("deposit_amount")) {
            err_resp["code"] = ERR_DEPOSIT_AMOUNT_INVALID;
            err_resp["message"] = "deposit_amount is required for deposit registration";
            return crow::response(200, crow::json::dump(err_resp));
        }
        deposit_amount = body["deposit_amount"].d();
        if (deposit_amount < 0.0) {
            err_resp["code"] = ERR_DEPOSIT_AMOUNT_INVALID;
            err_resp["message"] = "请输入有效的定金金额";
            return crow::response(200, crow::json::dump(err_resp));
        }
        /* 0 元预设后端兜底校验（FR-2.1，防前端绕过）：定金方式下必须存在 amount=0 的预设 */
        std::vector<PricePresetInfo> presets;
        if (class_dao_->QueryAllPricePresets(presets) == DB_OK) {
            bool has_zero = false;
            for (size_t i = 0; i < presets.size(); ++i) {
                if (presets[i].amount < 0.001) { has_zero = true; break; }
            }
            if (!has_zero) {
                err_resp["code"] = ERR_ZERO_PRESET_NOT_FOUND;
                err_resp["message"] = "请先创建价格为 0 的价格预设";
                return crow::response(200, crow::json::dump(err_resp));
            }
        }
    }

    /* 解析 students 数组；兼容旧调用：顶层有 student_name 时转单元素数组 */
    std::vector<RegistrationInfo> infos;
    if (body.has("students") && body["students"].t() == crow::json::type::List) {
        size_t s_count = body["students"].size();
        for (size_t i = 0; i < s_count; ++i) {
            const crow::json::rvalue& s = body["students"][i];
            if (!s.has("student_name") || std::string(s["student_name"].s()).empty()) {
                err_resp["code"] = ERR_INVALID_PARAM;
                err_resp["message"] = "student_name is required";
                return crow::response(400, crow::json::dump(err_resp));
            }
            if (!s.has("student_gender") ||
                (std::string(s["student_gender"].s()) != "male" &&
                 std::string(s["student_gender"].s()) != "female")) {
                err_resp["code"] = ERR_INVALID_PARAM;
                err_resp["message"] = "student_gender must be male or female";
                return crow::response(400, crow::json::dump(err_resp));
            }

            std::string parent_phone = s.has("parent_phone") ? std::string(s["parent_phone"].s()) : "";
            if (!parent_phone.empty()) {
                if (parent_phone.size() < 7 || parent_phone.size() > 15) {
                    err_resp["code"] = ERR_REGISTRATION_PHONE_INVALID;
                    err_resp["message"] = "parent_phone length must be 7-15 digits";
                    return crow::response(400, crow::json::dump(err_resp));
                }
                for (size_t k = 0; k < parent_phone.size(); ++k) {
                    if (parent_phone[k] < '0' || parent_phone[k] > '9') {
                        err_resp["code"] = ERR_REGISTRATION_PHONE_INVALID;
                        err_resp["message"] = "parent_phone must contain only digits";
                        return crow::response(400, crow::json::dump(err_resp));
                    }
                }
            }

            RegistrationInfo info;
            info.id = 0;
            info.class_id = class_id;
            info.student_name = s["student_name"].s();
            info.student_gender = s["student_gender"].s();
            info.parent_phone = parent_phone;
            info.has_allergy = s.has("has_allergy") ? s["has_allergy"].i() : 0;
            info.allergy_desc = s.has("allergy_desc") ? std::string(s["allergy_desc"].s()) : "";
            info.price_id = price_id;
            info.need_bed = s.has("need_bed") ? s["need_bed"].i() : 0;
            info.teacher_name = teacher_name;
            info.other_info = other_info;
            info.register_time = register_time;
            info.is_deposit = is_deposit;
            info.supplement_amount = 0;
            info.supplement_preset_id = -1;
            info.supplement_operator = "";
            info.supplement_time = "";
            /* 部分时段报名：优先取每学生独立日期，否则回退到顶层日期 */
            if (s.has("student_start_date")) {
                info.student_start_date = std::string(s["student_start_date"].s());
            } else if (body.has("student_start_date")) {
                info.student_start_date = std::string(body["student_start_date"].s());
            }
            if (s.has("student_end_date")) {
                info.student_end_date = std::string(s["student_end_date"].s());
            } else if (body.has("student_end_date")) {
                info.student_end_date = std::string(body["student_end_date"].s());
            }
            info.enrollment_ratio = 1.0;  /* 默认全额占用，稍后按日期折算 */
            if (is_deposit == 1) {
                info.paid_amount_snapshot = deposit_amount;  /* 定金：存定金金额 */
            } else {
                /* 全额报名：actual_amount 优先取每学生，否则回退到顶层 */
                double actual_amount = -1;
                if (s.has("actual_amount")) {
                    actual_amount = s["actual_amount"].d();
                } else if (body.has("actual_amount")) {
                    actual_amount = body["actual_amount"].d();
                }
                if (actual_amount >= 0) {
                    info.paid_amount_snapshot = actual_amount;
                } else {
                    info.paid_amount_snapshot = 0;  /* 未传 actual_amount，稍后回填预设全额 */
                }
            }

            if (info.has_allergy == 1 && info.allergy_desc.empty()) {
                err_resp["code"] = ERR_REGISTRATION_ALLERGY_REQUIRED;
                err_resp["message"] = "allergy_desc is required when has_allergy is 1";
                return crow::response(400, crow::json::dump(err_resp));
            }

            infos.push_back(info);
        }
    } else {
        /* 兼容旧调用：顶层单学生字段 */
        if (!body.has("student_name") || std::string(body["student_name"].s()).empty()) {
            err_resp["code"] = ERR_INVALID_PARAM;
            err_resp["message"] = "student_name is required";
            return crow::response(400, crow::json::dump(err_resp));
        }
        if (!body.has("student_gender") ||
            (std::string(body["student_gender"].s()) != "male" &&
             std::string(body["student_gender"].s()) != "female")) {
            err_resp["code"] = ERR_INVALID_PARAM;
            err_resp["message"] = "student_gender must be male or female";
            return crow::response(400, crow::json::dump(err_resp));
        }

        std::string parent_phone = body.has("parent_phone") ? std::string(body["parent_phone"].s()) : "";
        if (!parent_phone.empty()) {
            if (parent_phone.size() < 7 || parent_phone.size() > 15) {
                err_resp["code"] = ERR_REGISTRATION_PHONE_INVALID;
                err_resp["message"] = "parent_phone length must be 7-15 digits";
                return crow::response(400, crow::json::dump(err_resp));
            }
            for (size_t k = 0; k < parent_phone.size(); ++k) {
                if (parent_phone[k] < '0' || parent_phone[k] > '9') {
                    err_resp["code"] = ERR_REGISTRATION_PHONE_INVALID;
                    err_resp["message"] = "parent_phone must contain only digits";
                    return crow::response(400, crow::json::dump(err_resp));
                }
            }
        }

        RegistrationInfo info;
        info.id = 0;
        info.class_id = class_id;
        info.student_name = body["student_name"].s();
        info.student_gender = body["student_gender"].s();
        info.parent_phone = parent_phone;
        info.has_allergy = body.has("has_allergy") ? body["has_allergy"].i() : 0;
        info.allergy_desc = body.has("allergy_desc") ? std::string(body["allergy_desc"].s()) : "";
        info.price_id = price_id;
        info.need_bed = body.has("need_bed") ? body["need_bed"].i() : 0;
        info.teacher_name = teacher_name;
        info.other_info = other_info;
        info.register_time = register_time;
        info.is_deposit = is_deposit;
        info.supplement_amount = 0;
        info.supplement_preset_id = -1;
        info.supplement_operator = "";
        info.supplement_time = "";
        /* 部分时段报名：兼容旧调用也支持日期和金额 */
        info.student_start_date = body.has("student_start_date") ? std::string(body["student_start_date"].s()) : "";
        info.student_end_date = body.has("student_end_date") ? std::string(body["student_end_date"].s()) : "";
        info.enrollment_ratio = 1.0;  /* 默认全额占用，稍后按日期折算 */
        if (is_deposit == 1) {
            info.paid_amount_snapshot = deposit_amount;  /* 定金：存定金金额 */
        } else {
            /* 全额报名：actual_amount 优先，未传则稍后回填预设全额 */
            if (body.has("actual_amount")) {
                double actual_amount = body["actual_amount"].d();
                if (actual_amount < 0.0) {
                    err_resp["code"] = ERR_INVALID_PARAM;
                    err_resp["message"] = "actual_amount must be >= 0";
                    return crow::response(400, crow::json::dump(err_resp));
                }
                info.paid_amount_snapshot = actual_amount;
            } else {
                info.paid_amount_snapshot = 0;  /* 未传 actual_amount，稍后回填预设全额 */
            }
        }

        if (info.has_allergy == 1 && info.allergy_desc.empty()) {
            err_resp["code"] = ERR_REGISTRATION_ALLERGY_REQUIRED;
            err_resp["message"] = "allergy_desc is required when has_allergy is 1";
            return crow::response(400, crow::json::dump(err_resp));
        }

        infos.push_back(info);
    }

    if (infos.empty()) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "students list is empty";
        return crow::response(400, crow::json::dump(err_resp));
    }

    /* check class exists */
    ClassInfo class_info;
    int ret = class_dao_->QueryClassById(class_id, class_info);
    if (ret != DB_OK) {
        LOG_ERROR << "RegistrationHandler: query class by id failed, ret=" << ret;
        err_resp["code"] = ret;
        err_resp["message"] = "class not found";
        return crow::response(404, crow::json::dump(err_resp));
    }

    /* 部分时段日期校验 + enrollment_ratio 计算（FR-3, FR-7, FR-14） */
    int32_t total_class_days = register_student::CountWeekdaysInRange(class_info.start_time,
                                                                      class_info.end_time);
    if (total_class_days <= 0) { total_class_days = 1; }
    for (size_t i = 0; i < infos.size(); ++i) {
        /* 空日期默认使用班级完整时段 */
        if (infos[i].student_start_date.empty()) {
            infos[i].student_start_date = class_info.start_time;
        }
        if (infos[i].student_end_date.empty()) {
            infos[i].student_end_date = class_info.end_time;
        }
        /* 日期范围校验 */
        if (infos[i].student_start_date > infos[i].student_end_date) {
            err_resp["code"] = ERR_REGISTRATION_DATE_INVALID;
            err_resp["message"] = "student_start_date must not be later than student_end_date";
            return crow::response(200, crow::json::dump(err_resp));
        }
        if (infos[i].student_start_date < class_info.start_time) {
            err_resp["code"] = ERR_REGISTRATION_DATE_OUT_OF_RANGE;
            err_resp["message"] = "student_start_date is before class start_time";
            return crow::response(200, crow::json::dump(err_resp));
        }
        if (infos[i].student_end_date > class_info.end_time) {
            err_resp["code"] = ERR_REGISTRATION_DATE_OUT_OF_RANGE;
            err_resp["message"] = "student_end_date is after class end_time";
            return crow::response(200, crow::json::dump(err_resp));
        }
        /* 计算 enrollment_ratio */
        int32_t student_days = register_student::CountWeekdaysInRange(infos[i].student_start_date,
                                                                      infos[i].student_end_date);
        if (student_days <= 0) { student_days = 1; }
        double raw_ratio = static_cast<double>(student_days)
                           / static_cast<double>(total_class_days);
        infos[i].enrollment_ratio = std::round(raw_ratio * 10000.0) / 10000.0;
        LOG_INFO << "RegistrationHandler: enrollment_ratio calc, student=" << infos[i].student_name
                 << " student_days=" << student_days
                 << " total_class_days=" << total_class_days
                 << " raw_ratio=" << raw_ratio
                 << " rounded_ratio=" << infos[i].enrollment_ratio
                 << " period=" << infos[i].student_start_date << "~" << infos[i].student_end_date;
    }

    /* 校验价位人数：所选 price_id 的 expected_headcount 必须等于学生数；
       同时为全额报名回填 paid_amount_snapshot=预设全额（定金报名 paid_amount_snapshot 已存定金金额） */
    if (price_id > 0) {
        std::vector<PriceInfo> prices;
        int price_ret = class_dao_->QueryPricesByClassId(class_id, prices);
        if (price_ret == DB_OK) {
            int32_t expected_headcount = -1;
            double preset_full_amount = 0.0;
            for (size_t i = 0; i < prices.size(); ++i) {
                if (prices[i].id == price_id) {
                    expected_headcount = prices[i].snapshot_headcount;
                    preset_full_amount = prices[i].snapshot_amount;
                    break;
                }
            }
            if (expected_headcount > 0 && static_cast<int32_t>(infos.size()) != expected_headcount) {
                err_resp["code"] = ERR_REGISTRATION_HEADCOUNT_MISMATCH;
                err_resp["message"] = "该价位必须 " + std::to_string(expected_headcount)
                                      + " 个学生同时报名";
                return crow::response(200, crow::json::dump(err_resp));
            }
            /* 全额报名：回填 paid_amount_snapshot。
               若 actual_amount 已传（paid_amount_snapshot > 0），保留老师确认的实际金额；
               若未传（paid_amount_snapshot == 0），回填预设全额（向后兼容） */
            if (is_deposit == 0) {
                for (size_t i = 0; i < infos.size(); ++i) {
                    if (infos[i].paid_amount_snapshot < 0.001) {
                        infos[i].paid_amount_snapshot = preset_full_amount;
                    }
                }
            }
        }
    }

    /* resolve bed resource id if any student needs bed */
    int32_t bed_resource_id = -1;
    bool any_need_bed = false;
    for (size_t i = 0; i < infos.size(); ++i) {
        if (infos[i].need_bed == 1) { any_need_bed = true; break; }
    }
    if (any_need_bed) {
        if (!resource_dao_) {
            LOG_ERROR << "RegistrationHandler: resource_dao is null";
            err_resp["code"] = ERR_HANDLER_NULL_DAO;
            err_resp["message"] = "resource_dao is null";
            return crow::response(500, crow::json::dump(err_resp));
        }
        ResourceInfo bed_resource;
        int bed_ret = resource_dao_->QueryResourceByType(ResourceType_Bed, bed_resource);
        if (bed_ret != DB_OK) {
            err_resp["code"] = ERR_RESOURCE_BED_UNAVAILABLE;
            err_resp["message"] = "床位资源不存在，请先在资源管理中创建床位资源";
            return crow::response(200, crow::json::dump(err_resp));
        }
        bed_resource_id = bed_resource.id;
    }

    /* atomic batch registration: single transaction for all students.
       定金方式走 RegisterDepositAtomic（写 is_deposit=1, paid_amount_snapshot=定金金额），
       全额方式走 RegisterStudentsBatchAtomic。 */
    if (is_deposit == 1) {
        ret = reg_dao_->RegisterDepositAtomic(infos, class_id,
                                              class_info.enrollment_capacity,
                                              bed_resource_id);
    } else {
        ret = reg_dao_->RegisterStudentsBatchAtomic(infos, class_id,
                                                    class_info.enrollment_capacity,
                                                    bed_resource_id);
    }
    if (ret != DB_OK) {
        LOG_ERROR << "RegistrationHandler: batch register failed, ret=" << ret;
        err_resp["code"] = ret;
        if (ret == ERR_CLASS_ENROLLMENT_FULL) {
            double enrollment_used = reg_dao_->QueryEnrollmentUsedByClassId(class_id);
            double remaining = static_cast<double>(class_info.enrollment_capacity) - enrollment_used;
            err_resp["message"] = "剩余名额不足，需要 " + std::to_string(infos.size())
                                  + " 个，仅剩 " + FormatMoney(remaining) + " 个";
            return crow::response(200, crow::json::dump(err_resp));
        } else if (ret == ERR_RESOURCE_BED_UNAVAILABLE) {
            err_resp["message"] = "床位资源不足";
            return crow::response(200, crow::json::dump(err_resp));
        }
        err_resp["message"] = "register failed";
        return crow::response(500, crow::json::dump(err_resp));
    }

    /* insert operation log */
    if (log_dao_) {
        OperationLog op_log;
        op_log.id = 0;
        op_log.op_type = OpType_RegisterStudent;
        op_log.operator_name = teacher_name;
        op_log.target_class = class_info.class_name;
        op_log.target_student = "";
        op_log.target_resource = "";
        op_log.detail = (is_deposit == 1)
            ? ("deposit registered " + std::to_string(infos.size()) + " students, deposit="
               + FormatMoney(deposit_amount))
            : ("batch registered " + std::to_string(infos.size()) + " students");
        op_log.op_time = register_time;
        log_dao_->InsertLog(op_log);
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";

    LOG_INFO << "RegistrationHandler: batch register success, class_id=" << class_id
             << " count=" << infos.size();
    return crow::response(result);
}
