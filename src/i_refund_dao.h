#ifndef __I_REFUND_DAO_H__
#define __I_REFUND_DAO_H__

#include "refund_types.h"
#include <vector>

class IRefundDao {
public:
    virtual ~IRefundDao() {}

    /**
     * @brief 原子退费：单事务 BEGIN IMMEDIATE → 事务内重读累计退费 → 再次校验 → 插入 refund_record → COMMIT/ROLLBACK
     * @param info 退费记录（含 registration_id、refund_amount、operator_name、refund_time、审计快照）
     * @param original_amount 原始报名金额（用于事务内重读后再次校验上限）
     * @param tolerance 容差（默认 0.01 元，多给用户策略）
     * @param skip_attendance_check 管理员跳过考勤折损上限校验（仅校验 paid_limit）
     * @return DB_OK=成功, ERR_REFUND_EXCEEDS_PAID=超实缴上限, ERR_DB_*=失败
     */
    virtual int InsertRefundAtomic(RefundRecordInfo& info,
                                   double original_amount,
                                   double tolerance,
                                   bool skip_attendance_check = false) = 0;

    /**
     * @brief 原子撤销退费：UPDATE 最近一条 status=0 的记录为 status=1 + 填 cancel 字段
     * @param registration_id 报名记录 ID
     * @param cancel_operator_name 撤销操作人
     * @param cancel_time 撤销时间
     * @param restored_paid_amount 输出：撤销后实缴金额（= original_amount - 新累计未撤销）
     * @return DB_OK=成功, ERR_REFUND_NOT_FOUND=无未撤销退费, ERR_DB_*=失败
     */
    virtual int CancelRefundAtomic(int32_t registration_id,
                                   const std::string& cancel_operator_name,
                                   const std::string& cancel_time,
                                   double& restored_paid_amount) = 0;

    /**
     * @brief 查指定 registration_id 的所有退费记录（含已撤销），按时间倒序
     */
    virtual int QueryRefundsByRegId(int32_t registration_id,
                                    std::vector<RefundRecordInfo>& records) = 0;

    /**
     * @brief 查指定 registration_id 的所有 status=0 退费金额累计（SQL SUM 聚合）
     * @param registration_id 报名记录 ID
     * @param sum 输出：累计未撤销退费金额
     * @return DB_OK=成功, ERR_DB_*=失败
     */
    virtual int QueryActiveRefundSumByRegId(int32_t registration_id,
                                            double& sum) = 0;
};

#endif /* __I_REFUND_DAO_H__ */
