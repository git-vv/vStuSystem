#ifndef __I_USER_DAO_H__
#define __I_USER_DAO_H__

#include "user_types.h"
#include <vector>

class IUserDao {
public:
    virtual ~IUserDao() {}

    /**
     * @brief 插入用户
     * @param info 用户信息
     * @return 0=成功, 错误码=失败
     */
    virtual int InsertUser(const UserInfo& info) = 0;

    /**
     * @brief 按用户名查询用户
     * @param username 用户名
     * @param info 输出用户信息
     * @return 0=成功, 错误码=失败
     */
    virtual int QueryUserByUsername(const std::string& username, UserInfo& info) = 0;

    /**
     * @brief 按ID查询用户
     * @param id 用户ID
     * @param info 输出用户信息
     * @return 0=成功, 错误码=失败
     */
    virtual int QueryUserById(int32_t id, UserInfo& info) = 0;

    /**
     * @brief 更新密码
     * @param user_id 用户ID
     * @param password_hash 新密码哈希
     * @param salt 新salt
     * @return 0=成功, 错误码=失败
     */
    virtual int UpdatePassword(int32_t user_id, const std::string& password_hash, const std::string& salt) = 0;

    /**
     * @brief 更新用户信息
     * @param user_id 用户ID
     * @param display_name 显示名称
     * @return 0=成功, 错误码=失败
     */
    virtual int UpdateUserInfo(int32_t user_id, const std::string& display_name) = 0;

    /**
     * @brief 删除用户
     * @param user_id 用户ID
     * @return 0=成功, 错误码=失败
     */
    virtual int DeleteUser(int32_t user_id) = 0;

    /**
     * @brief 检查管理员是否存在
     * @return 0=不存在, 1=存在
     */
    virtual int CheckAdminExists() = 0;

    /**
     * @brief 查询所有教师
     * @param teachers 输出教师列表
     * @return 0=成功, 错误码=失败
     */
    virtual int QueryAllTeachers(std::vector<UserInfo>& teachers) = 0;

    /**
     * @brief 插入密码重置请求
     * @param req 重置请求信息
     * @return 0=成功, 错误码=失败
     */
    virtual int InsertResetRequest(const PasswordResetRequest& req) = 0;

    /**
     * @brief 查询待审批的重置请求
     * @param requests 输出请求列表
     * @return 0=成功, 错误码=失败
     */
    virtual int QueryPendingResetRequests(std::vector<PasswordResetRequest>& requests) = 0;

    /**
     * @brief 审批重置请求
     * @param request_id 请求ID
     * @param approver_id 审批人ID
     * @param new_hash 新密码哈希
     * @param new_salt 新salt
     * @return 0=成功, 错误码=失败
     */
    virtual int ApproveResetRequest(int32_t request_id, int32_t approver_id, const std::string& new_hash, const std::string& new_salt) = 0;

    /**
     * @brief 检查用户是否有待审批的重置请求
     * @param user_id 用户ID
     * @return 0=无, 1=有
     */
    virtual int CheckResetPending(int32_t user_id) = 0;

    /**
     * @brief 插入注册审核申请
     * @param request 申请信息
     * @return 0=成功, 错误码=失败
     */
    virtual int InsertRegistrationRequest(const RegistrationRequest& request) = 0;

    /**
     * @brief 查询用户名在注册审核表中的状态
     * @param username 用户名
     * @param status 输出审核状态
     * @return 0=找到, 非0=未找到或失败
     */
    virtual int QueryRegistrationRequestStatus(const std::string& username, int& status) = 0;

    /**
     * @brief 查询所有待审核的注册申请
     * @param requests 输出申请列表
     * @return 0=成功, 错误码=失败
     */
    virtual int QueryPendingRegistrationRequests(std::vector<RegistrationRequest>& requests) = 0;

    /**
     * @brief 按ID查询注册审核申请
     * @param id 申请ID
     * @param request 输出申请信息
     * @return 0=成功, 错误码=失败
     */
    virtual int QueryRegistrationRequestById(int32_t id, RegistrationRequest& request) = 0;

    /**
     * @brief 更新注册审核申请状态
     * @param id 申请ID
     * @param status 新状态
     * @return 0=成功, 错误码=失败
     */
    virtual int UpdateRegistrationRequestStatus(int32_t id, RegistrationRequestStatusType status) = 0;

    /**
     * @brief 删除注册审核申请
     * @param id 申请ID
     * @return 0=成功, 错误码=失败
     */
    virtual int DeleteRegistrationRequest(int32_t id) = 0;

    /**
     * @brief 检查用户名是否在注册审核表中（待审核状态）
     * @param username 用户名
     * @return 0=不存在, 1=存在
     */
    virtual int CheckRegistrationRequestExists(const std::string& username) = 0;

    /**
     * @brief 批量审核通过注册申请（事务原子操作）
     * @param ids 申请ID数组
     * @return 0=成功, 错误码=失败
     */
    virtual int ApproveRegistrationRequestsAtomic(const std::vector<int32_t>& ids) = 0;
};

#endif /* __I_USER_DAO_H__ */
