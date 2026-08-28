#ifndef __CLASS_MANAGE_HANDLER_H__
#define __CLASS_MANAGE_HANDLER_H__

#include "crow_safe.h"
#include "i_class_dao.h"
#include "i_registration_dao.h"
#include "i_resource_dao.h"
#include "i_attendance_dao.h"
#include "i_refund_dao.h"
#include "i_operation_log_dao.h"
#include "session_manager.h"
#include "refund_types.h"

class ClassManageHandler {
public:
    explicit ClassManageHandler(IClassDao* class_dao, IRegistrationDao* reg_dao,
                                IResourceDao* resource_dao, IAttendanceDao* attendance_dao,
                                IRefundDao* refund_dao, IOperationLogDao* log_dao,
                                SessionManager* session_mgr);
    ~ClassManageHandler();

    /* @brief 注册Crow路由 */
    void RegisterRoutes(crow::SimpleApp& app);

    /* @brief 修改学生基础信息或跨班级转班 */
    crow::response HandleUpdateStudent(const crow::request& req);

    /* @brief 发起退费 */
    crow::response HandleRefund(const crow::request& req);

    /* @brief 撤销退费 */
    crow::response HandleCancelRefund(const crow::request& req);

    /* @brief 定金补缴转全额 */
    crow::response HandleSupplement(const crow::request& req);

    /* @brief 删除学生报名记录（仅管理员） */
    crow::response HandleDeleteStudent(const crow::request& req);

    /* @brief 折算金额建议 API（部分时段报名/续费时后端单一来源计算） */
    crow::response HandleCalculateAmount(const crow::request& req);

    /* @brief 续费延长上课时段 */
    crow::response HandleRenew(const crow::request& req);

private:
    /* @brief 计算退费上限（实缴上限 vs 考勤折损上限取较小者 + 0.01 容差）
     * @param registration_id 报名记录 ID
     * @param original_amount 输出：原始报名金额
     * @param result 输出：上限计算结果
     * @return 0=成功, ERR_REGISTRATION_NOT_FOUND, ERR_INVALID_PARAM, ERR_DB_*=失败 */
    int ComputeRefundCap(int32_t registration_id, double& original_amount,
                         RefundCapResult& result);

private:
    /* @brief 查询班级列表 */
    crow::response HandleListClasses(const crow::request& req);

    /* @brief 查询班级详情 */
    crow::response HandleGetClassDetail(const crow::request& req);

    /* @brief 修改招生名额 */
    crow::response HandleUpdateEnrollment(const crow::request& req);

    /* @brief 查询班级学生列表 */
    crow::response HandleGetStudents(const crow::request& req);

    /* @brief 按时间段查询所有报名学生 */
    crow::response HandleQueryStudentsByTime(const crow::request& req);

    /* @brief 按时间段查询资源分配记录 */
    crow::response HandleQueryAllocationsByTime(const crow::request& req);

    /* @brief 查询班级学生详情 */
    crow::response HandleGetStudentDetail(const crow::request& req);

    /* @brief 提交考勤记录 */
    crow::response HandleSubmitAttendance(const crow::request& req);

    /* @brief 查询考勤记录 */
    crow::response HandleGetAttendance(const crow::request& req);

    /* @brief 分配资源 */
    crow::response HandleAllocateResource(const crow::request& req);

    /* @brief 查询本班级的资源分配记录 */
    crow::response HandleGetClassAllocations(const crow::request& req);

    /* @brief 编辑班级价位（不换 preset_id，原子） */
    crow::response HandleUpdateClassPrices(const crow::request& req);

    /* @brief 校验教师权限，成功返回0 */
    int CheckTeacherPermission(const crow::request& req, SessionInfo& info);

    /* @brief 校验管理员权限，成功返回0 */
    int CheckAdminPermission(const crow::request& req, SessionInfo& info);

    /* @brief 从Cookie中解析session_id */
    std::string GetSessionIdFromCookie(const crow::request& req);

    /* @brief 从URL查询参数中提取整数值 */
    int32_t ExtractIntParam(const crow::request& req, const std::string& key, int32_t default_val);

    IClassDao* class_dao_;
    IRegistrationDao* reg_dao_;
    IResourceDao* resource_dao_;
    IAttendanceDao* attendance_dao_;
    IRefundDao* refund_dao_;
    IOperationLogDao* log_dao_;
    SessionManager* session_mgr_;
};

#endif /* __CLASS_MANAGE_HANDLER_H__ */
