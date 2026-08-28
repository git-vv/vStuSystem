#include "activity_handler.h"
#include "error_codes.h"
#include "utils.h"
#include "group_session_manager.h"

#include <string>
#include <vector>
#include <algorithm>
#include <ctime>
#include <cstring>

ActivityHandler::ActivityHandler(IActivityDao* activity_dao,
                                 IActivitySignupDao* signup_dao,
                                 IActivityGroupDao* group_dao,
                                 GroupSessionManager* session_mgr)
    : activity_dao_(activity_dao)
    , signup_dao_(signup_dao)
    , group_dao_(group_dao)
    , session_mgr_(session_mgr) {}

ActivityHandler::~ActivityHandler() {}

void ActivityHandler::RegisterRoutes(crow::SimpleApp& app) {
    CROW_ROUTE(app, "/api/public/activity/list").methods("GET"_method)
    ([this](const crow::request& req) {
        return HandleListActivities(req);
    });

    CROW_ROUTE(app, "/api/public/activity/signup").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleSignup(req);
    });

    CROW_ROUTE(app, "/api/public/activity/promotion").methods("GET"_method)
    ([this]() {
        return HandleGetPromotion();
    });

    CROW_ROUTE(app, "/api/public/activity/notice").methods("GET"_method)
    ([this]() {
        return HandleGetNotice();
    });

    CROW_ROUTE(app, "/api/public/activity/about_us").methods("GET"_method)
    ([this]() {
        return HandleGetAboutUs();
    });

    CROW_ROUTE(app, "/api/public/activity/group_signup").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleGroupSignup(req);
    });

    CROW_ROUTE(app, "/api/public/activity/group_status").methods("GET"_method)
    ([this](const crow::request& req) {
        return HandleGetGroupStatus(req);
    });

    CROW_ROUTE(app, "/api/public/activity/confirm_group").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleConfirmGroup(req);
    });

    CROW_ROUTE(app, "/api/public/activity/leave_group").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleLeaveGroup(req);
    });

    CROW_ROUTE(app, "/api/public/activity/cancel_group").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleCancelGroup(req);
    });

    CROW_ROUTE(app, "/api/public/activity/batch_group_signup").methods("POST"_method)
    ([this](const crow::request& req) {
        return HandleBatchGroupSignup(req);
    });
}

namespace {

crow::response MakeJsonError(int code, const std::string& message) {
    crow::json::wvalue err;
    err["code"] = code;
    err["message"] = message;
    auto resp = crow::response(200, crow::json::dump(err));
    resp.add_header("Content-Type", "application/json");
    return resp;
}

std::string MaskPhone(const std::string& phone) {
    if (phone.size() < 7) { return phone; }
    return phone.substr(0, 3) + "****" + phone.substr(phone.size() - 4);
}

time_t ParseTimeToEpoch(const std::string& ts) {
    if (ts.size() < 19) { return 0; }
    std::tm tm_val;
    std::memset(&tm_val, 0, sizeof(tm_val));
    tm_val.tm_year = std::atoi(ts.substr(0, 4).c_str()) - 1900;
    tm_val.tm_mon  = std::atoi(ts.substr(5, 2).c_str()) - 1;
    tm_val.tm_mday = std::atoi(ts.substr(8, 2).c_str());
    tm_val.tm_hour = std::atoi(ts.substr(11, 2).c_str());
    tm_val.tm_min  = std::atoi(ts.substr(14, 2).c_str());
    tm_val.tm_sec  = std::atoi(ts.substr(17, 2).c_str());
    return std::mktime(&tm_val);
}

bool IsGroupTimedOut(const std::string& created_at) {
    time_t created = ParseTimeToEpoch(created_at);
    if (created == 0) { return false; }
    time_t now = std::time(nullptr);
    double diff = std::difftime(now, created);
    return diff > 1800.0;
}

int64_t RemainingSeconds(const std::string& created_at) {
    time_t created = ParseTimeToEpoch(created_at);
    if (created == 0) { return 0; }
    time_t now = std::time(nullptr);
    double diff = std::difftime(now, created);
    int64_t remaining = 1800 - static_cast<int64_t>(diff);
    return remaining > 0 ? remaining : 0;
}

} /* anonymous namespace */

crow::response ActivityHandler::HandleListActivities(const crow::request& req) {
    LOG_INFO << "ActivityHandler: list published activities";

    if (!activity_dao_) {
        LOG_ERROR << "ActivityHandler: activity_dao is null";
        return MakeJsonError(ERR_HANDLER_NULL_DAO, "activity_dao is null");
    }

    int page = 1;
    int limit = 10;
    const char* page_param = req.url_params.get("page");
    const char* limit_param = req.url_params.get("limit");
    if (page_param) { page = std::atoi(page_param); if (page < 1) { page = 1; } }
    if (limit_param) { limit = std::atoi(limit_param); if (limit < 1) { limit = 10; } if (limit > 50) { limit = 50; } }
    int offset = (page - 1) * limit;

    std::vector<ActivityInfo> list;
    int total_count = 0;
    int ret = activity_dao_->ListPublishedActivitiesPaged(list, limit, offset, total_count);
    if (ret != DB_OK) {
        LOG_ERROR << "ActivityHandler: list published activities failed, ret=" << ret;
        return MakeJsonError(ret, "query activities failed");
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
    data["total_count"] = total_count;
    data["page"] = page;
    data["limit"] = limit;
    data["has_more"] = (offset + static_cast<int>(list.size())) < total_count;

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    LOG_INFO << "ActivityHandler: list published activities success, count=" << list.size()
             << ", total=" << total_count << ", page=" << page;
    return crow::response(result);
}

crow::response ActivityHandler::HandleSignup(const crow::request& req) {
    LOG_INFO << "ActivityHandler: signup request";

    if (!activity_dao_) {
        LOG_ERROR << "ActivityHandler: activity_dao is null";
        return MakeJsonError(ERR_HANDLER_NULL_DAO, "activity_dao is null");
    }

    if (!signup_dao_) {
        LOG_ERROR << "ActivityHandler: signup_dao is null";
        return MakeJsonError(ERR_HANDLER_NULL_DAO, "signup_dao is null");
    }

    /* parse JSON body */
    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        return MakeJsonError(ERR_INVALID_PARAM, "invalid json body");
    }
    if (!body) {
        return MakeJsonError(ERR_INVALID_PARAM, "invalid json body");
    }

    /* validate activity_id */
    if (!body.has("activity_id") || body["activity_id"].i() <= 0) {
        return MakeJsonError(ERR_INVALID_PARAM, "activity_id is required");
    }

    /* validate name */
    std::string name;
    if (body.has("name")) {
        name = std::string(body["name"].s());
    }
    if (name.empty()) {
        return MakeJsonError(ERR_ACTIVITY_NAME_EMPTY, "name is required");
    }
    if (name.size() > 50) {
        return MakeJsonError(ERR_ACTIVITY_NAME_EMPTY, "name too long (max 50 chars)");
    }

    /* validate phone: must be 11 digits starting with 1 */
    std::string phone;
    if (body.has("phone")) {
        phone = std::string(body["phone"].s());
    }
    if (phone.size() != 11 || phone[0] != '1') {
        return MakeJsonError(ERR_ACTIVITY_PHONE_INVALID, "invalid phone number");
    }
    for (size_t k = 1; k < phone.size(); ++k) {
        if (phone[k] < '0' || phone[k] > '9') {
            return MakeJsonError(ERR_ACTIVITY_PHONE_INVALID, "invalid phone number");
        }
    }

    int64_t activity_id = static_cast<int64_t>(body["activity_id"].i());

    /* parse grade (optional) */
    std::string grade;
    if (body.has("grade")) {
        grade = std::string(body["grade"].s());
    }

    /* parse signup_type (required) */
    std::string signup_type;
    if (body.has("signup_type")) {
        signup_type = std::string(body["signup_type"].s());
    }
    if (signup_type.empty()) {
        return MakeJsonError(ERR_INVALID_PARAM, "请选择报名类型");
    }

    /* check if activity requires group signup */
    ActivityInfo act;
    int ret = activity_dao_->GetActivity(activity_id, act);
    if (ret != DB_OK) {
        return MakeJsonError(ERR_ACTIVITY_NOT_FOUND, "activity not found");
    }
    if (act.min_group_size > 1) {
        return MakeJsonError(ERR_INVALID_PARAM, "该活动需要拼团报名，请使用拼团报名功能");
    }

    /* create signup atomically */
    ActivitySignupInfo signup_info;
    signup_info.activity_id = activity_id;
    signup_info.name = name;
    signup_info.phone = phone;
    signup_info.grade = grade;
    signup_info.signup_type = signup_type;

    int64_t out_id = 0;
    ret = signup_dao_->CreateSignupAtomic(signup_info, out_id);
    if (ret != DB_OK) {
        LOG_ERROR << "ActivityHandler: signup failed, ret=" << ret;
        std::string msg;
        switch (ret) {
            case ERR_ACTIVITY_NOT_FOUND:
                msg = "activity not found";
                break;
            case ERR_ACTIVITY_NOT_PUBLISHED:
                msg = "activity is not published";
                break;
            case ERR_ACTIVITY_SIGNUP_NOT_STARTED:
                msg = "报名尚未开始";
                break;
            case ERR_ACTIVITY_SIGNUP_ENDED:
                msg = "signup deadline has passed";
                break;
            case ERR_ACTIVITY_CAPACITY_FULL:
                msg = "activity is full";
                break;
            case ERR_ACTIVITY_DUPLICATE_SIGNUP:
                msg = "该姓名、手机号和年级已报名，请勿重复报名";
                break;
            default:
                msg = "signup failed";
                break;
        }
        return MakeJsonError(ret, msg);
    }

    /* get group_image for success response */
    std::string group_image;
    if (activity_dao_->GetActivity(activity_id, act) == DB_OK) {
        group_image = act.group_image;
    }

    crow::json::wvalue data;
    data["group_image"] = group_image;

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    LOG_INFO << "ActivityHandler: signup success, activity_id=" << activity_id
             << ", name=" << name;
    return crow::response(result);
}

crow::response ActivityHandler::HandleGetPromotion() {
    LOG_INFO << "ActivityHandler: get promotion";

    if (!activity_dao_) {
        return MakeJsonError(ERR_HANDLER_NULL_DAO, "activity_dao is null");
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

crow::response ActivityHandler::HandleGetNotice() {
    LOG_INFO << "ActivityHandler: get notice";

    if (!activity_dao_) {
        return MakeJsonError(ERR_HANDLER_NULL_DAO, "activity_dao is null");
    }

    std::string content;
    activity_dao_->GetActivityNotice(content);

    crow::json::wvalue data;
    data["content"] = content;

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    return crow::response(result);
}

crow::response ActivityHandler::HandleGetAboutUs() {
    LOG_INFO << "ActivityHandler: get about us cards";

    if (!activity_dao_) {
        return MakeJsonError(ERR_HANDLER_NULL_DAO, "activity_dao is null");
    }

    std::vector<AboutUsCard> cards;
    int ret = activity_dao_->GetAboutUsCards(cards);
    if (ret != DB_OK) {
        return MakeJsonError(ret, "get about us cards failed");
    }

    crow::json::wvalue data;
    for (size_t i = 0; i < cards.size(); ++i) {
        crow::json::wvalue item;
        item["id"] = static_cast<int>(cards[i].id);
        item["image_path"] = cards[i].image_path;
        item["text_content"] = cards[i].text_content;
        item["layout_type"] = cards[i].layout_type;
        data[i] = std::move(item);
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    return crow::response(result);
}

/* ====== HandleGroupSignup ====== */

crow::response ActivityHandler::HandleGroupSignup(const crow::request& req) {
    LOG_INFO << "ActivityHandler: group_signup request";

    if (!activity_dao_) {
        return MakeJsonError(ERR_HANDLER_NULL_DAO, "dao is null");
    }

    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        return MakeJsonError(ERR_INVALID_PARAM, "invalid json body");
    }
    if (!body) {
        return MakeJsonError(ERR_INVALID_PARAM, "invalid json body");
    }

    if (!body.has("activity_id") || body["activity_id"].i() <= 0) {
        return MakeJsonError(ERR_INVALID_PARAM, "activity_id is required");
    }
    int64_t activity_id = static_cast<int64_t>(body["activity_id"].i());

    std::string name;
    if (body.has("name")) { name = std::string(body["name"].s()); }
    if (name.empty()) {
        return MakeJsonError(ERR_ACTIVITY_NAME_EMPTY, "name is required");
    }
    if (name.size() > 50) {
        return MakeJsonError(ERR_ACTIVITY_NAME_EMPTY, "name too long (max 50 chars)");
    }

    std::string phone;
    if (body.has("phone")) { phone = std::string(body["phone"].s()); }
    if (phone.size() != 11 || phone[0] != '1') {
        return MakeJsonError(ERR_ACTIVITY_PHONE_INVALID, "invalid phone number");
    }
    for (size_t k = 1; k < phone.size(); ++k) {
        if (phone[k] < '0' || phone[k] > '9') {
            return MakeJsonError(ERR_ACTIVITY_PHONE_INVALID, "invalid phone number");
        }
    }

    std::string grade;
    if (body.has("grade")) { grade = std::string(body["grade"].s()); }

    std::string signup_type;
    if (body.has("signup_type")) { signup_type = std::string(body["signup_type"].s()); }
    if (signup_type.empty()) {
        return MakeJsonError(ERR_INVALID_PARAM, "请选择报名类型");
    }

    std::string invite_code;
    if (body.has("invite_code")) { invite_code = std::string(body["invite_code"].s()); }

    ActivityInfo act;
    int ret = activity_dao_->GetActivity(activity_id, act);
    if (ret != DB_OK) {
        return MakeJsonError(ERR_ACTIVITY_NOT_FOUND, "activity not found");
    }
    if (act.status != 1) {
        return MakeJsonError(ERR_ACTIVITY_NOT_PUBLISHED, "activity is not published");
    }
    std::string now_str = register_student::GetCurrentTimeString();
    if (now_str < act.start_time) {
        return MakeJsonError(ERR_ACTIVITY_SIGNUP_NOT_STARTED, "报名尚未开始");
    }
    if (act.signup_deadline <= now_str) {
        return MakeJsonError(ERR_ACTIVITY_SIGNUP_ENDED, "signup deadline has passed");
    }
    if (act.min_group_size <= 1) {
        return MakeJsonError(ERR_ACTIVITY_NOT_GROUP_MODE, "activity is not group mode");
    }

    /* ====== type=1: session group (file-based) ====== */
    if (act.group_type == 1) {
        if (!session_mgr_) {
            return MakeJsonError(ERR_HANDLER_NULL_DAO, "session_mgr is null");
        }

        /* step 1: duplicate member detection in files */
        GroupSessionInfo existing_session;
        bool found_in_file = false;
        session_mgr_->FindMemberInActivity(activity_id, name, phone,
                                           existing_session, found_in_file);
        if (found_in_file) {
            bool is_leader = (existing_session.leader_name == name &&
                              existing_session.leader_phone == phone);
            crow::json::wvalue data;
            data["invite_code"] = existing_session.invite_code;
            data["target_count"] = existing_session.target_count;
            data["current_count"] = existing_session.current_count;
            data["is_leader"] = is_leader;
            data["group_image"] = act.group_image;
            data["activity_id"] = static_cast<int>(activity_id);
            for (size_t i = 0; i < existing_session.members.size(); ++i) {
                crow::json::wvalue m;
                m["name"] = existing_session.members[i].name;
                m["phone"] = MaskPhone(existing_session.members[i].phone);
                m["grade"] = existing_session.members[i].grade;
                m["is_leader"] = existing_session.members[i].is_leader;
                data["members"][i] = std::move(m);
            }
            crow::json::wvalue result;
            result["code"] = DB_OK;
            result["message"] = "success";
            result["data"] = std::move(data);
            return crow::response(result);
        }

        /* step 2: duplicate signup detection in DB */
        if (signup_dao_) {
            bool exists = false;
            signup_dao_->CheckDuplicateSignup(activity_id, name, phone, exists);
            if (exists) {
                return MakeJsonError(ERR_ACTIVITY_DUPLICATE_SIGNUP, "already signed up");
            }
        }

        if (invite_code.empty()) {
            /* === Create new session (leader) === */
            std::string code;
            for (int attempt = 0; attempt < 10; ++attempt) {
                code = register_student::GenerateInviteCode();
                GroupSessionInfo check;
                if (session_mgr_->GetSession(activity_id, code, check) != DB_OK) {
                    break;
                }
                code.clear();
            }
            if (code.empty()) {
                return MakeJsonError(ERR_DB_EXEC_FAILED, "failed to generate unique invite code");
            }

            GroupSessionInfo session;
            session.invite_code = code;
            session.activity_id = activity_id;
            session.leader_name = name;
            session.leader_phone = phone;
            session.leader_grade = grade;
            session.target_count = act.min_group_size;
            session.current_count = 1;
            session.created_at = now_str;

            ActivityGroupMemberInfo leader_member;
            leader_member.name = name;
            leader_member.phone = phone;
            leader_member.grade = grade;
            leader_member.signup_type = signup_type;
            leader_member.is_leader = 1;
            leader_member.created_at = now_str;
            session.members.push_back(leader_member);

            ret = session_mgr_->CreateSession(session);
            if (ret != DB_OK) {
                LOG_ERROR << "ActivityHandler: create session failed, ret=" << ret;
                return MakeJsonError(ret, "create session failed");
            }

            crow::json::wvalue data;
            data["invite_code"] = code;
            data["target_count"] = act.min_group_size;
            data["current_count"] = 1;
            data["is_leader"] = true;
            data["group_image"] = act.group_image;
            data["activity_id"] = static_cast<int>(activity_id);

            crow::json::wvalue member_obj;
            member_obj["name"] = name;
            member_obj["phone"] = MaskPhone(phone);
            member_obj["grade"] = grade;
            member_obj["is_leader"] = 1;
            data["members"][0] = std::move(member_obj);

            crow::json::wvalue result;
            result["code"] = DB_OK;
            result["message"] = "success";
            result["data"] = std::move(data);

            LOG_INFO << "ActivityHandler: session created, activity_id=" << activity_id;
            return crow::response(result);
        } else {
            /* === Join existing session (member) === */
            std::transform(invite_code.begin(), invite_code.end(),
                           invite_code.begin(), ::toupper);

            GroupSessionInfo session;
            ret = session_mgr_->GetSession(activity_id, invite_code, session);
            if (ret != DB_OK) {
                return MakeJsonError(ERR_ACTIVITY_GROUP_SESSION_EXPIRED,
                                     "邀请码无效或拼团已结束");
            }

            ActivityGroupMemberInfo new_member;
            new_member.name = name;
            new_member.phone = phone;
            new_member.grade = grade;
            new_member.signup_type = signup_type;
            new_member.is_leader = 0;
            new_member.created_at = now_str;

            GroupSessionInfo updated;
            ret = session_mgr_->AddMember(activity_id, invite_code, new_member, updated);
            if (ret != DB_OK) {
                LOG_ERROR << "ActivityHandler: add member to session failed, ret=" << ret;
                return MakeJsonError(ret, "add member failed");
            }

            /* check if full -> auto confirm */
            if (updated.current_count >= updated.target_count) {
                /* convert members to ActivitySignupInfo */
                std::vector<ActivitySignupInfo> signup_members;
                for (size_t i = 0; i < updated.members.size(); ++i) {
                    ActivitySignupInfo s;
                    s.activity_id = activity_id;
                    s.name = updated.members[i].name;
                    s.phone = updated.members[i].phone;
                    s.grade = updated.members[i].grade;
                    s.signup_type = updated.members[i].signup_type;
                    signup_members.push_back(s);
                }

                ret = signup_dao_->ConfirmSessionAtomic(activity_id, signup_members);
                if (ret == ERR_ACTIVITY_CAPACITY_FULL) {
                    return MakeJsonError(ERR_ACTIVITY_GROUP_CAPACITY_INSUFFICIENT,
                                         "活动容量已满，无法容纳拼团");
                }
                if (ret != DB_OK) {
                    LOG_ERROR << "ActivityHandler: ConfirmSessionAtomic failed, ret=" << ret;
                    return MakeJsonError(ret, "confirm failed");
                }

                /* create group and member records for admin view */
                if (group_dao_) {
                    int64_t new_group_id = 0;
                    ActivityGroupInfo group_info;
                    group_info.activity_id = activity_id;
                    group_info.invite_code = updated.invite_code;
                    group_info.target_count = updated.target_count;
                    group_info.current_count = updated.current_count;
                    group_info.status = 1;
                    group_info.created_at = now_str;
                    ret = group_dao_->CreateGroup(group_info, new_group_id);
                    if (ret == DB_OK) {
                        for (size_t i = 0; i < updated.members.size(); ++i) {
                            ActivityGroupMemberInfo member_info = updated.members[i];
                            member_info.group_id = new_group_id;
                            int64_t member_id = 0;
                            group_dao_->AddMember(member_info, member_id);
                        }
                        group_dao_->UpdateGroupStatus(new_group_id, 1, 0);
                        LOG_INFO << "ActivityHandler: created group records for auto-confirmed session, group_id=" << new_group_id;
                    } else {
                        LOG_ERROR << "ActivityHandler: failed to create group records, ret=" << ret;
                    }
                }

                /* delete session file (best effort) */
                session_mgr_->DeleteSession(activity_id, invite_code);

                crow::json::wvalue data;
                data["auto_confirmed"] = true;
                data["group_image"] = act.group_image;
                data["activity_id"] = static_cast<int>(activity_id);

                crow::json::wvalue result;
                result["code"] = DB_OK;
                result["message"] = "success";
                result["data"] = std::move(data);

                LOG_INFO << "ActivityHandler: session auto-confirmed, activity_id=" << activity_id;
                return crow::response(result);
            }

            /* not full yet */
            crow::json::wvalue data;
            data["invite_code"] = updated.invite_code;
            data["target_count"] = updated.target_count;
            data["current_count"] = updated.current_count;
            data["is_leader"] = false;
            data["group_image"] = act.group_image;
            data["activity_id"] = static_cast<int>(activity_id);
            for (size_t i = 0; i < updated.members.size(); ++i) {
                crow::json::wvalue m;
                m["name"] = updated.members[i].name;
                m["phone"] = MaskPhone(updated.members[i].phone);
                m["grade"] = updated.members[i].grade;
                m["is_leader"] = updated.members[i].is_leader;
                data["members"][i] = std::move(m);
            }

            crow::json::wvalue result;
            result["code"] = DB_OK;
            result["message"] = "success";
            result["data"] = std::move(data);

            LOG_INFO << "ActivityHandler: joined session, activity_id=" << activity_id
                     << ", count=" << updated.current_count;
            return crow::response(result);
        }
    }

    /* ====== type=2: sync group (database-based, original logic) ====== */
    if (!group_dao_) {
        return MakeJsonError(ERR_HANDLER_NULL_DAO, "group_dao is null");
    }

    if (invite_code.empty()) {
        /* === Create new group === */
        std::string code;
        for (int attempt = 0; attempt < 10; ++attempt) {
            code = register_student::GenerateInviteCode();
            ActivityGroupInfo check;
            if (group_dao_->GetGroupByInviteCode(code, check) != DB_OK) {
                break;
            }
            code.clear();
        }
        if (code.empty()) {
            return MakeJsonError(ERR_DB_EXEC_FAILED, "failed to generate unique invite code");
        }

        ActivityGroupInfo group;
        group.activity_id = activity_id;
        group.invite_code = code;
        group.leader_name = name;
        group.leader_phone = phone;
        group.leader_grade = grade;
        group.current_count = 1;
        group.target_count = act.min_group_size;
        group.status = GROUP_WAITING;
        group.cancel_reason = CANCEL_NONE;

        int64_t group_id = 0;
        ret = group_dao_->CreateGroup(group, group_id);
        if (ret != DB_OK) {
            LOG_ERROR << "ActivityHandler: create group failed, ret=" << ret;
            return MakeJsonError(ret, "create group failed");
        }

        ActivityGroupMemberInfo member;
        member.group_id = group_id;
        member.name = name;
        member.phone = phone;
        member.grade = grade;
        member.signup_type = signup_type;
        member.is_leader = 1;

        int64_t member_id = 0;
        ret = group_dao_->AddMember(member, member_id);
        if (ret != DB_OK) {
            LOG_ERROR << "ActivityHandler: add member failed, ret=" << ret;
            return MakeJsonError(ret, "add member failed");
        }

        crow::json::wvalue data;
        data["group_id"] = static_cast<int>(group_id);
        data["invite_code"] = code;
        data["target_count"] = act.min_group_size;
        data["current_count"] = 1;
        data["is_leader"] = true;
        data["created_at"] = now_str;

        crow::json::wvalue result;
        result["code"] = DB_OK;
        result["message"] = "success";
        result["data"] = std::move(data);

        LOG_INFO << "ActivityHandler: group created, group_id=" << group_id;
        return crow::response(result);
    } else {
        /* === Join existing group === */
        std::transform(invite_code.begin(), invite_code.end(),
                       invite_code.begin(), ::toupper);

        ActivityGroupInfo group;
        ret = group_dao_->GetGroupByInviteCode(invite_code, group);
        if (ret != DB_OK) {
            return MakeJsonError(ERR_ACTIVITY_GROUP_INVALID_CODE, "invalid invite code");
        }
        if (group.activity_id != activity_id) {
            return MakeJsonError(ERR_ACTIVITY_GROUP_INVALID_CODE, "invite code does not match this activity");
        }
        if (group.status == GROUP_CONFIRMED) {
            return MakeJsonError(ERR_ACTIVITY_GROUP_CONFIRMED, "group already confirmed");
        }
        if (group.status == GROUP_CANCELLED) {
            return MakeJsonError(ERR_ACTIVITY_GROUP_CANCELLED, "group already cancelled");
        }

        if (IsGroupTimedOut(group.created_at)) {
            group_dao_->UpdateGroupStatus(group.id, GROUP_CANCELLED, CANCEL_TIMEOUT);
            return MakeJsonError(ERR_ACTIVITY_GROUP_TIMEOUT, "group has timed out");
        }

        if (act.capacity > 0 &&
            act.capacity - act.signup_count < group.current_count + 1) {
            return MakeJsonError(ERR_ACTIVITY_CAPACITY_FULL, "activity is full");
        }

        bool dup = false;
        group_dao_->CheckDuplicateInGroup(group.id, name, phone, grade, dup);
        if (dup) {
            return MakeJsonError(ERR_ACTIVITY_DUPLICATE_SIGNUP, "already in this group");
        }

        ActivityGroupMemberInfo member;
        member.group_id = group.id;
        member.name = name;
        member.phone = phone;
        member.grade = grade;
        member.signup_type = signup_type;
        member.is_leader = 0;

        int64_t member_id = 0;
        ret = group_dao_->AddMember(member, member_id);
        if (ret != DB_OK) {
            LOG_ERROR << "ActivityHandler: add member failed, ret=" << ret;
            return MakeJsonError(ret, "add member failed");
        }

        group_dao_->UpdateGroupCount(group.id, 1);
        int32_t new_count = group.current_count + 1;

        crow::json::wvalue data;
        data["group_id"] = static_cast<int>(group.id);
        data["invite_code"] = group.invite_code;
        data["target_count"] = group.target_count;
        data["current_count"] = new_count;
        data["is_leader"] = false;
        data["created_at"] = now_str;

        crow::json::wvalue result;
        result["code"] = DB_OK;
        result["message"] = "success";
        result["data"] = std::move(data);

        LOG_INFO << "ActivityHandler: joined group, group_id=" << group.id
                 << ", count=" << new_count;
        return crow::response(result);
    }
}

/* ====== HandleGetGroupStatus ====== */

crow::response ActivityHandler::HandleGetGroupStatus(const crow::request& req) {
    /* check if type=1 (session group) */
    std::string group_type_str;
    const char* gt = req.url_params.get("group_type");
    if (gt) { group_type_str = gt; }
    int group_type = 0;
    if (!group_type_str.empty()) { group_type = std::atoi(group_type_str.c_str()); }

    if (group_type == 1) {
        /* type=1: file-based polling by invite_code */
        if (!session_mgr_) {
            return MakeJsonError(ERR_HANDLER_NULL_DAO, "session_mgr is null");
        }

        std::string invite_code;
        const char* ic = req.url_params.get("invite_code");
        if (ic) { invite_code = ic; }
        if (invite_code.empty()) {
            return MakeJsonError(ERR_INVALID_PARAM, "invite_code is required");
        }

        std::string name_param;
        const char* np = req.url_params.get("name");
        if (np) { name_param = np; }
        std::string phone_param;
        const char* pp = req.url_params.get("phone");
        if (pp) { phone_param = pp; }

        /* try to find the session file */
        /* we need to search across all activity dirs - use the invite_code to locate */
        /* first, try to find which activity this belongs to by scanning */
        /* optimization: we need activity_id. Let's get it from the session file content */

        /* scan all activity directories to find the session */
        /* we need a way to find the session without knowing activity_id */
        /* use ListAllSessions approach - but we need to know the activity_id */
        /* alternative: the frontend should pass activity_id */
        std::string aid_str;
        const char* aid_p = req.url_params.get("activity_id");
        if (aid_p) { aid_str = aid_p; }
        int64_t activity_id = 0;
        if (!aid_str.empty()) { activity_id = std::atoll(aid_str.c_str()); }

        if (activity_id <= 0) {
            return MakeJsonError(ERR_INVALID_PARAM, "activity_id is required");
        }

        GroupSessionInfo session;
        int ret = session_mgr_->GetSession(activity_id, invite_code, session);
        if (ret == DB_OK) {
            /* session file exists - return waiting status */
            ActivityInfo act;
            std::string group_image;
            if (activity_dao_ && activity_dao_->GetActivity(activity_id, act) == DB_OK) {
                group_image = act.group_image;
            }

            crow::json::wvalue data;
            data["status"] = static_cast<int>(GROUP_WAITING);
            data["invite_code"] = session.invite_code;
            data["current_count"] = session.current_count;
            data["target_count"] = session.target_count;
            data["group_image"] = group_image;
            data["activity_id"] = static_cast<int>(activity_id);

            for (size_t i = 0; i < session.members.size(); ++i) {
                crow::json::wvalue m;
                m["name"] = session.members[i].name;
                m["phone"] = MaskPhone(session.members[i].phone);
                m["grade"] = session.members[i].grade;
                m["is_leader"] = session.members[i].is_leader;
                data["members"][i] = std::move(m);
            }

            crow::json::wvalue result;
            result["code"] = DB_OK;
            result["message"] = "success";
            result["data"] = std::move(data);
            return crow::response(result);
        }

        /* session file not found - check if already signed up */
        if (signup_dao_ && !name_param.empty() && !phone_param.empty()) {
            bool exists = false;
            signup_dao_->CheckDuplicateSignup(activity_id, name_param, phone_param, exists);
            if (exists) {
                crow::json::wvalue data;
                data["status"] = static_cast<int>(GROUP_CONFIRMED);
                crow::json::wvalue result;
                result["code"] = DB_OK;
                result["message"] = "success";
                result["data"] = std::move(data);
                return crow::response(result);
            }
        }

        return MakeJsonError(ERR_ACTIVITY_GROUP_SESSION_EXPIRED,
                             "邀请码无效或拼团已结束");
    }

    /* type=2: original database-based logic */
    if (!group_dao_) {
        return MakeJsonError(ERR_HANDLER_NULL_DAO, "group_dao is null");
    }

    std::string group_id_str;
    const char* gid = req.url_params.get("group_id");
    if (gid) { group_id_str = gid; }
    if (group_id_str.empty()) {
        return MakeJsonError(ERR_INVALID_PARAM, "group_id is required");
    }
    int64_t group_id = std::atoll(group_id_str.c_str());
    if (group_id <= 0) {
        return MakeJsonError(ERR_INVALID_PARAM, "invalid group_id");
    }

    ActivityGroupInfo group;
    int ret = group_dao_->GetGroup(group_id, group);
    if (ret != DB_OK) {
        return MakeJsonError(ERR_ACTIVITY_GROUP_INVALID_CODE, "group not found");
    }

    if (group.status == GROUP_WAITING && IsGroupTimedOut(group.created_at)) {
        group_dao_->UpdateGroupStatus(group_id, GROUP_CANCELLED, CANCEL_TIMEOUT);
        group.status = GROUP_CANCELLED;
        group.cancel_reason = CANCEL_TIMEOUT;
    }

    std::vector<ActivityGroupMemberInfo> members;
    group_dao_->ListMembersByGroup(group_id, members);

    crow::json::wvalue data;
    data["group_id"] = static_cast<int>(group_id);
    data["status"] = group.status;
    data["current_count"] = group.current_count;
    data["target_count"] = group.target_count;
    data["cancel_reason"] = group.cancel_reason;
    data["remaining_seconds"] = static_cast<int>(RemainingSeconds(group.created_at));
    data["created_at"] = group.created_at;

    if (group.status == GROUP_CONFIRMED) {
        ActivityInfo act;
        if (activity_dao_ && activity_dao_->GetActivity(group.activity_id, act) == DB_OK) {
            data["group_image"] = act.group_image;
        }
    }

    for (size_t i = 0; i < members.size(); ++i) {
        crow::json::wvalue m;
        m["name"] = members[i].name;
        m["phone"] = MaskPhone(members[i].phone);
        m["grade"] = members[i].grade;
        m["is_leader"] = members[i].is_leader;
        data["members"][i] = std::move(m);
    }

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);
    return crow::response(result);
}

/* ====== HandleConfirmGroup ====== */

crow::response ActivityHandler::HandleConfirmGroup(const crow::request& req) {
    LOG_INFO << "ActivityHandler: confirm_group request";

    if (!activity_dao_ || !group_dao_) {
        return MakeJsonError(ERR_HANDLER_NULL_DAO, "dao is null");
    }

    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        return MakeJsonError(ERR_INVALID_PARAM, "invalid json body");
    }
    if (!body) {
        return MakeJsonError(ERR_INVALID_PARAM, "invalid json body");
    }

    /* type=1: auto-confirm replaces manual confirm */
    if (body.has("group_type") && body["group_type"].i() == 1) {
        return MakeJsonError(ERR_ACTIVITY_GROUP_SESSION_EXPIRED,
                             "session group auto-confirmed");
    }

    if (!body.has("group_id") || body["group_id"].i() <= 0) {
        return MakeJsonError(ERR_INVALID_PARAM, "group_id is required");
    }
    int64_t group_id = static_cast<int64_t>(body["group_id"].i());

    std::string name;
    if (body.has("name")) { name = std::string(body["name"].s()); }
    if (name.empty()) {
        return MakeJsonError(ERR_ACTIVITY_NAME_EMPTY, "name is required");
    }

    std::string phone;
    if (body.has("phone")) { phone = std::string(body["phone"].s()); }
    if (phone.size() != 11 || phone[0] != '1') {
        return MakeJsonError(ERR_ACTIVITY_PHONE_INVALID, "invalid phone number");
    }
    for (size_t k = 1; k < phone.size(); ++k) {
        if (phone[k] < '0' || phone[k] > '9') {
            return MakeJsonError(ERR_ACTIVITY_PHONE_INVALID, "invalid phone number");
        }
    }

    ActivityGroupInfo group;
    int ret = group_dao_->GetGroup(group_id, group);
    if (ret != DB_OK) {
        return MakeJsonError(ERR_ACTIVITY_GROUP_INVALID_CODE, "group not found");
    }
    if (group.status == GROUP_CONFIRMED) {
        return MakeJsonError(ERR_ACTIVITY_GROUP_CONFIRMED, "group already confirmed");
    }
    if (group.status == GROUP_CANCELLED) {
        return MakeJsonError(ERR_ACTIVITY_GROUP_CANCELLED, "group already cancelled");
    }

    if (IsGroupTimedOut(group.created_at)) {
        group_dao_->UpdateGroupStatus(group_id, GROUP_CANCELLED, CANCEL_TIMEOUT);
        return MakeJsonError(ERR_ACTIVITY_GROUP_TIMEOUT, "group has timed out");
    }

    if (group.leader_name != name || group.leader_phone != phone) {
        return MakeJsonError(ERR_ACTIVITY_GROUP_NOT_LEADER, "only the leader can confirm");
    }

    if (group.current_count < group.target_count) {
        return MakeJsonError(ERR_INVALID_PARAM, "group is not full yet");
    }

    std::vector<ActivityGroupMemberInfo> members;
    ret = group_dao_->ListMembersByGroup(group_id, members);
    if (ret != DB_OK) {
        return MakeJsonError(ret, "failed to list members");
    }

    ret = group_dao_->ConfirmGroupAtomic(group.activity_id, group_id, members);
    if (ret != DB_OK) {
        LOG_ERROR << "ActivityHandler: confirm group atomic failed, ret=" << ret;
        return MakeJsonError(ret, "confirm failed");
    }

    ActivityInfo act;
    std::string group_image;
    if (activity_dao_->GetActivity(group.activity_id, act) == DB_OK) {
        group_image = act.group_image;
    }

    crow::json::wvalue data;
    data["success"] = true;
    data["group_image"] = group_image;

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    LOG_INFO << "ActivityHandler: group confirmed, group_id=" << group_id;
    return crow::response(result);
}

/* ====== HandleLeaveGroup ====== */

crow::response ActivityHandler::HandleLeaveGroup(const crow::request& req) {
    LOG_INFO << "ActivityHandler: leave_group request";

    if (!group_dao_) {
        return MakeJsonError(ERR_HANDLER_NULL_DAO, "group_dao is null");
    }

    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        return MakeJsonError(ERR_INVALID_PARAM, "invalid json body");
    }
    if (!body) {
        return MakeJsonError(ERR_INVALID_PARAM, "invalid json body");
    }

    /* type=1: leader leaving does not cancel the session */
    if (body.has("group_type") && body["group_type"].i() == 1) {
        crow::json::wvalue data;
        data["success"] = true;
        crow::json::wvalue result;
        result["code"] = DB_OK;
        result["message"] = "success";
        result["data"] = std::move(data);
        return crow::response(result);
    }

    if (!body.has("group_id") || body["group_id"].i() <= 0) {
        return MakeJsonError(ERR_INVALID_PARAM, "group_id is required");
    }
    int64_t group_id = static_cast<int64_t>(body["group_id"].i());

    std::string name;
    if (body.has("name")) { name = std::string(body["name"].s()); }
    if (name.empty()) {
        return MakeJsonError(ERR_ACTIVITY_NAME_EMPTY, "name is required");
    }

    std::string phone;
    if (body.has("phone")) { phone = std::string(body["phone"].s()); }
    if (phone.size() != 11 || phone[0] != '1') {
        return MakeJsonError(ERR_ACTIVITY_PHONE_INVALID, "invalid phone number");
    }
    for (size_t k = 1; k < phone.size(); ++k) {
        if (phone[k] < '0' || phone[k] > '9') {
            return MakeJsonError(ERR_ACTIVITY_PHONE_INVALID, "invalid phone number");
        }
    }

    ActivityGroupInfo group;
    int ret = group_dao_->GetGroup(group_id, group);
    if (ret != DB_OK) {
        return MakeJsonError(ERR_ACTIVITY_GROUP_INVALID_CODE, "group not found");
    }
    if (group.status != GROUP_WAITING) {
        return MakeJsonError(ERR_ACTIVITY_GROUP_CONFIRMED, "group is not in waiting state");
    }

    int32_t is_leader = 0;
    int32_t remaining_count = 0;
    ret = group_dao_->RemoveMember(group_id, name, phone, is_leader, remaining_count);
    if (ret != DB_OK) {
        return MakeJsonError(ret, "remove member failed");
    }

    if (is_leader == 1) {
        group_dao_->UpdateGroupStatus(group_id, GROUP_CANCELLED, CANCEL_LEAVE);
    } else if (remaining_count <= 0) {
        group_dao_->UpdateGroupStatus(group_id, GROUP_CANCELLED, CANCEL_NONE);
    }

    crow::json::wvalue data;
    data["success"] = true;

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    LOG_INFO << "ActivityHandler: left group, group_id=" << group_id
             << ", is_leader=" << is_leader;
    return crow::response(result);
}

/* ====== HandleCancelGroup ====== */

crow::response ActivityHandler::HandleCancelGroup(const crow::request& req) {
    LOG_INFO << "ActivityHandler: cancel_group request";

    if (!group_dao_) {
        return MakeJsonError(ERR_HANDLER_NULL_DAO, "group_dao is null");
    }

    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        return MakeJsonError(ERR_INVALID_PARAM, "invalid json body");
    }
    if (!body) {
        return MakeJsonError(ERR_INVALID_PARAM, "invalid json body");
    }

    /* type=1: cancel by deleting session file */
    if (body.has("group_type") && body["group_type"].i() == 1) {
        if (!session_mgr_) {
            return MakeJsonError(ERR_HANDLER_NULL_DAO, "session_mgr is null");
        }

        std::string invite_code;
        if (body.has("invite_code")) { invite_code = std::string(body["invite_code"].s()); }
        if (invite_code.empty()) {
            return MakeJsonError(ERR_INVALID_PARAM, "invite_code is required");
        }

        int64_t activity_id = 0;
        if (body.has("activity_id")) { activity_id = static_cast<int64_t>(body["activity_id"].i()); }
        if (activity_id <= 0) {
            return MakeJsonError(ERR_INVALID_PARAM, "activity_id is required");
        }

        std::string name;
        if (body.has("name")) { name = std::string(body["name"].s()); }
        std::string phone;
        if (body.has("phone")) { phone = std::string(body["phone"].s()); }

        GroupSessionInfo session;
        int ret = session_mgr_->GetSession(activity_id, invite_code, session);
        if (ret != DB_OK) {
            return MakeJsonError(ERR_ACTIVITY_GROUP_SESSION_EXPIRED,
                                 "邀请码无效或拼团已结束");
        }

        if (session.leader_name != name || session.leader_phone != phone) {
            return MakeJsonError(ERR_ACTIVITY_GROUP_NOT_LEADER, "only the leader can cancel");
        }

        session_mgr_->DeleteSession(activity_id, invite_code);

        crow::json::wvalue data;
        data["success"] = true;
        crow::json::wvalue result;
        result["code"] = DB_OK;
        result["message"] = "success";
        result["data"] = std::move(data);

        LOG_INFO << "ActivityHandler: session cancelled, activity_id=" << activity_id;
        return crow::response(result);
    }

    if (!body.has("group_id") || body["group_id"].i() <= 0) {
        return MakeJsonError(ERR_INVALID_PARAM, "group_id is required");
    }
    int64_t group_id = static_cast<int64_t>(body["group_id"].i());

    std::string name;
    if (body.has("name")) { name = std::string(body["name"].s()); }
    if (name.empty()) {
        return MakeJsonError(ERR_ACTIVITY_NAME_EMPTY, "name is required");
    }

    std::string phone;
    if (body.has("phone")) { phone = std::string(body["phone"].s()); }
    if (phone.size() != 11 || phone[0] != '1') {
        return MakeJsonError(ERR_ACTIVITY_PHONE_INVALID, "invalid phone number");
    }
    for (size_t k = 1; k < phone.size(); ++k) {
        if (phone[k] < '0' || phone[k] > '9') {
            return MakeJsonError(ERR_ACTIVITY_PHONE_INVALID, "invalid phone number");
        }
    }

    ActivityGroupInfo group;
    int ret = group_dao_->GetGroup(group_id, group);
    if (ret != DB_OK) {
        return MakeJsonError(ERR_ACTIVITY_GROUP_INVALID_CODE, "group not found");
    }
    if (group.status != GROUP_WAITING) {
        return MakeJsonError(ERR_ACTIVITY_GROUP_CONFIRMED, "group is not in waiting state");
    }

    if (group.leader_name != name || group.leader_phone != phone) {
        return MakeJsonError(ERR_ACTIVITY_GROUP_NOT_LEADER, "only the leader can cancel");
    }

    group_dao_->UpdateGroupStatus(group_id, GROUP_CANCELLED, CANCEL_LEADER);

    crow::json::wvalue data;
    data["success"] = true;

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    LOG_INFO << "ActivityHandler: group cancelled by leader, group_id=" << group_id;
    return crow::response(result);
}

/* ====== HandleBatchGroupSignup ====== */

crow::response ActivityHandler::HandleBatchGroupSignup(const crow::request& req) {
    LOG_INFO << "ActivityHandler: batch_group_signup request";

    if (!activity_dao_ || !group_dao_) {
        return MakeJsonError(ERR_HANDLER_NULL_DAO, "dao is null");
    }

    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        return MakeJsonError(ERR_INVALID_PARAM, "invalid json body");
    }
    if (!body) {
        return MakeJsonError(ERR_INVALID_PARAM, "invalid json body");
    }

    if (!body.has("activity_id") || body["activity_id"].i() <= 0) {
        return MakeJsonError(ERR_INVALID_PARAM, "activity_id is required");
    }
    int64_t activity_id = static_cast<int64_t>(body["activity_id"].i());

    if (!body.has("members") || body["members"].t() != crow::json::type::List) {
        return MakeJsonError(ERR_INVALID_PARAM, "members array is required");
    }
    size_t member_count = body["members"].size();
    if (member_count == 0) {
        return MakeJsonError(ERR_INVALID_PARAM, "members cannot be empty");
    }

    ActivityInfo act;
    int ret = activity_dao_->GetActivity(activity_id, act);
    if (ret != DB_OK) {
        return MakeJsonError(ERR_ACTIVITY_NOT_FOUND, "activity not found");
    }
    if (act.status != 1) {
        return MakeJsonError(ERR_ACTIVITY_NOT_PUBLISHED, "activity is not published");
    }
    std::string now_str = register_student::GetCurrentTimeString();
    if (now_str < act.start_time) {
        return MakeJsonError(ERR_ACTIVITY_SIGNUP_NOT_STARTED, "报名尚未开始");
    }
    if (act.signup_deadline <= now_str) {
        return MakeJsonError(ERR_ACTIVITY_SIGNUP_ENDED, "signup deadline has passed");
    }

    int32_t effective_group_type = act.group_type;
    if (effective_group_type == 0 && act.min_group_size > 1) {
        effective_group_type = 1;
    }
    if (effective_group_type != 2) {
        return MakeJsonError(ERR_ACTIVITY_NOT_GROUP_MODE, "activity is not sync group mode");
    }
    if (act.min_group_size <= 1 || static_cast<int32_t>(member_count) != act.min_group_size) {
        return MakeJsonError(ERR_INVALID_PARAM, "members count must equal group size");
    }
    if (act.capacity > 0 && act.capacity - act.signup_count < static_cast<int32_t>(member_count)) {
        return MakeJsonError(ERR_ACTIVITY_CAPACITY_FULL, "activity is full");
    }

    std::vector<ActivityGroupMemberInfo> members;
    members.reserve(member_count);
    for (size_t i = 0; i < member_count; ++i) {
        std::string name;
        if (body["members"][i].has("name")) { name = std::string(body["members"][i]["name"].s()); }
        if (name.empty() || name.size() > 50) {
            return MakeJsonError(ERR_ACTIVITY_NAME_EMPTY, "member name is invalid");
        }

        std::string phone;
        if (body["members"][i].has("phone")) { phone = std::string(body["members"][i]["phone"].s()); }
        if (phone.size() != 11 || phone[0] != '1') {
            return MakeJsonError(ERR_ACTIVITY_PHONE_INVALID, "invalid phone number");
        }
        for (size_t k = 1; k < phone.size(); ++k) {
            if (phone[k] < '0' || phone[k] > '9') {
                return MakeJsonError(ERR_ACTIVITY_PHONE_INVALID, "invalid phone number");
            }
        }

        std::string grade;
        if (body["members"][i].has("grade")) { grade = std::string(body["members"][i]["grade"].s()); }

        std::string signup_type;
        if (body["members"][i].has("signup_type")) { signup_type = std::string(body["members"][i]["signup_type"].s()); }
        if (signup_type.empty()) {
            return MakeJsonError(ERR_INVALID_PARAM, "请选择报名类型");
        }

        ActivityGroupMemberInfo m;
        m.name = name;
        m.phone = phone;
        m.grade = grade;
        m.signup_type = signup_type;
        m.is_leader = (i == 0) ? 1 : 0;
        members.push_back(m);
    }

    std::string code;
    for (int attempt = 0; attempt < 10; ++attempt) {
        code = register_student::GenerateInviteCode();
        ActivityGroupInfo check;
        if (group_dao_->GetGroupByInviteCode(code, check) != DB_OK) {
            break;
        }
        code.clear();
    }
    if (code.empty()) {
        return MakeJsonError(ERR_DB_EXEC_FAILED, "failed to generate unique invite code");
    }

    ActivityGroupInfo group;
    group.activity_id = activity_id;
    group.invite_code = code;
    group.leader_name = members[0].name;
    group.leader_phone = members[0].phone;
    group.leader_grade = members[0].grade;
    group.current_count = static_cast<int32_t>(member_count);
    group.target_count = act.min_group_size;
    group.status = GROUP_WAITING;
    group.cancel_reason = CANCEL_NONE;

    int64_t group_id = 0;
    ret = group_dao_->CreateGroup(group, group_id);
    if (ret != DB_OK) {
        LOG_ERROR << "ActivityHandler: create batch group failed, ret=" << ret;
        return MakeJsonError(ret, "create group failed");
    }

    for (size_t i = 0; i < members.size(); ++i) {
        members[i].group_id = group_id;
        int64_t member_id = 0;
        ret = group_dao_->AddMember(members[i], member_id);
        if (ret != DB_OK) {
            LOG_ERROR << "ActivityHandler: add batch member failed, ret=" << ret;
            return MakeJsonError(ret, "add member failed");
        }
    }

    ret = group_dao_->ConfirmGroupAtomic(activity_id, group_id, members);
    if (ret != DB_OK) {
        LOG_ERROR << "ActivityHandler: confirm batch group atomic failed, ret=" << ret;
        group_dao_->UpdateGroupStatus(group_id, GROUP_CANCELLED, CANCEL_NONE);
        std::string msg;
        switch (ret) {
            case ERR_ACTIVITY_CAPACITY_FULL:
                msg = "activity is full";
                break;
            case ERR_ACTIVITY_DUPLICATE_SIGNUP:
                msg = "该姓名、手机号和年级已报名，请勿重复报名";
                break;
            default:
                msg = "confirm failed";
                break;
        }
        return MakeJsonError(ret, msg);
    }

    crow::json::wvalue data;
    data["success"] = true;
    data["group_image"] = act.group_image;
    data["invite_code"] = code;

    crow::json::wvalue result;
    result["code"] = DB_OK;
    result["message"] = "success";
    result["data"] = std::move(data);

    LOG_INFO << "ActivityHandler: batch group signup success, activity_id=" << activity_id
             << ", group_id=" << group_id << ", members=" << member_count;
    return crow::response(result);
}
