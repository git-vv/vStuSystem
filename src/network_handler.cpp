#include "network_handler.h"
#include "utils.h"
#include "error_codes.h"

NetworkHandler::NetworkHandler(uint16_t port, const std::string& domain)
    : port_(port), domain_(domain) {}

NetworkHandler::~NetworkHandler() {}

void NetworkHandler::RegisterRoutes(crow::SimpleApp& app) {
    CROW_ROUTE(app, "/api/network/info").methods("GET"_method)
    ([this]() {
        return HandleGetNetworkInfo();
    });
}

crow::response NetworkHandler::HandleGetNetworkInfo() {
    LOG_INFO << "NetworkHandler: network info requested";

    register_student::NetworkInfo info = register_student::GetLocalNetworkInfo();

    crow::json::wvalue resp;
    crow::json::wvalue data;

    if (info.ipv4.empty()) {
        resp["code"] = ERR_NETWORK_INFO_UNAVAILABLE;
        resp["message"] = "Network info unavailable";
        data["ipv4"] = "";
        data["ipv6"] = "";
        data["mac"] = "";
        data["hostname"] = "";
        data["adapter"] = "";
        data["port"] = 0;
        data["domain"] = domain_;
        resp["data"] = std::move(data);
        return crow::response(200, crow::json::dump(resp));
    }

    resp["code"] = 0;
    resp["message"] = "success";
    data["ipv4"] = info.ipv4;
    data["ipv6"] = info.ipv6;
    data["mac"] = info.mac;
    data["hostname"] = info.hostname;
    data["adapter"] = info.adapter;
    data["port"] = static_cast<int>(port_);
    data["domain"] = domain_;
    resp["data"] = std::move(data);
    return crow::response(200, crow::json::dump(resp));
}
