#ifndef __I_OPERATION_LOG_DAO_H__
#define __I_OPERATION_LOG_DAO_H__

#include "log_types.h"
#include <vector>

class IOperationLogDao {
public:
    virtual ~IOperationLogDao() {}

    virtual int InsertLog(const OperationLog& log) = 0;
    virtual int QueryLogs(const LogQueryCondition& cond, std::vector<OperationLog>& logs, int32_t& total) = 0;
    virtual int CleanLogs(const LogQueryCondition& cond) = 0;
};

#endif /* __I_OPERATION_LOG_DAO_H__ */
