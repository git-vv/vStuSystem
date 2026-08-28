#ifndef __UTILS_H__
#define __UTILS_H__

#include "plog/Log.h"
#include "registration_types.h"
#include "class_types.h"
#include "config.h"

/* 使用plog内置日志宏: LOG_INFO, LOG_ERROR, LOG_DEBUG, LOG_WARNING */

namespace register_student {

/**
 * @brief 初始化日志系统
 * @param log_file 日志文件路径，为空则仅输出到控制台
 * @param max_size 单个日志文件最大字节数，0表示不限制
 * @param max_files 日志回滚文件个数，0表示不回滚
 */
void InitLog(const char* log_file, size_t max_size, int max_files);

/**
 * @brief 生成随机salt（16字节十六进制字符串，32字符）
 * @return salt字符串
 */
std::string GenerateSalt();

/**
 * @brief SHA256+salt加密密码
 * @param password 明文密码
 * @param salt salt字符串
 * @return 加密后的哈希字符串（十六进制，64字符）
 */
std::string EncryptPassword(const std::string& password, const std::string& salt);

/**
 * @brief 校验密码
 * @param password 明文密码
 * @param salt salt字符串
 * @param expected_hash 期望的哈希值
 * @return true=匹配, false=不匹配
 */
bool VerifyPassword(const std::string& password, const std::string& salt, const std::string& expected_hash);

/**
 * @brief 获取当前时间字符串（YYYY-MM-DD HH:MM:SS格式）
 * @return 时间字符串
 */
std::string GetCurrentTimeString();

/**
 * @brief 获取当前日期字符串（YYYY-MM-DD 格式）
 * @return 日期字符串
 * @note   用于考勤日期分档权限校验（区分当天/历史/未来）
 */
std::string GetCurrentDateString();

/**
 * @brief 计算两个日期之间的周一到周五天数（去除周六周日）
 * @param start_ymd 起始日期，格式 YYYY-MM-DD（含）
 * @param end_ymd   结束日期，格式 YYYY-MM-DD（含）
 * @return 上课总天数；解析失败或 end < start 返回 0
 * @note   用于退费考勤折损计算；中国标准周末=周六+周日
 */
int CountWeekdaysInRange(const std::string& start_ymd, const std::string& end_ymd);

/**
 * @brief 生成学生信息修改前后的字段级 diff 字符串
 * @param old_info 修改前的报名信息
 * @param new_info 修改后的报名信息
 * @param old_class_name 原班级名称
 * @param new_class_name 新班级名称
 * @return diff 字符串，格式"字段: 旧值→新值; ..."；超过 40 字符截断为 37+"..."；无变更返回空串
 */
std::string BuildStudentDiff(const RegistrationInfo& old_info,
                             const RegistrationInfo& new_info,
                             const std::string& old_class_name,
                             const std::string& new_class_name);

/**
 * @brief 本机局域网网络信息
 * @note  各字段独立返回，获取失败时为空字符串，不影响其他字段
 */
struct NetworkInfo {
    std::string ipv4;      /* 局域网 IPv4 地址，如 "192.168.1.31" */
    std::string ipv6;      /* 本地链接 IPv6 地址，如 "fe80::xxxx"，无则为空 */
    std::string mac;       /* MAC 地址，格式 "AA:BB:CC:DD:EE:FF"，无则为空 */
    std::string hostname;  /* 本机主机名，如 "DESKTOP-XXX" */
    std::string adapter;   /* 网络适配器名称，如 "WLAN"、"以太网"，无则为空 */
};

/**
 * @brief 获取本机局域网网络信息
 * @return NetworkInfo 结构体，获取失败时各字段为空字符串
 * @note  Windows 用 GetAdaptersAddresses，Linux 用 getifaddrs，条件编译
 *        IPv4 优先级：192.168.x.x > 10.x.x.x > 172.16-31.x.x
 *        排除回环地址(127.x.x.x)和链路本地地址(169.254.x.x)
 */
NetworkInfo GetLocalNetworkInfo();

/**
 * @brief 生成6位随机邀请码（字符集：23456789ABCDEFGHJKMNPQRSTUVWXYZ，排除0/O/1/I/L）
 * @return 大写邀请码字符串（6字符）
 */
std::string GenerateInviteCode();

#ifdef _WIN32
/**
 * @brief UTF-8 转 wide string
 * @param utf8 UTF-8 编码字符串
 * @return wide string（std::wstring）
 * @note    用于 Windows wide API（CreateDirectoryW、SetCurrentDirectoryW 等）
 *         输入，以支持中文路径
 */
std::wstring Utf8ToWide(const std::string& utf8);

/**
 * @brief 获取可执行文件所在目录（不带末尾反斜杠）
 * @return 目录路径（UTF-8 编码），如 "D:\\我的程序\\vStuSystem"
 */
std::string GetModulePath();

/**
 * @brief 获取安装根目录（= exe 所在目录）
 * @return 安装根目录路径（UTF-8 编码）
 * @note  兼容旧命名，语义已变：不再返回 %APPDATA%\registerStudent，而是返回
 *        GetModulePath()，以支持自定义安装目录（含中文路径）
 */
std::string GetAppDataPath();

/**
 * @brief 创建 data/、logs/、data/uploads/ 子目录（基于安装根目录）
 * @param root 安装根目录路径
 * @return 0=成功, ERR_PLATFORM_INTERNAL=失败
 */
int EnsureDataDirs(const std::string& root);

/**
 * @brief 用默认浏览器打开 URL
 * @param url URL 字符串，必须以 http:// 或 https:// 开头
 * @return 0=成功, ERR_INVALID_PARAM/ERR_PLATFORM_INTERNAL=失败
 */
int OpenBrowser(const std::string& url);

/**
 * @brief 设置开机自启动（注册表 HKCU\...\Run\vStuSystem）
 * @param enable true=开启, false=关闭
 * @return 0=成功, ERR_PLATFORM_INTERNAL=失败
 */
int SetAutoStart(bool enable);

/**
 * @brief 查询开机自启动是否已开启
 * @return true=已开启, false=未开启
 */
bool IsAutoStartEnabled();

/**
 * @brief 检查配置中的路径是否与上次记录不同，若不同则将旧路径下的内容
 *        拷贝到新路径，并清理"上上次"路径下的文件。
 *
 * 保留策略（用户要求）：始终保留"当前"和"上一次"两代路径的文件，
 * 更早一代（"上上次"）在下次迁移时删除。即：
 *   - 第 1 次启动：sidecar 不存在，记录当前路径，不迁移。
 *   - 第 2 次启动（第 1 次修改）：prev→cur 拷贝，older 为空不清理，older←prev。
 *   - 第 3 次启动（第 2 次修改）：prev→cur 拷贝，清理 older 文件，older←prev。
 *   - 第 N 次启动：prev→cur 拷贝，清理 older（即第 N-2 代）文件，older←prev。
 *
 * 覆盖范围：db.path（业务库文件）、log_db.path（日志库文件）、
 *           upload.path（上传目录，递归拷贝/删除）、log.path（日志文件）。
 *
 * 触发时机：WinMain 启动早期、SetCurrentDirectoryW 之后、打开数据库之前。
 *           此时 db/log_db 句柄尚未打开，可安全拷贝和删除。
 *
 * sidecar 文件：<conf_path 同目录>/.last_paths，INI 格式，含 [paths] 和
 *               [older_paths] 两个 section，各 4 个路径（绝对路径）。
 *
 * @param config 当前加载的 Config（含本次启动的 4 个路径）
 * @param conf_path 配置文件路径（用于定位 sidecar 文件位置）
 * @return 0=成功（含首次启动/无变更/迁移完成），ERR_PLATFORM_INTERNAL=sidecar 写失败
 */
int MigratePathsIfNeeded(const Config& config, const std::string& conf_path);
#endif /* _WIN32 */

} /* namespace register_student */

#endif /* __UTILS_H__ */
