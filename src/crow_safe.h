#ifndef __CROW_SAFE_H__
#define __CROW_SAFE_H__

/**
 * @file crow_safe.h
 * @brief Crow 框架安全包装头文件
 *
 * 问题背景：
 *   - crow_all.h 内部通过 boost/date_time 和 boost/filesystem 间接包含 <windows.h>
 *   - winnt.h 定义 #define DELETE (0x00010000L) 作为访问权限宏
 *   - wingdi.h 定义 #define ERROR 0
 *   - minidrv.h 定义 #define WARNING(msg)
 *   - ntdef.h 定义 #define CRITICAL
 *   - 这些宏与 crow::HTTPMethod、crow::LogLevel 枚举成员冲突，导致枚举无法解析
 *
 * 解决方案：
 *   - 在包含 crow_all.h 之前，先包含 <windows.h> 并 #undef 掉与 HTTP 方法和日志级别冲突的宏
 *   - 利用 windows.h 的 include guard，后续 boost 包含 windows.h 时不会重新定义这些宏
 */

#ifdef _WIN32

#include <windows.h>

/* 与 crow::HTTPMethod 枚举冲突的宏（来自 winnt.h 等） */
#undef DELETE
#undef GET
#undef POST
#undef PUT
#undef HEAD
#undef OPTIONS
#undef TRACE
#undef CONNECT

/* 与 crow::LogLevel 枚举冲突的宏（来自 wingdi.h / minidrv.h / ntdef.h） */
#undef ERROR
#undef WARNING
#undef CRITICAL
#undef DEBUG
#undef INFO

#endif /* _WIN32 */

#include "crow_all.h"

#endif /* __CROW_SAFE_H__ */
