#include "config.h"
#include "inih/INIReader.h"
#include "plog/Log.h"
#include "utils.h"

#include <fstream>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#endif

namespace register_student {

/* 默认配置文件内容（与 conf/register_student.conf 完全一致）
   首次启动时写入此内容，避免依赖安装包释放模板文件 */
const char* kDefaultConfigContent =
    "# registerStudent 服务配置文件\n"
    "\n"
    "[server]\n"
    "port = 18080\n"
    "# 域名配置（为空则自动获取服务器IP，配置后页面链接将使用域名）\n"
    "domain =\n"
    "\n"
    "[ssl]\n"
    "enabled = true\n"
    "cert = ./conf/ssl/server.crt\n"
    "key = ./conf/ssl/server.key\n"
    "\n"
    "[log]\n"
    "path = ./logs/register_student.log\n"
    "max_size = 10485760\n"
    "max_files = 5\n"
    "\n"
    "[db]\n"
    "path = ./data/register_student.db\n"
    "\n"
    "[upload]\n"
    "path = ./data/uploads\n"
    "\n"
    "[log_db]\n"
    "path = ./data/operation_log.db\n";

/* 平台无关的目录递归创建：按路径分隔符分割，逐级创建。
   Windows 下用 _wmkdir + wide 转换支持中文路径（_mkdir 走 ANSI 会失败） */
static void MakeDirRecursive(const std::string& path) {
    size_t pos = 0;
    while ((pos = path.find_first_of("/\\", pos + 1)) != std::string::npos) {
        std::string sub = path.substr(0, pos);
#ifdef _WIN32
        _wmkdir(Utf8ToWide(sub).c_str());
#else
        mkdir(sub.c_str(), 0755);
#endif
    }
#ifdef _WIN32
    _wmkdir(Utf8ToWide(path).c_str());
#else
    mkdir(path.c_str(), 0755);
#endif
}

Config::Config()
    : port(18080)
    , domain("")
    , log_path("./logs/register_student.log")
    , log_max_size(10485760)
    , log_max_files(5)
    , db_path("./data/register_student.db")
    , upload_path("")
    , log_db_path("./data/operation_log.db")
    , ssl_enabled(true)
    , ssl_cert("./conf/ssl/server.crt")
    , ssl_key("./conf/ssl/server.key") {
}

Config LoadConfig(const std::string& conf_path) {
    Config config;

    /* 配置文件不存在时，写入默认内容 */
#ifdef _WIN32
    /* MSVC 的 std::ifstream/ofstream 支持 wide char 路径扩展，用于支持中文路径 */
    std::wstring wconf_path = Utf8ToWide(conf_path);
    std::ifstream check(wconf_path);
#else
    std::ifstream check(conf_path);
#endif
    if (!check.is_open()) {
        LOG_INFO << "Config file not found: " << conf_path
                 << ", creating with default content";
        /* 递归创建父目录 */
        size_t pos = conf_path.find_last_of("/\\");
        if (pos != std::string::npos) {
            std::string dir = conf_path.substr(0, pos);
            MakeDirRecursive(dir);
        }
#ifdef _WIN32
        std::ofstream ofs(wconf_path);
#else
        std::ofstream ofs(conf_path);
#endif
        if (ofs.is_open()) {
            ofs << kDefaultConfigContent;
            ofs.close();
        } else {
            LOG_ERROR << "Failed to create default config: " << conf_path;
            /* 返回默认 Config，不视为致命错误（依赖默认值运行） */
            return config;
        }
    } else {
        check.close();
    }

    /* inih 的 INIReader(filename) 内部用 fopen(filename) 打开文件，fopen 走 ANSI
       码页，中文路径会打开失败返回 -1。改为先把文件内容读入内存（用 wide 路径
       ifstream），再用 buffer 构造 INIReader，绕过 fopen 的中文路径限制 */
    std::string file_content;
#ifdef _WIN32
    std::ifstream ifs(wconf_path, std::ios::binary);
#else
    std::ifstream ifs(conf_path, std::ios::binary);
#endif
    if (!ifs.is_open()) {
        LOG_ERROR << "Failed to reopen config for parse: " << conf_path;
        return config;
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    file_content = oss.str();
    ifs.close();

    INIReader reader(file_content.data(), file_content.size());
    if (reader.ParseError() != 0) {
        LOG_WARNING << "Failed to parse config file: " << conf_path
                    << ", error code: " << reader.ParseError()
                    << ", using defaults";
        return config;
    }

    config.port = static_cast<uint16_t>(reader.GetInteger("server", "port", config.port));
    config.domain = reader.GetString("server", "domain", config.domain);
    config.ssl_enabled = reader.GetBoolean("ssl", "enabled", config.ssl_enabled);
    config.ssl_cert = reader.GetString("ssl", "cert", config.ssl_cert);
    config.ssl_key = reader.GetString("ssl", "key", config.ssl_key);
    config.log_path = reader.GetString("log", "path", config.log_path);
    config.log_max_size = static_cast<size_t>(reader.GetUnsigned64("log", "max_size", config.log_max_size));
    config.log_max_files = static_cast<int>(reader.GetInteger("log", "max_files", config.log_max_files));
    config.db_path = reader.GetString("db", "path", config.db_path);
    config.upload_path = reader.GetString("upload", "path", config.upload_path);
    config.log_db_path = reader.GetString("log_db", "path", config.log_db_path);

    LOG_INFO << "Config loaded from: " << conf_path;
    LOG_INFO << "  server.port = " << config.port;
    LOG_INFO << "  server.domain = " << config.domain;
    LOG_INFO << "  ssl.enabled = " << config.ssl_enabled;
    LOG_INFO << "  ssl.cert = " << config.ssl_cert;
    LOG_INFO << "  ssl.key = " << config.ssl_key;
    LOG_INFO << "  log.path = " << config.log_path;
    LOG_INFO << "  log.max_size = " << config.log_max_size;
    LOG_INFO << "  log.max_files = " << config.log_max_files;
    LOG_INFO << "  db.path = " << config.db_path;
    LOG_INFO << "  upload.path = " << config.upload_path;
    LOG_INFO << "  log_db.path = " << config.log_db_path;

    return config;
}

} /* namespace register_student */
