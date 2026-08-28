#ifndef __ACTIVITY_TYPES_H__
#define __ACTIVITY_TYPES_H__

#include <string>
#include <cstdint>
#include <vector>

struct ActivityInfo {
    int64_t id = 0;
    std::string title;
    std::string description;
    std::string cover_image;
    std::string start_time;         /* YYYY-MM-DD HH:MM */
    std::string end_time;           /* YYYY-MM-DD HH:MM */
    std::string signup_deadline;    /* YYYY-MM-DD HH:MM */
    int32_t capacity = 0;           /* 0 = unlimited */
    int32_t signup_count = 0;
    std::string group_image;
    int32_t sort_order = 0;
    int32_t status = 0;             /* 0=unpublished, 1=published */
    int32_t min_group_size = 1;     /* 1=normal, >1=group signup */
    int32_t group_type = 0;         /* 0=normal, 1=session group, 2=sync group */
    std::string created_at;
    std::string updated_at;
};

struct ActivitySignupInfo {
    int64_t id = 0;
    int64_t activity_id = 0;
    std::string name;
    std::string phone;
    std::string grade;
    std::string signup_type;
    std::string created_at;
};

struct ActivityCoverImage {
    int64_t id = 0;
    int64_t activity_id = 0;
    std::string image_path;
    int32_t sort_order = 0;
    std::string created_at;
};

enum ActivityGroupStatus {
    GROUP_WAITING   = 0,
    GROUP_CONFIRMED = 1,
    GROUP_CANCELLED = 2
};

enum ActivityGroupCancelReason {
    CANCEL_NONE    = 0,
    CANCEL_LEADER  = 1,
    CANCEL_LEAVE   = 2,
    CANCEL_TIMEOUT = 3
};

struct ActivityGroupInfo {
    int64_t id = 0;
    int64_t activity_id = 0;
    std::string invite_code;
    std::string leader_name;
    std::string leader_phone;
    std::string leader_grade;
    int32_t current_count = 0;
    int32_t target_count = 0;
    int32_t status = 0;
    int32_t cancel_reason = 0;
    std::string created_at;
    std::string updated_at;
};

struct ActivityGroupMemberInfo {
    int64_t id = 0;
    int64_t group_id = 0;
    std::string name;
    std::string phone;
    std::string grade;
    std::string signup_type;
    int32_t is_leader = 0;
    std::string created_at;
    std::string invite_code;        /* only filled by ListMembersByActivity */
};

struct GroupSessionInfo {
    std::string invite_code;
    int64_t activity_id = 0;
    std::string leader_name;
    std::string leader_phone;
    std::string leader_grade;
    int32_t target_count = 0;
    int32_t current_count = 0;
    std::vector<ActivityGroupMemberInfo> members;
    std::string created_at;
};

struct AboutUsCard {
    int64_t id = 0;
    std::string image_path;
    std::string text_content;
    int32_t layout_type = 1;
    int32_t sort_order = 0;
    std::string created_at;
};

#endif /* __ACTIVITY_TYPES_H__ */
