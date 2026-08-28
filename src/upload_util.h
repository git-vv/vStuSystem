#ifndef __UPLOAD_UTIL_H__
#define __UPLOAD_UTIL_H__

#include <string>
#include <cstdint>
#include <vector>

class UploadUtil {
public:
    /**
     * @brief 校验文件大小
     * @param file_size 文件大小（字节）
     * @return 0=合法, ERR_UPLOAD_SIZE_EXCEEDED=超限
     */
    static int ValidateSize(size_t file_size);

    /**
     * @brief 校验文件格式
     * @param filename 文件名（含扩展名）
     * @return 0=合法, ERR_UPLOAD_FORMAT_INVALID=不支持
     */
    static int ValidateFormat(const std::string& filename);

    /**
     * @brief 保存上传文件
     * @param upload_dir 配置的上传目录
     * @param filename 目标文件名
     * @param data 文件数据
     * @param data_size 数据大小
     * @param saved_path 输出保存后的相对路径
     * @return 0=成功, 错误码=失败
     */
    static int SaveFile(const std::string& upload_dir, const std::string& filename,
                        const char* data, size_t data_size, std::string& saved_path);

    /**
     * @brief 删除上传的物理文件
     * @param upload_dir 配置的上传目录（白名单根，绝对路径或相对 CWD）
     * @param saved_path SaveFile 返回的相对路径（如 /static/uploads/xxx.jpg）
     * @return 0=成功或文件不存在（仅记日志）, ERR_UPLOAD_PATH_NOT_CONFIGURED=路径越界/遍历
     */
    static int DeleteUploadedFile(const std::string& upload_dir, const std::string& saved_path);

    /**
     * @brief 获取允许的扩展名列表
     * @return 扩展名列表
     */
    static const std::vector<std::string>& GetAllowedExtensions();

private:
    static const size_t MAX_FILE_SIZE;
    static std::vector<std::string> allowed_extensions_;
};

#endif /* __UPLOAD_UTIL_H__ */
