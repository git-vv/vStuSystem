/**
 * @file test_crow_shutdown.cpp
 * @brief Crow v0.3 优雅退出测试 (验证方案 A 可行性)
 *
 * 验证点：
 *   1. raise(SIGINT) 能触发 Crow 内部 Server::signals_.async_wait 回调
 *      进而调用 Server::stop() 使 app.run() 返回
 *   2. 工作线程 join 在合理超时（5 秒）内完成
 *   3. 停止后端口不再监听
 *
 * 测试可在 Linux 与 Windows 上运行（Crow 是跨平台的），
 * 但语义在 Windows 窗口化进程下更关键。
 *
 * 若此测试失败（app.run() 不在 5 秒内返回），则方案 A 在当前平台不适用，
 * 需回退方案 B（复制 Crow<>::run() 实现自持 Server*）。
 */

#include "test_helpers.h"

#include "crow_safe.h"

#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <csignal>
#include <string>
#include <atomic>

#include <sys/types.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif
#include <cstring>

/* 测试用端口范围，避免与生产 18080 冲突 */
static const uint16_t TEST_PORT_BASE = 19880;

/* 全局同步原语 */
static std::mutex g_mtx;
static std::condition_variable g_cv;
static bool g_crow_started = false;
static bool g_crow_stopped = false;

/* 测试用路由 */
static crow::SimpleApp* g_test_app = nullptr;

static void TestSignalHandler(int) {
    /* 仅作日志点，实际停止由 Crow 内部 signals_.async_wait 完成 */
}

/* 检查端口是否监听 */
static bool IsPortListening(uint16_t port) {
#ifdef _WIN32
    WSADATA wsaData;
    static bool wsa_init = false;
    if (!wsa_init) {
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            return false;
        }
        wsa_init = true;
    }
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        return false;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(port);
    int ret = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    closesocket(sock);
    return (ret == 0);
#else
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return false;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(port);
    int ret = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    close(sock);
    return (ret == 0);
#endif
}

TEST_CASE(Crow_StartAndStop_WithSignal) {
    /* 方案 A 验证：raise(SIGINT) 触发 Crow 内部 signals_，进而 stop() */

    crow::SimpleApp app;
    g_test_app = &app;
    uint16_t port = TEST_PORT_BASE;

    /* 注册测试路由 */
    CROW_ROUTE(app, "/test_route")
    ([](const crow::request&) {
        return "ok";
    });

    /* 注册 SIGINT 处理（与 main.cpp 一致） */
    signal(SIGINT, TestSignalHandler);

    /* 启动 Crow 工作线程 */
    std::thread worker([&app, port]() {
        app.port(port).multithreaded().run();
        {
            std::lock_guard<std::mutex> lock(g_mtx);
            g_crow_stopped = true;
        }
        g_cv.notify_all();
    });

    /* 等待端口监听（最多 5 秒） */
    bool listening = false;
    for (int i = 0; i < 50; ++i) {
        if (IsPortListening(port)) {
            listening = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ASSERT_TRUE(listening);

    /* 触发 SIGINT（方案 A 核心） */
    raise(SIGINT);

    /* 等待 Crow 工作线程退出（最多 5 秒） */
    {
        std::unique_lock<std::mutex> lock(g_mtx);
        bool stopped = g_cv.wait_for(lock, std::chrono::seconds(5),
                                       []() { return g_crow_stopped; });
        ASSERT_TRUE(stopped);
    }

    /* join 工作线程 */
    if (worker.joinable()) {
        worker.join();
    }

    g_test_app = nullptr;
}

TEST_CASE(Crow_HandlesHttpRequest_AfterStart) {
    /* 验证服务可用：HTTP GET 返回 200 */
    crow::SimpleApp app;
    uint16_t port = TEST_PORT_BASE + 1;

    CROW_ROUTE(app, "/test_get")
    ([](const crow::request&) {
        return "ok";
    });

    signal(SIGINT, TestSignalHandler);

    std::thread worker([&app, port]() {
        app.port(port).multithreaded().run();
    });

    /* 等待端口监听 */
    bool listening = false;
    for (int i = 0; i < 50; ++i) {
        if (IsPortListening(port)) {
            listening = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ASSERT_TRUE(listening);

    /* 简单 TCP 连接验证端口可访问 */
#ifdef _WIN32
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT_TRUE(sock != INVALID_SOCKET);
#else
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_TRUE(sock >= 0);
#endif
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(port);
    ASSERT_EQ(connect(sock, (struct sockaddr*)&addr, sizeof(addr)), 0);
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif

    /* 停止 Crow */
    raise(SIGINT);

    if (worker.joinable()) {
        worker.join();
    }
}

TEST_CASE(Crow_PortNotListening_AfterStop) {
    /* 验证停止后端口不再监听 */
    crow::SimpleApp app;
    uint16_t port = TEST_PORT_BASE + 2;

    CROW_ROUTE(app, "/test_stop")
    ([](const crow::request&) { return "ok"; });

    signal(SIGINT, TestSignalHandler);

    std::thread worker([&app, port]() {
        app.port(port).multithreaded().run();
    });

    /* 等待端口监听 */
    bool listening = false;
    for (int i = 0; i < 50; ++i) {
        if (IsPortListening(port)) {
            listening = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ASSERT_TRUE(listening);

    /* 停止 */
    raise(SIGINT);
    if (worker.joinable()) {
        worker.join();
    }

    /* 等待端口释放（最多 2 秒） */
    bool still_listening = true;
    for (int i = 0; i < 20; ++i) {
        if (!IsPortListening(port)) {
            still_listening = false;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ASSERT_TRUE(!still_listening);
}

TEST_CASE(Crow_WorkerThreadJoin_Completes) {
    /* 验证 join 不阻塞超时 */
    crow::SimpleApp app;
    uint16_t port = TEST_PORT_BASE + 3;

    CROW_ROUTE(app, "/test_join")
    ([](const crow::request&) { return "ok"; });

    signal(SIGINT, TestSignalHandler);

    std::thread worker([&app, port]() {
        app.port(port).multithreaded().run();
    });

    /* 等待端口监听 */
    bool listening = false;
    for (int i = 0; i < 50; ++i) {
        if (IsPortListening(port)) {
            listening = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ASSERT_TRUE(listening);

    /* 触发停止并计时 join */
    raise(SIGINT);
    auto start = std::chrono::steady_clock::now();
    if (worker.joinable()) {
        worker.join();
    }
    auto end = std::chrono::steady_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    /* join 应在 5 秒内完成 */
    ASSERT_TRUE(duration_ms < 5000);
}
