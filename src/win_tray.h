#ifndef __WIN_TRAY_H__
#define __WIN_TRAY_H__

#ifdef _WIN32

#include <windows.h>
#include <shellapi.h>
#include <string>
#include <functional>
#include <vector>

#include "error_codes.h"

namespace register_student {

/**
 * @brief 菜单项结构体，仅 Windows 分支可见
 */
struct MenuItem {
    UINT id;                                /* 菜单 ID，从 WM_COMMAND 的 wParam 低字取 */
    std::string text;                       /* 显示文本（简体中文） */
    bool checked;                           /* 勾选状态 */
    std::function<void()> callback;         /* 点击回调 */

    MenuItem();
};

/**
 * @brief Win32 API 系统托盘封装类
 *        自实现 Shell_NotifyIcon + 隐藏窗口 + 消息路由 + 菜单弹出，
 *        不引入任何三方库。
 */
class WinTray {
public:
    WinTray();
    ~WinTray();

    /**
     * @brief 添加托盘图标
     * @param icon_res_id PE 资源 ID（如 IDI_APPICON）
     * @param tip 提示文本（不超过 128 字符）
     * @return 0=成功, ERR_PLATFORM_TRAY_INIT=失败
     */
    int Add(UINT icon_res_id, const std::string& tip);

    /**
     * @brief 移除托盘图标
     * @return 0=成功, ERR_PLATFORM_TRAY_INIT=失败
     */
    int Remove();

    /**
     * @brief 设置右键菜单项
     * @param items 菜单项列表（按顺序显示）
     */
    void SetMenu(const std::vector<MenuItem>& items);

    /**
     * @brief 触发退出消息（由"退出"回调通过 PostMessage 调用主线程）
     *        静态方法，因为依赖主窗口句柄由全局 WinTray 实例持有
     */
    static void RequestExit();

    /**
     * @brief 获取主隐藏窗口句柄（供主线程消息循环使用）
     */
    HWND GetMainWindow() const { return hwnd_; }

private:
    HWND hwnd_;
    NOTIFYICONDATAW nid_;
    std::vector<MenuItem> menu_items_;
    UINT taskbar_created_msg_;  /* TaskbarCreated 消息 ID */

    /* 静态 WndProc 路由到实例 */
    static LRESULT CALLBACK WndProcProc(HWND hwnd, UINT msg,
                                         WPARAM wparam, LPARAM lparam);
    LRESULT HandleMessage(UINT msg, WPARAM wparam, LPARAM lparam);

    void ShowContextMenu();
};

} /* namespace register_student */

#endif /* _WIN32 */
#endif /* __WIN_TRAY_H__ */
