#ifndef __I_DATA_TRANSFER_DAO_H__
#define __I_DATA_TRANSFER_DAO_H__

#include "data_transfer_types.h"
#include <string>
#include <vector>
#include <map>

class IDataTransferDao {
public:
    virtual ~IDataTransferDao() {}

    /* @brief Export all rows from the specified table
     * @param table_name Table name
     * @param out_rows Output: row list, each row is column_name -> value map
     * @return DB_OK on success, ERR_DB_* on failure
     */
    virtual int ExportTableRows(
        const std::string& table_name,
        std::vector<DataRow>& out_rows) = 0;

    /* @brief Import rows into the specified table
     * @param table_name Table name
     * @param rows Rows to import
     * @param unique_keys Business unique key fields for dedup
     * @param mode Import mode (incremental or overwrite)
     * @param out_stats Output: import statistics
     * @return DB_OK on success (including partial skip), ERR_DT_IMPORT_FAILED on critical failure
     */
    virtual int ImportTableRows(
        const std::string& table_name,
        const std::vector<DataRow>& rows,
        const std::vector<std::string>& unique_keys,
        ImportModeType mode,
        TableImportStats& out_stats) = 0;

    /* @brief Clear all business tables (preserves users table)
     * @return DB_OK on success
     */
    virtual int ClearBusinessTables() = 0;

    /* @brief Get column names for the specified table
     * @param table_name Table name
     * @param out_columns Output: column name list
     * @return DB_OK on success
     */
    virtual int GetTableColumnNames(
        const std::string& table_name,
        std::vector<std::string>& out_columns) = 0;
};

#endif /* __I_DATA_TRANSFER_DAO_H__ */
