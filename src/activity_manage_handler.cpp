#include "activity_manage_handler.h"
#include "error_codes.h"
#include "upload_util.h"
#include "utils.h"
#include "group_session_manager.h"

#include <vector>
#include <string>
#include <cstdint>

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

    if (len >= 1 && encoded[len - 1] == '=') {
        result.pop_back();
        if (len >= 2 && encoded[len - 2] == '=') {
            result.pop_back();
        }
    }

    return result;
}

ActivityManageHandler::ActivityManageHandler(IActivityDao* activity_dao,
                                             IActivitySignupDao* signup_dao,
                                             IActivityGroupDao* group_dao,
                                             SessionManager* session_mgr,
                                             GroupSessionManager* group_session_mgr,
                                             const std::string& upload_path)
    : activity_dao_(activity_dao)
    , signup_dao_(signup_dao)
    , group_dao_(group_dao)
    , session_mgr_(session_mgr)
    , group_session_mgr_(group_session_mgr)
    , upload_path_(upload_path) {}

ActivityManageHandler::~ActivityManageHandler() {}

void ActivityManageHandler::RegisterRoutes(crow::SimpleApp& app) {
    CROW_ROUTE(app, "/api/activity/create").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleCreateActivity(req);
    });

    CROW_ROUTE(app, "/api/activity/update").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleUpdateActivity(req);
    });

    CROW_ROUTE(app, "/api/activity/delete").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleDeleteActivity(req);
    });

    CROW_ROUTE(app, "/api/activity/list").methods("GET"_method)
    ([this](const crow::request& req) {
        return HandleListActivities(req);
    });

    CROW_ROUTE(app, "/api/activity/publish").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandlePublishActivity(req);
    });

    CROW_ROUTE(app, "/api/activity/sort").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleUpdateSort(req);
    });

    CROW_ROUTE(app, "/api/activity/signups").methods("GET"_method)
    ([this](const crow::request& req) {
        return HandleGetSignups(req);
    });

    CROW_ROUTE(app, "/api/activity/upload_image").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleUploadImage(req);
    });

    CROW_ROUTE(app, "/api/activity/delete_cover_image").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleDeleteCoverImage(req);
    });

    CROW_ROUTE(app, "/api/activity/upload_promotion_image").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleUploadPromotionImage(req);
    });

    CROW_ROUTE(app, "/api/activity/delete_promotion_image").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleDeletePromotionImage(req);
    });

    CROW_ROUTE(app, "/api/activity/sort_promotion_images").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleSortPromotionImages(req);
    });

    CROW_ROUTE(app, "/api/activity/update_promotion_text").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleUpdatePromotionText(req);
    });

    CROW_ROUTE(app, "/api/activity/get_promotion").methods("GET"_method)
    ([this](const crow::request& req) {
        return HandleGetPromotion(req);
    });

    CROW_ROUTE(app, "/api/activity/update_notice").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleUpdateActivityNotice(req);
    });

    CROW_ROUTE(app, "/api/activity/get_notice").methods("GET"_method)
    ([this](const crow::request& req) {
        return HandleGetActivityNotice(req);
    });

    CROW_ROUTE(app, "/api/activity/upload_about_us_card_image").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleUploadAboutUsCardImage(req);
    });

    CROW_ROUTE(app, "/api/activity/update_about_us_card").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleUpdateAboutUsCard(req);
    });

    CROW_ROUTE(app, "/api/activity/delete_about_us_card").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleDeleteAboutUsCard(req);
    });

    CROW_ROUTE(app, "/api/activity/sort_about_us_cards").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleSortAboutUsCards(req);
    });

    CROW_ROUTE(app, "/api/activity/get_about_us_cards").methods("GET"_method)
    ([this](const crow::request& req) {
        return HandleGetAboutUsCards(req);
    });
}

std::string ActivityManageHandler::GetSessionIdFromCookie(const crow::request& req) {
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

int ActivityManageHandler::CheckSession(const crow::request& req, SessionInfo& info) {
    if (!session_mgr_) {
        LOG_ERROR << "ActivityManageHandler: session_mgr is null";
        return ERR_HANDLER_NULL_DAO;
    }

    std::string session_id = GetSessionIdFromCookie(req);
    if (session_id.empty()) {
        LOG_ERROR << "ActivityManageHandler: no session_id in cookie";
        return ERR_AUTH_SESSION_EXPIRED;
    }

    int ret = session_mgr_->ValidateSession(session_id, info);
    if (ret != DB_OK) {
        LOG_ERROR << "ActivityManageHandler: session validation failed, ret=" << ret;
        return ERR_AUTH_SESSION_EXPIRED;
    }

    return DB_OK;
}

crow::response ActivityManageHandler::HandleCreateActivity(const crow::request& req) {
    LOG_INFO << "ActivityManageHandler: create activity";

    SessionInfo session_info;
    int ret = CheckSession(req, session_info);
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

    if (!activity_dao_) {
        LOG_ERROR << "ActivityManageHandler: activity_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "activity_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    crow::json::wvalue err_resp;
    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }
    if (!body) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }

    ActivityInfo info;
    if (body.has("title")) {
        info.title = std::string(body["title"].s());
    }
    if (body.has("description")) {
        info.description = std::string(body["description"].s());
    }
    if (body.has("cover_image")) {
        info.cover_image = std::string(body["cover_image"].s());
    }
    if (body.has("start_time")) {
        info.start_time = std::string(body["start_time"].s());
    }
    if (body.has("end_time")) {
        info.end_time = std::string(body["end_time"].s());
    }
    if (body.has("signup_deadline")) {
        info.signup_deadline = std::string(body["signup_deadline"].s());
    }
    if (body.has("capacity")) {
        info.capacity = body["capacity"].i();
    }
    if (body.has("min_group_size") && body["min_group_size"].i() > 0) {
        info.min_group_size = static_cast<int32_t>(body["min_group_size"].i());
    }
    if (body.has("group_type")) {
        info.group_type = static_cast<int32_t>(body["group_type"].i());
        if (info.group_type < 0 || info.group_type > 2) { info.group_type = 0; }
    }

    /* handler-level validation (defense in depth) */
    if (info.title.empty()) {
        err_resp["code"] = ERR_ACTIVITY_TITLE_EMPTY;
        err_resp["message"] = "title is required";
        return crow::response(400, crow::json::dump(err_resp));
    }
    if (!info.start_time.empty() && !info.end_time.empty() &&
        info.start_time >= info.end_time) {
        err_resp["code"] = ERR_ACTIVITY_TIME_INVALID;
        err_resp["message"] = "start_time must be before end_time";
        return crow::response(400, crow::json::dump(err_resp));
    }
    if (!info.signup_deadline.empty() && !info.end_time.empty() &&
        info.signup_deadline > info.end_time) {
        err_resp["code"] = ERR_ACTIVITY_DEADLINE_INVALID;
        err_resp["message"] = "signup_deadline must not be after end_time";
        return crow::response(400, crow::json::dump(err_resp));
    }
    if (info.capacity < 0) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "capacity must be >= 0";
        return crow::response(400, crow::json::dump(err_resp));
    }

    int64_t out_id = 0;
    ret = activity_dao_->CreateActivity(info, out_id);
    if (ret != DB_OK) {
        LOG_ERROR << "ActivityManageHandler: create activity failed, ret=" << ret;
        err_resp["code"] = ret;
        err_resp["message"] = "create activity failed";
        return crow::response(200, crow::json::dump(err_resp));
    }

    crow::json::wvalue data;
    data["id"] = static_cast<int>(out_id);

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    LOG_INFO << "ActivityManageHandler: create activity success, id=" << out_id;
    return crow::response(result);
}

crow::response ActivityManageHandler::HandleUpdateActivity(const crow::request& req) {
    LOG_INFO << "ActivityManageHandler: update activity";

    SessionInfo session_info;
    int ret = CheckSession(req, session_info);
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

    if (!activity_dao_) {
        LOG_ERROR << "ActivityManageHandler: activity_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "activity_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    crow::json::wvalue err_resp;
    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }
    if (!body) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (!body.has("id") || body["id"].i() <= 0) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "id is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    ActivityInfo info;
    info.id = static_cast<int64_t>(body["id"].i());
    if (body.has("title")) {
        info.title = std::string(body["title"].s());
    }
    if (body.has("description")) {
        info.description = std::string(body["description"].s());
    }
    if (body.has("start_time")) {
        info.start_time = std::string(body["start_time"].s());
    }
    if (body.has("end_time")) {
        info.end_time = std::string(body["end_time"].s());
    }
    if (body.has("signup_deadline")) {
        info.signup_deadline = std::string(body["signup_deadline"].s());
    }
    if (body.has("capacity")) {
        info.capacity = body["capacity"].i();
    }
    if (body.has("min_group_size") && body["min_group_size"].i() > 0) {
        info.min_group_size = static_cast<int32_t>(body["min_group_size"].i());
    }
    if (body.has("group_type")) {
        info.group_type = static_cast<int32_t>(body["group_type"].i());
        if (info.group_type < 0 || info.group_type > 2) { info.group_type = 0; }
    }

    /* handler-level validation (defense in depth) */
    if (info.title.empty()) {
        err_resp["code"] = ERR_ACTIVITY_TITLE_EMPTY;
        err_resp["message"] = "title is required";
        return crow::response(400, crow::json::dump(err_resp));
    }
    if (!info.start_time.empty() && !info.end_time.empty() &&
        info.start_time >= info.end_time) {
        err_resp["code"] = ERR_ACTIVITY_TIME_INVALID;
        err_resp["message"] = "start_time must be before end_time";
        return crow::response(400, crow::json::dump(err_resp));
    }
    if (!info.signup_deadline.empty() && !info.end_time.empty() &&
        info.signup_deadline > info.end_time) {
        err_resp["code"] = ERR_ACTIVITY_DEADLINE_INVALID;
        err_resp["message"] = "signup_deadline must not be after end_time";
        return crow::response(400, crow::json::dump(err_resp));
    }

    ret = activity_dao_->UpdateActivity(info);
    if (ret != DB_OK) {
        LOG_ERROR << "ActivityManageHandler: update activity failed, ret=" << ret;
        err_resp["code"] = ret;
        err_resp["message"] = "update activity failed";
        return crow::response(200, crow::json::dump(err_resp));
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";

    LOG_INFO << "ActivityManageHandler: update activity success, id=" << info.id;
    return crow::response(result);
}

crow::response ActivityManageHandler::HandleDeleteActivity(const crow::request& req) {
    LOG_INFO << "ActivityManageHandler: delete activity";

    SessionInfo session_info;
    int ret = CheckSession(req, session_info);
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

    if (!activity_dao_) {
        LOG_ERROR << "ActivityManageHandler: activity_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "activity_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    crow::json::wvalue err_resp;
    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }
    if (!body) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (!body.has("id") || body["id"].i() <= 0) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "id is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    int64_t id = static_cast<int64_t>(body["id"].i());

    /* get old image paths before deletion */
    ActivityInfo old_info;
    std::string old_cover;
    std::string old_group;
    std::vector<ActivityCoverImage> old_cover_images;
    if (activity_dao_->GetActivity(id, old_info) == DB_OK) {
        old_cover = old_info.cover_image;
        old_group = old_info.group_image;
    }
    activity_dao_->GetCoverImages(id, old_cover_images);

    ret = activity_dao_->DeleteActivity(id);
    if (ret != DB_OK) {
        LOG_ERROR << "ActivityManageHandler: delete activity failed, ret=" << ret;
        err_resp["code"] = ret;
        err_resp["message"] = "delete activity failed";
        return crow::response(200, crow::json::dump(err_resp));
    }

    /* cleanup session files for this activity */
    if (group_session_mgr_) {
        int cleanup_ret = group_session_mgr_->CleanupActivity(id);
        if (cleanup_ret != DB_OK) {
            LOG_WARNING << "ActivityManageHandler: cleanup session dir failed, activity_id=" << id;
        }
    }

    activity_dao_->DeleteCoverImagesByActivityId(id);

    /* delete physical files (best effort) */
    if (!old_cover.empty() && !upload_path_.empty()) {
        UploadUtil::DeleteUploadedFile(upload_path_, old_cover);
    }
    if (!old_group.empty() && !upload_path_.empty()) {
        UploadUtil::DeleteUploadedFile(upload_path_, old_group);
    }
    for (size_t i = 0; i < old_cover_images.size(); ++i) {
        if (!old_cover_images[i].image_path.empty() && !upload_path_.empty()) {
            UploadUtil::DeleteUploadedFile(upload_path_, old_cover_images[i].image_path);
        }
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";

    LOG_INFO << "ActivityManageHandler: delete activity success, id=" << id;
    return crow::response(result);
}

crow::response ActivityManageHandler::HandleListActivities(const crow::request& req) {
    LOG_INFO << "ActivityManageHandler: list activities";

    SessionInfo session_info;
    int ret = CheckSession(req, session_info);
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

    if (!activity_dao_) {
        LOG_ERROR << "ActivityManageHandler: activity_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "activity_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    std::vector<ActivityInfo> list;
    ret = activity_dao_->ListActivities(list);
    if (ret != DB_OK) {
        LOG_ERROR << "ActivityManageHandler: list activities failed, ret=" << ret;
        crow::json::wvalue err;
        err["code"] = ret;
        err["message"] = "query activities failed";
        return crow::response(500, crow::json::dump(err));
    }

    std::vector<crow::json::wvalue> items;
    for (size_t i = 0; i < list.size(); ++i) {
        crow::json::wvalue item;
        item["id"] = static_cast<int>(list[i].id);
        item["title"] = list[i].title;
        item["description"] = list[i].description;
        item["cover_image"] = list[i].cover_image;
        item["start_time"] = list[i].start_time;
        item["end_time"] = list[i].end_time;
        item["signup_deadline"] = list[i].signup_deadline;
        item["capacity"] = list[i].capacity;
        item["signup_count"] = list[i].signup_count;
        item["group_image"] = list[i].group_image;
        item["min_group_size"] = list[i].min_group_size;
        item["group_type"] = list[i].group_type;
        item["sort_order"] = list[i].sort_order;
        item["status"] = list[i].status;
        item["created_at"] = list[i].created_at;
        item["updated_at"] = list[i].updated_at;

        std::vector<ActivityCoverImage> cover_images;
        activity_dao_->GetCoverImages(list[i].id, cover_images);
        for (size_t j = 0; j < cover_images.size(); ++j) {
            crow::json::wvalue img;
            img["id"] = static_cast<int>(cover_images[j].id);
            img["path"] = cover_images[j].image_path;
            img["sort_order"] = cover_images[j].sort_order;
            item["cover_images"][j] = std::move(img);
        }

        items.push_back(std::move(item));
    }

    crow::json::wvalue data;
    for (size_t i = 0; i < items.size(); ++i) {
        data["activities"][i] = std::move(items[i]);
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    LOG_INFO << "ActivityManageHandler: list activities success, count=" << list.size();
    return crow::response(result);
}

crow::response ActivityManageHandler::HandlePublishActivity(const crow::request& req) {
    LOG_INFO << "ActivityManageHandler: publish/unpublish activity";

    SessionInfo session_info;
    int ret = CheckSession(req, session_info);
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

    if (!activity_dao_) {
        LOG_ERROR << "ActivityManageHandler: activity_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "activity_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    crow::json::wvalue err_resp;
    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }
    if (!body) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (!body.has("id") || body["id"].i() <= 0) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "id is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (!body.has("status")) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "status is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    int64_t id = static_cast<int64_t>(body["id"].i());
    int32_t status = body["status"].i();

    if (status != 0 && status != 1) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "status must be 0 or 1";
        return crow::response(400, crow::json::dump(err_resp));
    }

    ret = activity_dao_->UpdateActivityStatus(id, status);
    if (ret != DB_OK) {
        LOG_ERROR << "ActivityManageHandler: update status failed, ret=" << ret;
        err_resp["code"] = ret;
        err_resp["message"] = "update status failed";
        return crow::response(200, crow::json::dump(err_resp));
    }

    /* cleanup session files when unpublishing */
    if (status == 0 && group_session_mgr_) {
        int cleanup_ret = group_session_mgr_->CleanupActivity(id);
        if (cleanup_ret != DB_OK) {
            LOG_WARNING << "ActivityManageHandler: cleanup session dir failed, activity_id=" << id;
        }
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";

    LOG_INFO << "ActivityManageHandler: update status success, id=" << id << " status=" << status;
    return crow::response(result);
}

crow::response ActivityManageHandler::HandleUpdateSort(const crow::request& req) {
    LOG_INFO << "ActivityManageHandler: update sort order";

    SessionInfo session_info;
    int ret = CheckSession(req, session_info);
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

    if (!activity_dao_) {
        LOG_ERROR << "ActivityManageHandler: activity_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "activity_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    crow::json::wvalue err_resp;
    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }
    if (!body) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (!body.has("orders") || body["orders"].t() != crow::json::type::List) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "orders array is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    size_t count = body["orders"].size();
    std::vector<std::pair<int64_t, int32_t> > orders;
    orders.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        try {
            if (!body["orders"][i].has("id") || !body["orders"][i].has("sort_order")) {
                err_resp["code"] = ERR_INVALID_PARAM;
                err_resp["message"] = "each order must have id and sort_order";
                return crow::response(400, crow::json::dump(err_resp));
            }
            int64_t act_id = static_cast<int64_t>(body["orders"][i]["id"].i());
            int32_t sort = body["orders"][i]["sort_order"].i();
            if (act_id <= 0) {
                err_resp["code"] = ERR_INVALID_PARAM;
                err_resp["message"] = "order id must be > 0";
                return crow::response(400, crow::json::dump(err_resp));
            }
            orders.push_back(std::make_pair(act_id, sort));
        } catch (...) {
            err_resp["code"] = ERR_INVALID_PARAM;
            err_resp["message"] = "invalid order entry";
            return crow::response(400, crow::json::dump(err_resp));
        }
    }

    ret = activity_dao_->BatchUpdateSortOrder(orders);
    if (ret != DB_OK) {
        LOG_ERROR << "ActivityManageHandler: batch update sort failed, ret=" << ret;
        err_resp["code"] = ret;
        err_resp["message"] = "update sort order failed";
        return crow::response(200, crow::json::dump(err_resp));
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";

    LOG_INFO << "ActivityManageHandler: update sort order success, count=" << count;
    return crow::response(result);
}

crow::response ActivityManageHandler::HandleGetSignups(const crow::request& req) {
    LOG_INFO << "ActivityManageHandler: get signups";

    SessionInfo session_info;
    int ret = CheckSession(req, session_info);
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

    if (!signup_dao_) {
        LOG_ERROR << "ActivityManageHandler: signup_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "signup_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    char* id_str = const_cast<crow::request&>(req).url_params.get("activity_id");
    if (!id_str || id_str[0] == '\0') {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "activity_id is required";
        return crow::response(400, crow::json::dump(err));
    }

    int64_t activity_id = 0;
    try {
        activity_id = static_cast<int64_t>(std::stoi(id_str));
    } catch (...) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "invalid activity_id";
        return crow::response(400, crow::json::dump(err));
    }

    if (activity_id <= 0) {
        crow::json::wvalue err;
        err["code"] = ERR_INVALID_PARAM;
        err["message"] = "invalid activity_id";
        return crow::response(400, crow::json::dump(err));
    }

    std::vector<ActivitySignupInfo> list;
    ret = signup_dao_->ListSignupsByActivity(activity_id, list);
    if (ret != DB_OK) {
        LOG_ERROR << "ActivityManageHandler: list signups failed, ret=" << ret;
        crow::json::wvalue err;
        err["code"] = ret;
        err["message"] = "query signups failed";
        return crow::response(500, crow::json::dump(err));
    }

    /* query group members for this activity */
    std::vector<ActivityGroupMemberInfo> group_members;
    if (group_dao_) {
        group_dao_->ListMembersByActivity(activity_id, group_members);
    }

    /* query pending type=1 group sessions from GroupSessionManager */
    std::vector<GroupSessionInfo> pending_sessions;
    if (group_session_mgr_) {
        group_session_mgr_->ListAllSessions(activity_id, pending_sessions);
    }

    std::vector<crow::json::wvalue> items;
    for (size_t i = 0; i < list.size(); ++i) {
        crow::json::wvalue item;
        item["id"] = static_cast<int>(list[i].id);
        item["name"] = list[i].name;
        item["phone"] = list[i].phone;
        item["grade"] = list[i].grade;
        item["signup_type"] = list[i].signup_type;
        item["created_at"] = list[i].created_at;

        /* match group info by name+phone */
        std::string matched_code;
        int32_t matched_leader = 0;

        /* first check confirmed groups in database */
        for (size_t j = 0; j < group_members.size(); ++j) {
            if (group_members[j].name == list[i].name &&
                group_members[j].phone == list[i].phone) {
                matched_code = group_members[j].invite_code;
                matched_leader = group_members[j].is_leader;
                break;
            }
        }

        /* if not found in confirmed groups, check pending sessions */
        if (matched_code.empty()) {
            for (size_t j = 0; j < pending_sessions.size(); ++j) {
                for (size_t k = 0; k < pending_sessions[j].members.size(); ++k) {
                    if (pending_sessions[j].members[k].name == list[i].name &&
                        pending_sessions[j].members[k].phone == list[i].phone) {
                        matched_code = pending_sessions[j].invite_code;
                        matched_leader = pending_sessions[j].members[k].is_leader;
                        break;
                    }
                }
                if (!matched_code.empty()) break;
            }
        }

        item["group_invite_code"] = matched_code;
        item["is_leader"] = matched_leader;

        items.push_back(std::move(item));
    }

    crow::json::wvalue data;
    for (size_t i = 0; i < items.size(); ++i) {
        data["signups"][i] = std::move(items[i]);
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    LOG_INFO << "ActivityManageHandler: get signups success, activity_id=" << activity_id
             << " count=" << list.size();
    return crow::response(result);
}

crow::response ActivityManageHandler::HandleUploadImage(const crow::request& req) {
    LOG_INFO << "ActivityManageHandler: upload image";

    SessionInfo session_info;
    int ret = CheckSession(req, session_info);
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

    if (!activity_dao_) {
        LOG_ERROR << "ActivityManageHandler: activity_dao is null";
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "activity_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    crow::json::wvalue err_resp;
    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }
    if (!body) {
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

    if (!body.has("activity_id") || body["activity_id"].i() <= 0) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "activity_id is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (!body.has("type")) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "type is required (cover or group)";
        return crow::response(400, crow::json::dump(err_resp));
    }

    std::string filename = body["filename"].s();
    std::string base64_data = body["data"].s();
    int64_t activity_id = static_cast<int64_t>(body["activity_id"].i());
    std::string type = std::string(body["type"].s());

    /* validate type */
    std::string field;
    if (type == "cover") {
        field = "cover_image";
    } else if (type == "group") {
        field = "group_image";
    } else {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "type must be 'cover' or 'group'";
        return crow::response(400, crow::json::dump(err_resp));
    }

    /* validate file format */
    ret = UploadUtil::ValidateFormat(filename);
    if (ret != DB_OK) {
        LOG_ERROR << "ActivityManageHandler: invalid file format, filename=" << filename;
        err_resp["code"] = ERR_UPLOAD_FORMAT_INVALID;
        err_resp["message"] = "invalid file format";
        return crow::response(400, crow::json::dump(err_resp));
    }

    /* base64 decode */
    std::vector<uint8_t> file_data = Base64Decode(base64_data);
    if (file_data.empty()) {
        LOG_ERROR << "ActivityManageHandler: base64 decode failed";
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "base64 decode failed";
        return crow::response(400, crow::json::dump(err_resp));
    }

    /* validate file size */
    ret = UploadUtil::ValidateSize(file_data.size());
    if (ret != DB_OK) {
        LOG_ERROR << "ActivityManageHandler: file size exceeded, size=" << file_data.size();
        err_resp["code"] = ERR_UPLOAD_SIZE_EXCEEDED;
        err_resp["message"] = "file size exceeded";
        return crow::response(400, crow::json::dump(err_resp));
    }

    /* check upload path */
    if (upload_path_.empty()) {
        LOG_ERROR << "ActivityManageHandler: upload path not configured";
        err_resp["code"] = ERR_UPLOAD_PATH_NOT_CONFIGURED;
        err_resp["message"] = "upload path not configured";
        return crow::response(500, crow::json::dump(err_resp));
    }

    /* get old image path and delete physical file (group type only) */
    if (type == "group") {
        ActivityInfo old_info;
        if (activity_dao_->GetActivity(activity_id, old_info) == DB_OK) {
            if (!old_info.group_image.empty()) {
                UploadUtil::DeleteUploadedFile(upload_path_, old_info.group_image);
            }
        }
    }

    /* save file */
    std::string saved_path;
    ret = UploadUtil::SaveFile(upload_path_, filename,
                               reinterpret_cast<const char*>(file_data.data()),
                               file_data.size(), saved_path);
    if (ret != DB_OK) {
        LOG_ERROR << "ActivityManageHandler: save file failed, ret=" << ret;
        err_resp["code"] = ret;
        err_resp["message"] = "save file failed";
        return crow::response(500, crow::json::dump(err_resp));
    }

    /* update database */
    int64_t image_id = 0;
    if (type == "cover") {
        std::vector<ActivityCoverImage> existing;
        activity_dao_->GetCoverImages(activity_id, existing);
        int next_order = static_cast<int>(existing.size());

        ret = activity_dao_->AddCoverImage(activity_id, saved_path, next_order, image_id);
        if (ret != DB_OK) {
            LOG_ERROR << "ActivityManageHandler: add cover image failed, ret=" << ret;
            err_resp["code"] = ret;
            err_resp["message"] = "add cover image failed";
            return crow::response(500, crow::json::dump(err_resp));
        }

        if (existing.empty()) {
            activity_dao_->UpdateActivityImage(activity_id, "cover_image", saved_path);
        }
    } else {
        ret = activity_dao_->UpdateActivityImage(activity_id, field, saved_path);
        if (ret != DB_OK) {
            LOG_ERROR << "ActivityManageHandler: update activity image failed, ret=" << ret;
            err_resp["code"] = ret;
            err_resp["message"] = "update image path failed";
            return crow::response(500, crow::json::dump(err_resp));
        }
    }

    crow::json::wvalue data;
    data["path"] = saved_path;
    if (type == "cover") {
        data["image_id"] = static_cast<int>(image_id);
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    LOG_INFO << "ActivityManageHandler: upload image success, activity_id=" << activity_id
             << " type=" << type;
    return crow::response(result);
}

crow::response ActivityManageHandler::HandleDeleteCoverImage(const crow::request& req) {
    LOG_INFO << "ActivityManageHandler: delete cover image";

    SessionInfo session_info;
    int ret = CheckSession(req, session_info);
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

    if (!activity_dao_) {
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "activity_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    crow::json::wvalue err_resp;
    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }
    if (!body) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (!body.has("image_id") || body["image_id"].i() <= 0) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "image_id is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (!body.has("activity_id") || body["activity_id"].i() <= 0) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "activity_id is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    int64_t image_id = static_cast<int64_t>(body["image_id"].i());
    int64_t activity_id = static_cast<int64_t>(body["activity_id"].i());

    std::vector<ActivityCoverImage> activity_images;
    activity_dao_->GetCoverImages(activity_id, activity_images);

    ActivityCoverImage target;
    bool found = false;
    bool is_first = false;
    for (size_t i = 0; i < activity_images.size(); ++i) {
        if (activity_images[i].id == image_id) {
            target = activity_images[i];
            found = true;
            if (i == 0) { is_first = true; }
            break;
        }
    }

    if (!found) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "image not found";
        return crow::response(404, crow::json::dump(err_resp));
    }

    if (!target.image_path.empty()) {
        UploadUtil::DeleteUploadedFile(upload_path_, target.image_path);
    }

    ret = activity_dao_->DeleteCoverImage(image_id);
    if (ret != DB_OK) {
        LOG_ERROR << "ActivityManageHandler: delete cover image failed, ret=" << ret;
        err_resp["code"] = ret;
        err_resp["message"] = "delete cover image failed";
        return crow::response(500, crow::json::dump(err_resp));
    }

    if (is_first) {
        std::vector<ActivityCoverImage> remaining;
        activity_dao_->GetCoverImages(activity_id, remaining);
        if (!remaining.empty()) {
            activity_dao_->UpdateActivityImage(activity_id, "cover_image", remaining[0].image_path);
        } else {
            activity_dao_->UpdateActivityImage(activity_id, "cover_image", "");
        }
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";

    LOG_INFO << "ActivityManageHandler: delete cover image success, image_id=" << image_id;
    return crow::response(result);
}

crow::response ActivityManageHandler::HandleUploadPromotionImage(const crow::request& req) {
    LOG_INFO << "ActivityManageHandler: upload promotion image";

    SessionInfo session_info;
    int ret = CheckSession(req, session_info);
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

    if (!activity_dao_) {
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "activity_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    crow::json::wvalue err_resp;
    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }
    if (!body) {
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

    ret = UploadUtil::ValidateFormat(filename);
    if (ret != DB_OK) {
        err_resp["code"] = ERR_UPLOAD_FORMAT_INVALID;
        err_resp["message"] = "invalid file format";
        return crow::response(400, crow::json::dump(err_resp));
    }

    std::vector<uint8_t> file_data = Base64Decode(base64_data);
    if (file_data.empty()) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "base64 decode failed";
        return crow::response(400, crow::json::dump(err_resp));
    }

    ret = UploadUtil::ValidateSize(file_data.size());
    if (ret != DB_OK) {
        err_resp["code"] = ERR_UPLOAD_SIZE_EXCEEDED;
        err_resp["message"] = "file size exceeded";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (upload_path_.empty()) {
        err_resp["code"] = ERR_UPLOAD_PATH_NOT_CONFIGURED;
        err_resp["message"] = "upload path not configured";
        return crow::response(500, crow::json::dump(err_resp));
    }

    std::string saved_path;
    ret = UploadUtil::SaveFile(upload_path_, filename,
                               reinterpret_cast<const char*>(file_data.data()),
                               file_data.size(), saved_path);
    if (ret != DB_OK) {
        err_resp["code"] = ret;
        err_resp["message"] = "save file failed";
        return crow::response(500, crow::json::dump(err_resp));
    }

    std::vector<ActivityCoverImage> existing;
    activity_dao_->GetPromotionImages(existing);
    int next_order = static_cast<int>(existing.size());

    int64_t image_id = 0;
    ret = activity_dao_->AddPromotionImage(saved_path, next_order, image_id);
    if (ret != DB_OK) {
        err_resp["code"] = ret;
        err_resp["message"] = "add promotion image failed";
        return crow::response(500, crow::json::dump(err_resp));
    }

    crow::json::wvalue data;
    data["path"] = saved_path;
    data["image_id"] = static_cast<int>(image_id);

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    LOG_INFO << "ActivityManageHandler: upload promotion image success, id=" << image_id;
    return crow::response(result);
}

crow::response ActivityManageHandler::HandleDeletePromotionImage(const crow::request& req) {
    LOG_INFO << "ActivityManageHandler: delete promotion image";

    SessionInfo session_info;
    int ret = CheckSession(req, session_info);
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

    if (!activity_dao_) {
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "activity_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    crow::json::wvalue err_resp;
    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }
    if (!body) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (!body.has("image_id") || body["image_id"].i() <= 0) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "image_id is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    int64_t image_id = static_cast<int64_t>(body["image_id"].i());

    std::vector<ActivityCoverImage> images;
    activity_dao_->GetPromotionImages(images);

    std::string image_path;
    for (size_t i = 0; i < images.size(); ++i) {
        if (images[i].id == image_id) {
            image_path = images[i].image_path;
            break;
        }
    }

    if (!image_path.empty()) {
        UploadUtil::DeleteUploadedFile(upload_path_, image_path);
    }

    ret = activity_dao_->DeletePromotionImage(image_id);
    if (ret != DB_OK) {
        err_resp["code"] = ret;
        err_resp["message"] = "delete promotion image failed";
        return crow::response(500, crow::json::dump(err_resp));
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";

    LOG_INFO << "ActivityManageHandler: delete promotion image success, id=" << image_id;
    return crow::response(result);
}

crow::response ActivityManageHandler::HandleSortPromotionImages(const crow::request& req) {
    LOG_INFO << "ActivityManageHandler: sort promotion images";

    SessionInfo session_info;
    int ret = CheckSession(req, session_info);
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

    if (!activity_dao_) {
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "activity_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    crow::json::wvalue err_resp;
    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }
    if (!body) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (!body.has("orders") || body["orders"].t() != crow::json::type::List) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "orders array is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    size_t count = body["orders"].size();
    std::vector<std::pair<int64_t, int32_t> > orders;
    orders.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        try {
            if (!body["orders"][i].has("id") || !body["orders"][i].has("sort_order")) {
                err_resp["code"] = ERR_INVALID_PARAM;
                err_resp["message"] = "each order must have id and sort_order";
                return crow::response(400, crow::json::dump(err_resp));
            }
            int64_t img_id = static_cast<int64_t>(body["orders"][i]["id"].i());
            int32_t sort = body["orders"][i]["sort_order"].i();
            if (img_id <= 0) {
                err_resp["code"] = ERR_INVALID_PARAM;
                err_resp["message"] = "order id must be > 0";
                return crow::response(400, crow::json::dump(err_resp));
            }
            orders.push_back(std::make_pair(img_id, sort));
        } catch (...) {
            err_resp["code"] = ERR_INVALID_PARAM;
            err_resp["message"] = "invalid order entry";
            return crow::response(400, crow::json::dump(err_resp));
        }
    }

    ret = activity_dao_->BatchUpdatePromotionImageSortOrder(orders);
    if (ret != DB_OK) {
        err_resp["code"] = ret;
        err_resp["message"] = "update sort order failed";
        return crow::response(200, crow::json::dump(err_resp));
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";

    LOG_INFO << "ActivityManageHandler: sort promotion images success, count=" << count;
    return crow::response(result);
}

crow::response ActivityManageHandler::HandleUpdatePromotionText(const crow::request& req) {
    LOG_INFO << "ActivityManageHandler: update promotion text";

    SessionInfo session_info;
    int ret = CheckSession(req, session_info);
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

    if (!activity_dao_) {
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "activity_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    crow::json::wvalue err_resp;
    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }
    if (!body) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }

    std::string content;
    if (body.has("content")) {
        content = std::string(body["content"].s());
    }

    ret = activity_dao_->UpdatePromotionText(content);
    if (ret != DB_OK) {
        err_resp["code"] = ret;
        err_resp["message"] = "update promotion text failed";
        return crow::response(500, crow::json::dump(err_resp));
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";

    LOG_INFO << "ActivityManageHandler: update promotion text success";
    return crow::response(result);
}

crow::response ActivityManageHandler::HandleGetPromotion(const crow::request& req) {
    LOG_INFO << "ActivityManageHandler: get promotion";

    SessionInfo session_info;
    int ret = CheckSession(req, session_info);
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

    if (!activity_dao_) {
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "activity_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    std::vector<ActivityCoverImage> images;
    activity_dao_->GetPromotionImages(images);

    std::string text;
    activity_dao_->GetPromotionText(text);

    crow::json::wvalue data;
    for (size_t i = 0; i < images.size(); ++i) {
        crow::json::wvalue item;
        item["id"] = static_cast<int>(images[i].id);
        item["path"] = images[i].image_path;
        item["sort_order"] = images[i].sort_order;
        data["images"][i] = std::move(item);
    }
    data["text"] = text;

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    return crow::response(result);
}

crow::response ActivityManageHandler::HandleUpdateActivityNotice(const crow::request& req) {
    LOG_INFO << "ActivityManageHandler: update activity notice";

    SessionInfo session_info;
    int ret = CheckSession(req, session_info);
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

    if (!activity_dao_) {
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "activity_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    crow::json::wvalue err_resp;
    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }
    if (!body) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }

    std::string content;
    if (body.has("content")) {
        content = std::string(body["content"].s());
    }

    ret = activity_dao_->UpdateActivityNotice(content);
    if (ret != DB_OK) {
        err_resp["code"] = ret;
        err_resp["message"] = "update activity notice failed";
        return crow::response(500, crow::json::dump(err_resp));
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";

    LOG_INFO << "ActivityManageHandler: update activity notice success";
    return crow::response(result);
}

crow::response ActivityManageHandler::HandleGetActivityNotice(const crow::request& req) {
    LOG_INFO << "ActivityManageHandler: get activity notice";

    SessionInfo session_info;
    int ret = CheckSession(req, session_info);
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

    if (!activity_dao_) {
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "activity_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    std::string content;
    ret = activity_dao_->GetActivityNotice(content);
    if (ret != DB_OK) {
        crow::json::wvalue err;
        err["code"] = ret;
        err["message"] = "get activity notice failed";
        return crow::response(500, crow::json::dump(err));
    }

    crow::json::wvalue data;
    data["content"] = content;

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    return crow::response(result);
}

crow::response ActivityManageHandler::HandleUploadAboutUsCardImage(const crow::request& req) {
    LOG_INFO << "ActivityManageHandler: upload about us card image";

    SessionInfo session_info;
    int ret = CheckSession(req, session_info);
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

    if (!activity_dao_) {
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "activity_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    crow::json::wvalue err_resp;
    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }
    if (!body) {
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
    int32_t layout_type = 1;
    if (body.has("layout_type")) { layout_type = body["layout_type"].i(); }
    if (layout_type < 1 || layout_type > 9) { layout_type = 1; }
    std::string text_content = "";
    if (body.has("text")) { text_content = body["text"].s(); }

    ret = UploadUtil::ValidateFormat(filename);
    if (ret != DB_OK) {
        err_resp["code"] = ERR_UPLOAD_FORMAT_INVALID;
        err_resp["message"] = "invalid file format";
        return crow::response(400, crow::json::dump(err_resp));
    }

    std::vector<uint8_t> file_data = Base64Decode(base64_data);
    if (file_data.empty()) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "base64 decode failed";
        return crow::response(400, crow::json::dump(err_resp));
    }

    ret = UploadUtil::ValidateSize(file_data.size());
    if (ret != DB_OK) {
        err_resp["code"] = ERR_UPLOAD_SIZE_EXCEEDED;
        err_resp["message"] = "file size exceeded";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (upload_path_.empty()) {
        err_resp["code"] = ERR_UPLOAD_PATH_NOT_CONFIGURED;
        err_resp["message"] = "upload path not configured";
        return crow::response(500, crow::json::dump(err_resp));
    }

    std::string saved_path;
    ret = UploadUtil::SaveFile(upload_path_, filename,
                               reinterpret_cast<const char*>(file_data.data()),
                               file_data.size(), saved_path);
    if (ret != DB_OK) {
        err_resp["code"] = ret;
        err_resp["message"] = "save file failed";
        return crow::response(500, crow::json::dump(err_resp));
    }

    std::vector<AboutUsCard> existing;
    activity_dao_->GetAboutUsCards(existing);
    int next_order = static_cast<int>(existing.size());

    int64_t card_id = 0;
    ret = activity_dao_->AddAboutUsCard(saved_path, text_content, layout_type, next_order, card_id);
    if (ret != DB_OK) {
        err_resp["code"] = ret;
        err_resp["message"] = "add about us card failed";
        return crow::response(500, crow::json::dump(err_resp));
    }

    crow::json::wvalue data;
    data["card_id"] = static_cast<int>(card_id);
    data["path"] = saved_path;

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    LOG_INFO << "ActivityManageHandler: upload about us card image success, id=" << card_id;
    return crow::response(result);
}

crow::response ActivityManageHandler::HandleUpdateAboutUsCard(const crow::request& req) {
    LOG_INFO << "ActivityManageHandler: update about us card";

    SessionInfo session_info;
    int ret = CheckSession(req, session_info);
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

    if (!activity_dao_) {
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "activity_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    crow::json::wvalue err_resp;
    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }
    if (!body) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (!body.has("card_id") || body["card_id"].i() <= 0) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "card_id is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    int64_t card_id = static_cast<int64_t>(body["card_id"].i());
    std::string text;
    if (body.has("text")) { text = std::string(body["text"].s()); }
    int32_t layout_type = 1;
    if (body.has("layout_type")) { layout_type = body["layout_type"].i(); }
    if (layout_type < 1 || layout_type > 9) { layout_type = 1; }

    std::string image_path;
    if (body.has("image_path")) { image_path = std::string(body["image_path"].s()); }

    if (body.has("filename") && !std::string(body["filename"].s()).empty() &&
        body.has("data") && !std::string(body["data"].s()).empty()) {
        std::string filename = body["filename"].s();
        std::string base64_data = body["data"].s();

        ret = UploadUtil::ValidateFormat(filename);
        if (ret != DB_OK) {
            err_resp["code"] = ERR_UPLOAD_FORMAT_INVALID;
            err_resp["message"] = "invalid file format";
            return crow::response(400, crow::json::dump(err_resp));
        }

        std::vector<uint8_t> file_data = Base64Decode(base64_data);
        if (file_data.empty()) {
            err_resp["code"] = ERR_INVALID_PARAM;
            err_resp["message"] = "base64 decode failed";
            return crow::response(400, crow::json::dump(err_resp));
        }

        ret = UploadUtil::ValidateSize(file_data.size());
        if (ret != DB_OK) {
            err_resp["code"] = ERR_UPLOAD_SIZE_EXCEEDED;
            err_resp["message"] = "file size exceeded";
            return crow::response(400, crow::json::dump(err_resp));
        }

        if (upload_path_.empty()) {
            err_resp["code"] = ERR_UPLOAD_PATH_NOT_CONFIGURED;
            err_resp["message"] = "upload path not configured";
            return crow::response(500, crow::json::dump(err_resp));
        }

        std::string saved_path;
        ret = UploadUtil::SaveFile(upload_path_, filename,
                                   reinterpret_cast<const char*>(file_data.data()),
                                   file_data.size(), saved_path);
        if (ret != DB_OK) {
            err_resp["code"] = ret;
            err_resp["message"] = "save file failed";
            return crow::response(500, crow::json::dump(err_resp));
        }

        std::vector<AboutUsCard> cards;
        activity_dao_->GetAboutUsCards(cards);
        for (size_t i = 0; i < cards.size(); ++i) {
            if (cards[i].id == card_id && !cards[i].image_path.empty()) {
                UploadUtil::DeleteUploadedFile(upload_path_, cards[i].image_path);
                break;
            }
        }
        image_path = saved_path;
    }

    if (image_path.empty()) {
        std::vector<AboutUsCard> cards;
        activity_dao_->GetAboutUsCards(cards);
        for (size_t i = 0; i < cards.size(); ++i) {
            if (cards[i].id == card_id) {
                image_path = cards[i].image_path;
                break;
            }
        }
    }

    ret = activity_dao_->UpdateAboutUsCard(card_id, image_path, text, layout_type);
    if (ret != DB_OK) {
        err_resp["code"] = ret;
        err_resp["message"] = "update about us card failed";
        return crow::response(500, crow::json::dump(err_resp));
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";

    LOG_INFO << "ActivityManageHandler: update about us card success, id=" << card_id;
    return crow::response(result);
}

crow::response ActivityManageHandler::HandleDeleteAboutUsCard(const crow::request& req) {
    LOG_INFO << "ActivityManageHandler: delete about us card";

    SessionInfo session_info;
    int ret = CheckSession(req, session_info);
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

    if (!activity_dao_) {
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "activity_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    crow::json::wvalue err_resp;
    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }
    if (!body) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (!body.has("card_id") || body["card_id"].i() <= 0) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "card_id is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    int64_t card_id = static_cast<int64_t>(body["card_id"].i());

    std::vector<AboutUsCard> cards;
    activity_dao_->GetAboutUsCards(cards);

    std::string image_path;
    for (size_t i = 0; i < cards.size(); ++i) {
        if (cards[i].id == card_id) {
            image_path = cards[i].image_path;
            break;
        }
    }

    if (!image_path.empty()) {
        UploadUtil::DeleteUploadedFile(upload_path_, image_path);
    }

    ret = activity_dao_->DeleteAboutUsCard(card_id);
    if (ret != DB_OK) {
        err_resp["code"] = ret;
        err_resp["message"] = "delete about us card failed";
        return crow::response(500, crow::json::dump(err_resp));
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";

    LOG_INFO << "ActivityManageHandler: delete about us card success, id=" << card_id;
    return crow::response(result);
}

crow::response ActivityManageHandler::HandleSortAboutUsCards(const crow::request& req) {
    LOG_INFO << "ActivityManageHandler: sort about us cards";

    SessionInfo session_info;
    int ret = CheckSession(req, session_info);
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

    if (!activity_dao_) {
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "activity_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    crow::json::wvalue err_resp;
    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }
    if (!body) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (!body.has("orders") || body["orders"].t() != crow::json::type::List) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "orders array is required";
        return crow::response(400, crow::json::dump(err_resp));
    }

    size_t count = body["orders"].size();
    std::vector<std::pair<int64_t, int32_t> > orders;
    orders.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        try {
            if (!body["orders"][i].has("id") || !body["orders"][i].has("sort_order")) {
                err_resp["code"] = ERR_INVALID_PARAM;
                err_resp["message"] = "each order must have id and sort_order";
                return crow::response(400, crow::json::dump(err_resp));
            }
            int64_t cid = static_cast<int64_t>(body["orders"][i]["id"].i());
            int32_t sort = body["orders"][i]["sort_order"].i();
            if (cid <= 0) {
                err_resp["code"] = ERR_INVALID_PARAM;
                err_resp["message"] = "order id must be > 0";
                return crow::response(400, crow::json::dump(err_resp));
            }
            orders.push_back(std::make_pair(cid, sort));
        } catch (...) {
            err_resp["code"] = ERR_INVALID_PARAM;
            err_resp["message"] = "invalid order entry";
            return crow::response(400, crow::json::dump(err_resp));
        }
    }

    ret = activity_dao_->BatchUpdateAboutUsCardSortOrder(orders);
    if (ret != DB_OK) {
        err_resp["code"] = ret;
        err_resp["message"] = "update sort order failed";
        return crow::response(200, crow::json::dump(err_resp));
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";

    LOG_INFO << "ActivityManageHandler: sort about us cards success, count=" << count;
    return crow::response(result);
}

crow::response ActivityManageHandler::HandleGetAboutUsCards(const crow::request& req) {
    LOG_INFO << "ActivityManageHandler: get about us cards";

    SessionInfo session_info;
    int ret = CheckSession(req, session_info);
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

    if (!activity_dao_) {
        crow::json::wvalue err;
        err["code"] = ERR_HANDLER_NULL_DAO;
        err["message"] = "activity_dao is null";
        return crow::response(500, crow::json::dump(err));
    }

    std::vector<AboutUsCard> cards;
    ret = activity_dao_->GetAboutUsCards(cards);
    if (ret != DB_OK) {
        crow::json::wvalue err;
        err["code"] = ret;
        err["message"] = "get about us cards failed";
        return crow::response(500, crow::json::dump(err));
    }

    crow::json::wvalue data;
    for (size_t i = 0; i < cards.size(); ++i) {
        crow::json::wvalue item;
        item["id"] = static_cast<int>(cards[i].id);
        item["image_path"] = cards[i].image_path;
        item["text_content"] = cards[i].text_content;
        item["layout_type"] = cards[i].layout_type;
        item["sort_order"] = cards[i].sort_order;
        data[i] = std::move(item);
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    return crow::response(result);
}
