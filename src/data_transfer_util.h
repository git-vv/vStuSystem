#ifndef __DATA_TRANSFER_UTIL_H__
#define __DATA_TRANSFER_UTIL_H__

#include "data_transfer_types.h"
#include <string>
#include <vector>
#include <map>
#include <cstdint>

class DataTransferUtil {
public:
    /* @brief Serialize table data and metadata to data.json string */
    static int SerializeExportData(
        const std::map<std::string, std::vector<DataRow>>& table_data,
        const ExportMeta& meta,
        std::string& out_data_json,
        std::string& out_meta_json);

    /* @brief Deserialize data.json content to table data map */
    static int DeserializeImportData(
        const std::string& json_str,
        std::map<std::string, std::vector<DataRow>>& out_table_data);

    /* @brief Parse meta.json string to ExportMeta struct */
    static int ParseMetaJson(
        const std::string& meta_json_str,
        ExportMeta& out_meta);

    /* @brief Pack data.json + meta.json + uploads into .dtz (ZIP) file */
    static int PackToDtz(
        const std::string& data_json,
        const std::string& meta_json,
        const std::string& upload_dir,
        const std::string& output_path);

    /* @brief Unpack .dtz file, extract JSON and uploads to temp directory */
    static int UnpackFromDtz(
        const std::string& dtz_path,
        std::string& out_data_json,
        std::string& out_meta_json,
        std::string& out_temp_dir);

    /* @brief Validate .dtz file format, return format_version */
    static int ValidateDtzFile(
        const std::string& file_data,
        size_t data_size,
        int& out_format_version);

    /* @brief Copy images from temp directory to uploads directory */
    static int RestoreImages(
        const std::string& temp_dir,
        const std::string& upload_dir,
        ImportModeType mode,
        int& out_added,
        int& out_skipped);

    /* @brief Convert path separators between platforms */
    static std::string ConvertPathSeparators(
        const std::string& path,
        const std::string& target_platform);

    /* @brief Create a temporary directory, returns path */
    static int CreateTempDir(std::string& out_path);

    /* @brief Recursively remove a directory */
    static void RemoveTempDir(const std::string& path);

    /* @brief Get centralized table configuration list */
    static std::vector<TableConfig> GetTableConfigs();

    /* @brief Decode base64 string to binary data */
    static std::vector<uint8_t> Base64Decode(const std::string& encoded);

    /* @brief Save binary data to a temporary file */
    static int SaveTempFile(
        const std::string& dir,
        const std::string& filename,
        const uint8_t* data,
        size_t data_size,
        std::string& out_path);

    /* @brief Read entire file content to string */
    static int ReadFileToString(
        const std::string& path,
        std::string& out_content);

    /* @brief Ensure directory exists (create if not) */
    static int EnsureDir(const std::string& path);

    /* @brief Build meta.json string from ExportMeta struct */
    static std::string BuildMetaJson(const ExportMeta& meta);

    /* @brief Max import file size: 500MB */
    static const size_t MAX_IMPORT_SIZE;
};

#endif /* __DATA_TRANSFER_UTIL_H__ */
