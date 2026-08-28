#ifndef __RESOURCE_TYPES_H__
#define __RESOURCE_TYPES_H__

#include <string>
#include <cstdint>

/* 资源类型枚举 */
enum ResourceType {
    ResourceType_Other = 0,
    ResourceType_Bed = 1
};

struct ResourceInfo {
    int32_t id;
    std::string name;
    int32_t total_count;
    int32_t used_count;          /* 资源分配体系已用数量 */
    int32_t remain_count;        /* 资源分配体系剩余数量 = total_count - used_count */
    int32_t resource_type;       /* 0=其他, 1=床位 */
    int32_t bed_reserved_count;  /* 报名时选需要床位的占用数（仅床位类型有效，独立于used_count） */
};

struct ResourceAllocation {
    int32_t id;
    int32_t resource_id;
    int32_t registration_id;
    std::string student_name;
    std::string student_gender;
    std::string teacher_name;
    std::string class_name;
    int32_t resource_code;       /* 纯数字编号 */
    std::string allocate_time;
};

#endif /* __RESOURCE_TYPES_H__ */
