#include "data_transfer_util.h"
#include "error_codes.h"
#include "utils.h"
#include "crow_safe.h"

#include <sys/stat.h>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#endif

/* miniz header (single-file public domain zlib + ZIP library) */
extern "C" {
#include "miniz.h"
}

const size_t DataTransferUtil::MAX_IMPORT_SIZE = 524288000; /* 500MB */

/* ========== Table Configuration ========== */

std::vector<TableConfig> DataTransferUtil::GetTableConfigs() {
    std::vector<TableConfig> configs;

    configs.push_back({"class_type", 1, {"name"}, {}});
    configs.push_back({"class_info", 2, {"class_name"}, {}});
    configs.push_back({"price_preset", 3, {"amount", "expected_headcount"}, {}});
    configs.push_back({"price_preset_qrcode", 4, {"preset_id", "qrcode_path"}, {"qrcode_path"}});
    configs.push_back({"class_price", 5, {"class_id", "activity_name"}, {}});
    configs.push_back({"resource", 6, {"name"}, {}});
    configs.push_back({"registration", 7,
        {"class_id", "student_name", "parent_phone", "register_time"}, {}});
    configs.push_back({"resource_allocation", 8, {"resource_id", "resource_code"}, {}});
    configs.push_back({"attendance", 9,
        {"class_id", "registration_id", "attendance_date"}, {}});
    configs.push_back({"password_reset_request", 10, {"id"}, {}});
    configs.push_back({"refund_record", 11,
        {"registration_id", "refund_time", "refund_amount"}, {}});
    configs.push_back({"renewal_record", 12, {"registration_id", "renew_time"}, {}});
    configs.push_back({"activity", 13, {"id"}, {"cover_image", "group_image"}});
    configs.push_back({"activity_group", 14, {"invite_code"}, {}});
    configs.push_back({"activity_group_member", 15, {"group_id", "name", "phone", "grade"}, {}});
    configs.push_back({"activity_signup", 16, {"activity_id", "name", "phone", "grade"}, {}});
    configs.push_back({"activity_cover_image", 17, {"activity_id", "image_path"}, {"image_path"}});
    configs.push_back({"promotion_image", 18, {"id"}, {"image_path"}});
    configs.push_back({"promotion_text", 19, {"id"}, {}});
    configs.push_back({"activity_notice", 20, {"id"}, {}});
    configs.push_back({"about_us_card", 21, {"id"}, {"image_path"}});

    return configs;
}

/* ========== JSON Serialization ========== */

int DataTransferUtil::SerializeExportData(
    const std::map<std::string, std::vector<DataRow>>& table_data,
    const ExportMeta& meta,
    std::string& out_data_json,
    std::string& out_meta_json) {
    try {
        /* Build data.json */
        crow::json::wvalue data_root;
        for (auto it = table_data.begin(); it != table_data.end(); ++it) {
            const std::vector<DataRow>& rows = it->second;
            for (size_t i = 0; i < rows.size(); ++i) {
                crow::json::wvalue row_obj;
                for (auto col = rows[i].begin(); col != rows[i].end(); ++col) {
                    row_obj[col->first.c_str()] = col->second;
                }
                data_root[it->first.c_str()][i] = std::move(row_obj);
            }
        }
        out_data_json = crow::json::dump(data_root);

        /* Build meta.json */
        out_meta_json = BuildMetaJson(meta);
    } catch (const std::exception& e) {
        LOG_ERROR << "SerializeExportData failed: " << e.what();
        return ERR_DT_EXPORT_FAILED;
    }
    return DB_OK;
}

int DataTransferUtil::DeserializeImportData(
    const std::string& json_str,
    std::map<std::string, std::vector<DataRow>>& out_table_data) {
    try {
        crow::json::rvalue root = crow::json::load(json_str);
        if (!root) {
            LOG_ERROR << "DeserializeImportData: invalid JSON";
            return ERR_DT_INVALID_FORMAT;
        }

        std::vector<TableConfig> configs = GetTableConfigs();
        for (size_t c = 0; c < configs.size(); ++c) {
            const std::string& tname = configs[c].table_name;
            if (!root.has(tname.c_str())) {
                continue;
            }
            const crow::json::rvalue& arr = root[tname.c_str()];
            size_t row_count = arr.size();
            std::vector<DataRow> rows;
            for (size_t i = 0; i < row_count; ++i) {
                const crow::json::rvalue& obj = arr[i];
                DataRow row;
                /* Iterate over object members */
                for (auto it = obj.begin(); it != obj.end(); ++it) {
                    std::string key = std::string(it->key());
                    std::string val;
                    if (it->t() == crow::json::type::String) {
                        val = std::string(it->s());
                    } else if (it->t() == crow::json::type::Number) {
                        val = std::to_string(it->i());
                    } else if (it->t() == crow::json::type::True) {
                        val = "1";
                    } else if (it->t() == crow::json::type::False) {
                        val = "0";
                    }
                    row[key] = val;
                }
                rows.push_back(row);
            }
            out_table_data[tname] = rows;
        }
    } catch (const std::exception& e) {
        LOG_ERROR << "DeserializeImportData failed: " << e.what();
        return ERR_DT_INVALID_FORMAT;
    }
    return DB_OK;
}

int DataTransferUtil::ParseMetaJson(
    const std::string& meta_json_str,
    ExportMeta& out_meta) {
    try {
        crow::json::rvalue root = crow::json::load(meta_json_str);
        if (!root) {
            return ERR_DT_INVALID_FORMAT;
        }
        if (!root.has("format_version")) {
            return ERR_DT_INVALID_FORMAT;
        }
        out_meta.format_version = static_cast<int>(root["format_version"].i());
        if (root.has("export_time")) {
            out_meta.export_time = std::string(root["export_time"].s());
        }
        if (root.has("platform")) {
            out_meta.platform = std::string(root["platform"].s());
        }
        if (root.has("db_version")) {
            out_meta.db_version = std::string(root["db_version"].s());
        }
    } catch (const std::exception& e) {
        LOG_ERROR << "ParseMetaJson failed: " << e.what();
        return ERR_DT_INVALID_FORMAT;
    }
    return DB_OK;
}

std::string DataTransferUtil::BuildMetaJson(const ExportMeta& meta) {
    crow::json::wvalue v;
    v["format_version"] = meta.format_version;
    v["export_time"] = meta.export_time;
    v["platform"] = meta.platform;
    v["db_version"] = meta.db_version;
    return crow::json::dump(v);
}

/* ========== ZIP Operations ========== */

int DataTransferUtil::PackToDtz(
    const std::string& data_json,
    const std::string& meta_json,
    const std::string& upload_dir,
    const std::string& output_path) {

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_writer_init_file(&zip, output_path.c_str(), 0)) {
        LOG_ERROR << "PackToDtz: failed to create zip file: " << output_path;
        return ERR_DT_PACK_FAILED;
    }

    /* Write meta.json */
    if (!mz_zip_writer_add_mem(&zip, "meta.json",
            meta_json.data(), meta_json.size(),
            MZ_DEFAULT_COMPRESSION)) {
        mz_zip_writer_end(&zip);
        return ERR_DT_PACK_FAILED;
    }

    /* Write data.json */
    if (!mz_zip_writer_add_mem(&zip, "data.json",
            data_json.data(), data_json.size(),
            MZ_DEFAULT_COMPRESSION)) {
        mz_zip_writer_end(&zip);
        return ERR_DT_PACK_FAILED;
    }

    /* Write uploads directory files */
#ifdef _WIN32
    std::wstring wupload = register_student::Utf8ToWide(upload_dir);
    WIN32_FIND_DATAW ffd;
    std::wstring search = wupload + L"\\*";
    HANDLE hFind = FindFirstFileW(search.c_str(), &ffd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                continue;
            }
            std::wstring wfilename(ffd.cFileName);
            std::wstring wfullpath = wupload + L"\\" + wfilename;

            /* Read file content */
            std::ifstream ifs(wfullpath, std::ios::binary);
            if (!ifs.is_open()) { continue; }
            std::ostringstream oss;
            oss << ifs.rdbuf();
            std::string content = oss.str();
            ifs.close();

            /* Convert filename to UTF-8 for zip entry name */
            int utf8_len = WideCharToMultiByte(CP_UTF8, 0,
                wfilename.c_str(), static_cast<int>(wfilename.size()),
                nullptr, 0, nullptr, nullptr);
            std::string utf8_name(utf8_len, 0);
            WideCharToMultiByte(CP_UTF8, 0,
                wfilename.c_str(), static_cast<int>(wfilename.size()),
                &utf8_name[0], utf8_len, nullptr, nullptr);

            std::string entry_name = "uploads/" + utf8_name;

            mz_zip_writer_add_mem(&zip, entry_name.c_str(),
                content.data(), content.size(),
                MZ_DEFAULT_COMPRESSION);
        } while (FindNextFileW(hFind, &ffd));
        FindClose(hFind);
    }
#else
    DIR* dir = opendir(upload_dir.c_str());
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (entry->d_name[0] == '.') { continue; }

            std::string fullpath = upload_dir + "/" + entry->d_name;
            struct stat st;
            if (stat(fullpath.c_str(), &st) != 0 || S_ISDIR(st.st_mode)) {
                continue;
            }

            std::ifstream ifs(fullpath, std::ios::binary);
            if (!ifs.is_open()) { continue; }
            std::ostringstream oss;
            oss << ifs.rdbuf();
            std::string content = oss.str();
            ifs.close();

            std::string entry_name = "uploads/" + std::string(entry->d_name);

            mz_zip_writer_add_mem(&zip, entry_name.c_str(),
                content.data(), content.size(),
                MZ_DEFAULT_COMPRESSION);
        }
        closedir(dir);
    }
#endif

    mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
    return DB_OK;
}

int DataTransferUtil::UnpackFromDtz(
    const std::string& dtz_path,
    std::string& out_data_json,
    std::string& out_meta_json,
    std::string& out_temp_dir) {

    int ret = CreateTempDir(out_temp_dir);
    if (ret != DB_OK) {
        return ERR_DT_UNPACK_FAILED;
    }

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_reader_init_file(&zip, dtz_path.c_str(), 0)) {
        LOG_ERROR << "UnpackFromDtz: failed to open zip file";
        RemoveTempDir(out_temp_dir);
        return ERR_DT_UNPACK_FAILED;
    }

    mz_uint num_files = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < num_files; ++i) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(&zip, i, &file_stat)) {
            continue;
        }

        if (file_stat.m_is_directory) {
            continue;
        }

        /* Security: reject path traversal */
        if (strstr(file_stat.m_filename, "..") != nullptr) {
            LOG_ERROR << "UnpackFromDtz: path traversal detected: "
                      << file_stat.m_filename;
            continue;
        }

        size_t uncomp_size = static_cast<size_t>(file_stat.m_uncomp_size);
        std::vector<char> buffer(uncomp_size);
        if (uncomp_size > 0) {
            if (!mz_zip_reader_extract_to_mem(&zip, i,
                    buffer.data(), buffer.size(), 0)) {
                continue;
            }
        }

        std::string content(buffer.begin(), buffer.end());

        if (strcmp(file_stat.m_filename, "meta.json") == 0) {
            out_meta_json = content;
        } else if (strcmp(file_stat.m_filename, "data.json") == 0) {
            out_data_json = content;
        } else if (strncmp(file_stat.m_filename, "uploads/", 8) == 0) {
            std::string rel_path = file_stat.m_filename;
            std::string full_path = out_temp_dir + "/" + rel_path;

            std::string parent = full_path.substr(0, full_path.rfind('/'));
            EnsureDir(parent);

            std::ofstream ofs(full_path, std::ios::binary);
            if (ofs.is_open()) {
                ofs.write(buffer.data(),
                    static_cast<std::streamsize>(buffer.size()));
                ofs.close();
            }
        }
    }

    mz_zip_reader_end(&zip);

    if (out_data_json.empty() || out_meta_json.empty()) {
        LOG_ERROR << "UnpackFromDtz: missing data.json or meta.json";
        RemoveTempDir(out_temp_dir);
        return ERR_DT_UNPACK_FAILED;
    }

    return DB_OK;
}

int DataTransferUtil::ValidateDtzFile(
    const std::string& file_data,
    size_t data_size,
    int& out_format_version) {

    if (data_size > MAX_IMPORT_SIZE) {
        return ERR_DT_FILE_TOO_LARGE;
    }

    /* ZIP magic number check: PK\x03\x04 */
    if (data_size < 4 ||
        file_data[0] != 'P' || file_data[1] != 'K' ||
        static_cast<uint8_t>(file_data[2]) != 0x03 ||
        static_cast<uint8_t>(file_data[3]) != 0x04) {
        return ERR_DT_INVALID_FORMAT;
    }

    /* Write to temp file for miniz to parse */
    std::string temp_dir;
    int ret = CreateTempDir(temp_dir);
    if (ret != DB_OK) {
        return ERR_DT_UNPACK_FAILED;
    }

    std::string temp_dtz = temp_dir + "/validate.dtz";
    {
        std::ofstream ofs(temp_dtz, std::ios::binary);
        if (!ofs.is_open()) {
            RemoveTempDir(temp_dir);
            return ERR_DT_UNPACK_FAILED;
        }
        ofs.write(file_data.data(), static_cast<std::streamsize>(data_size));
        ofs.close();
    }

    /* Try to open and read meta.json */
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, temp_dtz.c_str(), 0)) {
        RemoveTempDir(temp_dir);
        return ERR_DT_INVALID_FORMAT;
    }

    std::string meta_json;
    mz_uint num_files = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < num_files; ++i) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(&zip, i, &file_stat)) {
            continue;
        }
        if (strcmp(file_stat.m_filename, "meta.json") == 0) {
            size_t uncomp_size = static_cast<size_t>(file_stat.m_uncomp_size);
            std::vector<char> buf(uncomp_size);
            if (uncomp_size > 0 && mz_zip_reader_extract_to_mem(
                    &zip, i, buf.data(), buf.size(), 0)) {
                meta_json.assign(buf.data(), uncomp_size);
            }
            break;
        }
    }
    mz_zip_reader_end(&zip);
    RemoveTempDir(temp_dir);

    if (meta_json.empty()) {
        return ERR_DT_INVALID_FORMAT;
    }

    ExportMeta meta;
    ret = ParseMetaJson(meta_json, meta);
    if (ret != DB_OK) {
        return ret;
    }

    if (meta.format_version < 1) {
        return ERR_DT_VERSION_UNSUPPORTED;
    }

    out_format_version = meta.format_version;
    return DB_OK;
}

/* ========== Image Restore ========== */

int DataTransferUtil::RestoreImages(
    const std::string& temp_dir,
    const std::string& upload_dir,
    ImportModeType mode,
    int& out_added,
    int& out_skipped) {

    out_added = 0;
    out_skipped = 0;

    std::string src_dir = temp_dir + "/uploads";
    EnsureDir(upload_dir);

#ifdef _WIN32
    std::wstring wsrc = register_student::Utf8ToWide(src_dir);
    WIN32_FIND_DATAW ffd;
    std::wstring search = wsrc + L"\\*";
    HANDLE hFind = FindFirstFileW(search.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) {
        return DB_OK; /* No uploads directory, not an error */
    }
    do {
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        std::wstring wfilename(ffd.cFileName);
        std::string src_path = src_dir + "/" + std::string(
            wfilename.begin(), wfilename.end());
        std::string dst_path = upload_dir + "/" + std::string(
            wfilename.begin(), wfilename.end());

        /* Check if destination exists */
        struct stat st;
        if (stat(dst_path.c_str(), &st) == 0) {
            if (mode == ImportMode_Incremental) {
                ++out_skipped;
                continue;
            }
            /* overwrite mode: remove existing */
            std::remove(dst_path.c_str());
        }

        /* Copy file */
        std::ifstream ifs(src_path, std::ios::binary);
        std::ofstream ofs(dst_path, std::ios::binary);
        if (ifs.is_open() && ofs.is_open()) {
            ofs << ifs.rdbuf();
            ++out_added;
        } else {
            LOG_ERROR << "RestoreImages: failed to copy " << src_path;
        }
        ifs.close();
        ofs.close();
    } while (FindNextFileW(hFind, &ffd));
    FindClose(hFind);
#else
    DIR* dir = opendir(src_dir.c_str());
    if (!dir) {
        return DB_OK; /* No uploads directory, not an error */
    }
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') { continue; }

        std::string src_path = src_dir + "/" + entry->d_name;
        std::string dst_path = upload_dir + "/" + entry->d_name;

        struct stat st;
        if (stat(dst_path.c_str(), &st) == 0) {
            if (mode == ImportMode_Incremental) {
                ++out_skipped;
                continue;
            }
            std::remove(dst_path.c_str());
        }

        std::ifstream ifs(src_path, std::ios::binary);
        std::ofstream ofs(dst_path, std::ios::binary);
        if (ifs.is_open() && ofs.is_open()) {
            ofs << ifs.rdbuf();
            ++out_added;
        } else {
            LOG_ERROR << "RestoreImages: failed to copy " << src_path;
        }
        ifs.close();
        ofs.close();
    }
    closedir(dir);
#endif

    return DB_OK;
}

/* ========== Path Conversion ========== */

std::string DataTransferUtil::ConvertPathSeparators(
    const std::string& path,
    const std::string& target_platform) {
    std::string result = path;
    char from = '/';
    char to = '\\';
    if (target_platform == "linux") {
        from = '\\';
        to = '/';
    }
    for (size_t i = 0; i < result.size(); ++i) {
        if (result[i] == from) {
            result[i] = to;
        }
    }
    return result;
}

/* ========== Temp Directory ========== */

static int g_temp_dir_counter = 0;

int DataTransferUtil::CreateTempDir(std::string& out_path) {
    std::string time_str = register_student::GetCurrentTimeString();
    /* Replace spaces and colons for filesystem safety */
    for (size_t i = 0; i < time_str.size(); ++i) {
        if (time_str[i] == ' ' || time_str[i] == ':') {
            time_str[i] = '_';
        }
    }
    int seq = g_temp_dir_counter++;
#ifdef _WIN32
    char temp[MAX_PATH];
    GetTempPathA(MAX_PATH, temp);
    out_path = std::string(temp) + "dtz_" + time_str + "_" +
        std::to_string(GetCurrentProcessId()) + "_" + std::to_string(seq);
    if (_mkdir(out_path.c_str()) != 0) {
        LOG_ERROR << "CreateTempDir: failed to create " << out_path;
        return ERR_DT_UNPACK_FAILED;
    }
#else
    out_path = "/tmp/dtz_" + time_str + "_" +
        std::to_string(getpid()) + "_" + std::to_string(seq);
    if (mkdir(out_path.c_str(), 0755) != 0) {
        LOG_ERROR << "CreateTempDir: failed to create " << out_path;
        return ERR_DT_UNPACK_FAILED;
    }
#endif
    return DB_OK;
}

void DataTransferUtil::RemoveTempDir(const std::string& path) {
    if (path.empty()) { return; }

#ifdef _WIN32
    std::wstring wpath = register_student::Utf8ToWide(path);
    WIN32_FIND_DATAW ffd;
    std::wstring search = wpath + L"\\*";
    HANDLE hFind = FindFirstFileW(search.c_str(), &ffd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            std::wstring wname(ffd.cFileName);
            if (wname == L"." || wname == L"..") { continue; }
            std::string full = path + "/" + std::string(
                wname.begin(), wname.end());
            if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                RemoveTempDir(full);
            } else {
                std::remove(full.c_str());
            }
        } while (FindNextFileW(hFind, &ffd));
        FindClose(hFind);
    }
    _rmdir(path.c_str());
#else
    DIR* dir = opendir(path.c_str());
    if (!dir) { return; }
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) { continue; }
        std::string full = path + "/" + entry->d_name;
        struct stat st;
        if (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            RemoveTempDir(full);
        } else {
            std::remove(full.c_str());
        }
    }
    closedir(dir);
    rmdir(path.c_str());
#endif
}

/* ========== Base64 ========== */

std::vector<uint8_t> DataTransferUtil::Base64Decode(
    const std::string& encoded) {
    static const std::string chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz0123456789+/";

    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; ++i) {
        T[static_cast<unsigned char>(chars[i])] = i;
    }

    std::vector<uint8_t> result;
    int val = 0;
    int valb = -8;
    for (size_t i = 0; i < encoded.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(encoded[i]);
        if (T[c] == -1) { break; }
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            result.push_back(
                static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return result;
}

/* ========== File I/O Helpers ========== */

int DataTransferUtil::SaveTempFile(
    const std::string& dir,
    const std::string& filename,
    const uint8_t* data,
    size_t data_size,
    std::string& out_path) {
    out_path = dir + "/" + filename;
    std::ofstream ofs(out_path, std::ios::binary);
    if (!ofs.is_open()) {
        LOG_ERROR << "SaveTempFile: failed to create " << out_path;
        return ERR_DT_EXPORT_FAILED;
    }
    ofs.write(reinterpret_cast<const char*>(data),
        static_cast<std::streamsize>(data_size));
    ofs.close();
    return DB_OK;
}

int DataTransferUtil::ReadFileToString(
    const std::string& path,
    std::string& out_content) {
#ifdef _WIN32
    std::wstring wpath = register_student::Utf8ToWide(path);
    std::ifstream ifs(wpath, std::ios::binary);
#else
    std::ifstream ifs(path, std::ios::binary);
#endif
    if (!ifs.is_open()) {
        LOG_ERROR << "ReadFileToString: failed to open " << path;
        return ERR_DT_EXPORT_FAILED;
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    out_content = oss.str();
    ifs.close();
    return DB_OK;
}

int DataTransferUtil::EnsureDir(const std::string& path) {
    if (path.empty()) { return DB_OK; }

#ifdef _WIN32
    std::wstring wpath = register_student::Utf8ToWide(path);
    if (GetFileAttributesW(wpath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return DB_OK;
    }
    /* Create parent first if needed */
    size_t pos = path.rfind('/');
    if (pos == std::string::npos) {
        pos = path.rfind('\\');
    }
    if (pos != std::string::npos && pos > 0) {
        EnsureDir(path.substr(0, pos));
    }
    if (!_wmkdir(wpath.c_str())) {
        return DB_OK;
    }
    return ERR_DT_EXPORT_FAILED;
#else
    struct stat st;
    if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        return DB_OK;
    }
    /* Create parent first if needed */
    size_t pos = path.rfind('/');
    if (pos != std::string::npos && pos > 0) {
        EnsureDir(path.substr(0, pos));
    }
    if (mkdir(path.c_str(), 0755) == 0) {
        return DB_OK;
    }
    return ERR_DT_EXPORT_FAILED;
#endif
}
