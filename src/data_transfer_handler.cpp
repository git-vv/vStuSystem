#include "data_transfer_handler.h"
#include "data_transfer_util.h"
#include "error_codes.h"
#include "utils.h"

#include <fstream>
#include <sstream>

DataTransferHandler::DataTransferHandler(
    IDataTransferDao* dao,
    SessionManager* session_mgr,
    const std::string& upload_path)
    : dao_(dao), session_mgr_(session_mgr), upload_path_(upload_path) {
}

DataTransferHandler::~DataTransferHandler() {
}

void DataTransferHandler::RegisterRoutes(crow::SimpleApp& app) {
    CROW_ROUTE(app, "/api/data_transfer/export").methods("GET"_method)
        ([this](const crow::request& req) { return HandleExport(req); });

    CROW_ROUTE(app, "/api/data_transfer/import").methods("POST"_method)
        ([this](const crow::request& req) { return HandleImport(req); });

    CROW_ROUTE(app, "/api/data_transfer/overwrite").methods("POST"_method)
        ([this](const crow::request& req) { return HandleOverwrite(req); });
}

std::string DataTransferHandler::GetSessionIdFromCookie(
    const crow::request& req) {
    std::string cookie_header =
        const_cast<crow::request&>(req).get_header_value("Cookie");
    if (cookie_header.empty()) { return ""; }

    std::string key = "session_id=";
    size_t pos = cookie_header.find(key);
    if (pos == std::string::npos) { return ""; }

    size_t start = pos + key.size();
    size_t end = cookie_header.find(';', start);
    if (end == std::string::npos) {
        return cookie_header.substr(start);
    }
    return cookie_header.substr(start, end - start);
}

int DataTransferHandler::CheckAdminSession(
    const crow::request& req, SessionInfo& info) {
    if (!session_mgr_) {
        LOG_ERROR << "DataTransferHandler: session_mgr is null";
        return ERR_HANDLER_NULL_DAO;
    }

    std::string session_id = GetSessionIdFromCookie(req);
    if (session_id.empty()) {
        LOG_ERROR << "DataTransferHandler: no session_id in cookie";
        return ERR_AUTH_SESSION_EXPIRED;
    }

    int ret = session_mgr_->ValidateSession(session_id, info);
    if (ret != DB_OK) {
        LOG_ERROR << "DataTransferHandler: session validation failed";
        return ERR_AUTH_SESSION_EXPIRED;
    }

    if (info.role != UserRole_Admin) {
        LOG_ERROR << "DataTransferHandler: non-admin access denied";
        return ERR_AUTH_PERMISSION_DENIED;
    }

    return DB_OK;
}

int DataTransferHandler::DoExport(
    const std::string& filename_prefix,
    std::string& out_dtz_content,
    std::string& out_filename) {
    if (!dao_) { return ERR_HANDLER_NULL_DAO; }

    /* Export all tables */
    std::vector<TableConfig> configs = DataTransferUtil::GetTableConfigs();
    std::map<std::string, std::vector<DataRow>> table_data;

    for (size_t i = 0; i < configs.size(); ++i) {
        std::vector<DataRow> rows;
        int ret = dao_->ExportTableRows(configs[i].table_name, rows);
        if (ret != DB_OK) {
            LOG_ERROR << "DoExport: failed to export table "
                      << configs[i].table_name;
            return ERR_DT_EXPORT_FAILED;
        }
        table_data[configs[i].table_name] = rows;
    }

    /* Build metadata */
    ExportMeta meta;
    meta.format_version = 1;
    meta.export_time = register_student::GetCurrentTimeString();
#ifdef _WIN32
    meta.platform = "windows";
#else
    meta.platform = "linux";
#endif
    meta.db_version = "0.0.2";

    /* Serialize to JSON */
    std::string data_json, meta_json;
    int ret = DataTransferUtil::SerializeExportData(
        table_data, meta, data_json, meta_json);
    if (ret != DB_OK) { return ret; }

    /* Pack to .dtz in temp directory */
    std::string temp_dir;
    ret = DataTransferUtil::CreateTempDir(temp_dir);
    if (ret != DB_OK) { return ERR_DT_PACK_FAILED; }

    /* Build filename with timestamp */
    std::string time_str = meta.export_time;
    for (size_t i = 0; i < time_str.size(); ++i) {
        if (time_str[i] == ' ' || time_str[i] == ':') {
            time_str[i] = '_';
        }
    }
    out_filename = filename_prefix + "_" + time_str + ".dtz";
    std::string dtz_path = temp_dir + "/" + out_filename;

    ret = DataTransferUtil::PackToDtz(
        data_json, meta_json, upload_path_, dtz_path);
    if (ret != DB_OK) {
        DataTransferUtil::RemoveTempDir(temp_dir);
        return ERR_DT_PACK_FAILED;
    }

    /* Read the .dtz file into memory */
    ret = DataTransferUtil::ReadFileToString(dtz_path, out_dtz_content);
    if (ret != DB_OK) {
        DataTransferUtil::RemoveTempDir(temp_dir);
        return ERR_DT_EXPORT_FAILED;
    }

    /* Cleanup temp */
    DataTransferUtil::RemoveTempDir(temp_dir);
    return DB_OK;
}

int DataTransferHandler::DoImport(
    const std::string& file_data,
    size_t data_size,
    ImportModeType mode,
    ImportResult& out_result) {
    if (!dao_) { return ERR_HANDLER_NULL_DAO; }

    out_result.images_added = 0;
    out_result.images_skipped = 0;

    /* Validate format */
    int format_version = 0;
    int ret = DataTransferUtil::ValidateDtzFile(
        file_data, data_size, format_version);
    if (ret != DB_OK) { return ret; }

    /* Save to temp file for unpacking */
    std::string temp_dir;
    ret = DataTransferUtil::CreateTempDir(temp_dir);
    if (ret != DB_OK) { return ERR_DT_UNPACK_FAILED; }

    std::string temp_dtz;
    ret = DataTransferUtil::SaveTempFile(
        temp_dir, "import.dtz",
        reinterpret_cast<const uint8_t*>(file_data.data()),
        data_size, temp_dtz);
    if (ret != DB_OK) {
        DataTransferUtil::RemoveTempDir(temp_dir);
        return ERR_DT_UNPACK_FAILED;
    }

    /* Unpack */
    std::string data_json, meta_json;
    ret = DataTransferUtil::UnpackFromDtz(
        temp_dtz, data_json, meta_json, temp_dir);
    if (ret != DB_OK) {
        DataTransferUtil::RemoveTempDir(temp_dir);
        return ret;
    }

    /* Deserialize data */
    std::map<std::string, std::vector<DataRow>> table_data;
    ret = DataTransferUtil::DeserializeImportData(data_json, table_data);
    if (ret != DB_OK) {
        DataTransferUtil::RemoveTempDir(temp_dir);
        return ret;
    }

    /* Overwrite mode: clear business tables first */
    if (mode == ImportMode_Overwrite) {
        ret = dao_->ClearBusinessTables();
        if (ret != DB_OK) {
            DataTransferUtil::RemoveTempDir(temp_dir);
            return ERR_DT_IMPORT_FAILED;
        }
    }

    /* Import tables in order */
    std::vector<TableConfig> configs = DataTransferUtil::GetTableConfigs();
    for (size_t i = 0; i < configs.size(); ++i) {
        const std::string& tname = configs[i].table_name;
        auto it = table_data.find(tname);
        if (it == table_data.end()) { continue; }

        TableImportStats stats;
        ret = dao_->ImportTableRows(
            tname, it->second, configs[i].unique_keys,
            mode, stats);
        if (ret != DB_OK) {
            LOG_ERROR << "DoImport: failed to import table " << tname;
            stats.failed = static_cast<int>(it->second.size());
        }
        out_result.table_stats.push_back(stats);
    }

    /* Restore images */
    DataTransferUtil::RestoreImages(
        temp_dir, upload_path_, mode,
        out_result.images_added, out_result.images_skipped);

    /* Cleanup temp */
    DataTransferUtil::RemoveTempDir(temp_dir);
    return DB_OK;
}

crow::response DataTransferHandler::HandleExport(
    const crow::request& req) {
    LOG_INFO << "DataTransferHandler: export requested";

    SessionInfo session_info;
    int ret = CheckAdminSession(req, session_info);
    if (ret != DB_OK) {
        crow::json::wvalue err;
        err["code"] = ret;
        err["message"] = "permission denied";
        return crow::response(403, crow::json::dump(err));
    }

    std::string dtz_content, filename;
    ret = DoExport("backup", dtz_content, filename);
    if (ret != DB_OK) {
        crow::json::wvalue err;
        err["code"] = ret;
        err["message"] = "export failed";
        return crow::response(500, crow::json::dump(err));
    }

    crow::response resp(200, dtz_content);
    resp.add_header("Content-Type", "application/octet-stream");
    resp.add_header("Content-Disposition",
        ("attachment; filename=\"" + filename + "\"").c_str());
    return resp;
}

crow::response DataTransferHandler::HandleImport(
    const crow::request& req) {
    LOG_INFO << "DataTransferHandler: incremental import requested";

    SessionInfo session_info;
    int ret = CheckAdminSession(req, session_info);
    if (ret != DB_OK) {
        crow::json::wvalue err;
        err["code"] = ret;
        err["message"] = "permission denied";
        return crow::response(403, crow::json::dump(err));
    }

    /* Parse request body */
    crow::json::wvalue err_resp;
    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }
    if (!body) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (!body.has("file_data")) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "missing file_data";
        return crow::response(400, crow::json::dump(err_resp));
    }

    std::string base64_data = std::string(body["file_data"].s());
    std::vector<uint8_t> file_bytes = DataTransferUtil::Base64Decode(base64_data);

    if (file_bytes.size() > DataTransferUtil::MAX_IMPORT_SIZE) {
        err_resp["code"] = ERR_DT_FILE_TOO_LARGE;
        err_resp["message"] = "file too large";
        return crow::response(400, crow::json::dump(err_resp));
    }

    ImportResult result;
    std::string file_data(file_bytes.begin(), file_bytes.end());
    ret = DoImport(file_data, file_data.size(),
                   ImportMode_Incremental, result);

    crow::json::wvalue resp_data;
    if (ret != DB_OK) {
        resp_data["code"] = ret;
        resp_data["message"] = "import failed";
        return crow::response(500, crow::json::dump(resp_data));
    }

    /* Build response with stats */
    resp_data["code"] = DB_OK;
    resp_data["message"] = "success";
    for (size_t i = 0; i < result.table_stats.size(); ++i) {
        crow::json::wvalue ts;
        ts["table_name"] = result.table_stats[i].table_name;
        ts["inserted"] = result.table_stats[i].inserted;
        ts["skipped"] = result.table_stats[i].skipped;
        ts["failed"] = result.table_stats[i].failed;
        resp_data["data"]["tables"][i] = std::move(ts);
    }
    resp_data["data"]["images_added"] = result.images_added;
    resp_data["data"]["images_skipped"] = result.images_skipped;

    return crow::response(200, crow::json::dump(resp_data));
}

crow::response DataTransferHandler::HandleOverwrite(
    const crow::request& req) {
    LOG_INFO << "DataTransferHandler: overwrite import requested";

    SessionInfo session_info;
    int ret = CheckAdminSession(req, session_info);
    if (ret != DB_OK) {
        crow::json::wvalue err;
        err["code"] = ret;
        err["message"] = "permission denied";
        return crow::response(403, crow::json::dump(err));
    }

    /* Parse request body */
    crow::json::wvalue err_resp;
    crow::json::rvalue body;
    try {
        body = crow::json::load(req.body);
    } catch (...) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }
    if (!body) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "invalid json body";
        return crow::response(400, crow::json::dump(err_resp));
    }

    if (!body.has("file_data")) {
        err_resp["code"] = ERR_INVALID_PARAM;
        err_resp["message"] = "missing file_data";
        return crow::response(400, crow::json::dump(err_resp));
    }

    std::string base64_data = std::string(body["file_data"].s());
    std::vector<uint8_t> file_bytes = DataTransferUtil::Base64Decode(base64_data);

    if (file_bytes.size() > DataTransferUtil::MAX_IMPORT_SIZE) {
        err_resp["code"] = ERR_DT_FILE_TOO_LARGE;
        err_resp["message"] = "file too large";
        return crow::response(400, crow::json::dump(err_resp));
    }

    /* Auto backup before overwrite */
    {
        std::string backup_content, backup_filename;
        ret = DoExport("auto_backup", backup_content, backup_filename);
        if (ret != DB_OK) {
            LOG_ERROR << "HandleOverwrite: auto backup failed, ret=" << ret;
            crow::json::wvalue err;
            err["code"] = ERR_DT_BACKUP_FAILED;
            err["message"] = "auto backup failed, aborting overwrite";
            return crow::response(500, crow::json::dump(err));
        }

        /* Save backup to ./data/backups/ */
        DataTransferUtil::EnsureDir("./data/backups");
        std::string backup_path = "./data/backups/" + backup_filename;
        std::ofstream ofs(backup_path, std::ios::binary);
        if (ofs.is_open()) {
            ofs.write(backup_content.data(),
                static_cast<std::streamsize>(backup_content.size()));
            ofs.close();
            LOG_INFO << "HandleOverwrite: auto backup saved to "
                     << backup_path;
        } else {
            LOG_ERROR << "HandleOverwrite: failed to save backup to "
                      << backup_path;
        }
    }

    /* Execute overwrite import */
    ImportResult result;
    std::string file_data(file_bytes.begin(), file_bytes.end());
    ret = DoImport(file_data, file_data.size(),
                   ImportMode_Overwrite, result);

    crow::json::wvalue resp_data;
    if (ret != DB_OK) {
        LOG_ERROR << "HandleOverwrite: import failed, ret=" << ret;
        resp_data["code"] = ret;
        resp_data["message"] = "overwrite import failed";
        return crow::response(500, crow::json::dump(resp_data));
    }

    resp_data["code"] = DB_OK;
    resp_data["message"] = "success";
    for (size_t i = 0; i < result.table_stats.size(); ++i) {
        crow::json::wvalue ts;
        ts["table_name"] = result.table_stats[i].table_name;
        ts["inserted"] = result.table_stats[i].inserted;
        ts["skipped"] = result.table_stats[i].skipped;
        ts["failed"] = result.table_stats[i].failed;
        resp_data["data"]["tables"][i] = std::move(ts);
    }
    resp_data["data"]["images_added"] = result.images_added;
    resp_data["data"]["images_skipped"] = result.images_skipped;

    return crow::response(200, crow::json::dump(resp_data));
}
