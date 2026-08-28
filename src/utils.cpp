#include "utils.h"

#include <plog/Appenders/ConsoleAppender.h>
#include <plog/Appenders/RollingFileAppender.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Init.h>

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <ctime>
#include <chrono>
#include <random>
#include <openssl/rand.h>

#ifdef _WIN32
/* Windows 系统头文件必须在命名空间外包含，避免 API 被错误地塞进 register_student 命名空间 */
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include "error_codes.h"
#endif

namespace register_student {

/* ====== SHA256 实现（纯C++，不依赖OpenSSL） ====== */

namespace {

struct Sha256Context {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t buffer[64];
    size_t buffer_len;
};

const uint32_t SHA256_K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define SHA256_ROTR(a, b) (((a) >> (b)) | ((a) << (32 - (b))))

void Sha256Init(Sha256Context& ctx) {
    ctx.state[0] = 0x6a09e667;
    ctx.state[1] = 0xbb67ae85;
    ctx.state[2] = 0x3c6ef372;
    ctx.state[3] = 0xa54ff53a;
    ctx.state[4] = 0x510e527f;
    ctx.state[5] = 0x9b05688c;
    ctx.state[6] = 0x1f83d9ab;
    ctx.state[7] = 0x5be0cd19;
    ctx.bitlen = 0;
    ctx.buffer_len = 0;
}

void Sha256Transform(Sha256Context& ctx, const uint8_t data[64]) {
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h, t1, t2;
    int32_t i;

    for (i = 0; i < 16; ++i) {
        w[i] = ((uint32_t)data[i * 4] << 24) |
               ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] << 8) |
               ((uint32_t)data[i * 4 + 3]);
    }
    for (i = 16; i < 64; ++i) {
        uint32_t s0 = SHA256_ROTR(w[i - 15], 7) ^ SHA256_ROTR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = SHA256_ROTR(w[i - 2], 17) ^ SHA256_ROTR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    a = ctx.state[0];
    b = ctx.state[1];
    c = ctx.state[2];
    d = ctx.state[3];
    e = ctx.state[4];
    f = ctx.state[5];
    g = ctx.state[6];
    h = ctx.state[7];

    for (i = 0; i < 64; ++i) {
        uint32_t S1 = SHA256_ROTR(e, 6) ^ SHA256_ROTR(e, 11) ^ SHA256_ROTR(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        t1 = h + S1 + ch + SHA256_K[i] + w[i];
        uint32_t S0 = SHA256_ROTR(a, 2) ^ SHA256_ROTR(a, 13) ^ SHA256_ROTR(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        t2 = S0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx.state[0] += a;
    ctx.state[1] += b;
    ctx.state[2] += c;
    ctx.state[3] += d;
    ctx.state[4] += e;
    ctx.state[5] += f;
    ctx.state[6] += g;
    ctx.state[7] += h;
}

void Sha256Update(Sha256Context& ctx, const uint8_t* data, size_t len) {
    size_t i;
    for (i = 0; i < len; ++i) {
        ctx.buffer[ctx.buffer_len++] = data[i];
        if (ctx.buffer_len == 64) {
            Sha256Transform(ctx, ctx.buffer);
            ctx.bitlen += 512;
            ctx.buffer_len = 0;
        }
    }
}

void Sha256Final(Sha256Context& ctx, uint8_t hash[32]) {
    uint32_t i;

    /* 追加0x80 */
    ctx.buffer[ctx.buffer_len++] = 0x80;

    /* 如果剩余空间不足8字节，先填满一块 */
    if (ctx.buffer_len > 56) {
        while (ctx.buffer_len < 64) {
            ctx.buffer[ctx.buffer_len++] = 0x00;
        }
        Sha256Transform(ctx, ctx.buffer);
        ctx.buffer_len = 0;
    }

    /* 填充0到56字节 */
    while (ctx.buffer_len < 56) {
        ctx.buffer[ctx.buffer_len++] = 0x00;
    }

    /* 追加长度（大端） */
    ctx.bitlen += (uint64_t)ctx.buffer_len * 8;
    ctx.buffer[56] = (uint8_t)(ctx.bitlen >> 56);
    ctx.buffer[57] = (uint8_t)(ctx.bitlen >> 48);
    ctx.buffer[58] = (uint8_t)(ctx.bitlen >> 40);
    ctx.buffer[59] = (uint8_t)(ctx.bitlen >> 32);
    ctx.buffer[60] = (uint8_t)(ctx.bitlen >> 24);
    ctx.buffer[61] = (uint8_t)(ctx.bitlen >> 16);
    ctx.buffer[62] = (uint8_t)(ctx.bitlen >> 8);
    ctx.buffer[63] = (uint8_t)(ctx.bitlen);
    Sha256Transform(ctx, ctx.buffer);

    /* 输出大端哈希 */
    for (i = 0; i < 4; ++i) {
        hash[i]      = (ctx.state[0] >> (24 - i * 8)) & 0xff;
        hash[i + 4]  = (ctx.state[1] >> (24 - i * 8)) & 0xff;
        hash[i + 8]  = (ctx.state[2] >> (24 - i * 8)) & 0xff;
        hash[i + 12] = (ctx.state[3] >> (24 - i * 8)) & 0xff;
        hash[i + 16] = (ctx.state[4] >> (24 - i * 8)) & 0xff;
        hash[i + 20] = (ctx.state[5] >> (24 - i * 8)) & 0xff;
        hash[i + 24] = (ctx.state[6] >> (24 - i * 8)) & 0xff;
        hash[i + 28] = (ctx.state[7] >> (24 - i * 8)) & 0xff;
    }
}

std::string BytesToHex(const uint8_t* data, size_t len) {
    static const char hex_chars[] = "0123456789abcdef";
    std::string result;
    result.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        result.push_back(hex_chars[(data[i] >> 4) & 0x0f]);
        result.push_back(hex_chars[data[i] & 0x0f]);
    }
    return result;
}

} /* anonymous namespace */

/* ====== 日志初始化 ====== */

void InitLog(const char* log_file, size_t max_size, int max_files) {
    static plog::ConsoleAppender<plog::TxtFormatter> console_appender;

    if (log_file != nullptr && log_file[0] != '\0') {
        static plog::RollingFileAppender<plog::TxtFormatter> file_appender(
            log_file, max_size, max_files);
        plog::init(plog::debug, &console_appender);
        plog::get()->addAppender(&file_appender);
    } else {
        plog::init(plog::debug, &console_appender);
    }
}

/* ====== 密码加密 ====== */

std::string GenerateSalt() {
    uint8_t bytes[16];
    if (RAND_bytes(bytes, sizeof(bytes)) == 1) {
        return BytesToHex(bytes, sizeof(bytes));
    }

    /* 回退方案：使用时间+rand */
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
    for (size_t i = 0; i < sizeof(bytes); ++i) {
        bytes[i] = static_cast<uint8_t>(std::rand() & 0xff);
    }
    return BytesToHex(bytes, sizeof(bytes));
}

std::string EncryptPassword(const std::string& password, const std::string& salt) {
    /* 哈希 = SHA256(password + salt) */
    std::string combined = password + salt;

    Sha256Context ctx;
    Sha256Init(ctx);
    Sha256Update(ctx, reinterpret_cast<const uint8_t*>(combined.data()), combined.size());

    uint8_t hash[32];
    Sha256Final(ctx, hash);

    return BytesToHex(hash, sizeof(hash));
}

bool VerifyPassword(const std::string& password, const std::string& salt, const std::string& expected_hash) {
    std::string actual_hash = EncryptPassword(password, salt);
    return actual_hash == expected_hash;
}

/* ====== 时间工具 ====== */

std::string GetCurrentTimeString() {
    std::time_t now = std::time(nullptr);
    std::tm tm_now;
#ifdef _WIN32
    localtime_s(&tm_now, &now);
#else
    localtime_r(&now, &tm_now);
#endif

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                  tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
                  tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
    return std::string(buf);
}

std::string GetCurrentDateString() {
    std::time_t now = std::time(nullptr);
    std::tm tm_now;
#ifdef _WIN32
    localtime_s(&tm_now, &now);
#else
    localtime_r(&now, &tm_now);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                  tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday);
    return std::string(buf);
}

/* @brief 解析 YYYY-MM-DD 为 std::tm，失败返回 false */
static bool ParseYMD(const std::string& ymd, std::tm& out) {
    if (ymd.size() != 10) { return false; }
    if (ymd[4] != '-' || ymd[7] != '-') { return false; }
    for (size_t i = 0; i < ymd.size(); ++i) {
        if (i == 4 || i == 7) { continue; }
        if (ymd[i] < '0' || ymd[i] > '9') { return false; }
    }
    std::memset(&out, 0, sizeof(out));
    out.tm_year = std::atoi(ymd.substr(0, 4).c_str()) - 1900;
    out.tm_mon  = std::atoi(ymd.substr(5, 2).c_str()) - 1;
    out.tm_mday = std::atoi(ymd.substr(8, 2).c_str());
    out.tm_hour = 12;  /* 中午避免夏令时边界 */
    return out.tm_year >= 0 && out.tm_mon >= 0 && out.tm_mon <= 11
        && out.tm_mday >= 1 && out.tm_mday <= 31;
}

int CountWeekdaysInRange(const std::string& start_ymd, const std::string& end_ymd) {
    std::tm tm_start;
    std::tm tm_end;
    if (!ParseYMD(start_ymd, tm_start)) { return 0; }
    if (!ParseYMD(end_ymd, tm_end)) { return 0; }

    std::time_t t_start = std::mktime(&tm_start);
    std::time_t t_end = std::mktime(&tm_end);
    if (t_start == static_cast<std::time_t>(-1) || t_end == static_cast<std::time_t>(-1)) {
        return 0;
    }
    if (t_end < t_start) { return 0; }

    int weekdays = 0;
    const int one_day_seconds = 24 * 60 * 60;
    for (std::time_t t = t_start; t <= t_end; t += one_day_seconds) {
        std::tm tm_cur;
#ifdef _WIN32
        localtime_s(&tm_cur, &t);
#else
        localtime_r(&t, &tm_cur);
#endif
        /* tm_wday: 0=Sunday, 6=Saturday */
        if (tm_cur.tm_wday != 0 && tm_cur.tm_wday != 6) {
            ++weekdays;
        }
    }
    return weekdays;
}

/* @brief 性别字符串转中文标签（male→男, female→女, 其他原样返回） */
static std::string GenderLabel(const std::string& gender) {
    if (gender == "male") { return "\xE7\x94\xB7"; }   /* 男 */
    if (gender == "female") { return "\xE5\xA5\xB3"; } /* 女 */
    return gender;
}

/* @brief 拼接单字段 diff 片段，无变化返回空串 */
static std::string AppendFieldDiff(const std::string& label,
                                   const std::string& old_val,
                                   const std::string& new_val) {
    if (old_val == new_val) { return std::string(); }
    return label + ": " + old_val + "\xE2\x86\x92" + new_val;  /* → */
}

std::string BuildStudentDiff(const RegistrationInfo& old_info,
                             const RegistrationInfo& new_info,
                             const std::string& old_class_name,
                             const std::string& new_class_name) {
    std::string result;
    const std::string sep = "; ";

    /* 字段顺序固定：姓名 → 性别 → 家长电话 → 过敏 → 班级 → 备注 → 负责教师 */
    std::string item;

    /* 1. 姓名 */
    item = AppendFieldDiff("\xE5\xA7\x93\xE5\x90\x8D", old_info.student_name, new_info.student_name); /* 姓名 */
    if (!item.empty()) { result += (result.empty() ? "" : sep) + item; }

    /* 2. 性别 */
    item = AppendFieldDiff("\xE6\x80\xA7\xE5\x88\xAB", GenderLabel(old_info.student_gender), GenderLabel(new_info.student_gender)); /* 性别 */
    if (!item.empty()) { result += (result.empty() ? "" : sep) + item; }

    /* 3. 家长电话 */
    item = AppendFieldDiff("\xE5\xAE\xB6\xE9\x95\xBF\xE7\x94\xB5\xE8\xAF\x9D", old_info.parent_phone, new_info.parent_phone); /* 家长电话 */
    if (!item.empty()) { result += (result.empty() ? "" : sep) + item; }

    /* 4. 过敏（has_allergy + allergy_desc 联合判断） */
    std::string old_allergy = (old_info.has_allergy == 1) ? "\xE6\x98\xAF" : "\xE5\x90\xA6"; /* 是/否 */
    std::string new_allergy = (new_info.has_allergy == 1) ? "\xE6\x98\xAF" : "\xE5\x90\xA6";
    bool allergy_changed = (old_info.has_allergy != new_info.has_allergy);
    bool desc_changed = (old_info.allergy_desc != new_info.allergy_desc);
    if (allergy_changed) {
        item = "\xE8\xBF\x87\xE6\x95\x8F" + std::string(": ") + old_allergy + "\xE2\x86\x92" + new_allergy; /* 过敏 */
        result += (result.empty() ? "" : sep) + item;
    }
    if (allergy_changed || desc_changed) {
        item = "\xE8\xBF\x87\xE6\x95\x8F\xE6\x8F\x8F\xE8\xBF\xB0" + std::string(": ") + old_info.allergy_desc + "\xE2\x86\x92" + new_info.allergy_desc; /* 过敏描述 */
        result += (result.empty() ? "" : sep) + item;
    }

    /* 5. 班级 */
    if (old_info.class_id != new_info.class_id) {
        item = "\xE7\x8F\xAD\xE7\xBA\xA7" + std::string(": ") + old_class_name + "\xE2\x86\x92" + new_class_name; /* 班级 */
        result += (result.empty() ? "" : sep) + item;
    }

    /* 6. 备注 */
    item = AppendFieldDiff("\xE5\xA4\x87\xE6\xB3\xA8", old_info.other_info, new_info.other_info); /* 备注 */
    if (!item.empty()) { result += (result.empty() ? "" : sep) + item; }

    /* 7. 负责教师 */
    item = AppendFieldDiff("\xE8\xB4\x9F\xE8\xB4\xA3\xE6\x95\x99\xE5\xB8\x88", old_info.teacher_name, new_info.teacher_name); /* 负责教师 */
    if (!item.empty()) { result += (result.empty() ? "" : sep) + item; }

    /* 截断：超过 40 字符（按字节）截断为 37 + "..." */
    const size_t kMaxLen = 40;
    if (result.size() > kMaxLen) {
        result = result.substr(0, 37) + "...";
    }

    return result;
}

/* ====== 邀请码生成 ====== */

std::string GenerateInviteCode() {
    static const char charset[] = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";
    char code[7];
    code[6] = '\0';

    unsigned char bytes[6];
    if (RAND_bytes(bytes, 6) == 1) {
        for (int i = 0; i < 6; ++i) {
            unsigned int val;
            do {
                val = bytes[i] & 0x1F;
            } while (val >= 31); /* charset has 31 chars, reject val=31 */
            code[i] = charset[val];
        }
    } else {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dist(0, 30); /* 0-30 for 31 characters */
        for (int i = 0; i < 6; ++i) {
            code[i] = charset[dist(gen)];
        }
    }

    for (int i = 0; i < 6; ++i) {
        char c = code[i];
        bool valid = (c >= '2' && c <= '9') || (c >= 'A' && c <= 'Z' && c != 'I' && c != 'L' && c != 'O');
        if (!valid) {
            LOG_ERROR << "GenerateInviteCode: invalid char detected, code=" << code;
            return "";
        }
    }
    return std::string(code, 6);
}

#ifdef _WIN32
/* ====== Windows 平台辅助方法 ====== */

/* UTF-8 转 wide string。Windows ANSI API（CreateDirectoryA、SetCurrentDirectoryA、_mkdir）
   遇到非系统 ANSI 码页字符（如中文路径）会失败。改用 wide 版本 API，需先将 UTF-8 转 wide。
   无效字符替换为 U+FFEF，不返回失败。 */
std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) { return std::wstring(); }
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (wlen <= 0) { return std::wstring(); }
    std::wstring wide(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], wlen);
    if (wlen > 0 && wide[wlen - 1] == 0) { wide.pop_back(); }
    return wide;
}

std::string GetModulePath() {
    wchar_t buf[MAX_PATH] = {0};
    DWORD len = GetModuleFileNameW(NULL, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return std::string();
    }
    /* 截取最后一个反斜杠前的内容 */
    wchar_t* last_sep = wcsrchr(buf, L'\\');
    if (last_sep != nullptr) {
        *last_sep = L'\0';
    }
    /* wide 转 UTF-8 返回，统一与外部 std::string 接口 */
    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
    if (utf8_len <= 0) { return std::string(); }
    std::string result(utf8_len, 0);
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, &result[0], utf8_len, nullptr, nullptr);
    if (utf8_len > 0 && result[utf8_len - 1] == '\0') { result.pop_back(); }
    return result;
}

std::string GetAppDataPath() {
    /* 兼容旧接口：返回 exe 所在目录，而非 %APPDATA%\registerStudent。
       原因：用户要求支持自定义安装目录（含中文路径），数据必须跟着 exe 走，
       不能再硬编码到 APPDATA 下。此函数保留命名以减少调用点改动，语义已变。 */
    return GetModulePath();
}

int EnsureDataDirs(const std::string& root) {
    /* 用 wide API 创建目录，支持中文路径（ANSI 版本会失败） */
    std::wstring wroot = Utf8ToWide(root);
    if (wroot.empty()) { return ERR_INVALID_PARAM; }
    std::wstring wdata = wroot + L"\\data";
    std::wstring wlogs = wroot + L"\\logs";
    std::wstring wuploads = wroot + L"\\data\\uploads";

    /* CreateDirectoryW 不会递归创建父目录，但 root 是 exe 所在目录必然已存在，
       故只需创建子目录。仍尝试创建 root 以兼容首次启动场景 */
    if (!CreateDirectoryW(wroot.c_str(), NULL) &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
        return ERR_PLATFORM_INTERNAL;
    }
    if (!CreateDirectoryW(wdata.c_str(), NULL) &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
        return ERR_PLATFORM_INTERNAL;
    }
    if (!CreateDirectoryW(wlogs.c_str(), NULL) &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
        return ERR_PLATFORM_INTERNAL;
    }
    if (!CreateDirectoryW(wuploads.c_str(), NULL) &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
        return ERR_PLATFORM_INTERNAL;
    }
    return 0;
}

int OpenBrowser(const std::string& url) {
    if (url.size() < 7) {
        return ERR_INVALID_PARAM;
    }
    if (url.compare(0, 7, "http://") != 0 &&
        url.compare(0, 8, "https://") != 0) {
        return ERR_INVALID_PARAM;
    }
    HINSTANCE ret = ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
    /* ShellExecuteA 返回值 > 32 表示成功 */
    if (reinterpret_cast<INT_PTR>(ret) <= 32) {
        return ERR_PLATFORM_INTERNAL;
    }
    return 0;
}

static const char* kAutoStartRunKey =
    "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const char* kAutoStartValueName = "vStuSystem";

int SetAutoStart(bool enable) {
    HKEY hKey = nullptr;
    LONG ret = RegOpenKeyExW(HKEY_CURRENT_USER,
                              std::wstring(kAutoStartRunKey, kAutoStartRunKey + strlen(kAutoStartRunKey)).c_str(),
                              0, KEY_SET_VALUE, &hKey);
    if (ret != ERROR_SUCCESS) {
        return ERR_PLATFORM_INTERNAL;
    }

    if (enable) {
        wchar_t exe_path[MAX_PATH] = {0};
        DWORD len = GetModuleFileNameW(NULL, exe_path, MAX_PATH);
        if (len == 0 || len >= MAX_PATH) {
            RegCloseKey(hKey);
            return ERR_PLATFORM_INTERNAL;
        }
        /* exe_path 是 wide string，直接写入注册表；用 wide 版本支持中文路径 */
        std::wstring value_name(kAutoStartValueName, kAutoStartValueName + strlen(kAutoStartValueName));
        ret = RegSetValueExW(hKey, value_name.c_str(), 0, REG_SZ,
                              reinterpret_cast<const BYTE*>(exe_path),
                              (len + 1) * sizeof(wchar_t));
    } else {
        std::wstring value_name(kAutoStartValueName, kAutoStartValueName + strlen(kAutoStartValueName));
        ret = RegDeleteValueW(hKey, value_name.c_str());
        /* 值不存在视为成功 */
        if (ret == ERROR_FILE_NOT_FOUND) {
            ret = ERROR_SUCCESS;
        }
    }

    RegCloseKey(hKey);
    return (ret == ERROR_SUCCESS) ? 0 : ERR_PLATFORM_INTERNAL;
}

bool IsAutoStartEnabled() {
    HKEY hKey = nullptr;
    std::wstring wkey(kAutoStartRunKey, kAutoStartRunKey + strlen(kAutoStartRunKey));
    LONG ret = RegOpenKeyExW(HKEY_CURRENT_USER, wkey.c_str(),
                              0, KEY_QUERY_VALUE, &hKey);
    if (ret != ERROR_SUCCESS) {
        return false;
    }

    std::wstring wvalue(kAutoStartValueName, kAutoStartValueName + strlen(kAutoStartValueName));
    DWORD value_type = 0;
    DWORD value_len = 0;
    ret = RegQueryValueExW(hKey, wvalue.c_str(), NULL,
                            &value_type, NULL, &value_len);
    RegCloseKey(hKey);
    return (ret == ERROR_SUCCESS && value_type == REG_SZ && value_len > 0);
}

/* ====== 路径内容迁移（用户修改配置后重启时把旧路径内容拷贝到新路径） ======
 *
 * 保留策略（用户要求）：
 *   - 第 N 次修改配置（N >= 2）后重启时，把第 N-1 次的路径内容拷贝到第 N 次路径。
 *   - 第 N 次修改时，第 N-2 次（更早一次）的旧文件要删除。
 *   - 即：始终保留"当前"和"上一次"两代路径的文件，更早的删除。
 *
 * sidecar 文件 <conf 同目录>/.last_paths（INI 格式）：
 *   [paths]        ← 上一次启动的路径（prev 一代）
 *   db_path=...
 *   log_db_path=...
 *   upload_path=...
 *   log_path=...
 *   [older_paths]  ← 上上次启动的路径（older 一代，第 3 次启动时清理）
 *   db_path=...
 *   log_db_path=...
 *   upload_path=...
 *   log_path=... */

struct PathSet {
    std::string db_path;
    std::string log_db_path;
    std::string upload_path;
    std::string log_path;
};

/* @brief 把相对路径转为基于当前工作目录的绝对路径（UTF-8）。
   CWD 已在 WinMain 中锚定到 exe 所在目录，所以相对路径会正确解析。
   已是绝对路径则原样返回。 */
static std::string ToAbsolute(const std::string& path) {
    if (path.empty()) { return std::string(); }
    if (path.size() >= 2 && ((path[0] >= 'a' && path[0] <= 'z') ||
        (path[0] >= 'A' && path[0] <= 'Z')) && path[1] == ':') {
        return path;  /* 已是 Windows 绝对路径 */
    }
    if (path[0] == '\\' || path[0] == '/') { return path; }  /* UNC 或根路径 */

    std::wstring wpath = Utf8ToWide(path);
    wchar_t buf[MAX_PATH] = {0};
    DWORD len = GetFullPathNameW(wpath.c_str(), MAX_PATH, buf, NULL);
    if (len == 0 || len >= MAX_PATH) { return path; /* 失败则回退原值 */ }
    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
    if (utf8_len <= 0) { return path; }
    std::string result(utf8_len, 0);
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, &result[0], utf8_len, nullptr, nullptr);
    if (utf8_len > 0 && result[utf8_len - 1] == '\0') { result.pop_back(); }
    return result;
}

/* @brief 递归拷贝目录内容（old_dir → new_dir），不删除 old_dir。
   单文件失败不中断整体流程，仅记录 WARNING。 */
static void CopyDirectoryRecursive(const std::wstring& wold_dir,
                                   const std::wstring& wnew_dir) {
    /* 确保目标目录存在 */
    CreateDirectoryW(wnew_dir.c_str(), NULL);

    std::wstring pattern = wold_dir + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        LOG_WARNING << "MigratePaths: FindFirstFile failed on dir_len="
                    << wold_dir.length();
        return;
    }
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") { continue; }
        std::wstring old_path = wold_dir + L"\\" + name;
        std::wstring new_path = wnew_dir + L"\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            CopyDirectoryRecursive(old_path, new_path);
        } else {
            /* FALSE=不覆盖已存在的目标文件（避免误覆盖新路径下已有数据） */
            if (!CopyFileW(old_path.c_str(), new_path.c_str(), FALSE)) {
                DWORD err = GetLastError();
                LOG_WARNING << "MigratePaths: CopyFileW failed, err=" << err
                            << ", src_len=" << old_path.length();
            }
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

/* @brief 递归删除目录及其所有内容 */
static void RemoveDirectoryRecursive(const std::wstring& wdir) {
    std::wstring pattern = wdir + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..") { continue; }
            std::wstring full = wdir + L"\\" + name;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                RemoveDirectoryRecursive(full);
            } else {
                if (!DeleteFileW(full.c_str())) {
                    DWORD err = GetLastError();
                    LOG_WARNING << "MigratePaths: DeleteFileW failed, err=" << err
                                << ", path_len=" << full.length();
                }
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }
    RemoveDirectoryW(wdir.c_str());
}

/* @brief 拷贝单个文件（db / log_db / log 文件），不删除旧文件。
   若旧文件不存在视为已迁移完成（无操作）。 */
static void CopySingleFile(const std::string& old_path,
                           const std::string& new_path) {
    std::wstring wold = Utf8ToWide(old_path);
    std::wstring wnew = Utf8ToWide(new_path);
    DWORD attr = GetFileAttributesW(wold.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        /* 旧文件不存在，无需拷贝 */
        return;
    }
    /* 确保目标父目录存在 */
    size_t pos = new_path.find_last_of("/\\");
    if (pos != std::string::npos) {
        std::string dir = new_path.substr(0, pos);
        std::wstring wdir = Utf8ToWide(dir);
        /* 递归创建父目录 */
        size_t start = 0;
        while ((start = dir.find_first_of("/\\", start + 1)) != std::string::npos) {
            std::wstring sub = Utf8ToWide(dir.substr(0, start));
            CreateDirectoryW(sub.c_str(), NULL);
        }
        CreateDirectoryW(wdir.c_str(), NULL);
    }
    /* FALSE=不覆盖已存在的目标文件 */
    if (!CopyFileW(wold.c_str(), wnew.c_str(), FALSE)) {
        DWORD err = GetLastError();
        LOG_WARNING << "MigratePaths: CopyFileW failed, err=" << err
                    << ", old=" << old_path << ", new=" << new_path;
    }
}

/* @brief 删除单个文件（若存在），用于清理 older 一代文件 */
static void DeleteSingleFile(const std::string& path) {
    if (path.empty()) { return; }
    std::wstring wpath = Utf8ToWide(path);
    DWORD attr = GetFileAttributesW(wpath.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) { return; }  /* 不存在，无需删除 */
    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        RemoveDirectoryRecursive(wpath);
    } else {
        if (!DeleteFileW(wpath.c_str())) {
            DWORD err = GetLastError();
            LOG_WARNING << "MigratePaths: DeleteFileW (cleanup) failed, err=" << err
                        << ", path=" << path;
        }
    }
}

/* @brief 从 conf_path 推导 sidecar 文件路径（与 conf 同目录，文件名 .last_paths） */
static std::string GetLastPathsFilePath(const std::string& conf_path) {
    size_t pos = conf_path.find_last_of("/\\");
    if (pos == std::string::npos) { return ".last_paths"; }
    return conf_path.substr(0, pos) + "/.last_paths";
}

/* @brief 解析 sidecar 文件（INI 格式），返回 [paths] 和 [older_paths] 两个 section。
   sidecar 不存在时两 section 均为空（视为首次启动）。 */
static void LoadLastPaths(const std::string& sidecar_path,
                          PathSet& prev,
                          PathSet& older) {
    std::wstring wpath = Utf8ToWide(sidecar_path);
    std::ifstream ifs(wpath, std::ios::binary);
    if (!ifs.is_open()) { return; }
    std::string line;
    std::string current_section;
    while (std::getline(ifs, line)) {
        if (!line.empty() && line.back() == '\r') { line.pop_back(); }
        size_t s = line.find_first_not_of(" \t");
        size_t e = line.find_last_not_of(" \t");
        if (s == std::string::npos) { continue; }
        line = line.substr(s, e - s + 1);
        if (line.empty() || line[0] == '#' || line[0] == ';') { continue; }
        if (line[0] == '[' && line.back() == ']') {
            current_section = line.substr(1, line.size() - 2);
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) { continue; }
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if (current_section == "paths") {
            if (key == "db_path") { prev.db_path = val; }
            else if (key == "log_db_path") { prev.log_db_path = val; }
            else if (key == "upload_path") { prev.upload_path = val; }
            else if (key == "log_path") { prev.log_path = val; }
        } else if (current_section == "older_paths") {
            if (key == "db_path") { older.db_path = val; }
            else if (key == "log_db_path") { older.log_db_path = val; }
            else if (key == "upload_path") { older.upload_path = val; }
            else if (key == "log_path") { older.log_path = val; }
        }
    }
}

/* @brief 写 sidecar 文件（覆盖写），记录 prev 和 older 两代路径 */
static int SaveLastPaths(const std::string& sidecar_path,
                         const PathSet& prev,
                         const PathSet& older) {
    std::wstring wpath = Utf8ToWide(sidecar_path);
    std::ofstream ofs(wpath, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        LOG_ERROR << "MigratePaths: failed to write sidecar " << sidecar_path;
        return ERR_PLATFORM_INTERNAL;
    }
    ofs << "[paths]\n";
    ofs << "db_path=" << prev.db_path << "\n";
    ofs << "log_db_path=" << prev.log_db_path << "\n";
    ofs << "upload_path=" << prev.upload_path << "\n";
    ofs << "log_path=" << prev.log_path << "\n";
    ofs << "[older_paths]\n";
    ofs << "db_path=" << older.db_path << "\n";
    ofs << "log_db_path=" << older.log_db_path << "\n";
    ofs << "upload_path=" << older.upload_path << "\n";
    ofs << "log_path=" << older.log_path << "\n";
    ofs.close();
    return 0;
}

int MigratePathsIfNeeded(const Config& config, const std::string& conf_path) {
    std::string sidecar_path = GetLastPathsFilePath(conf_path);
    PathSet prev;
    PathSet older;
    LoadLastPaths(sidecar_path, prev, older);

    /* 计算本次启动的 4 个绝对路径 */
    PathSet cur;
    cur.db_path = ToAbsolute(config.db_path);
    cur.log_db_path = ToAbsolute(config.log_db_path);
    cur.upload_path = ToAbsolute(config.upload_path);
    cur.log_path = ToAbsolute(config.log_path);

    bool first_launch = prev.db_path.empty() && prev.log_db_path.empty()
                       && prev.upload_path.empty() && prev.log_path.empty();
    if (first_launch) {
        LOG_INFO << "MigratePaths: first launch, recording paths without migration";
        return SaveLastPaths(sidecar_path, cur, PathSet());
    }

    /* 1. 拷贝 prev → cur（每个变更的路径） */
    bool any_migrated = false;
    if (!cur.db_path.empty() && prev.db_path != cur.db_path && !prev.db_path.empty()) {
        LOG_INFO << "MigratePaths: db_path changed, " << prev.db_path
                 << " -> " << cur.db_path;
        CopySingleFile(prev.db_path, cur.db_path);
        any_migrated = true;
    }
    if (!cur.log_db_path.empty() && prev.log_db_path != cur.log_db_path && !prev.log_db_path.empty()) {
        LOG_INFO << "MigratePaths: log_db_path changed, " << prev.log_db_path
                 << " -> " << cur.log_db_path;
        CopySingleFile(prev.log_db_path, cur.log_db_path);
        any_migrated = true;
    }
    if (!cur.log_path.empty() && prev.log_path != cur.log_path && !prev.log_path.empty()) {
        LOG_INFO << "MigratePaths: log_path changed, " << prev.log_path
                 << " -> " << cur.log_path;
        CopySingleFile(prev.log_path, cur.log_path);
        any_migrated = true;
    }
    if (!cur.upload_path.empty() && prev.upload_path != cur.upload_path && !prev.upload_path.empty()) {
        LOG_INFO << "MigratePaths: upload_path changed, " << prev.upload_path
                 << " -> " << cur.upload_path;
        std::wstring wold = Utf8ToWide(prev.upload_path);
        std::wstring wnew = Utf8ToWide(cur.upload_path);
        CopyDirectoryRecursive(wold, wnew);
        any_migrated = true;
    }

    /* 2. 清理 older 一代（上次的上次的路径）的文件，保留 prev 和 cur 两代。
       older 在第 3 次启动（第 2 次修改）时才会被填充并触发清理。 */
    bool any_cleaned = false;
    if (!older.db_path.empty() && older.db_path != cur.db_path && older.db_path != prev.db_path) {
        LOG_INFO << "MigratePaths: cleanup older db_path " << older.db_path;
        DeleteSingleFile(older.db_path);
        any_cleaned = true;
    }
    if (!older.log_db_path.empty() && older.log_db_path != cur.log_db_path && older.log_db_path != prev.log_db_path) {
        LOG_INFO << "MigratePaths: cleanup older log_db_path " << older.log_db_path;
        DeleteSingleFile(older.log_db_path);
        any_cleaned = true;
    }
    if (!older.log_path.empty() && older.log_path != cur.log_path && older.log_path != prev.log_path) {
        LOG_INFO << "MigratePaths: cleanup older log_path " << older.log_path;
        DeleteSingleFile(older.log_path);
        any_cleaned = true;
    }
    if (!older.upload_path.empty() && older.upload_path != cur.upload_path && older.upload_path != prev.upload_path) {
        LOG_INFO << "MigratePaths: cleanup older upload_path " << older.upload_path;
        DeleteSingleFile(older.upload_path);
        any_cleaned = true;
    }

    if (any_migrated) {
        LOG_INFO << "MigratePaths: migration completed (prev retained)";
    }
    if (any_cleaned) {
        LOG_INFO << "MigratePaths: older generation cleaned";
    }

    /* 3. 推换代：older = prev，prev = cur，写入 sidecar */
    return SaveLastPaths(sidecar_path, cur, prev);
}

#endif /* _WIN32 */

/* ====== 网络信息获取 ====== */

#ifdef _WIN32

/**
 * @brief Windows 实现：使用 GetAdaptersAddresses 获取本机局域网网络信息
 * @note  CMakeLists.txt 定义了 WIN32_LEAN_AND_MEAN，阻止 <windows.h> 包含
 *        <winsock.h>(Winsock 1.x)，因此此处可安全包含 <winsock2.h>。
 *        <iphlpapi.h> 需要 winsock2.h 支持。
 */
#include <winsock2.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>

NetworkInfo GetLocalNetworkInfo() {
    NetworkInfo info;

    /* 获取主机名 */
    char hostname_buf[256] = {0};
    DWORD hostname_len = sizeof(hostname_buf);
    if (GetComputerNameA(hostname_buf, &hostname_len)) {
        info.hostname = std::string(hostname_buf, hostname_len);
    }

    /* 获取适配器信息 */
    ULONG buf_len = 0;
    ULONG ret = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX,
                                      NULL, NULL, &buf_len);
    if (ret != ERROR_BUFFER_OVERFLOW && ret != NO_ERROR) {
        LOG_WARNING << "GetLocalNetworkInfo: GetAdaptersAddresses size query failed, ret=" << ret;
        return info;
    }

    IP_ADAPTER_ADDRESSES* adapters =
        static_cast<IP_ADAPTER_ADDRESSES*>(malloc(buf_len));
    if (adapters == nullptr) {
        LOG_WARNING << "GetLocalNetworkInfo: malloc failed for adapter buffer";
        return info;
    }

    ret = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX,
                                NULL, adapters, &buf_len);
    if (ret != NO_ERROR) {
        LOG_WARNING << "GetLocalNetworkInfo: GetAdaptersAddresses failed, ret=" << ret;
        free(adapters);
        return info;
    }

    /* 遍历适配器，按优先级筛选最佳 IPv4 地址
       优先级：192.168.x.x > 10.x.x.x > 172.16-31.x.x
       排除：回环(127.x.x.x)、链路本地(169.254.x.x) */
    int best_priority = 4; /* 0=192.168, 1=10, 2=172.16-31, 3=other, 4=none */
    std::string best_ipv4;
    std::string best_mac;
    std::string best_adapter;
    std::string best_ipv6;

    for (IP_ADAPTER_ADDRESSES* adapter = adapters; adapter != nullptr;
         adapter = adapter->Next) {
        /* 跳过回环适配器 */
        if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
            continue;
        }

        /* 提取适配器名称（FriendlyName 是 wide string，转 UTF-8） */
        std::string adapter_name;
        if (adapter->FriendlyName != nullptr) {
            int utf8_len = WideCharToMultiByte(CP_UTF8, 0,
                adapter->FriendlyName, -1, nullptr, 0, nullptr, nullptr);
            if (utf8_len > 0) {
                std::vector<char> name_buf(utf8_len);
                WideCharToMultiByte(CP_UTF8, 0,
                    adapter->FriendlyName, -1, name_buf.data(), utf8_len,
                    nullptr, nullptr);
                /* WideCharToMultiByte 写入 null terminator，去掉 */
                adapter_name = std::string(name_buf.data(),
                                           utf8_len > 0 ? utf8_len - 1 : 0);
            }
        }

        /* 提取 MAC 地址 */
        std::string mac_str;
        if (adapter->PhysicalAddressLength == 6) {
            char mac_buf[18];
            snprintf(mac_buf, sizeof(mac_buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                     adapter->PhysicalAddress[0],
                     adapter->PhysicalAddress[1],
                     adapter->PhysicalAddress[2],
                     adapter->PhysicalAddress[3],
                     adapter->PhysicalAddress[4],
                     adapter->PhysicalAddress[5]);
            mac_str = mac_buf;
        }

        /* 遍历单播地址 */
        for (IP_ADAPTER_UNICAST_ADDRESS* ua = adapter->FirstUnicastAddress;
             ua != nullptr; ua = ua->Next) {
            sockaddr* sa = ua->Address.lpSockaddr;

            if (sa->sa_family == AF_INET) {
                sockaddr_in* sin = reinterpret_cast<sockaddr_in*>(sa);
                char ip_buf[INET_ADDRSTRLEN] = {0};
                inet_ntop(AF_INET, &sin->sin_addr, ip_buf, sizeof(ip_buf));
                std::string ip = ip_buf;

                /* 排除回环和链路本地 */
                if (ip.find("127.") == 0 || ip.find("169.254.") == 0) {
                    continue;
                }

                /* 计算优先级 */
                int priority = 3; /* other */
                if (ip.find("192.168.") == 0) {
                    priority = 0;
                } else if (ip.find("10.") == 0) {
                    priority = 1;
                } else if (ip.find("172.") == 0) {
                    /* 172.16.x.x ~ 172.31.x.x */
                    int second_octet = 0;
                    if (ip.size() > 4) {
                        second_octet = atoi(ip.c_str() + 4);
                    }
                    if (second_octet >= 16 && second_octet <= 31) {
                        priority = 2;
                    }
                }

                if (priority < best_priority) {
                    best_priority = priority;
                    best_ipv4 = ip;
                    best_mac = mac_str;
                    best_adapter = adapter_name;
                }
            } else if (sa->sa_family == AF_INET6 && best_ipv6.empty()) {
                sockaddr_in6* sin6 = reinterpret_cast<sockaddr_in6*>(sa);
                char ip6_buf[INET6_ADDRSTRLEN] = {0};
                inet_ntop(AF_INET6, &sin6->sin6_addr, ip6_buf, sizeof(ip6_buf));
                std::string ip6 = ip6_buf;

                /* 只取 fe80:: 开头的本地链接地址 */
                if (ip6.find("fe80:") == 0) {
                    best_ipv6 = ip6;
                }
            }
        }
    }

    free(adapters);

    info.ipv4 = best_ipv4;
    info.ipv6 = best_ipv6;
    info.mac = best_mac;
    info.adapter = best_adapter;
    /* hostname 已在前面设置 */

    return info;
}

#else /* !_WIN32 */

/* ====== Linux 平台网络信息获取 ====== */

#include <ifaddrs.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/if_link.h>
#include <unistd.h>
#include <cstring>

NetworkInfo GetLocalNetworkInfo() {
    NetworkInfo info;

    /* 获取主机名 */
    char hostname_buf[256] = {0};
    if (gethostname(hostname_buf, sizeof(hostname_buf)) == 0) {
        info.hostname = hostname_buf;
    }

    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1) {
        LOG_WARNING << "GetLocalNetworkInfo: getifaddrs failed";
        return info;
    }

    /* 遍历接口，按优先级筛选最佳 IPv4 地址
       优先级：192.168.x.x > 10.x.x.x > 172.16-31.x.x
       排除：回环(127.x.x.x)、链路本地(169.254.x.x) */
    int best_priority = 4;
    std::string best_ipv4;
    std::string best_adapter;
    std::string best_ipv6;

    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) {
            continue;
        }

        /* 跳过回环接口 */
        if (ifa->ifa_flags & IFF_LOOPBACK) {
            continue;
        }

        int family = ifa->ifa_addr->sa_family;

        if (family == AF_INET) {
            sockaddr_in* sin = reinterpret_cast<sockaddr_in*>(ifa->ifa_addr);
            char ip_buf[INET_ADDRSTRLEN] = {0};
            inet_ntop(AF_INET, &sin->sin_addr, ip_buf, sizeof(ip_buf));
            std::string ip = ip_buf;

            /* 排除回环和链路本地 */
            if (ip.find("127.") == 0 || ip.find("169.254.") == 0) {
                continue;
            }

            /* 计算优先级 */
            int priority = 3;
            if (ip.find("192.168.") == 0) {
                priority = 0;
            } else if (ip.find("10.") == 0) {
                priority = 1;
            } else if (ip.find("172.") == 0) {
                int second_octet = 0;
                if (ip.size() > 4) {
                    second_octet = atoi(ip.c_str() + 4);
                }
                if (second_octet >= 16 && second_octet <= 31) {
                    priority = 2;
                }
            }

            if (priority < best_priority) {
                best_priority = priority;
                best_ipv4 = ip;
                best_adapter = ifa->ifa_name;
            }
        } else if (family == AF_INET6 && best_ipv6.empty()) {
            sockaddr_in6* sin6 = reinterpret_cast<sockaddr_in6*>(ifa->ifa_addr);
            char ip6_buf[INET6_ADDRSTRLEN] = {0};
            inet_ntop(AF_INET6, &sin6->sin6_addr, ip6_buf, sizeof(ip6_buf));
            std::string ip6 = ip6_buf;

            if (ip6.find("fe80:") == 0) {
                best_ipv6 = ip6;
            }
        }
    }

    /* 获取 MAC 地址（通过 ioctl SIOCGIFHWADDR） */
    std::string best_mac;
    if (!best_adapter.empty()) {
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd >= 0) {
            struct ifreq ifr;
            memset(&ifr, 0, sizeof(ifr));
            strncpy(ifr.ifr_name, best_adapter.c_str(), IFNAMSIZ - 1);
            if (ioctl(fd, SIOCGIFHWADDR, &ifr) == 0) {
                unsigned char* mac =
                    reinterpret_cast<unsigned char*>(ifr.ifr_hwaddr.sa_data);
                char mac_buf[18];
                snprintf(mac_buf, sizeof(mac_buf),
                         "%02X:%02X:%02X:%02X:%02X:%02X",
                         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                best_mac = mac_buf;
            }
            close(fd);
        }
    }

    freeifaddrs(ifaddr);

    info.ipv4 = best_ipv4;
    info.ipv6 = best_ipv6;
    info.mac = best_mac;
    info.adapter = best_adapter;

    return info;
}

#endif /* _WIN32 */

} /* namespace register_student */
