/**
 * @file test_main.cpp
 * @brief win_pack 测试入口，独立于 tests/test_main.cpp 的 56 个用例
 *
 * 仅组织 win-build-pack 相关的 3 个测试文件：
 *   - test_config_default_content.cpp
 *   - test_crow_shutdown.cpp
 *   - test_win_tray.cpp (仅 Windows)
 *
 * 由 CMake 在 if(WIN32) 分支构建为 run_win_pack_tests 可执行文件，
 * 在 Linux 上由 #ifdef _WIN32 包裹的文件为空，避免编译失败。
 *
 * 注意：通过 #include 其他 .cpp 文件实现单 TU 编译，确保 test_helpers.h
 *       中的 static g_tests 数组只存在一份副本（与 tests/test_main.cpp 模式一致）。
 */

#include "test_helpers.h"

#include "test_config_default_content.cpp"
#include "test_crow_shutdown.cpp"
#ifdef _WIN32
#include "test_win_tray.cpp"
#endif

/* TEST_CASE 宏在上述 .cpp 文件中通过静态变量自动注册到 g_tests 数组 */

int main() {
    return RunAllTests();
}

