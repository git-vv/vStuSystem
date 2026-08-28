#ifndef __SQLITE_LOG_DATABASE_H__
#define __SQLITE_LOG_DATABASE_H__

#include "i_operation_log_dao.h"
#include <sqlite3.h>
#include <mutex>

class SqliteLogDatabase : public IOperationLogDao {
public:
    SqliteLogDatabase();
    ~SqliteLogDatabase();

    int Open(const std::string& db_path);
    void Close();

    int InsertLog(const OperationLog& log) override;
    int QueryLogs(const LogQueryCondition& cond, std::vector<OperationLog>& logs, int32_t& total) override;
    int CleanLogs(const LogQueryCondition& cond) override;

private:
    int CreateTables();

    sqlite3* db_;
    std::mutex db_mutex_;
};

#endif /* __SQLITE_LOG_DATABASE_H__ */
