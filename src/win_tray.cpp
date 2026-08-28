#ifdef _WIN32

#include "win_tray.h"

#include <plog/Log.h>

namespace register_student {

/* WinTray 内部使用的窗口类名 */
static const char* kTrayWindowClass = "vStuSystemTrayWindow";

/* 托盘回调消息 ID（WM_APP + 1，避免与系统消息冲突） */
#define WM_TRAY_CALLBACK (WM_APP + 1)

/* 自定义退出消息 ID（由"退出"菜单通过 PostMessage 调用主线程） */
#define WM_APP_EXIT_REQUEST (WM_APP + 2)

/* 左键单击/双击节流间隔（毫秒），避免双击时 WM_LBUTTONUP + WM_LBUTTONDBLCLK
   连续触发两次 OpenBrowser 导致弹出两个浏览器标签页 */
static const DWORD kLeftClickThrottleMs = 500;
static DWORD g_last_left_click_tick = 0;

/* 静态全局指针，用于 WndProcProc 路由到实例 */
static WinTray* g_tray_instance = nullptr;

MenuItem::MenuItem()
    : id(0)
    , text()
    , checked(false)
    , callback() {
}

WinTray::WinTray()
    : hwnd_(NULL)
    , nid_()
    , menu_items_()
    , taskbar_created_msg_(0) {
    memset(&nid_, 0, sizeof(nid_));
}

WinTray::~WinTray() {
    if (hwnd_ != NULL) {
        DestroyWindow(hwnd_);
        hwnd_ = NULL;
    }
    /* 反注册窗口类 */
    HINSTANCE hinst = GetModuleHandleA(NULL);
    UnregisterClassA(kTrayWindowClass, hinst);
    g_tray_instance = nullptr;
}

int WinTray::Add(UINT icon_res_id, const std::string& tip) {
    HINSTANCE hinst = GetModuleHandleA(NULL);

    /* 注册隐藏窗口类 */
    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = &WinTray::WndProcProc;
    wc.hInstance = hinst;
    wc.lpszClassName = kTrayWindowClass;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(hinst, MAKEINTRESOURCEA(icon_res_id));
    wc.hbrBackground = NULL;

    if (!RegisterClassExA(&wc)) {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            LOG_ERROR << "RegisterClassExA failed, error=" << err;
            return ERR_PLATFORM_TRAY_INIT;
        }
    }

    /* TaskbarCreated 消息：资源管理器重启时通知重新添加图标 */
    taskbar_created_msg_ = RegisterWindowMessageA("TaskbarCreated");

    /* 创建隐藏窗口 */
    g_tray_instance = this;
    hwnd_ = CreateWindowExA(
        WS_EX_TOOLWINDOW,               /* 不在 Alt+Tab 中显示 */
        kTrayWindowClass,
        "",
        WS_OVERLAPPED,
        0, 0, 0, 0,
        NULL, NULL, hinst, NULL);
    if (hwnd_ == NULL) {
        LOG_ERROR << "CreateWindowExA failed, error=" << GetLastError();
        return ERR_PLATFORM_TRAY_INIT;
    }

    /* 设置 NOTIFYICONDATAW */
    nid_.cbSize = sizeof(NOTIFYICONDATAW);
    nid_.hWnd = hwnd_;
    nid_.uID = 1;
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid_.uCallbackMessage = WM_TRAY_CALLBACK;
    nid_.hIcon = LoadIconW(hinst, MAKEINTRESOURCEW(icon_res_id));

    /* 将提示文本转换为宽字符 */
    int wlen = MultiByteToWideChar(CP_UTF8, 0, tip.c_str(),
                                    static_cast<int>(tip.size()), NULL, 0);
    if (wlen > 0 && wlen < 128) {
        MultiByteToWideChar(CP_UTF8, 0, tip.c_str(),
                             static_cast<int>(tip.size()),
                             nid_.szTip, wlen);
        nid_.szTip[wlen] = L'\0';
    }

    if (!Shell_NotifyIconW(NIM_ADD, &nid_)) {
        LOG_ERROR << "Shell_NotifyIcon NIM_ADD failed";
        return ERR_PLATFORM_TRAY_INIT;
    }

    return 0;
}

int WinTray::Remove() {
    if (nid_.hWnd == NULL) {
        return 0;
    }
    nid_.uFlags = 0;
    BOOL ret = Shell_NotifyIconW(NIM_DELETE, &nid_);
    return ret ? 0 : ERR_PLATFORM_TRAY_INIT;
}

void WinTray::SetMenu(const std::vector<MenuItem>& items) {
    menu_items_ = items;
}

void WinTray::RequestExit() {
    if (g_tray_instance != nullptr && g_tray_instance->hwnd_ != NULL) {
        PostMessage(g_tray_instance->hwnd_, WM_APP_EXIT_REQUEST, 0, 0);
    }
}

LRESULT CALLBACK WinTray::WndProcProc(HWND hwnd, UINT msg,
                                        WPARAM wparam, LPARAM lparam) {
    if (g_tray_instance != nullptr && g_tray_instance->hwnd_ == hwnd) {
        return g_tray_instance->HandleMessage(msg, wparam, lparam);
    }
    return DefWindowProcA(hwnd, msg, wparam, lparam);
}

LRESULT WinTray::HandleMessage(UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == taskbar_created_msg_ && taskbar_created_msg_ != 0) {
        /* 资源管理器重启，重新添加图标 */
        nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        Shell_NotifyIconW(NIM_ADD, &nid_);
        return 0;
    }

    if (msg == WM_TRAY_CALLBACK) {
        /* lParam 低字是鼠标事件 */
        UINT mouse_event = static_cast<UINT>(LOWORD(lparam));
        if (mouse_event == WM_RBUTTONUP) {
            /* 右键松开，弹出菜单 */
            ShowContextMenu();
            return 0;
        }
        if (mouse_event == WM_LBUTTONUP || mouse_event == WM_LBUTTONDBLCLK) {
            /* 左键单击/双击，触发 id=0 的回调（约定为"打开页面"）
               节流：双击时 Windows 会先发 WM_LBUTTONUP 再发 WM_LBUTTONDBLCLK，
               用 GetTickCount() 闸门在 kLeftClickThrottleMs 内只触发一次，
               双击也只弹一个浏览器标签页 */
            DWORD now = GetTickCount();
            if (now - g_last_left_click_tick < kLeftClickThrottleMs) {
                return 0;
            }
            g_last_left_click_tick = now;
            for (size_t i = 0; i < menu_items_.size(); ++i) {
                if (menu_items_[i].id == 0 && menu_items_[i].callback) {
                    menu_items_[i].callback();
                    break;
                }
            }
            return 0;
        }
        return 0;
    }

    if (msg == WM_COMMAND) {
        /* wParam 低字是菜单 ID */
        UINT menu_id = static_cast<UINT>(LOWORD(wparam));
        for (size_t i = 0; i < menu_items_.size(); ++i) {
            if (menu_items_[i].id == menu_id && menu_items_[i].callback) {
                menu_items_[i].callback();
                break;
            }
        }
        return 0;
    }

    if (msg == WM_APP_EXIT_REQUEST) {
        /* 退出请求：触发 WM_DESTROY + PostQuitMessage */
        DestroyWindow(hwnd_);
        return 0;
    }

    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcA(hwnd_, msg, wparam, lparam);
}

void WinTray::ShowContextMenu() {
    if (menu_items_.empty()) {
        return;
    }

    /* 创建弹出菜单 */
    HMENU hMenu = CreatePopupMenu();
    if (hMenu == NULL) {
        LOG_ERROR << "CreatePopupMenu failed";
        return;
    }

    for (size_t i = 0; i < menu_items_.size(); ++i) {
        UINT flags = MF_STRING;
        if (menu_items_[i].checked) {
            flags |= MF_CHECKED;
        } else {
            flags |= MF_UNCHECKED;
        }
        AppendMenuA(hMenu, flags, menu_items_[i].id,
                     menu_items_[i].text.c_str());
    }

    /* 获取鼠标位置 */
    POINT pt;
    GetCursorPos(&pt);

    /* 必须调用 SetForegroundWindow，否则菜单点击外部不消失 */
    SetForegroundWindow(hwnd_);

    /* 右对齐弹窗，TPM_RETURNCMD 让 TrackPopupMenu 返回选中项 ID */
    int cmd = TrackPopupMenu(hMenu,
                              TPM_RIGHTALIGN | TPM_BOTTOMALIGN |
                              TPM_RETURNCMD | TPM_LEFTBUTTON,
                              pt.x, pt.y, 0, hwnd_, NULL);

    /* 通知窗口失去焦点，让菜单消失 */
    PostMessage(hwnd_, WM_NULL, 0, 0);

    if (cmd != 0) {
        /* 模拟 WM_COMMAND 触发回调 */
        PostMessage(hwnd_, WM_COMMAND, cmd, 0);
    }

    DestroyMenu(hMenu);
}

} /* namespace register_student */

#endif /* _WIN32 */
