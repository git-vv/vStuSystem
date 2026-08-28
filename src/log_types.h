#ifndef __LOG_TYPES_H__
#define __LOG_TYPES_H__

#include <string>
#include <cstdint>

enum OperationType {
    OpType_RegisterStudent    = 1,
    OpType_AllocateResource   = 2,
    OpType_ReleaseResource    = 3,
    OpType_ResetPassword      = 4,
    OpType_ModifyEnrollment   = 5,
    OpType_CreateClass        = 6,
    OpType_CreateResource     = 7,
    OpType_ModifyResource     = 8,
    OpType_DeleteResource     = 9,
    OpType_ApproveReset       = 10,
    OpType_DeleteUser         = 11,
    OpType_ModifyStudent      = 12,  /* 修改学生信息（含跨班级转班） */
    OpType_SupplementDeposit  = 13,  /* 定金补缴转全额 */
    OpType_DeleteStudent      = 14,  /* 删除学生报名记录 */
    OpType_RenewRegistration  = 15,  /* 续费延长上课时段 */
    OpType_ApproveRegistration = 16  /* 审批注册申请 */
};

struct OperationLog {
    int32_t id;
    OperationType op_type;
    std::string operator_name;
    std::string target_class;
    std::string target_student;
    std::string target_resource;
    std::string detail;
    std::string op_time;
};

struct LogQueryCondition {
    int32_t op_type;           /* -1=全部 */
    std::string start_time;    /* 空=不限 */
    std::string end_time;      /* 空=不限 */
    std::string class_name;    /* 空=不限 */
    std::string teacher_name;  /* 空=不限 */
    std::string student_name;  /* 空=不限 */
    std::string resource_name; /* 空=不限 */
    int32_t page;              /* 从1开始 */
    int32_t page_size;         /* 每页条数 */
};

#endif /* __LOG_TYPES_H__ */
