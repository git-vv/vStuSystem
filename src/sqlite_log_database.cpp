#include "sqlite_log_database.h"
#include "utils.h"
#include "error_codes.h"

SqliteLogDatabase::SqliteLogDatabase()
    : db_(nullptr) {}

SqliteLogDatabase::~SqliteLogDatabase() {
    Close();
}

int SqliteLogDatabase::Open(const std::string& db_path) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
        LOG_ERROR << "SqliteLogDatabase: failed to open: " << db_path;
        sqlite3_close(db_);
        db_ = nullptr;
        return ERR_DB_NOT_OPEN;
    }

    sqlite3_busy_timeout(db_, 5000);
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);

    int ret = CreateTables();
    if (ret != DB_OK) {
        LOG_ERROR << "SqliteLogDatabase: failed to create tables";
        return ret;
    }

    LOG_INFO << "SqliteLogDatabase: opened: " << db_path;
    return DB_OK;
}

void SqliteLogDatabase::Close() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
        LOG_INFO << "SqliteLogDatabase: closed";
    }
}

int SqliteLogDatabase::CreateTables() {
    const char* sql =
        "CREATE TABLE IF NOT EXISTS operation_log ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "op_type INTEGER NOT NULL, "
        "operator_name TEXT NOT NULL, "
        "target_class TEXT DEFAULT '', "
        "target_student TEXT DEFAULT '', "
        "target_resource TEXT DEFAULT '', "
        "detail TEXT DEFAULT '', "
        "op_time TEXT NOT NULL"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_log_op_type ON operation_log(op_type);"
        "CREATE INDEX IF NOT EXISTS idx_log_op_time ON operation_log(op_time);"
        "CREATE INDEX IF NOT EXISTS idx_log_operator ON operation_log(operator_name);"
        "CREATE INDEX IF NOT EXISTS idx_log_class ON operation_log(target_class);"
        "CREATE INDEX IF NOT EXISTS idx_log_student ON operation_log(target_student);"
        "CREATE INDEX IF NOT EXISTS idx_log_resource ON operation_log(target_resource);";

    char* err_msg = nullptr;
    int ret = sqlite3_exec(db_, sql, nullptr, nullptr, &err_msg);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "SqliteLogDatabase: create tables failed: " << (err_msg ? err_msg : "unknown");
        if (err_msg) {
            sqlite3_free(err_msg);
        }
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteLogDatabase::InsertLog(const OperationLog& log) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) {
        return ERR_LOG_DB_NOT_OPEN;
    }

    const char* sql = "INSERT INTO operation_log (op_type, operator_name, target_class, target_student, target_resource, detail, op_time) VALUES (?, ?, ?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "SqliteLogDatabase: prepare failed for InsertLog";
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, static_cast<int32_t>(log.op_type));
    sqlite3_bind_text(stmt, 2, log.operator_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, log.target_class.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, log.target_student.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, log.target_resource.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, log.detail.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, log.op_time.c_str(), -1, SQLITE_TRANSIENT);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "SqliteLogDatabase: exec failed for InsertLog";
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteLogDatabase::QueryLogs(const LogQueryCondition& cond, std::vector<OperationLog>& logs, int32_t& total) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) {
        return ERR_LOG_DB_NOT_OPEN;
    }

    logs.clear();

    /* 构建WHERE子句 */
    std::string where;
    std::vector<std::string> params;
    int32_t param_idx = 0;

    if (cond.op_type >= 0) {
        where += (where.empty() ? " WHERE " : " AND ") + std::string("op_type = ?");
        params.push_back(std::to_string(cond.op_type));
        ++param_idx;
    }
    if (!cond.start_time.empty()) {
        where += (where.empty() ? " WHERE " : " AND ") + std::string("op_time >= ?");
        params.push_back(cond.start_time);
        ++param_idx;
    }
    if (!cond.end_time.empty()) {
        where += (where.empty() ? " WHERE " : " AND ") + std::string("op_time <= ?");
        params.push_back(cond.end_time);
        ++param_idx;
    }
    if (!cond.class_name.empty()) {
        where += (where.empty() ? " WHERE " : " AND ") + std::string("target_class LIKE ?");
        params.push_back(std::string("%") + cond.class_name + "%");
        ++param_idx;
    }
    if (!cond.teacher_name.empty()) {
        where += (where.empty() ? " WHERE " : " AND ") + std::string("operator_name LIKE ?");
        params.push_back(std::string("%") + cond.teacher_name + "%");
        ++param_idx;
    }
    if (!cond.student_name.empty()) {
        where += (where.empty() ? " WHERE " : " AND ") + std::string("target_student LIKE ?");
        params.push_back(std::string("%") + cond.student_name + "%");
        ++param_idx;
    }
    if (!cond.resource_name.empty()) {
        where += (where.empty() ? " WHERE " : " AND ") + std::string("target_resource LIKE ?");
        params.push_back(std::string("%") + cond.resource_name + "%");
        ++param_idx;
    }

    /* 先查询总数 */
    std::string count_sql = "SELECT COUNT(*) FROM operation_log" + where;
    sqlite3_stmt* count_stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, count_sql.c_str(), -1, &count_stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "SqliteLogDatabase: prepare failed for count";
        return ERR_DB_PREPARE_FAILED;
    }

    for (int32_t i = 0; i < param_idx; ++i) {
        if (i < static_cast<int32_t>(params.size())) {
            sqlite3_bind_text(count_stmt, i + 1, params[i].c_str(), -1, SQLITE_TRANSIENT);
        }
    }

    ret = sqlite3_step(count_stmt);
    if (ret == SQLITE_ROW) {
        total = sqlite3_column_int(count_stmt, 0);
    } else {
        total = 0;
    }
    sqlite3_finalize(count_stmt);

    /* 查询分页数据 */
    int32_t page = cond.page > 0 ? cond.page : 1;
    int32_t page_size = cond.page_size > 0 ? cond.page_size : 20;
    int32_t offset = (page - 1) * page_size;

    std::string query_sql = "SELECT id, op_type, operator_name, target_class, target_student, target_resource, detail, op_time FROM operation_log" + where + " ORDER BY id DESC LIMIT ? OFFSET ?";
    sqlite3_stmt* stmt = nullptr;
    ret = sqlite3_prepare_v2(db_, query_sql.c_str(), -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "SqliteLogDatabase: prepare failed for query";
        return ERR_DB_PREPARE_FAILED;
    }

    for (int32_t i = 0; i < param_idx; ++i) {
        if (i < static_cast<int32_t>(params.size())) {
            sqlite3_bind_text(stmt, i + 1, params[i].c_str(), -1, SQLITE_TRANSIENT);
        }
    }
    sqlite3_bind_int(stmt, param_idx + 1, page_size);
    sqlite3_bind_int(stmt, param_idx + 2, offset);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        OperationLog log;
        log.id = sqlite3_column_int(stmt, 0);
        log.op_type = static_cast<OperationType>(sqlite3_column_int(stmt, 1));
        log.operator_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        log.target_class = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        log.target_student = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        log.target_resource = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        log.detail = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        log.op_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        logs.push_back(log);
    }
    sqlite3_finalize(stmt);

    return DB_OK;
}

int SqliteLogDatabase::CleanLogs(const LogQueryCondition& cond) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) {
        return ERR_LOG_DB_NOT_OPEN;
    }

    /* 构建WHERE子句（同QueryLogs） */
    std::string where;
    std::vector<std::string> params;
    int32_t param_idx = 0;

    if (cond.op_type >= 0) {
        where += (where.empty() ? " WHERE " : " AND ") + std::string("op_type = ?");
        params.push_back(std::to_string(cond.op_type));
        ++param_idx;
    }
    if (!cond.start_time.empty()) {
        where += (where.empty() ? " WHERE " : " AND ") + std::string("op_time >= ?");
        params.push_back(cond.start_time);
        ++param_idx;
    }
    if (!cond.end_time.empty()) {
        where += (where.empty() ? " WHERE " : " AND ") + std::string("op_time <= ?");
        params.push_back(cond.end_time);
        ++param_idx;
    }
    if (!cond.class_name.empty()) {
        where += (where.empty() ? " WHERE " : " AND ") + std::string("target_class LIKE ?");
        params.push_back(std::string("%") + cond.class_name + "%");
        ++param_idx;
    }
    if (!cond.teacher_name.empty()) {
        where += (where.empty() ? " WHERE " : " AND ") + std::string("operator_name LIKE ?");
        params.push_back(std::string("%") + cond.teacher_name + "%");
        ++param_idx;
    }
    if (!cond.student_name.empty()) {
        where += (where.empty() ? " WHERE " : " AND ") + std::string("target_student LIKE ?");
        params.push_back(std::string("%") + cond.student_name + "%");
        ++param_idx;
    }
    if (!cond.resource_name.empty()) {
        where += (where.empty() ? " WHERE " : " AND ") + std::string("target_resource LIKE ?");
        params.push_back(std::string("%") + cond.resource_name + "%");
        ++param_idx;
    }

    std::string delete_sql = "DELETE FROM operation_log" + where;
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, delete_sql.c_str(), -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "SqliteLogDatabase: prepare failed for clean";
        return ERR_DB_PREPARE_FAILED;
    }

    for (int32_t i = 0; i < param_idx; ++i) {
        if (i < static_cast<int32_t>(params.size())) {
            sqlite3_bind_text(stmt, i + 1, params[i].c_str(), -1, SQLITE_TRANSIENT);
        }
    }

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "SqliteLogDatabase: exec failed for clean";
        return ERR_DB_EXEC_FAILED;
    }

    LOG_INFO << "SqliteLogDatabase: logs cleaned";
    return DB_OK;
}
