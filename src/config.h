#ifndef __CONFIG_H__
#define __CONFIG_H__

#include <string>
#include <cstdint>

namespace register_student {

struct Config {
    uint16_t port;
    std::string domain;
    std::string log_path;
    size_t log_max_size;
    int log_max_files;
    std::string db_path;
    std::string upload_path;
    std::string log_db_path;
    bool ssl_enabled;
    std::string ssl_cert;
    std::string ssl_key;

    Config();
};

/**
 * @brief 默认配置文件内容（与 conf/register_student.conf 一致）
 *        用于首次启动时由程序生成默认配置文件，不依赖安装包释放模板
 */
extern const char* kDefaultConfigContent;

/**
 * @brief 从配置文件加载配置
 * @param conf_path 配置文件路径
 * @return 加载后的Config结构体，解析失败的字段使用默认值
 */
Config LoadConfig(const std::string& conf_path);

} /* namespace register_student */

#endif /* __CONFIG_H__ */
