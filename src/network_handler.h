#ifndef __NETWORK_HANDLER_H__
#define __NETWORK_HANDLER_H__

#include "crow_safe.h"

class NetworkHandler {
public:
    NetworkHandler(uint16_t port, const std::string& domain);
    ~NetworkHandler();

    /* @brief 注册Crow路由 */
    void RegisterRoutes(crow::SimpleApp& app);

    /* @brief 获取本机网络信息（public for unit test access） */
    crow::response HandleGetNetworkInfo();

private:
    uint16_t port_;
    std::string domain_;
};

#endif /* __NETWORK_HANDLER_H__ */
