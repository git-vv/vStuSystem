#include "page_handler.h"
#include "utils.h"

PageHandler::PageHandler() {}

PageHandler::~PageHandler() {}

void PageHandler::RegisterRoutes(crow::SimpleApp& app) {
    CROW_ROUTE(app, "/")
    ([this]() {
        return HandleIndex();
    });

    CROW_ROUTE(app, "/registration")
    ([this]() {
        return HandleRegistration();
    });

    CROW_ROUTE(app, "/class_create")
    ([this]() {
        return HandleClassCreate();
    });

    CROW_ROUTE(app, "/class_manage")
    ([this]() {
        return HandleClassManage();
    });

    CROW_ROUTE(app, "/resource")
    ([this]() {
        return HandleResource();
    });

    CROW_ROUTE(app, "/admin")
    ([this]() {
        return HandleAdmin();
    });

    CROW_ROUTE(app, "/login")
    ([this]() {
        return HandleLogin();
    });

    CROW_ROUTE(app, "/register")
    ([this]() {
        return HandleRegisterPage();
    });

    CROW_ROUTE(app, "/preset")
    ([this]() {
        return HandlePreset();
    });

    CROW_ROUTE(app, "/credential")
    ([this]() {
        return HandleCredential();
    });

    CROW_ROUTE(app, "/account")
    ([this]() {
        return HandleAccount();
    });

    CROW_ROUTE(app, "/activity_manage")
    ([this]() {
        return HandleActivityManage();
    });

    CROW_ROUTE(app, "/data_transfer")
    ([this]() {
        return HandleDataTransfer();
    });

    CROW_ROUTE(app, "/api_manage")
    ([this]() {
        return HandleApiManage();
    });
}

crow::response PageHandler::HandleIndex() {
    LOG_INFO << "PageHandler: index page requested";
    auto page = crow::mustache::load("index.html");
    crow::response resp(200, page.render());
    resp.set_header("Content-Type", "text/html; charset=utf-8");
    resp.set_header("Cache-Control", "no-cache, no-store, must-revalidate");
    return resp;
}

crow::response PageHandler::HandleRegistration() {
    LOG_INFO << "PageHandler: registration page requested";
    auto page = crow::mustache::load("registration.html");
    crow::response resp(200, page.render());
    resp.set_header("Content-Type", "text/html; charset=utf-8");
    resp.set_header("Cache-Control", "no-cache, no-store, must-revalidate");
    return resp;
}

crow::response PageHandler::HandleClassCreate() {
    LOG_INFO << "PageHandler: class create page requested";
    auto page = crow::mustache::load("class_create.html");
    crow::response resp(200, page.render());
    resp.set_header("Content-Type", "text/html; charset=utf-8");
    resp.set_header("Cache-Control", "no-cache, no-store, must-revalidate");
    return resp;
}

crow::response PageHandler::HandleClassManage() {
    LOG_INFO << "PageHandler: class manage page requested";
    auto page = crow::mustache::load("class_manage.html");
    crow::response resp(200, page.render());
    resp.set_header("Content-Type", "text/html; charset=utf-8");
    resp.set_header("Cache-Control", "no-cache, no-store, must-revalidate");
    return resp;
}

crow::response PageHandler::HandleResource() {
    LOG_INFO << "PageHandler: resource page requested";
    auto page = crow::mustache::load("resource.html");
    crow::response resp(200, page.render());
    resp.set_header("Content-Type", "text/html; charset=utf-8");
    resp.set_header("Cache-Control", "no-cache, no-store, must-revalidate");
    return resp;
}

crow::response PageHandler::HandleAdmin() {
    LOG_INFO << "PageHandler: admin page requested";
    auto page = crow::mustache::load("admin.html");
    crow::response resp(200, page.render());
    resp.set_header("Content-Type", "text/html; charset=utf-8");
    resp.set_header("Cache-Control", "no-cache, no-store, must-revalidate");
    return resp;
}

crow::response PageHandler::HandleLogin() {
    LOG_INFO << "PageHandler: login page requested";
    auto page = crow::mustache::load("login.html");
    crow::response resp(200, page.render());
    resp.set_header("Content-Type", "text/html; charset=utf-8");
    resp.set_header("Cache-Control", "no-cache, no-store, must-revalidate");
    return resp;
}

crow::response PageHandler::HandleRegisterPage() {
    LOG_INFO << "PageHandler: register page requested";
    auto page = crow::mustache::load("register.html");
    crow::response resp(200, page.render());
    resp.set_header("Content-Type", "text/html; charset=utf-8");
    resp.set_header("Cache-Control", "no-cache, no-store, must-revalidate");
    return resp;
}

crow::response PageHandler::HandlePreset() {
    LOG_INFO << "PageHandler: preset page requested";
    auto page = crow::mustache::load("preset.html");
    crow::response resp(200, page.render());
    resp.set_header("Content-Type", "text/html; charset=utf-8");
    resp.set_header("Cache-Control", "no-cache, no-store, must-revalidate");
    return resp;
}

crow::response PageHandler::HandleCredential() {
    LOG_INFO << "PageHandler: credential page requested";
    auto page = crow::mustache::load("credential.html");
    crow::response resp(200, page.render());
    resp.set_header("Content-Type", "text/html; charset=utf-8");
    resp.set_header("Cache-Control", "no-cache, no-store, must-revalidate");
    return resp;
}

crow::response PageHandler::HandleAccount() {
    LOG_INFO << "PageHandler: account page requested";
    auto page = crow::mustache::load("account.html");
    crow::response resp(200, page.render());
    resp.set_header("Content-Type", "text/html; charset=utf-8");
    resp.set_header("Cache-Control", "no-cache, no-store, must-revalidate");
    return resp;
}

crow::response PageHandler::HandleActivity() {
    LOG_INFO << "PageHandler: activity page requested";
    auto page = crow::mustache::load("activity.html");
    crow::response resp(200, page.render());
    resp.set_header("Content-Type", "text/html; charset=utf-8");
    resp.set_header("Cache-Control", "no-cache, no-store, must-revalidate");
    return resp;
}

crow::response PageHandler::HandleActivityManage() {
    LOG_INFO << "PageHandler: activity manage page requested";
    auto page = crow::mustache::load("activity_manage.html");
    crow::response resp(200, page.render());
    resp.set_header("Content-Type", "text/html; charset=utf-8");
    resp.set_header("Cache-Control", "no-cache, no-store, must-revalidate");
    return resp;
}

crow::response PageHandler::HandleDataTransfer() {
    LOG_INFO << "PageHandler: data transfer page requested";
    auto page = crow::mustache::load("data_transfer.html");
    crow::response resp(200, page.render());
    resp.set_header("Content-Type", "text/html; charset=utf-8");
    resp.set_header("Cache-Control", "no-cache, no-store, must-revalidate");
    return resp;
}

crow::response PageHandler::HandleApiManage() {
    LOG_INFO << "PageHandler: api manage page requested";
    auto page = crow::mustache::load("api_manage.html");
    crow::response resp(200, page.render());
    resp.set_header("Content-Type", "text/html; charset=utf-8");
    resp.set_header("Cache-Control", "no-cache, no-store, must-revalidate");
    return resp;
}
