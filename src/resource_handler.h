#ifndef __RESOURCE_HANDLER_H__
#define __RESOURCE_HANDLER_H__

#include "crow_safe.h"
#include "i_resource_dao.h"
#include "i_operation_log_dao.h"
#include "session_manager.h"

class ResourceHandler {
public:
    explicit ResourceHandler(IResourceDao* resource_dao, IOperationLogDao* log_dao, SessionManager* session_mgr);
    ~ResourceHandler();

    void RegisterRoutes(crow::SimpleApp& app);

private:
    crow::response HandleListResources();
    crow::response HandleCreateResource(const crow::request& req);
    crow::response HandleUpdateResource(const crow::request& req);
    crow::response HandleDeleteResource(const crow::request& req);
    crow::response HandleGetResourceUsage(const crow::request& req);

    int CheckAdminPermission(const crow::request& req, SessionInfo& info);
    std::string GetSessionIdFromCookie(const crow::request& req);
    int32_t ExtractIntParam(const crow::request& req, const std::string& key, int32_t default_val);

    IResourceDao* resource_dao_;
    IOperationLogDao* log_dao_;
    SessionManager* session_mgr_;
};

#endif /* __RESOURCE_HANDLER_H__ */
