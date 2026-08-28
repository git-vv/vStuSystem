#include "crow_safe.h"
#include "utils.h"
#include "config.h"
#include "error_codes.h"
#include "page_handler.h"

/* URL解码辅助函数 */
static std::string UrlDecode(const std::string& str) {
    std::string result;
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '%' && i + 2 < str.size()) {
            char hex[3] = {str[i + 1], str[i + 2], 0};
            char* end = nullptr;
            long val = strtol(hex, &end, 16);
            if (end != hex) {
                result += static_cast<char>(val);
                i += 2;
                continue;
            }
        }
        result += str[i];
    }
    return result;
}
#include "auth_handler.h"
#include "admin_handler.h"
#include "registration_handler.h"
#include "class_create_handler.h"
#include "class_manage_handler.h"
#include "resource_handler.h"
#include "network_handler.h"
#include "activity_handler.h"
#include "activity_manage_handler.h"
#include "data_transfer_handler.h"
#include "sqlite_database.h"
#include "sqlite_log_database.h"
#include "session_manager.h"
#include "group_session_manager.h"

#include <fstream>
#include <sstream>
#include <string>
#include <csignal>
#include <cstdlib>
#include <thread>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#endif

static void SignalHandler(int signum) {
    LOG_INFO << "Received signal " << signum << ", exiting...";
    _exit(0);
}

static std::string ReadFile(const std::string& path) {
    /* 必须以二进制模式打开：Windows 文本模式会将 0x1A (Ctrl-Z) 解析为 EOF，
       导致 JPEG/PNG 等二进制文件被截断（如本例 JPEG 在 778 字节处出现 0x1A） */
#ifdef _WIN32
    /* Windows: ifstream 默认用 ANSI(GBK) 解析路径，UTF-8 中文路径会打开失败。
       转 wide string 后用 MSVC 的 wide-char ifstream 扩展 */
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wlen <= 0) { return ""; }
    std::wstring wpath(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wpath[0], wlen);
    if (wlen > 0 && wpath[wlen - 1] == 0) { wpath.pop_back(); }
    std::ifstream ifs(wpath, std::ios::binary);
#else
    std::ifstream ifs(path, std::ios::binary);
#endif
    if (!ifs.is_open()) {
        return "";
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

static std::string GetMimeType(const std::string& path) {
    if (path.find(".css") != std::string::npos) {
        return "text/css";
    }
    if (path.find(".js") != std::string::npos) {
        return "application/javascript";
    }
    if (path.find(".png") != std::string::npos) {
        return "image/png";
    }
    if (path.find(".jpg") != std::string::npos || path.find(".jpeg") != std::string::npos) {
        return "image/jpeg";
    }
    if (path.find(".bmp") != std::string::npos) {
        return "image/bmp";
    }
    return "application/octet-stream";
}

static void EnsureSSLCerts(register_student::Config& config) {
    if (!config.ssl_enabled) return;

    std::ifstream cert_check(config.ssl_cert);
    std::ifstream key_check(config.ssl_key);
    if (cert_check.good() && key_check.good()) return;
    cert_check.close();
    key_check.close();

    size_t pos = config.ssl_cert.find_last_of("/\\");
    if (pos != std::string::npos) {
        std::string dir = config.ssl_cert.substr(0, pos);
#ifdef _WIN32
        _mkdir(dir.c_str());
#else
        mkdir(dir.c_str(), 0755);
#endif
    }

    LOG_WARNING << "SSL cert/key not found, generating self-signed certificate...";
    std::string cmd = "openssl req -x509 -newkey rsa:2048"
        " -keyout \"" + config.ssl_key + "\""
        " -out \"" + config.ssl_cert + "\""
        " -days 365 -nodes"
        " -subj \"/CN=vStuSystem\"";
#ifdef _WIN32
    cmd += " 2>nul";
#else
    cmd += " 2>/dev/null";
#endif
    int ret = system(cmd.c_str());
    if (ret != 0) {
        LOG_ERROR << "Failed to generate self-signed certificate, disabling SSL";
        config.ssl_enabled = false;
    } else {
        LOG_INFO << "Self-signed certificate generated: " << config.ssl_cert;
    }
}

int main(int argc, char** argv) {
    /* 默认配置文件路径，可通过命令行参数 -c/--conf 覆盖 */
    std::string conf_path = "conf/register_student.conf";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-c" || arg == "--conf") && i + 1 < argc) {
            conf_path = argv[++i];
        } else if (arg.rfind("--conf=", 0) == 0) {
            conf_path = arg.substr(7);
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [-c|--conf <config_path>]\n"
                      << "  -c, --conf <path>  Path to config file (default: conf/register_student.conf)\n"
                      << "  -h, --help         Show this help message\n";
            return 0;
        }
    }

    register_student::Config config = register_student::LoadConfig(conf_path);
    register_student::InitLog(config.log_path.c_str(), config.log_max_size, config.log_max_files);

    /* 校验上传路径配置 */
    if (config.upload_path.empty()) {
        LOG_ERROR << "upload_path is not configured, service cannot start";
        return 1;
    }

    /* 创建必要目录 */
#ifdef _WIN32
    _mkdir("./data");
    _mkdir(config.upload_path.c_str());
    _mkdir("./logs");
#else
    mkdir("./data", 0755);
    mkdir(config.upload_path.c_str(), 0755);
    mkdir("./logs", 0755);
#endif

    /* 确保SSL证书存在 */
    EnsureSSLCerts(config);

    /* 打开业务数据库 */
    SqliteDatabase db;
    int ret = db.Open(config.db_path);
    if (ret != DB_OK) {
        LOG_ERROR << "Failed to open business database: " << config.db_path;
        return 1;
    }

    /* 打开日志数据库 */
    SqliteLogDatabase log_db;
    ret = log_db.Open(config.log_db_path);
    if (ret != DB_OK) {
        LOG_ERROR << "Failed to open log database: " << config.log_db_path;
        return 1;
    }

    /* 创建会话管理器 */
    SessionManager session_mgr;

    /* 创建拼团会话管理器 */
    GroupSessionManager group_session_mgr;
    {
        std::string session_base = config.db_path;
        size_t last_sep = session_base.find_last_of("/\\");
        if (last_sep != std::string::npos) {
            session_base = session_base.substr(0, last_sep);
        }
        int gs_ret = group_session_mgr.Init(session_base);
        if (gs_ret != DB_OK) {
            LOG_ERROR << "Failed to init GroupSessionManager, base=" << session_base;
            return 1;
        }
    }

    /* 启动时清理孤立拼团会话 */
    {
        std::vector<ActivityInfo> acts;
        if (db.ListActivities(acts) == DB_OK) {
            for (size_t i = 0; i < acts.size(); ++i) {
                if (acts[i].status == 1) {
                    int64_t aid = acts[i].id;
                    group_session_mgr.CleanupOrphans(aid,
                        [&db, aid](const std::string& n, const std::string& p) -> bool {
                            bool exists = false;
                            db.CheckDuplicateSignup(aid, n, p, exists);
                            return exists;
                        });
                }
            }
        }
    }

    crow::SimpleApp app;

    /* 注册信号处理，支持 kill -15 (SIGTERM) 和 Ctrl+C (SIGINT) 优雅退出 */
    signal(SIGTERM, SignalHandler);
    signal(SIGINT, SignalHandler);

    /* 设置模板目录 */
    crow::mustache::set_base("./webui/templates/");

    /* 页面路由 */
    PageHandler page_handler;
    page_handler.RegisterRoutes(app);

    /* 认证Handler */
    AuthHandler auth_handler(&db, &log_db, &session_mgr);
    auth_handler.RegisterRoutes(app);

    /* 超级管理员Handler */
    AdminHandler admin_handler(&db, &log_db, &session_mgr);
    admin_handler.RegisterRoutes(app);

    /* 创建班级Handler */
    ClassCreateHandler class_create_handler(&db, &log_db, &session_mgr);
    class_create_handler.RegisterRoutes(app);

    /* 班级管理Handler */
    ClassManageHandler class_manage_handler(&db, &db, &db, &db, &db, &log_db, &session_mgr);
    class_manage_handler.RegisterRoutes(app);

    /* 报名缴费Handler */
    RegistrationHandler registration_handler(&db, &db, &db, &log_db, &session_mgr);
    registration_handler.RegisterRoutes(app);

    /* 资源管理Handler */
    ResourceHandler resource_handler(&db, &log_db, &session_mgr);
    resource_handler.RegisterRoutes(app);

    /* 网络信息Handler */
    NetworkHandler network_handler(config.port, config.domain);
    network_handler.RegisterRoutes(app);

    /* 活动管理Handler（需认证，挂载在主应用） */
    ActivityManageHandler activity_manage_handler(&db, &db, &db, &session_mgr,
                                                  &group_session_mgr,
                                                  config.upload_path);
    activity_manage_handler.RegisterRoutes(app);

    /* 数据管理Handler（需管理员认证） */
    DataTransferHandler data_transfer_handler(&db, &session_mgr,
                                              config.upload_path);
    data_transfer_handler.RegisterRoutes(app);

    CROW_ROUTE(app, "/favicon.ico")
    ([]() {
        return crow::response(204);
    });

    /* 静态资源服务 */
    CROW_ROUTE(app, "/static/<string>/<string>")
    ([](const crow::request&, std::string dir, std::string filename) {
        /* 白名单校验：仅允许css/js/images/uploads目录 */
        if (dir != "css" && dir != "js" && dir != "images" && dir != "uploads") {
            return crow::response(403, "Forbidden");
        }
        /* 路径遍历校验：禁止..路径段 */
        if (dir.find("..") != std::string::npos || filename.find("..") != std::string::npos) {
            return crow::response(403, "Forbidden");
        }
        /* uploads目录映射到data/uploads，其他映射到webui/static */
        std::string decoded_filename = UrlDecode(filename);
        std::string path;
        if (dir == "uploads") {
            path = "./data/uploads/" + decoded_filename;
        } else {
            path = "./webui/static/" + dir + "/" + decoded_filename;
        }
        std::string content = ReadFile(path);
        if (content.empty()) {
            return crow::response(404, "File not found");
        }
        crow::response resp(200, content);
        resp.set_header("Content-Type", GetMimeType(filename));
        resp.set_header("Cache-Control", "no-cache, no-store, must-revalidate");
        return resp;
    });

    /* 公开活动服务（独立端口 8000，网络隔离） */
    const uint16_t public_port = 8000;
    crow::SimpleApp public_app;
    ActivityHandler activity_handler(&db, &db, &db, &group_session_mgr);
    activity_handler.RegisterRoutes(public_app);

    CROW_ROUTE(public_app, "/favicon.ico")
    ([]() {
        return crow::response(204);
    });

    CROW_ROUTE(public_app, "/activity")
    ([]() {
        auto page = crow::mustache::load("activity.html");
        crow::response resp(200, page.render());
        resp.set_header("Content-Type", "text/html; charset=utf-8");
        resp.set_header("Cache-Control", "no-cache, no-store, must-revalidate");
        return resp;
    });

    CROW_ROUTE(public_app, "/static/<string>/<string>")
    ([](const crow::request&, std::string dir, std::string filename) {
        if (dir != "css" && dir != "js" && dir != "images" && dir != "uploads") {
            return crow::response(403, "Forbidden");
        }
        if (dir.find("..") != std::string::npos || filename.find("..") != std::string::npos) {
            return crow::response(403, "Forbidden");
        }
        std::string decoded_filename = UrlDecode(filename);
        std::string path;
        if (dir == "uploads") {
            path = "./data/uploads/" + decoded_filename;
        } else {
            path = "./webui/static/" + dir + "/" + decoded_filename;
        }
        std::string content = ReadFile(path);
        if (content.empty()) {
            return crow::response(404, "File not found");
        }
        crow::response resp(200, content);
        resp.set_header("Content-Type", GetMimeType(filename));
        resp.set_header("Cache-Control", "no-cache, no-store, must-revalidate");
        return resp;
    });

    std::thread public_thread([&]() {
        if (config.ssl_enabled) {
            public_app.ssl_file(config.ssl_cert, config.ssl_key);
        }
        LOG_INFO << "Public activity server starting on port " << public_port
                 << (config.ssl_enabled ? " (HTTPS)" : " (HTTP)");
        public_app.port(public_port).multithreaded().run();
    });

    std::string protocol = config.ssl_enabled ? "https" : "http";
    LOG_INFO << "Server starting on port " << config.port
             << (config.ssl_enabled ? " (HTTPS)" : " (HTTP)");
    LOG_INFO << "Credential page: " << protocol << "://127.0.0.1:" << config.port << "/credential";
    if (config.ssl_enabled) {
        app.ssl_file(config.ssl_cert, config.ssl_key);
    }
    app.port(config.port).multithreaded().run();

    public_app.stop();
    if (public_thread.joinable()) { public_thread.join(); }

    /* 关闭数据库 */
    db.Close();
    log_db.Close();

    return 0;
}

/* ===================================================================
 * Windows 入口：WinMain + 系统托盘 + Crow 工作线程 + 优雅退出
 * ===================================================================
 * 设计要点：
 *   1. 不提取共享初始化函数（按 D-9 确认），WinMain 内独立复制初始化代码
 *   2. 主线程跑 Windows 消息循环接收托盘事件
 *   3. Crow app.run() 在工作线程中阻塞
 *   4. "退出"菜单通过 PostMessage(WM_APP_EXIT_REQUEST) 通知主线程
 *   5. 主线程 GracefulExit: raise(SIGINT) 触发 Crow 内部 Server::stop() (方案 A)
 *   6. 工作线程 join 后关闭数据库，保证数据持久化
 * =================================================================== */
#ifdef _WIN32

#include <windows.h>
#include <shellapi.h>
#include <process.h>
#include <csignal>
#include <thread>
#include <mutex>
#include "win_tray.h"
#include "resource.h"

/* 自定义退出消息 ID（与 win_tray.cpp 内部约定一致） */
#define WM_APP_EXIT_REQUEST (WM_APP + 2)

/* 全局指针，供回调函数访问主线程持有的资源 */
static crow::SimpleApp* g_app = nullptr;
static SqliteDatabase* g_db = nullptr;
static SqliteLogDatabase* g_log_db = nullptr;
static register_student::WinTray* g_tray = nullptr;
static std::thread* g_crow_thread = nullptr;
static std::thread* g_public_crow_thread = nullptr;
static std::mutex g_exit_mutex;
static bool g_exiting = false;
static uint16_t g_actual_port = 18080;
static bool g_ssl_enabled = true;

/* SIGINT 信号处理函数（方案 A 核心：Crow 内部 signals_.async_wait 监听 SIGINT）
   Crow Server::run() 注册的 signals_ 回调会调用 Server::stop()，使 app.run() 返回。
   此处的 WindowsSignalHandler 仅作日志记录，实际停止由 Crow 内部完成。 */
static void WindowsSignalHandler(int signum) {
    LOG_INFO << "Windows signal handler received signum=" << signum;
}

/* 优雅退出流程：由主线程 WM_APP_EXIT_REQUEST 触发 */
static void GracefulExit() {
    std::lock_guard<std::mutex> lock(g_exit_mutex);
    if (g_exiting) {
        return;
    }
    g_exiting = true;

    LOG_INFO << "Graceful exit started";

    /* 1. 移除托盘图标（避免遗留） */
    if (g_tray != nullptr) {
        g_tray->Remove();
    }

    /* 2. 触发 Crow 停止（方案 A：raise(SIGINT) 触发 Crow 内部 signals_） */
    LOG_INFO << "Sending SIGINT to stop Crow worker thread";
    raise(SIGINT);

    /* 3. 等待 Crow 工作线程退出（最多 5 秒） */
    if (g_crow_thread != nullptr) {
        g_crow_thread->join();
        delete g_crow_thread;
        g_crow_thread = nullptr;
    }

    /* 3.5 等待公开活动服务线程退出 */
    if (g_public_crow_thread != nullptr) {
        g_public_crow_thread->join();
        delete g_public_crow_thread;
        g_public_crow_thread = nullptr;
    }

    /* 4. 关闭数据库句柄（保证数据持久化） */
    if (g_db != nullptr) {
        g_db->Close();
        delete g_db;
        g_db = nullptr;
    }
    if (g_log_db != nullptr) {
        g_log_db->Close();
        delete g_log_db;
        g_log_db = nullptr;
    }

    LOG_INFO << "Graceful exit completed";

    /* 5. 退出消息循环（PostQuitMessage 让 GetMessage 返回 false） */
    PostQuitMessage(0);
}

/* "打开页面"菜单回调 */
static void OnOpenPageMenu() {
    std::string protocol = g_ssl_enabled ? "https" : "http";
    std::string url = protocol + "://localhost:" + std::to_string(g_actual_port);
    register_student::OpenBrowser(url);
}

/* "打开配置文件"菜单回调：用记事本打开 register_student.conf 供用户编辑保存
   配置文件路径锚定到 %APPDATA%\registerStudent\conf\，与 ParseConfigFromCommandLine
   默认路径一致；先用 ShellExecute "edit" 动词调用系统默认编辑器，失败时直接
   用 notepad.exe 作为 lpFile 调用（**禁止**经由 cmd.exe 中转——不加 /c 标志的
   cmd 会进入交互模式弹出黑色终端窗口而不执行 notepad） */
static void OnOpenConfigMenu() {
    std::string root = register_student::GetAppDataPath();
    if (root.empty()) {
        MessageBoxA(NULL, "Failed to resolve install directory",
                    "vStuSystem", MB_OK | MB_ICONERROR);
        return;
    }
    std::string conf_path = root + "\\conf\\register_student.conf";
    /* 用 wide API 支持中文路径（ShellExecuteA 走 ANSI 码页会失败） */
    std::wstring wconf_path = register_student::Utf8ToWide(conf_path);
    HINSTANCE ret = ShellExecuteW(NULL, L"edit", wconf_path.c_str(),
                                   NULL, NULL, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(ret) <= 32) {
        /* "edit" 动词失败（.conf 无 edit handler）时直接调用 notepad.exe */
        ShellExecuteW(NULL, L"open", L"notepad.exe", wconf_path.c_str(),
                      NULL, SW_SHOWNORMAL);
    }
}

/* "获取凭证"菜单回调：在默认浏览器中打开凭证页面 */
static void OnCredentialMenu() {
    std::string protocol = g_ssl_enabled ? "https" : "http";
    std::string url = protocol + "://127.0.0.1:" + std::to_string(g_actual_port) + "/credential";
    register_student::OpenBrowser(url);
}

/* "开机自启动"菜单回调（切换状态） */
static void OnAutoStartMenu() {
    bool current = register_student::IsAutoStartEnabled();
    int ret = register_student::SetAutoStart(!current);
    if (ret != 0) {
        LOG_ERROR << "SetAutoStart failed, error=" << ret;
    }
}

/* "关于"菜单回调 */
static void OnAboutMenu() {
    MessageBoxA(NULL,
                "vStuSystem 0.0.2\r\nXingya Education System\r\n\r\n"
                "Build with Crow v0.3 + SQLite3 amalgamation",
                "About vStuSystem",
                MB_OK | MB_ICONINFORMATION);
}

/* "退出"菜单回调：通过 PostMessage 通知主线程 */
static void OnExitMenu() {
    if (g_tray != nullptr) {
        register_student::WinTray::RequestExit();
    }
}

/* "重启服务"菜单回调：启动一个新实例（继承当前命令行参数），再触发当前实例优雅退出。
   - 用 GetModuleFileNameW 取当前 exe 路径（含中文路径也无碍）
   - 用 GetCommandLineW 取原始命令行；新进程跳过 argv[0]（程序名）部分，
     把剩余参数原样透传，保证 -c <conf_path> 等参数不丢失
   - 新进程脱离当前作业（DETACHED_PROCESS）独立运行
   - 当前实例随后调用 OnExitMenu 走优雅退出路径（关 db、停 Crow、PostQuitMessage） */
static void OnRestartMenu() {
    wchar_t exe_path[MAX_PATH] = {0};
    DWORD len = GetModuleFileNameW(NULL, exe_path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        LOG_ERROR << "OnRestartMenu: GetModuleFileNameW failed";
        MessageBoxA(NULL, "Failed to resolve exe path for restart",
                    "vStuSystem", MB_OK | MB_ICONERROR);
        return;
    }

    /* 取原始命令行，跳过 argv[0]（exe 路径或程序名） */
    LPWSTR raw_cmd = GetCommandLineW();
    std::wstring new_cmd;
    if (raw_cmd != nullptr) {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(raw_cmd, &argc);
        if (argv != nullptr && argc > 1) {
            for (int i = 1; i < argc; ++i) {
                if (i > 1) { new_cmd += L" "; }
                /* 用双引号包裹避免路径含空格/中文导致解析错 */
                new_cmd += L"\"";
                new_cmd += argv[i];
                new_cmd += L"\"";
            }
        }
        LocalFree(argv);
    }

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWNORMAL;
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    std::wstring wexe = exe_path;
    LPWSTR cmd_line = new_cmd.empty() ? nullptr : const_cast<LPWSTR>(new_cmd.c_str());
    BOOL ok = CreateProcessW(wexe.c_str(), cmd_line, NULL, NULL, FALSE,
                              CREATE_NEW_PROCESS_GROUP, NULL, NULL, &si, &pi);
    if (!ok) {
        DWORD err = GetLastError();
        LOG_ERROR << "OnRestartMenu: CreateProcessW failed, err=" << err;
        MessageBoxA(NULL, "Failed to start new instance for restart",
                    "vStuSystem", MB_OK | MB_ICONERROR);
        return;
    }
    LOG_INFO << "OnRestartMenu: new instance started, pid=" << pi.dwProcessId;
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    /* 新进程已起，触发当前实例优雅退出 */
    OnExitMenu();
}

/* 解析命令行参数，提取 -c/--conf 配置路径
   Windows 下 lpCmdLine 不含程序名，需手动解析 */
static std::string ParseConfigFromCommandLine(LPSTR lpCmdLine) {
    std::string cmd = (lpCmdLine != nullptr) ? std::string(lpCmdLine) : std::string();
    std::string conf_path;

    /* 查找 -c 或 --conf 参数 */
    const char* markers[] = {"-c ", "--conf "};
    for (size_t i = 0; i < sizeof(markers) / sizeof(markers[0]); ++i) {
        size_t pos = cmd.find(markers[i]);
        if (pos != std::string::npos) {
            size_t value_start = pos + std::string(markers[i]).size();
            size_t value_end = cmd.find(' ', value_start);
            if (value_end == std::string::npos) {
                value_end = cmd.size();
            }
            /* 去除可能的引号 */
            std::string value = cmd.substr(value_start, value_end - value_start);
            if (!value.empty() && value.front() == '"') {
                value.erase(0, 1);
            }
            if (!value.empty() && value.back() == '"') {
                value.pop_back();
            }
            if (!value.empty()) {
                conf_path = value;
                break;
            }
        }
    }
    /* 处理 --conf=path 形式 */
    if (conf_path.empty()) {
        size_t pos = cmd.find("--conf=");
        if (pos != std::string::npos) {
            size_t value_start = pos + 7;
            size_t value_end = cmd.find(' ', value_start);
            if (value_end == std::string::npos) {
                value_end = cmd.size();
            }
            conf_path = cmd.substr(value_start, value_end - value_start);
        }
    }

    /* 默认路径：<exe 所在目录>\conf\register_student.conf */
    if (conf_path.empty()) {
        std::string root = register_student::GetAppDataPath();
        conf_path = root + "\\conf\\register_student.conf";
    }
    return conf_path;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
    (void)hInstance;
    (void)hPrevInstance;
    (void)nCmdShow;

    /* 1. 解析配置路径，确保数据目录存在 */
    std::string conf_path = ParseConfigFromCommandLine(lpCmdLine);
    std::string root = register_student::GetAppDataPath();
    if (root.empty()) {
        MessageBoxA(NULL, "Failed to resolve install directory",
                    "vStuSystem", MB_OK | MB_ICONERROR);
        return 1;
    }
    int ret = register_student::EnsureDataDirs(root);
    if (ret != 0) {
        MessageBoxA(NULL, "Failed to create data directories",
                    "vStuSystem", MB_OK | MB_ICONERROR);
        return 1;
    }

    /* 2. 加载配置（首次启动会自动创建配置文件） */
    register_student::Config config = register_student::LoadConfig(conf_path);
    register_student::InitLog(config.log_path.c_str(),
                               config.log_max_size,
                               config.log_max_files);
    g_actual_port = config.port;
    g_ssl_enabled = config.ssl_enabled;

    /* 3. 校验 upload_path */
    if (config.upload_path.empty()) {
        LOG_ERROR << "upload_path is not configured, service cannot start";
        MessageBoxA(NULL, "Configuration error: upload_path not set",
                    "vStuSystem", MB_OK | MB_ICONERROR);
        return 1;
    }

    /* 4. 设置工作目录为安装根目录（exe 所在目录），使相对路径锚定到该目录。
       用 wide API 支持中文路径（SetCurrentDirectoryA 在非 ANSI 路径下会失败） */
    std::wstring wroot = register_student::Utf8ToWide(root);
    if (wroot.empty() || !SetCurrentDirectoryW(wroot.c_str())) {
        LOG_ERROR << "Failed to set working directory: " << root;
        MessageBoxA(NULL, "Failed to set working directory",
                    "vStuSystem", MB_OK | MB_ICONERROR);
        return 1;
    }

    /* 4.5 路径内容迁移：检测配置中 db/log_db/upload/log 路径是否与上次启动不同，
       若不同则把上一代路径内容拷贝到新路径，并清理更早一代（上上次）的文件。
       必须在 SetCurrentDirectoryW 之后（相对路径已锚定）且在打开数据库之前
       （db/log_db 句柄未占用，可安全拷贝/删除）。失败不阻断启动。 */
    {
        int mig_ret = register_student::MigratePathsIfNeeded(config, conf_path);
        if (mig_ret != 0) {
            LOG_WARNING << "MigratePathsIfNeeded returned non-zero: " << mig_ret
                        << ", continuing startup";
        }
    }

    /* 4.6 确保SSL证书存在 */
    EnsureSSLCerts(config);

    /* 5. 打开业务数据库 */
    g_db = new SqliteDatabase();
    ret = g_db->Open(config.db_path);
    if (ret != DB_OK) {
        LOG_ERROR << "Failed to open business database: " << config.db_path;
        MessageBoxA(NULL, "Failed to open database",
                    "vStuSystem", MB_OK | MB_ICONERROR);
        delete g_db;
        g_db = nullptr;
        return 1;
    }

    /* 6. 打开日志数据库 */
    g_log_db = new SqliteLogDatabase();
    ret = g_log_db->Open(config.log_db_path);
    if (ret != DB_OK) {
        LOG_ERROR << "Failed to open log database: " << config.log_db_path;
        MessageBoxA(NULL, "Failed to open log database",
                    "vStuSystem", MB_OK | MB_ICONERROR);
        g_db->Close();
        delete g_db;
        delete g_log_db;
        g_db = nullptr;
        g_log_db = nullptr;
        return 1;
    }

    /* 7. 创建会话管理器 */
    SessionManager session_mgr;

    /* 7.5 创建拼团会话管理器 */
    GroupSessionManager group_session_mgr;
    {
        std::string session_base = config.db_path;
        size_t last_sep = session_base.find_last_of("/\\");
        if (last_sep != std::string::npos) {
            session_base = session_base.substr(0, last_sep);
        }
        int gs_ret = group_session_mgr.Init(session_base);
        if (gs_ret != DB_OK) {
            LOG_ERROR << "Failed to init GroupSessionManager, base=" << session_base;
            MessageBoxA(NULL, "Failed to init group session manager",
                        "vStuSystem", MB_OK | MB_ICONERROR);
            g_db->Close();
            delete g_db;
            g_log_db->Close();
            delete g_log_db;
            return 1;
        }
    }

    /* 7.6 启动时清理孤立拼团会话 */
    {
        std::vector<ActivityInfo> acts;
        if (g_db->ListActivities(acts) == DB_OK) {
            for (size_t i = 0; i < acts.size(); ++i) {
                if (acts[i].status == 1) {
                    int64_t aid = acts[i].id;
                    group_session_mgr.CleanupOrphans(aid,
                        [aid](const std::string& n, const std::string& p) -> bool {
                            bool exists = false;
                            g_db->CheckDuplicateSignup(aid, n, p, exists);
                            return exists;
                        });
                }
            }
        }
    }

    /* 8. 创建 Crow 应用 */
    g_app = new crow::SimpleApp();

    /* 9. 注册 SIGINT 处理函数（方案 A：Crow 内部 signals_ 监听 SIGINT）
       注意：Crow 的 signals_.async_wait(SIGINT) 在 app.run() 内部注册，
       此处的 signal(SIGINT) 可能被 Crow 覆盖；测试用例 test_crow_shutdown 会验证 */
    signal(SIGINT, WindowsSignalHandler);

    /* 10. 设置模板目录（安装后模板在 ./templates/，非开发环境的 ./webui/templates/） */
    crow::mustache::set_base("./templates/");

    /* 11. 注册所有路由（与 Linux main() 逻辑一致：复制代码，不提取共享函数） */
    PageHandler page_handler;
    page_handler.RegisterRoutes(*g_app);

    AuthHandler auth_handler(g_db, g_log_db, &session_mgr);
    auth_handler.RegisterRoutes(*g_app);

    AdminHandler admin_handler(g_db, g_log_db, &session_mgr);
    admin_handler.RegisterRoutes(*g_app);

    ClassCreateHandler class_create_handler(g_db, g_log_db, &session_mgr);
    class_create_handler.RegisterRoutes(*g_app);

    ClassManageHandler class_manage_handler(g_db, g_db, g_db, g_db, g_db,
                                            g_log_db, &session_mgr);
    class_manage_handler.RegisterRoutes(*g_app);

    RegistrationHandler registration_handler(g_db, g_db, g_db,
                                             g_log_db, &session_mgr);
    registration_handler.RegisterRoutes(*g_app);

    ResourceHandler resource_handler(g_db, g_log_db, &session_mgr);
    resource_handler.RegisterRoutes(*g_app);

    /* 网络信息Handler */
    NetworkHandler network_handler(g_actual_port, config.domain);
    network_handler.RegisterRoutes(*g_app);

    /* 活动管理Handler（需认证，挂载在主应用） */
    ActivityManageHandler activity_manage_handler(g_db, g_db, g_db, &session_mgr,
                                                  &group_session_mgr,
                                                  config.upload_path);
    activity_manage_handler.RegisterRoutes(*g_app);

    /* 数据管理Handler（需管理员认证） */
    DataTransferHandler data_transfer_handler(g_db, &session_mgr,
                                              config.upload_path);
    data_transfer_handler.RegisterRoutes(*g_app);

    CROW_ROUTE((*g_app), "/favicon.ico")
    ([]() {
        return crow::response(204);
    });

    /* 12. 静态资源路由（安装后静态资源在 ./static/，非开发环境的 ./webui/static/） */
    CROW_ROUTE((*g_app), "/static/<string>/<string>")
    ([](const crow::request&, std::string dir, std::string filename) {
        if (dir != "css" && dir != "js" && dir != "images" && dir != "uploads") {
            return crow::response(403, "Forbidden");
        }
        if (dir.find("..") != std::string::npos || filename.find("..") != std::string::npos) {
            return crow::response(403, "Forbidden");
        }
        std::string decoded_filename = UrlDecode(filename);
        std::string path;
        if (dir == "uploads") {
            path = "./data/uploads/" + decoded_filename;
        } else {
            path = "./static/" + dir + "/" + decoded_filename;
        }
        std::string content = ReadFile(path);
        if (content.empty()) {
            return crow::response(404, "File not found");
        }
        crow::response resp(200, content);
        resp.set_header("Content-Type", GetMimeType(filename));
        resp.set_header("Cache-Control", "no-cache, no-store, must-revalidate");
        return resp;
    });

    /* 12.5 公开活动服务（独立端口 8000，网络隔离） */
    const uint16_t public_port = 8000;
    crow::SimpleApp* public_app = new crow::SimpleApp();
    ActivityHandler* activity_handler = new ActivityHandler(g_db, g_db, g_db,
                                                            &group_session_mgr);
    activity_handler->RegisterRoutes(*public_app);

    CROW_ROUTE((*public_app), "/favicon.ico")
    ([]() {
        return crow::response(204);
    });

    CROW_ROUTE((*public_app), "/activity")
    ([]() {
        auto page = crow::mustache::load("activity.html");
        crow::response resp(200, page.render());
        resp.set_header("Content-Type", "text/html; charset=utf-8");
        resp.set_header("Cache-Control", "no-cache, no-store, must-revalidate");
        return resp;
    });

    CROW_ROUTE((*public_app), "/static/<string>/<string>")
    ([](const crow::request&, std::string dir, std::string filename) {
        if (dir != "css" && dir != "js" && dir != "images" && dir != "uploads") {
            return crow::response(403, "Forbidden");
        }
        if (dir.find("..") != std::string::npos || filename.find("..") != std::string::npos) {
            return crow::response(403, "Forbidden");
        }
        std::string decoded_filename = UrlDecode(filename);
        std::string path;
        if (dir == "uploads") {
            path = "./data/uploads/" + decoded_filename;
        } else {
            path = "./static/" + dir + "/" + decoded_filename;
        }
        std::string content = ReadFile(path);
        if (content.empty()) {
            return crow::response(404, "File not found");
        }
        crow::response resp(200, content);
        resp.set_header("Content-Type", GetMimeType(filename));
        resp.set_header("Cache-Control", "no-cache, no-store, must-revalidate");
        return resp;
    });

    g_public_crow_thread = new std::thread([public_app, public_port, &config]() {
        if (config.ssl_enabled) {
            public_app->ssl_file(config.ssl_cert, config.ssl_key);
        }
        LOG_INFO << "Public activity server starting on port " << public_port
                 << (config.ssl_enabled ? " (HTTPS)" : " (HTTP)");
        public_app->port(public_port).multithreaded().run();
        LOG_INFO << "Public activity server stopped";
    });

    /* 13. 创建托盘 */
    g_tray = new register_student::WinTray();
    int tray_ret = g_tray->Add(IDI_APPICON, "vStuSystem");
    if (tray_ret != 0) {
        LOG_ERROR << "Tray init failed, error=" << tray_ret
                  << " (service will continue without tray icon)";
    }
    std::vector<register_student::MenuItem> menu(7);
    menu[0].id = 0;
    menu[0].text = "Open Page";
    menu[0].checked = false;
    menu[0].callback = OnOpenPageMenu;
    menu[1].id = 4;
    menu[1].text = "Open Config File";
    menu[1].checked = false;
    menu[1].callback = OnOpenConfigMenu;
    menu[2].id = 6;
    menu[2].text = "Get Credential";
    menu[2].checked = false;
    menu[2].callback = OnCredentialMenu;
    menu[3].id = 5;
    menu[3].text = "Restart Service";
    menu[3].checked = false;
    menu[3].callback = OnRestartMenu;
    menu[4].id = 1;
    menu[4].text = "Auto Start";
    menu[4].checked = register_student::IsAutoStartEnabled();
    menu[4].callback = OnAutoStartMenu;
    menu[5].id = 2;
    menu[5].text = "About";
    menu[5].checked = false;
    menu[5].callback = OnAboutMenu;
    menu[6].id = 3;
    menu[6].text = "Exit";
    menu[6].checked = false;
    menu[6].callback = OnExitMenu;
    g_tray->SetMenu(menu);

    /* 14. 启动 Crow 工作线程 */
    g_crow_thread = new std::thread([&]() {
        if (config.ssl_enabled) {
            g_app->ssl_file(config.ssl_cert, config.ssl_key);
        }
        std::string protocol = config.ssl_enabled ? "https" : "http";
        LOG_INFO << "Server starting on port " << config.port
                 << (config.ssl_enabled ? " (HTTPS)" : " (HTTP)");
        LOG_INFO << "Credential page: " << protocol << "://127.0.0.1:" << config.port << "/credential";
        g_app->port(config.port).multithreaded().run();
        LOG_INFO << "Crow run() returned, worker thread exiting";
    });

    /* 15. 主线程消息循环 */
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        if (msg.message == WM_APP_EXIT_REQUEST) {
            GracefulExit();
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    /* 16. 兜底：等待工作线程彻底退出（GracefulExit 已 join 一次，此处防御性检查） */
    if (g_crow_thread != nullptr && g_crow_thread->joinable()) {
        g_crow_thread->join();
        delete g_crow_thread;
        g_crow_thread = nullptr;
    }

    /* 17. 清理资源 */
    delete g_tray;
    g_tray = nullptr;
    delete g_app;
    g_app = nullptr;
    if (g_db != nullptr) {
        g_db->Close();
        delete g_db;
        g_db = nullptr;
    }
    if (g_log_db != nullptr) {
        g_log_db->Close();
        delete g_log_db;
        g_log_db = nullptr;
    }

    LOG_INFO << "WinMain returning, process exit";
    return 0;
}

#endif /* _WIN32 */
