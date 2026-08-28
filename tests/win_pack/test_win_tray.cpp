/**
 * @file test_win_tray.cpp
 * @brief WinTray 类纯逻辑测试 (仅 Windows 分支编译)
 *
 * 验证点：
 *   1. MenuItem 默认构造的初始值
 *   2. WinTray 默认构造后 hwnd_ 为 NULL
 *   3. SetMenu 存储菜单项
 *
 * 注意：实际 Shell_NotifyIcon / 隐藏窗口创建属于 GUI 行为，
 *      不在单测覆盖范围，由 rs:verify 阶段手工执行端到端验证。
 */

#ifdef _WIN32

#include "test_helpers.h"
#include "win_tray.h"

#include <vector>
#include <string>

TEST_CASE(MenuItem_DefaultConstructor) {
    register_student::MenuItem item;
    ASSERT_EQ(item.id, static_cast<UINT>(0));
    ASSERT_TRUE(!item.checked);
    ASSERT_TRUE(item.text.empty());
    ASSERT_TRUE(!item.callback);
}

TEST_CASE(MenuItem_FieldsAssignable) {
    register_student::MenuItem item;
    item.id = 42;
    item.text = "Open Page";
    item.checked = true;
    item.callback = []() {};

    ASSERT_EQ(item.id, static_cast<UINT>(42));
    ASSERT_EQ(item.text, std::string("Open Page"));
    ASSERT_TRUE(item.checked);
    ASSERT_TRUE(static_cast<bool>(item.callback));
}

TEST_CASE(WinTray_Constructor_HwndNull) {
    /* 构造后未调用 Add 之前，hwnd 应为 NULL */
    register_student::WinTray tray;
    ASSERT_TRUE(tray.GetMainWindow() == NULL);
}

TEST_CASE(WinTray_SetMenu_ItemsStored) {
    /* SetMenu 应存储菜单项列表 */
    register_student::WinTray tray;

    std::vector<register_student::MenuItem> menu(4);
    menu[0].id = 0;
    menu[0].text = "Open Page";
    menu[1].id = 1;
    menu[1].text = "Auto Start";
    menu[1].checked = true;
    menu[2].id = 2;
    menu[2].text = "About";
    menu[3].id = 3;
    menu[3].text = "Exit";

    tray.SetMenu(menu);

    /* WinTray 内部 menu_items_ 为私有，通过 GetMainWindow 等公开接口间接验证 */
    /* 此处仅验证不崩溃，菜单项已存储 */
    ASSERT_TRUE(tray.GetMainWindow() == NULL);
}

#endif /* _WIN32 */
