#ifndef __DATA_TRANSFER_TYPES_H__
#define __DATA_TRANSFER_TYPES_H__

#include <string>
#include <vector>
#include <map>
#include <cstdint>

/* Import mode for data restore */
enum ImportModeType {
    ImportMode_Incremental = 0,  /* Skip existing records by business unique key */
    ImportMode_Overwrite   = 1   /* Clear all business data then import */
};

/* Configuration for a single table in export/import */
struct TableConfig {
    std::string table_name;
    int import_order;
    std::vector<std::string> unique_keys;
    std::vector<std::string> path_columns;
};

/* Single row: column name -> value (all values as string) */
typedef std::map<std::string, std::string> DataRow;

/* Import statistics for a single table */
struct TableImportStats {
    std::string table_name;
    int inserted;
    int skipped;
    int failed;
};

/* Overall import result */
struct ImportResult {
    std::vector<TableImportStats> table_stats;
    int images_added;
    int images_skipped;
};

/* Export metadata */
struct ExportMeta {
    int format_version;
    std::string export_time;
    std::string platform;
    std::string db_version;
};

#endif /* __DATA_TRANSFER_TYPES_H__ */
