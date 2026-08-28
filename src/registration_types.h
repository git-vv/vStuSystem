#ifndef __REGISTRATION_TYPES_H__
#define __REGISTRATION_TYPES_H__

#include <string>
#include <cstdint>

struct RegistrationInfo {
    int32_t id;
    int32_t class_id;
    std::string student_name;
    std::string student_gender;  /* "male"/"female" */
    std::string parent_phone;
    int32_t has_allergy;         /* 0=否, 1=是 */
    std::string allergy_desc;
    int32_t price_id;            /* 报名方式对应的价位ID */
    int32_t need_bed;            /* 0=否, 1=是 */
    std::string teacher_name;
    std::string other_info;
    std::string register_time;
    double paid_amount;          /* 退费后实缴金额 = paid_amount_snapshot - 累计未撤销退费；Dao LEFT JOIN 填充，默认 0 */
    double refund_amount;        /* 累计未撤销退费金额；Dao LEFT JOIN 填充，默认 0 */
    int32_t is_deposit;            /* 0=全额报名, 1=定金报名；默认 0 */
    double paid_amount_snapshot;   /* 已付基准金额：定金=定金金额, 全额=预设全额, 补缴后=目标全额；退费/查询统一取此值 */
    double supplement_amount;      /* 补缴金额；0=未补缴 */
    int32_t supplement_preset_id;  /* 补缴目标预设 id；-1=未补缴 */
    std::string supplement_operator; /* 补缴操作人 */
    std::string supplement_time;   /* 补缴时间（YYYY-MM-DD HH:MM:SS） */
    std::string student_start_date;  /* 学生上课开始日期（YYYY-MM-DD），空=班级完整时段 */
    std::string student_end_date;    /* 学生上课结束日期（YYYY-MM-DD），空=班级完整时段 */
    double enrollment_ratio = 1.0;     /* 名额占用比例 = 学生上课工作日数 / 班级总工作日数；默认 1.0 */
};

struct RenewalRecordInfo {
    int32_t id;
    int32_t registration_id;
    std::string old_end_date;    /* 原结束日期 */
    std::string new_end_date;    /* 新结束日期 */
    double renew_amount;         /* 续费金额，元 */
    std::string operator_name;   /* 操作人 */
    std::string renew_time;      /* 操作时间 YYYY-MM-DD HH:MM:SS */
};

#endif /* __REGISTRATION_TYPES_H__ */
