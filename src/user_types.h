#ifndef __USER_TYPES_H__
#define __USER_TYPES_H__

#include <string>
#include <cstdint>

enum UserRoleType {
    UserRole_Admin   = 0,
    UserRole_Teacher = 1
};

enum ResetRequestStatusType {
    ResetStatus_Pending  = 0,
    ResetStatus_Approved = 1,
    ResetStatus_Rejected = 2
};

enum RegistrationRequestStatusType {
    RegStatus_Pending  = 0,
    RegStatus_Approved = 1,
    RegStatus_Rejected = 2
};

struct UserInfo {
    int32_t id;
    std::string username;
    std::string password_hash;
    std::string salt;
    UserRoleType role;
    std::string display_name;
    std::string create_time;
};

struct PasswordResetRequest {
    int32_t id;
    int32_t user_id;
    std::string username;
    ResetRequestStatusType status;
    int32_t approver_id;
    std::string new_password_hash;
    std::string new_salt;
    std::string request_time;
    std::string approve_time;
};

struct RegistrationRequest {
    int32_t id;
    std::string username;
    std::string password_hash;
    std::string salt;
    UserRoleType role;
    std::string display_name;
    RegistrationRequestStatusType status;
    std::string request_time;
};

#endif /* __USER_TYPES_H__ */
