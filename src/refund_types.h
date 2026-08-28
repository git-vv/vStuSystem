#ifndef __REFUND_TYPES_H__
#define __REFUND_TYPES_H__

#include <string>
#include <cstdint>

/* 退费记录状态 */
enum RefundStatusType {
    RefundStatus_Active    = 0,  /* 有效 */
    RefundStatus_Cancelled = 1   /* 已撤销（软删除） */
};

/* 退费记录条目（对应 refund_record 表） */
struct RefundRecordInfo {
    int32_t id;                          /* 主键 */
    int32_t registration_id;             /* 外键引用 registration.id */
    double refund_amount;                /* 退费金额（元） */
    std::string operator_name;           /* 操作人 */
    std::string refund_time;             /* 操作时间 YYYY-MM-DD HH:MM:SS */
    RefundStatusType status;             /* 0=有效, 1=已撤销 */
    std::string cancel_operator_name;    /* 撤销时填，初始空 */
    std::string cancel_time;             /* 撤销时填，初始空 */
    /* 审计快照（折损计算依据，便于历史回溯） */
    double unit_price;                   /* 单位价 = 最高单人价 / 上课总天数 */
    int32_t total_class_days;            /* 上课总天数（去除周末） */
    int32_t attended_days;               /* 折损天数（已出勤天数，0 天保底 1） */
    double original_amount;              /* 原始报名金额（snapshot_amount） */
    double tolerance_used;               /* 实际放行的容差（默认 0.01 元） */
};

/* ComputeRefundCap 的返回结构 */
struct RefundCapResult {
    double paid_limit;                   /* 实缴上限 = original_amount - 累计未撤销退费 */
    double attendance_limit;             /* 考勤折损上限 = original_amount - unit_price × 折损天数 */
    double final_limit;                  /* 最终可退上限 = min(paid_limit, attendance_limit) + 0.01 容差 */
    double unit_price;                   /* 单位价 = 最高单人价 / 上课总天数 */
    int32_t total_class_days;            /* 上课总天数 */
    int32_t attended_days;               /* 已出勤天数（未做 max(.,1) 之前的原始值） */
    int32_t deduction_days;              /* 折损天数 = max(attended_days, 1) */
    double original_amount;              /* 原始报名金额 */
    double accumulated_refund;           /* 累计未撤销退费金额 */
    bool zero_attendance;                /* 是否 0 天出勤（决定错误消息分支） */
};

#endif /* __REFUND_TYPES_H__ */
