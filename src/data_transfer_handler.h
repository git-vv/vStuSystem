#ifndef __DATA_TRANSFER_HANDLER_H__
#define __DATA_TRANSFER_HANDLER_H__

#include "crow_safe.h"
#include "i_data_transfer_dao.h"
#include "session_manager.h"
#include <string>

class DataTransferHandler {
public:
    explicit DataTransferHandler(IDataTransferDao* dao,
                                 SessionManager* session_mgr,
                                 const std::string& upload_path);
    ~DataTransferHandler();

    void RegisterRoutes(crow::SimpleApp& app);

private:
    /* @brief Export data package and download */
    crow::response HandleExport(const crow::request& req);

    /* @brief Incremental restore */
    crow::response HandleImport(const crow::request& req);

    /* @brief Full overwrite (auto backup then clear and import) */
    crow::response HandleOverwrite(const crow::request& req);

    /* @brief Admin session check, returns 0 on success */
    int CheckAdminSession(const crow::request& req, SessionInfo& info);

    /* @brief Extract session_id from Cookie header */
    std::string GetSessionIdFromCookie(const crow::request& req);

    /* @brief Execute export flow, returns .dtz file content */
    int DoExport(const std::string& filename_prefix,
                 std::string& out_dtz_content,
                 std::string& out_filename);

    /* @brief Execute import flow */
    int DoImport(const std::string& file_data,
                 size_t data_size,
                 ImportModeType mode,
                 ImportResult& out_result);

    IDataTransferDao* dao_;
    SessionManager* session_mgr_;
    std::string upload_path_;
};

#endif /* __DATA_TRANSFER_HANDLER_H__ */
