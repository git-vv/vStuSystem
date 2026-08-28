#ifndef __I_REGISTRATION_DAO_H__
#define __I_REGISTRATION_DAO_H__

#include "registration_types.h"
#include <vector>

class IRegistrationDao {
public:
    virtual ~IRegistrationDao() {}

    virtual int InsertRegistration(const RegistrationInfo& info) = 0;
    virtual int QueryRegistrationById(int32_t id, RegistrationInfo& info) = 0;
    virtual int QueryRegistrationsByClassId(int32_t class_id, std::vector<RegistrationInfo>& regs) = 0;
    virtual int QueryRegistrationsByTimeRange(const std::string& start_time, const std::string& end_time, std::vector<RegistrationInfo>& regs) = 0;
    virtual int CountEnrolledByClassId(int32_t class_id) = 0;
    virtual int CheckEnrollmentAvailable(int32_t class_id, int32_t capacity) = 0;

    /**
     * @brief 原子报名：检查余量+插入报名+增加已招人数+增加床位占用
     * @param info 报名信息
     * @param class_id 班级ID
     * @param capacity 班级招生容量
     * @param need_bed 是否需要床位
     * @param bed_resource_id 床位资源ID（need_bed=1时使用，否则传-1）
     * @return 0=成功, ERR_CLASS_ENROLLMENT_FULL=满, ERR_RESOURCE_BED_UNAVAILABLE=床位不足, 其他错误码
     */
    virtual int RegisterStudentAtomic(const RegistrationInfo& info, int32_t class_id,
                                      int32_t capacity, int32_t need_bed,
                                      int32_t bed_resource_id) = 0;

    /**
     * @brief 批量原子报名：单事务内为多个学生依次执行余量+床位校验+插入+计数
     * @param infos 多个学生报名信息，class_id 必须一致，每学生独立 need_bed
     * @param class_id 班级ID
     * @param capacity 班级招生容量
     * @param bed_resource_id 床位资源ID（无床位报名学生时传 -1 不触发床位分支）
     * @return 0=成功, ERR_CLASS_ENROLLMENT_FULL=名额不足, ERR_RESOURCE_BED_UNAVAILABLE=床位不足, ERR_INVALID_PARAM=空列表, 其他=失败
     */
    virtual int RegisterStudentsBatchAtomic(const std::vector<RegistrationInfo>& infos,
                                            int32_t class_id, int32_t capacity,
                                            int32_t bed_resource_id) = 0;

    /**
     * @brief 修改学生基础信息（不含 class_id、price_id、need_bed、register_time）
     * @param info 含 id 与待更新字段值的报名信息
     * @return 0=成功, ERR_REGISTRATION_NOT_FOUND=id 不存在, ERR_DB_*=失败
     */
    virtual int UpdateStudentBasicInfo(const RegistrationInfo& info) = 0;

    /**
     * @brief 跨班级转班原子事务：原班级减名额、新班级加名额、registration.class_id 更新
     * @param registration_id 报名记录 ID
     * @param old_class_id 原班级 ID
     * @param new_class_id 新班级 ID
     * @param new_class_capacity 新班级招生容量（用于余量校验）
     * @return 0=成功, ERR_CLASS_ENROLLMENT_FULL=新班级已满, ERR_REGISTRATION_NOT_FOUND=报名记录不存在, ERR_DB_*=失败
     */
    virtual int TransferClassAtomic(int32_t registration_id, int32_t old_class_id,
                                    int32_t new_class_id, int32_t new_class_capacity) = 0;

    /**
     * @brief 定金批量报名原子事务：单事务余量校验+循环每学生定金金额校验+插入(is_deposit=1,
     *        paid_amount_snapshot=定金金额)+enrollment_used+++床位预留。复用 Internal helper，不改动
     *        RegisterStudentsBatchAtomic（遵循 OCP）。
     * @param infos 多个学生报名信息，class_id 必须一致，每学生 paid_amount_snapshot 字段存该学生定金金额
     * @param class_id 班级ID
     * @param capacity 班级招生容量
     * @param bed_resource_id 床位资源ID（无床位学生传 -1）
     * @return 0=成功, ERR_CLASS_ENROLLMENT_FULL=名额不足, ERR_RESOURCE_BED_UNAVAILABLE=床位不足,
     *         ERR_INVALID_PARAM=空列表, 其他=失败
     */
    virtual int RegisterDepositAtomic(const std::vector<RegistrationInfo>& infos,
                                      int32_t class_id, int32_t capacity,
                                      int32_t bed_resource_id) = 0;

    /**
     * @brief 补缴转全额原子事务：单事务重读定金状态(防并发重复补缴)+校验目标预设属于班级+
     *        校验目标全额>已付定金+更新 registration(is_deposit=0, paid_amount_snapshot=目标全额,
     *        price_id=目标class_price.id, supplement_*审计字段)。
     * @param registration_id 报名记录ID
     * @param target_class_price_id 目标预设对应的 class_price.id（由 Handler 从班级预设下拉解析）
     * @param target_preset_id 目标预设 id（审计用，写入 supplement_preset_id）
     * @param target_amount 目标预设全额（= class_price.snapshot_amount，由 Handler 传入避免 Dao 再查）
     * @param operator_name 操作人
     * @param operate_time 操作时间
     * @param out_supplement_amount 输出：补缴金额 = target_amount - 原已付定金
     * @return 0=成功, ERR_REGISTRATION_NOT_FOUND=记录不存在, ERR_SUPPLEMENT_ALREADY_DONE=已全额,
     *         ERR_SUPPLEMENT_PRESET_NOT_IN_CLASS=目标预设不属于班级, ERR_SUPPLEMENT_AMOUNT_INVALID=目标全额<=已付,
     *         ERR_DB_*=失败
     */
    virtual int SupplementDepositAtomic(int32_t registration_id,
                                        int32_t target_class_price_id,
                                        int32_t target_preset_id,
                                        double target_amount,
                                        const std::string& operator_name,
                                        const std::string& operate_time,
                                        double& out_supplement_amount) = 0;

    /**
     * @brief 原子删除学生：单事务内级联删除报名记录+考勤+资源分配+退费，
     *        同步减 enrollment_used，释放已分配资源(used_count-1/remain_count+1)，
     *        need_bed=1时减bed_reserved_count
     * @param registration_id 报名记录ID
     * @param bed_resource_id 床位资源ID（need_bed=1时使用，否则传-1）
     * @return 0=成功, ERR_REGISTRATION_NOT_FOUND=记录不存在, ERR_DELETE_STUDENT_FAILED=事务失败
     */
    virtual int DeleteRegistrationAtomic(int32_t registration_id, int32_t bed_resource_id) = 0;

    /**
     * @brief 续费原子事务：BEGIN IMMEDIATE -> 重读确认 student_end_date 未变 ->
     *        更新 student_end_date -> 更新 paid_amount_snapshot += renew_amount ->
     *        更新 enrollment_used 增量 -> 插入 renewal_record -> COMMIT/ROLLBACK
     * @param registration_id 报名记录 ID
     * @param new_end_date 新结束日期（YYYY-MM-DD）
     * @param renew_amount 续费金额（>=0）
     * @param enrollment_delta 名额增量 = (新时段工作日数 - 原时段工作日数) / 班级总工作日数
     * @param operator_name 操作人
     * @param operate_time 操作时间
     * @return DB_OK=成功, ERR_REGISTRATION_NOT_FOUND, ERR_RENEWAL_DATE_INVALID, ERR_DB_*=失败
     */
    virtual int RenewRegistrationAtomic(int32_t registration_id,
                                        const std::string& new_end_date,
                                        double renew_amount,
                                        double enrollment_delta,
                                        const std::string& operator_name,
                                        const std::string& operate_time) = 0;

    /**
     * @brief 查询班级已用名额（REAL），用于余量校验
     * @param class_id 班级 ID
     * @return 已用名额（浮点数），<0 表示查询失败
     */
    virtual double QueryEnrollmentUsedByClassId(int32_t class_id) = 0;

    /**
     * @brief 查询班级当前活跃学生人数（基于当前日期）
     * @param class_id 班级 ID
     * @return 活跃学生人数（整数），<0 表示查询失败
     */
    virtual int CountActiveStudentsByClassId(int32_t class_id) = 0;

    /**
     * @brief 查询续费记录（审计追溯）
     * @param registration_id 报名记录 ID
     * @param records 输出续费记录列表
     * @return DB_OK=成功, ERR_DB_*=失败
     */
    virtual int QueryRenewalsByRegId(int32_t registration_id,
                                     std::vector<RenewalRecordInfo>& records) = 0;
};

#endif /* __I_REGISTRATION_DAO_H__ */
