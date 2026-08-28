#include "upload_util.h"
#include "error_codes.h"
#include "utils.h"

#include <sys/stat.h>
#include <cstring>
#include <algorithm>
#include <string>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#endif

const size_t UploadUtil::MAX_FILE_SIZE = 52428800; /* 50MB */

std::vector<std::string> UploadUtil::allowed_extensions_ = {
    "jpg", "jpeg", "png", "bmp", "gif", "webp", "svg", "tiff", "tif", "ico", "avif"
};

int UploadUtil::ValidateSize(size_t file_size) {
    if (file_size > MAX_FILE_SIZE) {
        return ERR_UPLOAD_SIZE_EXCEEDED;
    }
    return DB_OK;
}

int UploadUtil::ValidateFormat(const std::string& filename) {
    /* 查找最后一个点号，获取扩展名 */
    size_t pos = filename.rfind('.');
    if (pos == std::string::npos || pos == filename.size() - 1) {
        return ERR_UPLOAD_FORMAT_INVALID;
    }

    std::string ext = filename.substr(pos + 1);
    /* 转小写 */
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    const std::vector<std::string>& allowed = GetAllowedExtensions();
    for (size_t i = 0; i < allowed.size(); ++i) {
        if (ext == allowed[i]) {
            return DB_OK;
        }
    }

    return ERR_UPLOAD_FORMAT_INVALID;
}

#ifdef _WIN32
/* Windows: UTF-8 字符串转 UTF-16 wide string，用于 _wmkdir / _wfopen */
static std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) { return std::wstring(); }
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (wlen <= 0) { return std::wstring(); }
    std::wstring wide(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], wlen);
    if (wlen > 0 && wide[wlen - 1] == 0) { wide.pop_back(); }
    return wide;
}
#endif

int UploadUtil::SaveFile(const std::string& upload_dir, const std::string& filename,
                         const char* data, size_t data_size, std::string& saved_path) {
    /* 路径遍历校验：禁止..路径段 */
    if (filename.find("..") != std::string::npos) {
        return ERR_UPLOAD_FORMAT_INVALID;
    }

    /* 确保上传目录存在 */
    std::string dir = upload_dir;
    if (dir.empty()) {
        return ERR_UPLOAD_PATH_NOT_CONFIGURED;
    }

    /* 去除末尾斜杠 */
    while (!dir.empty() && (dir[dir.size() - 1] == '/' || dir[dir.size() - 1] == '\\')) {
        dir = dir.substr(0, dir.size() - 1);
    }

    /* 创建目录（如果不存在） */
    struct stat st;
    if (stat(dir.c_str(), &st) != 0) {
#ifdef _WIN32
        std::wstring wdir = Utf8ToWide(dir);
        _wmkdir(wdir.c_str());
#else
        mkdir(dir.c_str(), 0755);
#endif
    }

    /* 生成唯一文件名：时间戳_原始文件名 */
    std::string time_str = register_student::GetCurrentTimeString();
    /* 替换空格和冒号为下划线 */
    std::string safe_time;
    for (size_t i = 0; i < time_str.size(); ++i) {
        if (time_str[i] == ' ' || time_str[i] == ':') {
            safe_time.push_back('_');
        } else {
            safe_time.push_back(time_str[i]);
        }
    }

    std::string unique_name = safe_time + "_" + filename;
    std::string full_path = dir + "/" + unique_name;

    /* 写入文件 */
#ifdef _WIN32
    /* Windows: fopen 默认用 ANSI(GBK) 解析文件名，UTF-8 中文会乱码导致打开失败。
       用 _wfopen + UTF-16 wide string 解决 */
    std::wstring wfull_path = Utf8ToWide(full_path);
    FILE* fp = _wfopen(wfull_path.c_str(), L"wb");
#else
    FILE* fp = fopen(full_path.c_str(), "wb");
#endif
    if (!fp) {
        LOG_ERROR << "UploadUtil: failed to open file for writing: " << full_path;
        return ERR_UPLOAD_PATH_NOT_CONFIGURED;
    }

    size_t written = fwrite(data, 1, data_size, fp);
    fclose(fp);

    if (written != data_size) {
        LOG_ERROR << "UploadUtil: failed to write complete file data";
        return ERR_UPLOAD_PATH_NOT_CONFIGURED;
    }

    /* 返回相对路径（用于数据库存储和前端访问） */
    saved_path = "/static/uploads/" + unique_name;
    LOG_INFO << "UploadUtil: file saved: " << saved_path;
    return DB_OK;
}

const std::vector<std::string>& UploadUtil::GetAllowedExtensions() {
    return allowed_extensions_;
}

int UploadUtil::DeleteUploadedFile(const std::string& upload_dir, const std::string& saved_path) {
    /* 路径遍历防护：saved_path 含 .. 段则拒绝 */
    if (saved_path.find("..") != std::string::npos) {
        LOG_ERROR << "UploadUtil: path traversal detected in saved_path: " << saved_path;
        return ERR_UPLOAD_PATH_NOT_CONFIGURED;
    }

    if (upload_dir.empty()) {
        return ERR_UPLOAD_PATH_NOT_CONFIGURED;
    }

    /* 从 saved_path 提取文件名部分（/static/uploads/xxx.jpg -> xxx.jpg） */
    std::string filename;
    size_t last_sep = saved_path.find_last_of("/\\");
    if (last_sep == std::string::npos) {
        filename = saved_path;
    } else {
        filename = saved_path.substr(last_sep + 1);
    }

    if (filename.empty()) {
        return ERR_UPLOAD_PATH_NOT_CONFIGURED;
    }

    /* 拼接 upload_dir 与文件名，规范化目录末尾斜杠 */
    std::string dir = upload_dir;
    while (!dir.empty() && (dir[dir.size() - 1] == '/' || dir[dir.size() - 1] == '\\')) {
        dir = dir.substr(0, dir.size() - 1);
    }
    std::string full_path = dir + "/" + filename;

    /* 白名单校验：full_path 必须以 upload_dir 为前缀 */
    if (full_path.find(dir + "/") != 0 && full_path.find(dir + "\\") != 0 && full_path != dir) {
        LOG_ERROR << "UploadUtil: full_path not within upload_dir: " << full_path;
        return ERR_UPLOAD_PATH_NOT_CONFIGURED;
    }

    /* 删除文件：文件不存在视为成功（业务不阻断） */
#ifdef _WIN32
    std::wstring wfull_path = Utf8ToWide(full_path);
    int ret = _wremove(wfull_path.c_str());
#else
    int ret = std::remove(full_path.c_str());
#endif
    if (ret != 0) {
        /* 文件不存在或删除失败，仅记日志，不阻断业务 */
        LOG_ERROR << "UploadUtil: delete file failed (may not exist): " << full_path;
    } else {
        LOG_INFO << "UploadUtil: file deleted: " << full_path;
    }
    return DB_OK;
}
