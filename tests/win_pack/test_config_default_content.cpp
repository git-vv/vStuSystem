/**
 * @file test_config_default_content.cpp
 * @brief 配置文件生成一致性测试
 *
 * 验证点：
 *   1. kDefaultConfigContent 常量字符串与 conf/register_student.conf 文件内容完全一致
 *   2. LoadConfig 在配置文件不存在时自动创建默认配置
 *   3. LoadConfig 不会覆盖已存在的配置文件
 *   4. LoadConfig 在配置文件为空时使用默认值
 */

#include "test_helpers.h"
#include "config.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdio>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#else
#include <unistd.h>
#define MKDIR(path) mkdir(path, 0755)
#endif

/* 读取文件全部内容到字符串 */
static std::string ReadFileContent(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        return std::string();
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

/* 创建临时测试目录，返回路径 */
static std::string MakeTempDir(const std::string& suffix) {
    std::string base = "test_tmp_config_";
    std::string dir = base + suffix;
    /* 删除已存在的目录（递归简化处理） */
#ifdef _WIN32
    std::string rm = "rmdir /S /Q " + dir;
#else
    std::string rm = "rm -rf " + dir;
#endif
    std::system(rm.c_str());
    MKDIR(dir.c_str());
    return dir;
}

/* 清理临时目录 */
static void CleanupTempDir(const std::string& dir) {
#ifdef _WIN32
    std::string rm = "rmdir /S /Q " + dir;
#else
    std::string rm = "rm -rf " + dir;
#endif
    std::system(rm.c_str());
}

TEST_CASE(KDefaultConfigContent_MatchesConfFile) {
    /* 防止常量字符串与模板文件内容漂移 */
    std::string file_content = ReadFileContent("../../conf/register_student.conf");
    /* 模板文件不存在或无法读取时跳过（CI 环境可能不同路径） */
    if (file_content.empty()) {
        file_content = ReadFileContent("../../../conf/register_student.conf");
    }
    if (file_content.empty()) {
        /* 找不到模板文件则跳过比较，但常量必须非空 */
        ASSERT_TRUE(strlen(register_student::kDefaultConfigContent) > 0);
        return;
    }
    ASSERT_EQ(file_content, std::string(register_student::kDefaultConfigContent));
}

TEST_CASE(LoadConfig_CreatesFileIfNotExists) {
    std::string dir = MakeTempDir("create_test");
    std::string conf_path = dir + "/register_student.conf";

    /* 确认配置文件不存在 */
    std::ifstream check(conf_path);
    ASSERT_TRUE(!check.is_open());
    check.close();

    /* 调用 LoadConfig */
    register_student::Config config = register_student::LoadConfig(conf_path);

    /* 验证文件已被创建 */
    std::ifstream created(conf_path);
    ASSERT_TRUE(created.is_open());
    created.close();

    /* 验证文件内容与默认配置一致 */
    std::string content = ReadFileContent(conf_path);
    ASSERT_EQ(content, std::string(register_student::kDefaultConfigContent));

    /* 验证默认配置字段值 */
    ASSERT_EQ(config.port, static_cast<uint16_t>(18080));
    ASSERT_EQ(config.db_path, std::string("./data/register_student.db"));

    CleanupTempDir(dir);
}

TEST_CASE(LoadConfig_ExistingFileNotOverwritten) {
    std::string dir = MakeTempDir("exist_test");
    std::string conf_path = dir + "/register_student.conf";

    /* 写入自定义配置 */
    std::string custom_content =
        "[server]\nport = 19999\n\n"
        "[log]\npath = /tmp/test.log\nmax_size = 1024\nmax_files = 3\n\n"
        "[db]\npath = /tmp/test.db\n\n"
        "[upload]\npath = /tmp/uploads\n\n"
        "[log_db]\npath = /tmp/log.db\n";
    std::ofstream ofs(conf_path);
    ofs << custom_content;
    ofs.close();

    /* 调用 LoadConfig */
    register_student::Config config = register_student::LoadConfig(conf_path);

    /* 验证文件内容保持自定义值，未被覆盖 */
    std::string content = ReadFileContent(conf_path);
    ASSERT_EQ(content, custom_content);

    /* 验证配置字段被正确解析 */
    ASSERT_EQ(config.port, static_cast<uint16_t>(19999));
    ASSERT_EQ(config.db_path, std::string("/tmp/test.db"));

    CleanupTempDir(dir);
}

TEST_CASE(LoadConfig_DefaultValuesOnMissing) {
    std::string dir = MakeTempDir("default_test");
    std::string conf_path = dir + "/register_student.conf";

    /* 写入空配置文件（仅 section 名，无字段） */
    std::ofstream ofs(conf_path);
    ofs << "[server]\n[log]\n[db]\n[upload]\n[log_db]\n";
    ofs.close();

    /* 调用 LoadConfig */
    register_student::Config config = register_student::LoadConfig(conf_path);

    /* 验证使用 Config 构造函数的默认值 */
    ASSERT_EQ(config.port, static_cast<uint16_t>(18080));
    ASSERT_EQ(config.log_path, std::string("./logs/register_student.log"));
    ASSERT_EQ(config.log_max_size, static_cast<size_t>(10485760));
    ASSERT_EQ(config.log_max_files, 5);
    ASSERT_EQ(config.db_path, std::string("./data/register_student.db"));
    ASSERT_EQ(config.log_db_path, std::string("./data/operation_log.db"));

    CleanupTempDir(dir);
}
