#ifndef __REGISTRATION_HANDLER_H__
#define __REGISTRATION_HANDLER_H__

#include "crow_safe.h"
#include "i_class_dao.h"
#include "i_registration_dao.h"
#include "i_resource_dao.h"
#include "i_operation_log_dao.h"
#include "session_manager.h"

class RegistrationHandler {
public:
    explicit RegistrationHandler(IClassDao* class_dao, IRegistrationDao* reg_dao,
                                 IResourceDao* resource_dao, IOperationLogDao* log_dao,
                                 SessionManager* session_mgr);
    ~RegistrationHandler();

    void RegisterRoutes(crow::SimpleApp& app);

    /* @brief 报名（支持全额/定金两种方式，定金方式 is_deposit=1 + deposit_amount） */
    crow::response HandleRegister(const crow::request& req);

private:
    crow::response HandleListClasses();
    crow::response HandleGetClassDetail(const crow::request& req);

    /* @brief 从Cookie中解析session_id */
    std::string GetSessionIdFromCookie(const crow::request& req);

    IClassDao* class_dao_;
    IRegistrationDao* reg_dao_;
    IResourceDao* resource_dao_;
    IOperationLogDao* log_dao_;
    SessionManager* session_mgr_;
};

#endif /* __REGISTRATION_HANDLER_H__ */
