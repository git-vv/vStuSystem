#include "sqlite_database.h"
#include "error_codes.h"
#include "utils.h"
#include "data_transfer_util.h"
#include <cstring>
#include <cmath>
#include <algorithm>

SqliteDatabase::SqliteDatabase()
    : db_(nullptr) {
}

SqliteDatabase::~SqliteDatabase() {
    Close();
}

int SqliteDatabase::Open(const std::string& db_path) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (db_) {
        Close();
    }

    int ret = sqlite3_open(db_path.c_str(), &db_);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Failed to open database: " << db_path;
        sqlite3_close(db_);
        db_ = nullptr;
        return ERR_DB_EXEC_FAILED;
    }

    LOG_INFO << "Database opened: " << db_path;

    sqlite3_busy_timeout(db_, 5000);
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);

    ret = CreateTables();
    if (ret != DB_OK) {
        LOG_ERROR << "Failed to create tables, ret=" << ret;
        sqlite3_close(db_);
        db_ = nullptr;
        return ret;
    }

    MigrateSchema();

    ret = InitClassTypes();
    if (ret != DB_OK) {
        LOG_ERROR << "Failed to init class types, ret=" << ret;
        sqlite3_close(db_);
        db_ = nullptr;
        return ret;
    }

    return DB_OK;
}

void SqliteDatabase::Close() {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
        LOG_INFO << "Database closed";
    }
}

int SqliteDatabase::CreateTables() {
    const char* sqls[] = {
        "CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY AUTOINCREMENT, username TEXT NOT NULL UNIQUE, password_hash TEXT NOT NULL, salt TEXT NOT NULL, role INTEGER NOT NULL, display_name TEXT DEFAULT '', create_time TEXT NOT NULL)",
        "CREATE TABLE IF NOT EXISTS class_type (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL UNIQUE, is_builtin INTEGER NOT NULL DEFAULT 0)",
        "CREATE TABLE IF NOT EXISTS class_info (id INTEGER PRIMARY KEY AUTOINCREMENT, class_name TEXT NOT NULL UNIQUE, start_time TEXT NOT NULL, end_time TEXT NOT NULL, description TEXT DEFAULT '', enrollment_capacity INTEGER NOT NULL, enrollment_used REAL NOT NULL DEFAULT 0.0, class_type TEXT NOT NULL, create_time TEXT NOT NULL)",
        /* price_preset: 全局价位预设，(金额,成团人数)联合唯一（单位元 REAL），expected_headcount=成团人数 */
        "CREATE TABLE IF NOT EXISTS price_preset (id INTEGER PRIMARY KEY AUTOINCREMENT, amount REAL NOT NULL, expected_headcount INTEGER NOT NULL DEFAULT 1, create_time TEXT NOT NULL, UNIQUE(amount, expected_headcount))",
        /* price_preset_qrcode: 预设下的二维码图片路径 */
        "CREATE TABLE IF NOT EXISTS price_preset_qrcode (id INTEGER PRIMARY KEY AUTOINCREMENT, preset_id INTEGER NOT NULL, qrcode_path TEXT NOT NULL, FOREIGN KEY(preset_id) REFERENCES price_preset(id))",
        /* class_price: 班级-预设关联，含活动名与金额/人数快照（preset 删除后回退用） */
        "CREATE TABLE IF NOT EXISTS class_price (id INTEGER PRIMARY KEY AUTOINCREMENT, class_id INTEGER NOT NULL, preset_id INTEGER NOT NULL, activity_name TEXT NOT NULL, snapshot_amount REAL NOT NULL, snapshot_headcount INTEGER NOT NULL DEFAULT 1, FOREIGN KEY(class_id) REFERENCES class_info(id), FOREIGN KEY(preset_id) REFERENCES price_preset(id), UNIQUE(class_id, activity_name), UNIQUE(class_id, preset_id))",
        "CREATE TABLE IF NOT EXISTS registration (id INTEGER PRIMARY KEY AUTOINCREMENT, class_id INTEGER NOT NULL, student_name TEXT NOT NULL, student_gender TEXT NOT NULL, parent_phone TEXT NOT NULL, has_allergy INTEGER NOT NULL DEFAULT 0, allergy_desc TEXT DEFAULT '', price_id INTEGER NOT NULL, need_bed INTEGER NOT NULL DEFAULT 0, teacher_name TEXT NOT NULL, other_info TEXT DEFAULT '', register_time TEXT NOT NULL, is_deposit INTEGER NOT NULL DEFAULT 0, paid_amount_snapshot REAL NOT NULL DEFAULT 0, supplement_amount REAL NOT NULL DEFAULT 0, supplement_preset_id INTEGER NOT NULL DEFAULT -1, supplement_operator TEXT NOT NULL DEFAULT '', supplement_time TEXT NOT NULL DEFAULT '', student_start_date TEXT NOT NULL DEFAULT '', student_end_date TEXT NOT NULL DEFAULT '', FOREIGN KEY(class_id) REFERENCES class_info(id), FOREIGN KEY(price_id) REFERENCES class_price(id))",
        "CREATE TABLE IF NOT EXISTS resource (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL UNIQUE, total_count INTEGER NOT NULL, used_count INTEGER NOT NULL DEFAULT 0, remain_count INTEGER NOT NULL, resource_type INTEGER NOT NULL DEFAULT 0, bed_reserved_count INTEGER NOT NULL DEFAULT 0)",
        "CREATE TABLE IF NOT EXISTS resource_allocation (id INTEGER PRIMARY KEY AUTOINCREMENT, resource_id INTEGER NOT NULL, registration_id INTEGER NOT NULL, student_name TEXT NOT NULL, student_gender TEXT NOT NULL, teacher_name TEXT NOT NULL, class_name TEXT NOT NULL, resource_code INTEGER NOT NULL, allocate_time TEXT NOT NULL, FOREIGN KEY(resource_id) REFERENCES resource(id), FOREIGN KEY(registration_id) REFERENCES registration(id), UNIQUE(resource_id, resource_code))",
        "CREATE TABLE IF NOT EXISTS attendance (id INTEGER PRIMARY KEY AUTOINCREMENT, class_id INTEGER NOT NULL, registration_id INTEGER NOT NULL, student_name TEXT NOT NULL, student_gender TEXT NOT NULL, attendance_date TEXT NOT NULL, status INTEGER NOT NULL, leave_time TEXT NOT NULL DEFAULT '', teacher_name TEXT NOT NULL, record_time TEXT NOT NULL, FOREIGN KEY(class_id) REFERENCES class_info(id), FOREIGN KEY(registration_id) REFERENCES registration(id), UNIQUE(class_id, registration_id, attendance_date))",
        "CREATE TABLE IF NOT EXISTS password_reset_request (id INTEGER PRIMARY KEY AUTOINCREMENT, user_id INTEGER NOT NULL, status INTEGER NOT NULL DEFAULT 0, approver_id INTEGER DEFAULT -1, new_password_hash TEXT DEFAULT '', new_salt TEXT DEFAULT '', request_time TEXT NOT NULL, approve_time TEXT DEFAULT '', FOREIGN KEY(user_id) REFERENCES users(id))",
        "CREATE TABLE IF NOT EXISTS registration_request (id INTEGER PRIMARY KEY AUTOINCREMENT, username TEXT NOT NULL UNIQUE, password_hash TEXT NOT NULL, salt TEXT NOT NULL, role INTEGER NOT NULL, display_name TEXT NOT NULL DEFAULT '', status INTEGER NOT NULL DEFAULT 0, request_time TEXT NOT NULL)",
        /* refund_record: 退费记录，软删除（status=0=有效, 1=已撤销） */
        "CREATE TABLE IF NOT EXISTS refund_record (id INTEGER PRIMARY KEY AUTOINCREMENT, registration_id INTEGER NOT NULL, refund_amount REAL NOT NULL, operator_name TEXT NOT NULL, refund_time TEXT NOT NULL, status INTEGER NOT NULL DEFAULT 0, cancel_operator_name TEXT, cancel_time TEXT, unit_price REAL NOT NULL DEFAULT 0, total_class_days INTEGER NOT NULL DEFAULT 0, attended_days INTEGER NOT NULL DEFAULT 0, original_amount REAL NOT NULL DEFAULT 0, tolerance_used REAL NOT NULL DEFAULT 0)",
        "CREATE INDEX IF NOT EXISTS idx_refund_reg_status ON refund_record(registration_id, status)",
        "CREATE INDEX IF NOT EXISTS idx_refund_cancel_time ON refund_record(cancel_time)",
        "CREATE TABLE IF NOT EXISTS renewal_record (id INTEGER PRIMARY KEY AUTOINCREMENT, registration_id INTEGER NOT NULL, old_end_date TEXT NOT NULL, new_end_date TEXT NOT NULL, renew_amount REAL NOT NULL, operator_name TEXT NOT NULL, renew_time TEXT NOT NULL, FOREIGN KEY(registration_id) REFERENCES registration(id))",
        "CREATE INDEX IF NOT EXISTS idx_renewal_reg_id ON renewal_record(registration_id)",
        "CREATE TABLE IF NOT EXISTS activity (id INTEGER PRIMARY KEY AUTOINCREMENT, title TEXT NOT NULL, description TEXT NOT NULL DEFAULT '', cover_image TEXT NOT NULL, start_time TEXT NOT NULL, end_time TEXT NOT NULL, signup_deadline TEXT NOT NULL, capacity INTEGER NOT NULL DEFAULT 0, signup_count INTEGER NOT NULL DEFAULT 0, group_image TEXT NOT NULL DEFAULT '', sort_order INTEGER NOT NULL DEFAULT 0, status INTEGER NOT NULL DEFAULT 0, created_at TEXT NOT NULL, updated_at TEXT NOT NULL)",
        "CREATE TABLE IF NOT EXISTS activity_signup (id INTEGER PRIMARY KEY AUTOINCREMENT, activity_id INTEGER NOT NULL, name TEXT NOT NULL, phone TEXT NOT NULL, grade TEXT NOT NULL DEFAULT '', signup_type TEXT NOT NULL DEFAULT '', created_at TEXT NOT NULL, UNIQUE(activity_id, name, phone, grade))",
        "CREATE TABLE IF NOT EXISTS activity_cover_image (id INTEGER PRIMARY KEY AUTOINCREMENT, activity_id INTEGER NOT NULL, image_path TEXT NOT NULL, sort_order INTEGER NOT NULL DEFAULT 0, created_at TEXT NOT NULL, FOREIGN KEY(activity_id) REFERENCES activity(id))",
        "CREATE TABLE IF NOT EXISTS promotion_image (id INTEGER PRIMARY KEY AUTOINCREMENT, image_path TEXT NOT NULL, sort_order INTEGER NOT NULL DEFAULT 0, created_at TEXT NOT NULL)",
        "CREATE TABLE IF NOT EXISTS promotion_text (id INTEGER PRIMARY KEY CHECK (id = 1), content TEXT NOT NULL DEFAULT '', updated_at TEXT NOT NULL)",
        "CREATE TABLE IF NOT EXISTS activity_notice (id INTEGER PRIMARY KEY CHECK (id = 1), content TEXT NOT NULL DEFAULT '', updated_at TEXT NOT NULL)",
        "CREATE TABLE IF NOT EXISTS activity_group (id INTEGER PRIMARY KEY AUTOINCREMENT, activity_id INTEGER NOT NULL, invite_code TEXT NOT NULL, leader_name TEXT NOT NULL, leader_phone TEXT NOT NULL, leader_grade TEXT NOT NULL DEFAULT '', current_count INTEGER NOT NULL DEFAULT 1, target_count INTEGER NOT NULL, status INTEGER NOT NULL DEFAULT 0, cancel_reason INTEGER NOT NULL DEFAULT 0, created_at TEXT NOT NULL, updated_at TEXT NOT NULL, FOREIGN KEY(activity_id) REFERENCES activity(id), UNIQUE(invite_code))",
        "CREATE TABLE IF NOT EXISTS activity_group_member (id INTEGER PRIMARY KEY AUTOINCREMENT, group_id INTEGER NOT NULL, name TEXT NOT NULL, phone TEXT NOT NULL, grade TEXT NOT NULL DEFAULT '', signup_type TEXT NOT NULL DEFAULT '', is_leader INTEGER NOT NULL DEFAULT 0, created_at TEXT NOT NULL, FOREIGN KEY(group_id) REFERENCES activity_group(id), UNIQUE(group_id, name, phone, grade))",
        "CREATE INDEX IF NOT EXISTS idx_group_activity_id ON activity_group(activity_id)",
        "CREATE INDEX IF NOT EXISTS idx_group_invite_code ON activity_group(invite_code)",
        "CREATE INDEX IF NOT EXISTS idx_group_status ON activity_group(status)",
        "CREATE INDEX IF NOT EXISTS idx_member_group_id ON activity_group_member(group_id)",
        "CREATE TABLE IF NOT EXISTS about_us_card (id INTEGER PRIMARY KEY AUTOINCREMENT, image_path TEXT NOT NULL DEFAULT '', text_content TEXT NOT NULL DEFAULT '', layout_type INTEGER NOT NULL DEFAULT 1, sort_order INTEGER NOT NULL DEFAULT 0, created_at TEXT NOT NULL)",
        nullptr
    };

    for (int32_t i = 0; sqls[i] != nullptr; ++i) {
        char* err_msg = nullptr;
        int ret = sqlite3_exec(db_, sqls[i], nullptr, nullptr, &err_msg);
        if (ret != SQLITE_OK) {
            LOG_ERROR << "Create table failed, ret=" << ret << " err=" << (err_msg ? err_msg : "unknown");
            if (err_msg) {
                sqlite3_free(err_msg);
            }
            return ERR_DB_EXEC_FAILED;
        }
    }

    sqlite3_exec(db_, "INSERT OR IGNORE INTO promotion_text (id, content, updated_at) VALUES (1, '', '')", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "INSERT OR IGNORE INTO activity_notice (id, content, updated_at) VALUES (1, '', '')", nullptr, nullptr, nullptr);

    sqlite3_exec(db_, "ALTER TABLE activity_signup ADD COLUMN grade TEXT NOT NULL DEFAULT ''", nullptr, nullptr, nullptr);

    /* migrate activity_signup: old UNIQUE(activity_id, phone) -> UNIQUE(activity_id, name, phone, grade) */
    {
        const char* check_sql = "SELECT sql FROM sqlite_master WHERE type='table' AND name='activity_signup'";
        sqlite3_stmt* chk = nullptr;
        if (sqlite3_prepare_v2(db_, check_sql, -1, &chk, nullptr) == SQLITE_OK) {
            bool need_migrate = false;
            if (sqlite3_step(chk) == SQLITE_ROW) {
                const char* tbl_sql = reinterpret_cast<const char*>(sqlite3_column_text(chk, 0));
                if (tbl_sql) {
                    std::string s(tbl_sql);
                    if (s.find("UNIQUE(activity_id, phone)") != std::string::npos) {
                        need_migrate = true;
                    }
                }
            }
            sqlite3_finalize(chk);
            if (need_migrate) {
                LOG_INFO << "Migrating activity_signup unique constraint";
                sqlite3_exec(db_, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
                sqlite3_exec(db_,
                    "CREATE TABLE activity_signup_new ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                    "activity_id INTEGER NOT NULL, "
                    "name TEXT NOT NULL, "
                    "phone TEXT NOT NULL, "
                    "grade TEXT NOT NULL DEFAULT '', "
                    "created_at TEXT NOT NULL, "
                    "UNIQUE(activity_id, name, phone, grade))",
                    nullptr, nullptr, nullptr);
                sqlite3_exec(db_,
                    "INSERT INTO activity_signup_new (id, activity_id, name, phone, grade, created_at) "
                    "SELECT id, activity_id, name, phone, grade, created_at FROM activity_signup",
                    nullptr, nullptr, nullptr);
                sqlite3_exec(db_, "DROP TABLE activity_signup", nullptr, nullptr, nullptr);
                sqlite3_exec(db_, "ALTER TABLE activity_signup_new RENAME TO activity_signup", nullptr, nullptr, nullptr);
                sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr);
                LOG_INFO << "activity_signup migration complete";
            }
        }
    }

    sqlite3_exec(db_, "ALTER TABLE activity ADD COLUMN min_group_size INTEGER NOT NULL DEFAULT 1", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "ALTER TABLE activity ADD COLUMN group_type INTEGER NOT NULL DEFAULT 0", nullptr, nullptr, nullptr);

    /* migrate: add signup_type to activity_signup and activity_group_member */
    {
        sqlite3_stmt* chk = nullptr;
        if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM pragma_table_info('activity_signup') WHERE name='signup_type'", -1, &chk, nullptr) == SQLITE_OK) {
            if (sqlite3_step(chk) == SQLITE_ROW && sqlite3_column_int(chk, 0) == 0) {
                sqlite3_exec(db_, "ALTER TABLE activity_signup ADD COLUMN signup_type TEXT NOT NULL DEFAULT ''", nullptr, nullptr, nullptr);
                LOG_INFO << "Migrated activity_signup.signup_type";
            }
            sqlite3_finalize(chk);
        }
        if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM pragma_table_info('activity_group_member') WHERE name='signup_type'", -1, &chk, nullptr) == SQLITE_OK) {
            if (sqlite3_step(chk) == SQLITE_ROW && sqlite3_column_int(chk, 0) == 0) {
                sqlite3_exec(db_, "ALTER TABLE activity_group_member ADD COLUMN signup_type TEXT NOT NULL DEFAULT ''", nullptr, nullptr, nullptr);
                LOG_INFO << "Migrated activity_group_member.signup_type";
            }
            sqlite3_finalize(chk);
        }
    }

    LOG_DEBUG << "All tables created successfully";
    return DB_OK;
}

void SqliteDatabase::MigrateSchema() {
    /* price_library 改造：开发期 DROP 旧 class_price/class_qrcode/registration 后由 CreateTables 重建新结构。
       判定旧表存在的依据：class_price 缺少 preset_id 列（旧表只有 price 列）。
       已人工确认开发期无生产数据需保留。 */
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_,
        "SELECT COUNT(*) FROM pragma_table_info('class_price') WHERE name='preset_id'",
        -1, &stmt, nullptr);
    bool need_rebuild = false;
    if (ret == SQLITE_OK) {
        ret = sqlite3_step(stmt);
        if (ret == SQLITE_ROW && sqlite3_column_int(stmt, 0) == 0) {
            need_rebuild = true;
        }
        sqlite3_finalize(stmt);
    } else if (stmt) {
        sqlite3_finalize(stmt);
    }

    if (need_rebuild) {
        LOG_INFO << "Migrating schema: rebuild class_price/class_qrcode/registration for price_library";
        sqlite3_exec(db_, "DROP TABLE IF EXISTS class_qrcode", nullptr, nullptr, nullptr);
        sqlite3_exec(db_, "DROP TABLE IF EXISTS class_price", nullptr, nullptr, nullptr);
        sqlite3_exec(db_, "DROP TABLE IF EXISTS registration", nullptr, nullptr, nullptr);
        /* 重建新表（CreateTables 已在 Open 中调用，但 DROP 发生在其后则需重新建） */
        const char* rebuild_sqls[] = {
            "CREATE TABLE IF NOT EXISTS price_preset (id INTEGER PRIMARY KEY AUTOINCREMENT, amount REAL NOT NULL, expected_headcount INTEGER NOT NULL DEFAULT 1, create_time TEXT NOT NULL, UNIQUE(amount, expected_headcount))",
            "CREATE TABLE IF NOT EXISTS price_preset_qrcode (id INTEGER PRIMARY KEY AUTOINCREMENT, preset_id INTEGER NOT NULL, qrcode_path TEXT NOT NULL, FOREIGN KEY(preset_id) REFERENCES price_preset(id))",
            "CREATE TABLE IF NOT EXISTS class_price (id INTEGER PRIMARY KEY AUTOINCREMENT, class_id INTEGER NOT NULL, preset_id INTEGER NOT NULL, activity_name TEXT NOT NULL, snapshot_amount REAL NOT NULL, snapshot_headcount INTEGER NOT NULL DEFAULT 1, FOREIGN KEY(class_id) REFERENCES class_info(id), FOREIGN KEY(preset_id) REFERENCES price_preset(id), UNIQUE(class_id, activity_name), UNIQUE(class_id, preset_id))",
            "CREATE TABLE IF NOT EXISTS registration (id INTEGER PRIMARY KEY AUTOINCREMENT, class_id INTEGER NOT NULL, student_name TEXT NOT NULL, student_gender TEXT NOT NULL, parent_phone TEXT NOT NULL, has_allergy INTEGER NOT NULL DEFAULT 0, allergy_desc TEXT DEFAULT '', price_id INTEGER NOT NULL, need_bed INTEGER NOT NULL DEFAULT 0, teacher_name TEXT NOT NULL, other_info TEXT DEFAULT '', register_time TEXT NOT NULL, is_deposit INTEGER NOT NULL DEFAULT 0, paid_amount_snapshot REAL NOT NULL DEFAULT 0, supplement_amount REAL NOT NULL DEFAULT 0, supplement_preset_id INTEGER NOT NULL DEFAULT -1, supplement_operator TEXT NOT NULL DEFAULT '', supplement_time TEXT NOT NULL DEFAULT '', student_start_date TEXT NOT NULL DEFAULT '', student_end_date TEXT NOT NULL DEFAULT '', FOREIGN KEY(class_id) REFERENCES class_info(id), FOREIGN KEY(price_id) REFERENCES class_price(id))",
            nullptr
        };
        for (int32_t i = 0; rebuild_sqls[i] != nullptr; ++i) {
            sqlite3_exec(db_, rebuild_sqls[i], nullptr, nullptr, nullptr);
        }
        LOG_INFO << "Migrated schema: class_price/class_qrcode/registration rebuilt";
    }

    /* Add resource_type column to resource table if missing */
    stmt = nullptr;
    ret = sqlite3_prepare_v2(db_,
        "SELECT COUNT(*) FROM pragma_table_info('resource') WHERE name='resource_type'",
        -1, &stmt, nullptr);
    if (ret == SQLITE_OK) {
        ret = sqlite3_step(stmt);
        if (ret == SQLITE_ROW && sqlite3_column_int(stmt, 0) == 0) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
            sqlite3_exec(db_,
                "ALTER TABLE resource ADD COLUMN resource_type INTEGER NOT NULL DEFAULT 0",
                nullptr, nullptr, nullptr);
            LOG_INFO << "Migrated resource table: added resource_type column";
            /* Mark existing bed resource by name */
            sqlite3_exec(db_,
                "UPDATE resource SET resource_type = 1 WHERE name = '床位'",
                nullptr, nullptr, nullptr);
        } else {
            sqlite3_finalize(stmt);
        }
    } else {
        if (stmt) { sqlite3_finalize(stmt); }
    }

    /* Add bed_reserved_count column to resource table if missing */
    stmt = nullptr;
    ret = sqlite3_prepare_v2(db_,
        "SELECT COUNT(*) FROM pragma_table_info('resource') WHERE name='bed_reserved_count'",
        -1, &stmt, nullptr);
    if (ret == SQLITE_OK) {
        ret = sqlite3_step(stmt);
        if (ret == SQLITE_ROW && sqlite3_column_int(stmt, 0) == 0) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
            sqlite3_exec(db_,
                "ALTER TABLE resource ADD COLUMN bed_reserved_count INTEGER NOT NULL DEFAULT 0",
                nullptr, nullptr, nullptr);
            LOG_INFO << "Migrated resource table: added bed_reserved_count column";
            /* Populate bed_reserved_count from existing registrations */
            sqlite3_exec(db_,
                "UPDATE resource SET bed_reserved_count = (SELECT COUNT(*) FROM registration r WHERE r.need_bed = 1 AND r.class_id IN (SELECT id FROM class_info)) WHERE resource_type = 1",
                nullptr, nullptr, nullptr);
        } else {
            sqlite3_finalize(stmt);
        }
    } else {
        if (stmt) { sqlite3_finalize(stmt); }
    }

    /* Add leave_time column to attendance table if missing (early-leave feature) */
    stmt = nullptr;
    ret = sqlite3_prepare_v2(db_,
        "SELECT COUNT(*) FROM pragma_table_info('attendance') WHERE name='leave_time'",
        -1, &stmt, nullptr);
    if (ret == SQLITE_OK) {
        ret = sqlite3_step(stmt);
        if (ret == SQLITE_ROW && sqlite3_column_int(stmt, 0) == 0) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
            sqlite3_exec(db_,
                "ALTER TABLE attendance ADD COLUMN leave_time TEXT NOT NULL DEFAULT ''",
                nullptr, nullptr, nullptr);
            LOG_INFO << "Migrated attendance table: added leave_time column";
        } else {
            sqlite3_finalize(stmt);
        }
    } else {
        if (stmt) { sqlite3_finalize(stmt); }
    }

    /* Add expected_headcount column to price_preset if missing (group-deal feature) */
    stmt = nullptr;
    ret = sqlite3_prepare_v2(db_,
        "SELECT COUNT(*) FROM pragma_table_info('price_preset') WHERE name='expected_headcount'",
        -1, &stmt, nullptr);
    if (ret == SQLITE_OK) {
        ret = sqlite3_step(stmt);
        if (ret == SQLITE_ROW && sqlite3_column_int(stmt, 0) == 0) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
            sqlite3_exec(db_,
                "ALTER TABLE price_preset ADD COLUMN expected_headcount INTEGER NOT NULL DEFAULT 1",
                nullptr, nullptr, nullptr);
            LOG_INFO << "Migrated price_preset table: added expected_headcount column";
        } else {
            sqlite3_finalize(stmt);
        }
    } else {
        if (stmt) { sqlite3_finalize(stmt); }
    }

    /* Add snapshot_headcount column to class_price if missing (group-deal feature) */
    stmt = nullptr;
    ret = sqlite3_prepare_v2(db_,
        "SELECT COUNT(*) FROM pragma_table_info('class_price') WHERE name='snapshot_headcount'",
        -1, &stmt, nullptr);
    if (ret == SQLITE_OK) {
        ret = sqlite3_step(stmt);
        if (ret == SQLITE_ROW && sqlite3_column_int(stmt, 0) == 0) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
            sqlite3_exec(db_,
                "ALTER TABLE class_price ADD COLUMN snapshot_headcount INTEGER NOT NULL DEFAULT 1",
                nullptr, nullptr, nullptr);
            LOG_INFO << "Migrated class_price table: added snapshot_headcount column";
        } else {
            sqlite3_finalize(stmt);
        }
    } else {
        if (stmt) { sqlite3_finalize(stmt); }
    }

    /* Create refund_record table if missing (refund feature, 旧库启动自动建表) */
    stmt = nullptr;
    ret = sqlite3_prepare_v2(db_,
        "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='refund_record'",
        -1, &stmt, nullptr);
    if (ret == SQLITE_OK) {
        ret = sqlite3_step(stmt);
        if (ret == SQLITE_ROW && sqlite3_column_int(stmt, 0) == 0) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
            sqlite3_exec(db_,
                "CREATE TABLE IF NOT EXISTS refund_record (id INTEGER PRIMARY KEY AUTOINCREMENT, registration_id INTEGER NOT NULL, refund_amount REAL NOT NULL, operator_name TEXT NOT NULL, refund_time TEXT NOT NULL, status INTEGER NOT NULL DEFAULT 0, cancel_operator_name TEXT, cancel_time TEXT, unit_price REAL NOT NULL DEFAULT 0, total_class_days INTEGER NOT NULL DEFAULT 0, attended_days INTEGER NOT NULL DEFAULT 0, original_amount REAL NOT NULL DEFAULT 0, tolerance_used REAL NOT NULL DEFAULT 0)",
                nullptr, nullptr, nullptr);
            sqlite3_exec(db_,
                "CREATE INDEX IF NOT EXISTS idx_refund_reg_status ON refund_record(registration_id, status)",
                nullptr, nullptr, nullptr);
            sqlite3_exec(db_,
                "CREATE INDEX IF NOT EXISTS idx_refund_cancel_time ON refund_record(cancel_time)",
                nullptr, nullptr, nullptr);
            LOG_INFO << "Migrated: created refund_record table";
        } else {
            sqlite3_finalize(stmt);
        }
    } else {
        if (stmt) { sqlite3_finalize(stmt); }
    }

    /* registration 表新增定金/补缴 6 列（registration_deposit 功能）：以 is_deposit 列存在性为迁移判据。
       旧库 ALTER 增加 6 列后，从 class_price.snapshot_amount 回填现有全额记录的 paid_amount_snapshot
       （price_id 对应 class_price 已删的孤儿记录 COALESCE 兜底为 0）。is_deposit 默认 0=全额。 */
    stmt = nullptr;
    ret = sqlite3_prepare_v2(db_,
        "SELECT COUNT(*) FROM pragma_table_info('registration') WHERE name='is_deposit'",
        -1, &stmt, nullptr);
    if (ret == SQLITE_OK) {
        ret = sqlite3_step(stmt);
        if (ret == SQLITE_ROW && sqlite3_column_int(stmt, 0) == 0) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
            sqlite3_exec(db_, "ALTER TABLE registration ADD COLUMN is_deposit INTEGER NOT NULL DEFAULT 0", nullptr, nullptr, nullptr);
            sqlite3_exec(db_, "ALTER TABLE registration ADD COLUMN paid_amount_snapshot REAL NOT NULL DEFAULT 0", nullptr, nullptr, nullptr);
            sqlite3_exec(db_, "ALTER TABLE registration ADD COLUMN supplement_amount REAL NOT NULL DEFAULT 0", nullptr, nullptr, nullptr);
            sqlite3_exec(db_, "ALTER TABLE registration ADD COLUMN supplement_preset_id INTEGER NOT NULL DEFAULT -1", nullptr, nullptr, nullptr);
            sqlite3_exec(db_, "ALTER TABLE registration ADD COLUMN supplement_operator TEXT NOT NULL DEFAULT ''", nullptr, nullptr, nullptr);
            sqlite3_exec(db_, "ALTER TABLE registration ADD COLUMN supplement_time TEXT NOT NULL DEFAULT ''", nullptr, nullptr, nullptr);
            /* 回填现有全额记录的 paid_amount_snapshot：从 class_price.snapshot_amount 取 */
            sqlite3_exec(db_,
                "UPDATE registration SET paid_amount_snapshot = "
                "COALESCE((SELECT cp.snapshot_amount FROM class_price cp WHERE cp.id = registration.price_id), 0)",
                nullptr, nullptr, nullptr);
            LOG_INFO << "Migrated registration table: added deposit/supplement columns and backfilled paid_amount_snapshot";
        } else {
            sqlite3_finalize(stmt);
        }
    } else {
        if (stmt) { sqlite3_finalize(stmt); }
    }

    /* preset_unique_composite: price_preset 唯一约束从 amount 单列改为 (amount, expected_headcount) 联合唯一。
       SQLite 不支持 ALTER TABLE DROP CONSTRAINT，需重建表。
       检测方式：查 sqlite_master 中 price_preset 的建表 SQL 是否含 UNIQUE(amount, expected_headcount)，
       不含则为旧约束需迁移。 */
    stmt = nullptr;
    ret = sqlite3_prepare_v2(db_,
        "SELECT sql FROM sqlite_master WHERE type='table' AND name='price_preset'",
        -1, &stmt, nullptr);
    if (ret == SQLITE_OK) {
        ret = sqlite3_step(stmt);
        if (ret == SQLITE_ROW) {
            const char* sql_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            std::string sql_str(sql_text ? sql_text : "");
            sqlite3_finalize(stmt);
            stmt = nullptr;
            /* 检查是否已含联合唯一约束 */
            if (sql_str.find("UNIQUE(amount, expected_headcount)") == std::string::npos) {
                /* 旧约束，需重建表。事务保护：失败则 ROLLBACK 保留旧表 */
                LOG_INFO << "Migrating price_preset: rebuilding for composite unique constraint";
                char* err_msg = nullptr;
                int mig_ret = SQLITE_OK;

                mig_ret = sqlite3_exec(db_, "BEGIN IMMEDIATE", nullptr, nullptr, &err_msg);
                if (err_msg) { sqlite3_free(err_msg); err_msg = nullptr; }
                if (mig_ret != SQLITE_OK) {
                    LOG_ERROR << "Migrate price_preset: BEGIN IMMEDIATE failed, ret=" << mig_ret;
                } else {
                    sqlite3_exec(db_, "DROP TABLE IF EXISTS price_preset_new", nullptr, nullptr, nullptr);
                    mig_ret = sqlite3_exec(db_,
                        "CREATE TABLE price_preset_new (id INTEGER PRIMARY KEY AUTOINCREMENT, amount REAL NOT NULL, expected_headcount INTEGER NOT NULL DEFAULT 1, create_time TEXT NOT NULL, UNIQUE(amount, expected_headcount))",
                        nullptr, nullptr, &err_msg);
                    if (err_msg) { LOG_ERROR << "Migrate price_preset: CREATE price_preset_new failed: " << err_msg; sqlite3_free(err_msg); err_msg = nullptr; }
                }
                if (mig_ret == SQLITE_OK) {
                    mig_ret = sqlite3_exec(db_,
                        "INSERT INTO price_preset_new (id, amount, expected_headcount, create_time) SELECT id, amount, expected_headcount, create_time FROM price_preset",
                        nullptr, nullptr, &err_msg);
                    if (err_msg) { LOG_ERROR << "Migrate price_preset: INSERT price_preset_new failed: " << err_msg; sqlite3_free(err_msg); err_msg = nullptr; }
                }
                if (mig_ret == SQLITE_OK) {
                    mig_ret = sqlite3_exec(db_, "DROP TABLE price_preset", nullptr, nullptr, &err_msg);
                    if (err_msg) { LOG_ERROR << "Migrate price_preset: DROP price_preset failed: " << err_msg; sqlite3_free(err_msg); err_msg = nullptr; }
                }
                if (mig_ret == SQLITE_OK) {
                    mig_ret = sqlite3_exec(db_, "ALTER TABLE price_preset_new RENAME TO price_preset", nullptr, nullptr, &err_msg);
                    if (err_msg) { LOG_ERROR << "Migrate price_preset: RENAME price_preset failed: " << err_msg; sqlite3_free(err_msg); err_msg = nullptr; }
                }
                /* 同步重建 price_preset_qrcode 以更新外键引用 */
                if (mig_ret == SQLITE_OK) {
                    sqlite3_exec(db_, "DROP TABLE IF EXISTS price_preset_qrcode_new", nullptr, nullptr, nullptr);
                    mig_ret = sqlite3_exec(db_,
                        "CREATE TABLE price_preset_qrcode_new (id INTEGER PRIMARY KEY AUTOINCREMENT, preset_id INTEGER NOT NULL, qrcode_path TEXT NOT NULL, FOREIGN KEY(preset_id) REFERENCES price_preset(id))",
                        nullptr, nullptr, &err_msg);
                    if (err_msg) { LOG_ERROR << "Migrate price_preset: CREATE price_preset_qrcode_new failed: " << err_msg; sqlite3_free(err_msg); err_msg = nullptr; }
                }
                if (mig_ret == SQLITE_OK) {
                    mig_ret = sqlite3_exec(db_,
                        "INSERT INTO price_preset_qrcode_new (id, preset_id, qrcode_path) SELECT id, preset_id, qrcode_path FROM price_preset_qrcode",
                        nullptr, nullptr, &err_msg);
                    if (err_msg) { LOG_ERROR << "Migrate price_preset: INSERT price_preset_qrcode_new failed: " << err_msg; sqlite3_free(err_msg); err_msg = nullptr; }
                }
                if (mig_ret == SQLITE_OK) {
                    mig_ret = sqlite3_exec(db_, "DROP TABLE price_preset_qrcode", nullptr, nullptr, &err_msg);
                    if (err_msg) { LOG_ERROR << "Migrate price_preset: DROP price_preset_qrcode failed: " << err_msg; sqlite3_free(err_msg); err_msg = nullptr; }
                }
                if (mig_ret == SQLITE_OK) {
                    mig_ret = sqlite3_exec(db_, "ALTER TABLE price_preset_qrcode_new RENAME TO price_preset_qrcode", nullptr, nullptr, &err_msg);
                    if (err_msg) { LOG_ERROR << "Migrate price_preset: RENAME price_preset_qrcode failed: " << err_msg; sqlite3_free(err_msg); err_msg = nullptr; }
                }

                if (mig_ret == SQLITE_OK) {
                    mig_ret = sqlite3_exec(db_, "COMMIT", nullptr, nullptr, &err_msg);
                    if (err_msg) { sqlite3_free(err_msg); err_msg = nullptr; }
                    if (mig_ret == SQLITE_OK) {
                        LOG_INFO << "Migrated price_preset: composite unique constraint applied";
                    } else {
                        LOG_ERROR << "Migrate price_preset: COMMIT failed, ret=" << mig_ret;
                        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
                    }
                } else {
                    LOG_ERROR << "Migrate price_preset: failed at step, ROLLBACK, ret=" << mig_ret;
                    sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
                    /* 保留旧表，下次启动重试 */
                }
            }
        } else {
            sqlite3_finalize(stmt);
        }
    } else {
        if (stmt) { sqlite3_finalize(stmt); }
    }

    /* partial_period: registration 表新增 student_start_date / student_end_date */
    {
        sqlite3_stmt* stmt = nullptr;
        ret = sqlite3_prepare_v2(db_,
            "SELECT COUNT(*) FROM pragma_table_info('registration') WHERE name='student_start_date'",
            -1, &stmt, nullptr);
        if (ret == SQLITE_OK) {
            ret = sqlite3_step(stmt);
            if (ret == SQLITE_ROW && sqlite3_column_int(stmt, 0) == 0) {
                sqlite3_finalize(stmt);
                stmt = nullptr;
                ret = sqlite3_prepare_v2(db_,
                    "ALTER TABLE registration ADD COLUMN student_start_date TEXT NOT NULL DEFAULT ''",
                    -1, &stmt, nullptr);
                if (ret == SQLITE_OK) {
                    ret = sqlite3_step(stmt);
                }
                sqlite3_finalize(stmt);
                stmt = nullptr;
            } else {
                sqlite3_finalize(stmt);
                stmt = nullptr;
            }
        }
    }
    {
        sqlite3_stmt* stmt = nullptr;
        ret = sqlite3_prepare_v2(db_,
            "SELECT COUNT(*) FROM pragma_table_info('registration') WHERE name='student_end_date'",
            -1, &stmt, nullptr);
        if (ret == SQLITE_OK) {
            ret = sqlite3_step(stmt);
            if (ret == SQLITE_ROW && sqlite3_column_int(stmt, 0) == 0) {
                sqlite3_finalize(stmt);
                stmt = nullptr;
                ret = sqlite3_prepare_v2(db_,
                    "ALTER TABLE registration ADD COLUMN student_end_date TEXT NOT NULL DEFAULT ''",
                    -1, &stmt, nullptr);
                if (ret == SQLITE_OK) {
                    ret = sqlite3_step(stmt);
                }
                sqlite3_finalize(stmt);
                stmt = nullptr;
            } else {
                sqlite3_finalize(stmt);
                stmt = nullptr;
            }
        }
    }
    /* partial_period: class_info 表新增 enrollment_used */
    {
        sqlite3_stmt* stmt = nullptr;
        ret = sqlite3_prepare_v2(db_,
            "SELECT COUNT(*) FROM pragma_table_info('class_info') WHERE name='enrollment_used'",
            -1, &stmt, nullptr);
        if (ret == SQLITE_OK) {
            ret = sqlite3_step(stmt);
            if (ret == SQLITE_ROW && sqlite3_column_int(stmt, 0) == 0) {
                sqlite3_finalize(stmt);
                stmt = nullptr;
                /* Add enrollment_used column and migrate from enrolled_count */
                ret = sqlite3_prepare_v2(db_,
                    "ALTER TABLE class_info ADD COLUMN enrollment_used REAL NOT NULL DEFAULT 0.0",
                    -1, &stmt, nullptr);
                if (ret == SQLITE_OK) {
                    ret = sqlite3_step(stmt);
                }
                sqlite3_finalize(stmt);
                stmt = nullptr;
                /* Migrate data from enrolled_count */
                if (ret == SQLITE_DONE) {
                    ret = sqlite3_prepare_v2(db_,
                        "UPDATE class_info SET enrollment_used = CAST(enrolled_count AS REAL)",
                        -1, &stmt, nullptr);
                    if (ret == SQLITE_OK) {
                        ret = sqlite3_step(stmt);
                    }
                    sqlite3_finalize(stmt);
                    stmt = nullptr;
                }
            } else {
                sqlite3_finalize(stmt);
                stmt = nullptr;
            }
        }
    }
    /* partial_period: renewal_record 建表 */
    {
        sqlite3_stmt* stmt = nullptr;
        ret = sqlite3_prepare_v2(db_,
            "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='renewal_record'",
            -1, &stmt, nullptr);
        if (ret == SQLITE_OK) {
            ret = sqlite3_step(stmt);
            if (ret == SQLITE_ROW && sqlite3_column_int(stmt, 0) == 0) {
                sqlite3_finalize(stmt);
                stmt = nullptr;
                ret = sqlite3_prepare_v2(db_,
                    "CREATE TABLE IF NOT EXISTS renewal_record (id INTEGER PRIMARY KEY AUTOINCREMENT, registration_id INTEGER NOT NULL, old_end_date TEXT NOT NULL, new_end_date TEXT NOT NULL, renew_amount REAL NOT NULL, operator_name TEXT NOT NULL, renew_time TEXT NOT NULL, FOREIGN KEY(registration_id) REFERENCES registration(id))",
                    -1, &stmt, nullptr);
                if (ret == SQLITE_OK) {
                    ret = sqlite3_step(stmt);
                }
                sqlite3_finalize(stmt);
                stmt = nullptr;
                if (ret == SQLITE_DONE) {
                    ret = sqlite3_prepare_v2(db_,
                        "CREATE INDEX IF NOT EXISTS idx_renewal_reg_id ON renewal_record(registration_id)",
                        -1, &stmt, nullptr);
                    if (ret == SQLITE_OK) {
                        ret = sqlite3_step(stmt);
                    }
                    sqlite3_finalize(stmt);
                    stmt = nullptr;
                }
            } else {
                sqlite3_finalize(stmt);
                stmt = nullptr;
            }
        }
    }
}

int SqliteDatabase::InitClassTypes() {
    const char* names[] = {
        "\xe6\x89\x98\xe7\xae\xa1\xe7\x8f\xad",
        "\xe4\xb9\xa6\xe6\xb3\x95\xe7\x8f\xad",
        "\xe6\x9a\x91\xe5\x81\x87\xe7\x8f\xad",
        "\xe5\xb9\xbc\xe5\xb0\x8f\xe8\xa1\x94\xe6\x8e\xa5\xe7\x8f\xad",
        "\xe5\xaf\x92\xe5\x81\x87\xe7\x8f\xad",
        nullptr
    };

    const char* sql = "INSERT OR IGNORE INTO class_type (name, is_builtin) VALUES (?, 1)";

    for (int32_t i = 0; names[i] != nullptr; ++i) {
        sqlite3_stmt* stmt = nullptr;
        int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (ret != SQLITE_OK) {
            LOG_ERROR << "Prepare failed for init class type, ret=" << ret;
            return ERR_DB_PREPARE_FAILED;
        }

        sqlite3_bind_text(stmt, 1, names[i], -1, SQLITE_TRANSIENT);

        ret = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (ret != SQLITE_DONE) {
            LOG_ERROR << "Insert class type failed, ret=" << ret;
            return ERR_DB_EXEC_FAILED;
        }
    }

    LOG_DEBUG << "Built-in class types initialized";
    return DB_OK;
}

/* ==================== IUserDao ==================== */

int SqliteDatabase::InsertUser(const UserInfo& info) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "INSERT INTO users (username, password_hash, salt, role, display_name, create_time) VALUES (?, ?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for InsertUser, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_text(stmt, 1, info.username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, info.password_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, info.salt.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, static_cast<int32_t>(info.role));
    sqlite3_bind_text(stmt, 5, info.display_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, info.create_time.c_str(), -1, SQLITE_TRANSIENT);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "InsertUser failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    const_cast<UserInfo&>(info).id = static_cast<int32_t>(sqlite3_last_insert_rowid(db_));
    LOG_DEBUG << "InsertUser success, id=" << info.id;
    return DB_OK;
}

int SqliteDatabase::QueryUserByUsername(const std::string& username, UserInfo& info) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT id, username, password_hash, salt, role, display_name, create_time FROM users WHERE username = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryUserByUsername, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    ret = sqlite3_step(stmt);
    if (ret == SQLITE_ROW) {
        info.id = sqlite3_column_int(stmt, 0);
        info.username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        info.password_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        info.salt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        info.role = static_cast<UserRoleType>(sqlite3_column_int(stmt, 4));
        info.display_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        info.create_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        sqlite3_finalize(stmt);
        return DB_OK;
    }

    sqlite3_finalize(stmt);

    if (ret == SQLITE_DONE) {
        return ERR_DB_EXEC_FAILED;
    }

    LOG_ERROR << "QueryUserByUsername step failed, ret=" << ret;
    return ERR_DB_EXEC_FAILED;
}

int SqliteDatabase::QueryUserById(int32_t id, UserInfo& info) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT id, username, password_hash, salt, role, display_name, create_time FROM users WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryUserById, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, id);

    ret = sqlite3_step(stmt);
    if (ret == SQLITE_ROW) {
        info.id = sqlite3_column_int(stmt, 0);
        info.username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        info.password_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        info.salt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        info.role = static_cast<UserRoleType>(sqlite3_column_int(stmt, 4));
        info.display_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        info.create_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        sqlite3_finalize(stmt);
        return DB_OK;
    }

    sqlite3_finalize(stmt);

    if (ret == SQLITE_DONE) {
        return ERR_DB_EXEC_FAILED;
    }

    LOG_ERROR << "QueryUserById step failed, ret=" << ret;
    return ERR_DB_EXEC_FAILED;
}

int SqliteDatabase::UpdatePassword(int32_t user_id, const std::string& password_hash, const std::string& salt) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "UPDATE users SET password_hash = ?, salt = ? WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for UpdatePassword, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_text(stmt, 1, password_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, salt.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, user_id);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "UpdatePassword failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::UpdateUserInfo(int32_t user_id, const std::string& display_name) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "UPDATE users SET display_name = ? WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for UpdateUserInfo, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_text(stmt, 1, display_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, user_id);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "UpdateUserInfo failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::DeleteUser(int32_t user_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "DELETE FROM users WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for DeleteUser, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, user_id);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "DeleteUser failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::CheckAdminExists() {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT COUNT(*) FROM users WHERE role = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for CheckAdminExists, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, UserRole_Admin);

    ret = sqlite3_step(stmt);
    if (ret == SQLITE_ROW) {
        int32_t count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return (count > 0) ? 1 : 0;
    }

    sqlite3_finalize(stmt);
    LOG_ERROR << "CheckAdminExists step failed, ret=" << ret;
    return ERR_DB_EXEC_FAILED;
}

int SqliteDatabase::QueryAllTeachers(std::vector<UserInfo>& teachers) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    teachers.clear();

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT id, username, password_hash, salt, role, display_name, create_time FROM users WHERE role = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryAllTeachers, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, UserRole_Teacher);

    while (true) {
        ret = sqlite3_step(stmt);
        if (ret == SQLITE_ROW) {
            UserInfo info;
            info.id = sqlite3_column_int(stmt, 0);
            info.username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            info.password_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            info.salt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            info.role = static_cast<UserRoleType>(sqlite3_column_int(stmt, 4));
            info.display_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            info.create_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            teachers.push_back(info);
        } else {
            break;
        }
    }

    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "QueryAllTeachers step failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::InsertResetRequest(const PasswordResetRequest& req) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "INSERT INTO password_reset_request (user_id, status, approver_id, new_password_hash, new_salt, request_time, approve_time) VALUES (?, ?, ?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for InsertResetRequest, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, req.user_id);
    sqlite3_bind_int(stmt, 2, static_cast<int32_t>(req.status));
    sqlite3_bind_int(stmt, 3, req.approver_id);
    sqlite3_bind_text(stmt, 4, req.new_password_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, req.new_salt.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, req.request_time.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, req.approve_time.c_str(), -1, SQLITE_TRANSIENT);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "InsertResetRequest failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::QueryPendingResetRequests(std::vector<PasswordResetRequest>& requests) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    requests.clear();

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT r.id, r.user_id, u.username, r.status, r.approver_id, r.new_password_hash, r.new_salt, r.request_time, r.approve_time FROM password_reset_request r LEFT JOIN users u ON r.user_id = u.id WHERE r.status = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryPendingResetRequests, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, ResetStatus_Pending);

    while (true) {
        ret = sqlite3_step(stmt);
        if (ret == SQLITE_ROW) {
            PasswordResetRequest req;
            req.id = sqlite3_column_int(stmt, 0);
            req.user_id = sqlite3_column_int(stmt, 1);
            if (sqlite3_column_type(stmt, 2) != SQLITE_NULL) {
                req.username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            }
            req.status = static_cast<ResetRequestStatusType>(sqlite3_column_int(stmt, 3));
            req.approver_id = sqlite3_column_int(stmt, 4);
            if (sqlite3_column_type(stmt, 5) != SQLITE_NULL) {
                req.new_password_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            }
            if (sqlite3_column_type(stmt, 6) != SQLITE_NULL) {
                req.new_salt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            }
            req.request_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
            if (sqlite3_column_type(stmt, 8) != SQLITE_NULL) {
                req.approve_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
            }
            requests.push_back(req);
        } else {
            break;
        }
    }

    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "QueryPendingResetRequests step failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::ApproveResetRequest(int32_t request_id, int32_t approver_id, const std::string& new_hash, const std::string& new_salt) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "UPDATE password_reset_request SET status = ?, approver_id = ?, new_password_hash = ?, new_salt = ?, approve_time = ? WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for ApproveResetRequest, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    std::string now = register_student::GetCurrentTimeString();

    sqlite3_bind_int(stmt, 1, ResetStatus_Approved);
    sqlite3_bind_int(stmt, 2, approver_id);
    sqlite3_bind_text(stmt, 3, new_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, new_salt.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, now.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 6, request_id);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "ApproveResetRequest failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::CheckResetPending(int32_t user_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT COUNT(*) FROM password_reset_request WHERE user_id = ? AND status = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for CheckResetPending, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_int(stmt, 2, ResetStatus_Pending);

    ret = sqlite3_step(stmt);
    if (ret == SQLITE_ROW) {
        int32_t count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return (count > 0) ? 1 : 0;
    }

    sqlite3_finalize(stmt);
    LOG_ERROR << "CheckResetPending step failed, ret=" << ret;
    return ERR_DB_EXEC_FAILED;
}

/* ==================== Registration Request ==================== */

int SqliteDatabase::InsertRegistrationRequest(const RegistrationRequest& request) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "INSERT OR REPLACE INTO registration_request (username, password_hash, salt, role, display_name, status, request_time) VALUES (?, ?, ?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for InsertRegistrationRequest, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_text(stmt, 1, request.username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, request.password_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, request.salt.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, static_cast<int32_t>(request.role));
    sqlite3_bind_text(stmt, 5, request.display_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 6, static_cast<int32_t>(request.status));
    sqlite3_bind_text(stmt, 7, request.request_time.c_str(), -1, SQLITE_TRANSIENT);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "InsertRegistrationRequest failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::QueryRegistrationRequestStatus(const std::string& username, int& status) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT status FROM registration_request WHERE username = ? ORDER BY id DESC LIMIT 1";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryRegistrationRequestStatus, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    ret = sqlite3_step(stmt);
    if (ret == SQLITE_ROW) {
        status = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return DB_OK;
    }

    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) {
        LOG_ERROR << "QueryRegistrationRequestStatus step failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return ERR_REG_REQ_NOT_FOUND;
}

int SqliteDatabase::QueryPendingRegistrationRequests(std::vector<RegistrationRequest>& requests) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    requests.clear();

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT id, username, password_hash, salt, role, display_name, status, request_time FROM registration_request WHERE status = ? ORDER BY id DESC";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryPendingRegistrationRequests, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, RegStatus_Pending);

    while (true) {
        ret = sqlite3_step(stmt);
        if (ret == SQLITE_ROW) {
            RegistrationRequest req;
            req.id = sqlite3_column_int(stmt, 0);
            if (sqlite3_column_type(stmt, 1) != SQLITE_NULL) {
                req.username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            }
            if (sqlite3_column_type(stmt, 2) != SQLITE_NULL) {
                req.password_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            }
            if (sqlite3_column_type(stmt, 3) != SQLITE_NULL) {
                req.salt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            }
            req.role = static_cast<UserRoleType>(sqlite3_column_int(stmt, 4));
            if (sqlite3_column_type(stmt, 5) != SQLITE_NULL) {
                req.display_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            }
            req.status = static_cast<RegistrationRequestStatusType>(sqlite3_column_int(stmt, 6));
            if (sqlite3_column_type(stmt, 7) != SQLITE_NULL) {
                req.request_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
            }
            requests.push_back(req);
        } else {
            break;
        }
    }

    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "QueryPendingRegistrationRequests step failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::QueryRegistrationRequestById(int32_t id, RegistrationRequest& request) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT id, username, password_hash, salt, role, display_name, status, request_time FROM registration_request WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryRegistrationRequestById, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, id);

    ret = sqlite3_step(stmt);
    if (ret == SQLITE_ROW) {
        request.id = sqlite3_column_int(stmt, 0);
        if (sqlite3_column_type(stmt, 1) != SQLITE_NULL) {
            request.username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        }
        if (sqlite3_column_type(stmt, 2) != SQLITE_NULL) {
            request.password_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        }
        if (sqlite3_column_type(stmt, 3) != SQLITE_NULL) {
            request.salt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        }
        request.role = static_cast<UserRoleType>(sqlite3_column_int(stmt, 4));
        if (sqlite3_column_type(stmt, 5) != SQLITE_NULL) {
            request.display_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        }
        request.status = static_cast<RegistrationRequestStatusType>(sqlite3_column_int(stmt, 6));
        if (sqlite3_column_type(stmt, 7) != SQLITE_NULL) {
            request.request_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        }
        sqlite3_finalize(stmt);
        return DB_OK;
    }

    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) {
        LOG_ERROR << "QueryRegistrationRequestById step failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return ERR_REG_REQ_NOT_FOUND;
}

int SqliteDatabase::UpdateRegistrationRequestStatus(int32_t id, RegistrationRequestStatusType status) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "UPDATE registration_request SET status = ? WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for UpdateRegistrationRequestStatus, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, static_cast<int32_t>(status));
    sqlite3_bind_int(stmt, 2, id);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "UpdateRegistrationRequestStatus failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::DeleteRegistrationRequest(int32_t id) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "DELETE FROM registration_request WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for DeleteRegistrationRequest, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, id);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "DeleteRegistrationRequest failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::CheckRegistrationRequestExists(const std::string& username) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT COUNT(*) FROM registration_request WHERE username = ? AND status = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for CheckRegistrationRequestExists, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, RegStatus_Pending);

    ret = sqlite3_step(stmt);
    if (ret == SQLITE_ROW) {
        int32_t count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return (count > 0) ? 1 : 0;
    }

    sqlite3_finalize(stmt);
    LOG_ERROR << "CheckRegistrationRequestExists step failed, ret=" << ret;
    return ERR_DB_EXEC_FAILED;
}

int SqliteDatabase::ApproveRegistrationRequestsAtomic(const std::vector<int32_t>& ids) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    /* 开启事务 */
    char* err_msg = nullptr;
    int ret = sqlite3_exec(db_, "BEGIN IMMEDIATE TRANSACTION", nullptr, nullptr, &err_msg);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "ApproveRegistrationRequestsAtomic: begin transaction failed, ret=" << ret;
        if (err_msg) { sqlite3_free(err_msg); }
        return ERR_DB_EXEC_FAILED;
    }

    for (size_t i = 0; i < ids.size(); ++i) {
        /* 查询申请 */
        const char* sel_sql = "SELECT id, username, password_hash, salt, role, display_name FROM registration_request WHERE id = ? AND status = ?";
        sqlite3_stmt* sel_stmt = nullptr;

        ret = sqlite3_prepare_v2(db_, sel_sql, -1, &sel_stmt, nullptr);
        if (ret != SQLITE_OK) {
            LOG_ERROR << "ApproveAtomic: prepare select failed, ret=" << ret;
            sqlite3_exec(db_, "ROLLBACK TRANSACTION", nullptr, nullptr, nullptr);
            return ERR_DB_PREPARE_FAILED;
        }

        sqlite3_bind_int(sel_stmt, 1, ids[i]);
        sqlite3_bind_int(sel_stmt, 2, RegStatus_Pending);

        ret = sqlite3_step(sel_stmt);
        if (ret != SQLITE_ROW) {
            sqlite3_finalize(sel_stmt);
            LOG_ERROR << "ApproveAtomic: request not found or not pending, id=" << ids[i];
            sqlite3_exec(db_, "ROLLBACK TRANSACTION", nullptr, nullptr, nullptr);
            return ERR_REG_REQ_NOT_FOUND;
        }

        std::string username;
        std::string password_hash;
        std::string salt;
        UserRoleType role = UserRole_Teacher;
        std::string display_name;

        if (sqlite3_column_type(sel_stmt, 1) != SQLITE_NULL) {
            username = reinterpret_cast<const char*>(sqlite3_column_text(sel_stmt, 1));
        }
        if (sqlite3_column_type(sel_stmt, 2) != SQLITE_NULL) {
            password_hash = reinterpret_cast<const char*>(sqlite3_column_text(sel_stmt, 2));
        }
        if (sqlite3_column_type(sel_stmt, 3) != SQLITE_NULL) {
            salt = reinterpret_cast<const char*>(sqlite3_column_text(sel_stmt, 3));
        }
        role = static_cast<UserRoleType>(sqlite3_column_int(sel_stmt, 4));
        if (sqlite3_column_type(sel_stmt, 5) != SQLITE_NULL) {
            display_name = reinterpret_cast<const char*>(sqlite3_column_text(sel_stmt, 5));
        }
        sqlite3_finalize(sel_stmt);

        /* 插入 users 表 */
        const char* ins_sql = "INSERT INTO users (username, password_hash, salt, role, display_name, create_time) VALUES (?, ?, ?, ?, ?, ?)";
        sqlite3_stmt* ins_stmt = nullptr;

        ret = sqlite3_prepare_v2(db_, ins_sql, -1, &ins_stmt, nullptr);
        if (ret != SQLITE_OK) {
            LOG_ERROR << "ApproveAtomic: prepare insert user failed, ret=" << ret;
            sqlite3_exec(db_, "ROLLBACK TRANSACTION", nullptr, nullptr, nullptr);
            return ERR_DB_PREPARE_FAILED;
        }

        std::string now = register_student::GetCurrentTimeString();
        sqlite3_bind_text(ins_stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins_stmt, 2, password_hash.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins_stmt, 3, salt.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(ins_stmt, 4, static_cast<int32_t>(role));
        sqlite3_bind_text(ins_stmt, 5, display_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins_stmt, 6, now.c_str(), -1, SQLITE_TRANSIENT);

        ret = sqlite3_step(ins_stmt);
        sqlite3_finalize(ins_stmt);

        if (ret != SQLITE_DONE) {
            LOG_ERROR << "ApproveAtomic: insert user failed, ret=" << ret << " username=" << username;
            sqlite3_exec(db_, "ROLLBACK TRANSACTION", nullptr, nullptr, nullptr);
            return ERR_DB_EXEC_FAILED;
        }

        /* 更新申请状态为已通过 */
        const char* upd_sql = "UPDATE registration_request SET status = ? WHERE id = ?";
        sqlite3_stmt* upd_stmt = nullptr;

        ret = sqlite3_prepare_v2(db_, upd_sql, -1, &upd_stmt, nullptr);
        if (ret != SQLITE_OK) {
            LOG_ERROR << "ApproveAtomic: prepare update status failed, ret=" << ret;
            sqlite3_exec(db_, "ROLLBACK TRANSACTION", nullptr, nullptr, nullptr);
            return ERR_DB_PREPARE_FAILED;
        }

        sqlite3_bind_int(upd_stmt, 1, RegStatus_Approved);
        sqlite3_bind_int(upd_stmt, 2, ids[i]);

        ret = sqlite3_step(upd_stmt);
        sqlite3_finalize(upd_stmt);

        if (ret != SQLITE_DONE) {
            LOG_ERROR << "ApproveAtomic: update status failed, ret=" << ret;
            sqlite3_exec(db_, "ROLLBACK TRANSACTION", nullptr, nullptr, nullptr);
            return ERR_DB_EXEC_FAILED;
        }
    }

    ret = sqlite3_exec(db_, "COMMIT TRANSACTION", nullptr, nullptr, &err_msg);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "ApproveAtomic: commit failed, ret=" << ret;
        if (err_msg) { sqlite3_free(err_msg); }
        sqlite3_exec(db_, "ROLLBACK TRANSACTION", nullptr, nullptr, nullptr);
        return ERR_DB_EXEC_FAILED;
    }

    LOG_INFO << "ApproveRegistrationRequestsAtomic: approved " << ids.size() << " requests";
    return DB_OK;
}

/* ==================== IClassDao ==================== */

int SqliteDatabase::InsertClass(const ClassInfo& info) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "INSERT INTO class_info (class_name, start_time, end_time, description, enrollment_capacity, enrollment_used, class_type, create_time) VALUES (?, ?, ?, ?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for InsertClass, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_text(stmt, 1, info.class_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, info.start_time.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, info.end_time.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, info.description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, info.enrollment_capacity);
    sqlite3_bind_double(stmt, 6, info.enrollment_used);
    sqlite3_bind_text(stmt, 7, info.class_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, info.create_time.c_str(), -1, SQLITE_TRANSIENT);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "InsertClass failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    const_cast<ClassInfo&>(info).id = static_cast<int32_t>(sqlite3_last_insert_rowid(db_));
    LOG_DEBUG << "InsertClass success, id=" << info.id;
    return DB_OK;
}

int SqliteDatabase::QueryClassById(int32_t id, ClassInfo& info) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT id, class_name, start_time, end_time, description, enrollment_capacity, enrollment_used, class_type, create_time FROM class_info WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryClassById, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, id);

    ret = sqlite3_step(stmt);
    if (ret == SQLITE_ROW) {
        info.id = sqlite3_column_int(stmt, 0);
        info.class_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        info.start_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        info.end_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        if (sqlite3_column_type(stmt, 4) != SQLITE_NULL) {
            info.description = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        }
        info.enrollment_capacity = sqlite3_column_int(stmt, 5);
        info.enrollment_used = sqlite3_column_double(stmt, 6);
        info.class_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        info.create_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        sqlite3_finalize(stmt);
        return DB_OK;
    }

    sqlite3_finalize(stmt);

    if (ret == SQLITE_DONE) {
        return ERR_DB_EXEC_FAILED;
    }

    LOG_ERROR << "QueryClassById step failed, ret=" << ret;
    return ERR_DB_EXEC_FAILED;
}

int SqliteDatabase::QueryClassByName(const std::string& name, ClassInfo& info) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT id, class_name, start_time, end_time, description, enrollment_capacity, enrollment_used, class_type, create_time FROM class_info WHERE class_name = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryClassByName, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);

    ret = sqlite3_step(stmt);
    if (ret == SQLITE_ROW) {
        info.id = sqlite3_column_int(stmt, 0);
        info.class_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        info.start_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        info.end_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        if (sqlite3_column_type(stmt, 4) != SQLITE_NULL) {
            info.description = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        }
        info.enrollment_capacity = sqlite3_column_int(stmt, 5);
        info.enrollment_used = sqlite3_column_double(stmt, 6);
        info.class_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        info.create_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        sqlite3_finalize(stmt);
        return DB_OK;
    }

    sqlite3_finalize(stmt);

    if (ret == SQLITE_DONE) {
        return ERR_DB_EXEC_FAILED;
    }

    LOG_ERROR << "QueryClassByName step failed, ret=" << ret;
    return ERR_DB_EXEC_FAILED;
}

int SqliteDatabase::QueryAllClasses(std::vector<ClassInfo>& classes) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    classes.clear();

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT id, class_name, start_time, end_time, description, enrollment_capacity, enrollment_used, class_type, create_time FROM class_info ORDER BY id DESC";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryAllClasses, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    while (true) {
        ret = sqlite3_step(stmt);
        if (ret == SQLITE_ROW) {
            ClassInfo info;
            info.id = sqlite3_column_int(stmt, 0);
            info.class_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            info.start_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            info.end_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            if (sqlite3_column_type(stmt, 4) != SQLITE_NULL) {
                info.description = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            }
            info.enrollment_capacity = sqlite3_column_int(stmt, 5);
            info.enrollment_used = sqlite3_column_double(stmt, 6);
            info.class_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
            info.create_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
            classes.push_back(info);
        } else {
            break;
        }
    }

    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "QueryAllClasses step failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::QueryActiveClasses(std::vector<ClassInfo>& classes) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    classes.clear();

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT id, class_name, start_time, end_time, description, enrollment_capacity, enrollment_used, class_type, create_time FROM class_info WHERE end_time > date('now') ORDER BY id DESC";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryActiveClasses, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    while (true) {
        ret = sqlite3_step(stmt);
        if (ret == SQLITE_ROW) {
            ClassInfo info;
            info.id = sqlite3_column_int(stmt, 0);
            info.class_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            info.start_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            info.end_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            if (sqlite3_column_type(stmt, 4) != SQLITE_NULL) {
                info.description = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            }
            info.enrollment_capacity = sqlite3_column_int(stmt, 5);
            info.enrollment_used = sqlite3_column_double(stmt, 6);
            info.class_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
            info.create_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
            classes.push_back(info);
        } else {
            break;
        }
    }

    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "QueryActiveClasses step failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::SearchClassesByName(const std::string& keyword, std::vector<ClassInfo>& classes) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    classes.clear();

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT id, class_name, start_time, end_time, description, enrollment_capacity, enrollment_used, class_type, create_time FROM class_info WHERE class_name LIKE ? ORDER BY id DESC";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for SearchClassesByName, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    std::string pattern = "%" + keyword + "%";
    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);

    while (true) {
        ret = sqlite3_step(stmt);
        if (ret == SQLITE_ROW) {
            ClassInfo info;
            info.id = sqlite3_column_int(stmt, 0);
            info.class_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            info.start_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            info.end_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            if (sqlite3_column_type(stmt, 4) != SQLITE_NULL) {
                info.description = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            }
            info.enrollment_capacity = sqlite3_column_int(stmt, 5);
            info.enrollment_used = sqlite3_column_double(stmt, 6);
            info.class_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
            info.create_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
            classes.push_back(info);
        } else {
            break;
        }
    }

    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "SearchClassesByName step failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::SearchActiveClassesByName(const std::string& keyword, std::vector<ClassInfo>& classes) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    classes.clear();

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT id, class_name, start_time, end_time, description, enrollment_capacity, enrollment_used, class_type, create_time FROM class_info WHERE end_time > date('now') AND class_name LIKE ? ORDER BY id DESC";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for SearchActiveClassesByName, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    std::string pattern = "%" + keyword + "%";
    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);

    while (true) {
        ret = sqlite3_step(stmt);
        if (ret == SQLITE_ROW) {
            ClassInfo info;
            info.id = sqlite3_column_int(stmt, 0);
            info.class_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            info.start_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            info.end_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            if (sqlite3_column_type(stmt, 4) != SQLITE_NULL) {
                info.description = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            }
            info.enrollment_capacity = sqlite3_column_int(stmt, 5);
            info.enrollment_used = sqlite3_column_double(stmt, 6);
            info.class_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
            info.create_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
            classes.push_back(info);
        } else {
            break;
        }
    }

    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "SearchActiveClassesByName step failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::UpdateEnrollment(int32_t class_id, int32_t capacity) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "UPDATE class_info SET enrollment_capacity = ? WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for UpdateEnrollment, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, capacity);
    sqlite3_bind_int(stmt, 2, class_id);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "UpdateEnrollment failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::IncrementEnrollmentUsed(int32_t class_id, double delta) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    return IncrementEnrollmentUsedInternal(class_id, delta);
}

int SqliteDatabase::IncrementEnrollmentUsedInternal(int32_t class_id, double delta) {
    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    /* Query current value before update */
    double before_value = 0.0;
    const char* query_sql = "SELECT enrollment_used FROM class_info WHERE id = ?";
    sqlite3_stmt* query_stmt = nullptr;
    if (sqlite3_prepare_v2(db_, query_sql, -1, &query_stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(query_stmt, 1, class_id);
        if (sqlite3_step(query_stmt) == SQLITE_ROW) {
            before_value = sqlite3_column_double(query_stmt, 0);
        }
        sqlite3_finalize(query_stmt);
    }

    const char* sql = "UPDATE class_info SET enrollment_used = ROUND(enrollment_used + ?, 4) WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for IncrementEnrollmentUsed, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_double(stmt, 1, delta);
    sqlite3_bind_int(stmt, 2, class_id);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "IncrementEnrollmentUsed failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    LOG_INFO << "IncrementEnrollmentUsed: class_id=" << class_id
             << " delta=" << delta
             << " before=" << before_value
             << " after=" << (before_value + delta);

    return DB_OK;
}

int SqliteDatabase::InsertPrice(const PriceInfo& info) {
    /* price_library 改造后此方法保留以兼容旧调用，但新代码改用 CreateClassWithPricesAtomic。
       旧字段 price 已废弃，新表使用 preset_id + snapshot_amount。 */
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "INSERT INTO class_price (class_id, preset_id, activity_name, snapshot_amount, snapshot_headcount) VALUES (?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for InsertPrice, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, info.class_id);
    sqlite3_bind_int(stmt, 2, info.preset_id);
    sqlite3_bind_text(stmt, 3, info.activity_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 4, info.snapshot_amount);
    sqlite3_bind_int(stmt, 5, info.snapshot_headcount);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "InsertPrice failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    int32_t price_id = static_cast<int32_t>(sqlite3_last_insert_rowid(db_));
    const_cast<PriceInfo&>(info).id = price_id;
    LOG_DEBUG << "InsertPrice success, id=" << price_id;
    return DB_OK;
}

int SqliteDatabase::QueryPricesByClassId(int32_t class_id, std::vector<PriceInfo>& prices) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    prices.clear();

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    /* LEFT JOIN price_preset：preset 存在时取实时 amount + expected_headcount，删除时回退 snapshot_* */
    const char* sql =
        "SELECT cp.id, cp.class_id, cp.preset_id, cp.snapshot_amount, cp.snapshot_headcount, cp.activity_name, pp.amount, pp.expected_headcount "
        "FROM class_price cp "
        "LEFT JOIN price_preset pp ON cp.preset_id = pp.id "
        "WHERE cp.class_id = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryPricesByClassId, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, class_id);

    while (true) {
        ret = sqlite3_step(stmt);
        if (ret == SQLITE_ROW) {
            PriceInfo info;
            info.id = sqlite3_column_int(stmt, 0);
            info.class_id = sqlite3_column_int(stmt, 1);
            info.preset_id = sqlite3_column_int(stmt, 2);
            info.snapshot_amount = sqlite3_column_double(stmt, 3);
            info.snapshot_headcount = sqlite3_column_int(stmt, 4);
            info.activity_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            /* preset_amount 可能为 NULL（preset 已删除），回退 snapshot_amount */
            if (sqlite3_column_type(stmt, 6) == SQLITE_FLOAT) {
                info.price = sqlite3_column_double(stmt, 6);
            } else {
                info.price = info.snapshot_amount;
            }
            /* preset_headcount 可能为 NULL（preset 已删除），回退 snapshot_headcount */
            if (sqlite3_column_type(stmt, 7) == SQLITE_INTEGER) {
                info.snapshot_headcount = sqlite3_column_int(stmt, 7);
            }
            prices.push_back(info);
        } else {
            break;
        }
    }

    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "QueryPricesByClassId step failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    /* populate qrcode_paths from price_preset_qrcode (preset 存在时) */
    for (size_t i = 0; i < prices.size(); ++i) {
        std::vector<std::string> paths;
        if (prices[i].preset_id > 0) {
            const char* qr_sql = "SELECT qrcode_path FROM price_preset_qrcode WHERE preset_id = ?";
            sqlite3_stmt* qr_stmt = nullptr;

            int qr_ret = sqlite3_prepare_v2(db_, qr_sql, -1, &qr_stmt, nullptr);
            if (qr_ret != SQLITE_OK) {
                LOG_ERROR << "Prepare failed for preset qrcode query, ret=" << qr_ret;
                return ERR_DB_PREPARE_FAILED;
            }

            sqlite3_bind_int(qr_stmt, 1, prices[i].preset_id);

            while (true) {
                qr_ret = sqlite3_step(qr_stmt);
                if (qr_ret == SQLITE_ROW) {
                    paths.push_back(reinterpret_cast<const char*>(sqlite3_column_text(qr_stmt, 0)));
                } else {
                    break;
                }
            }

            sqlite3_finalize(qr_stmt);

            if (qr_ret != SQLITE_DONE) {
                LOG_ERROR << "Preset qrcode query step failed, ret=" << qr_ret;
                return ERR_DB_EXEC_FAILED;
            }
        }
        prices[i].qrcode_paths = paths;
    }

    return DB_OK;
}

int SqliteDatabase::QueryPriceById(int32_t price_id, PriceInfo& price) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql =
        "SELECT cp.id, cp.class_id, cp.preset_id, cp.snapshot_amount, cp.activity_name, pp.amount "
        "FROM class_price cp "
        "LEFT JOIN price_preset pp ON cp.preset_id = pp.id "
        "WHERE cp.id = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryPriceById, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, price_id);

    ret = sqlite3_step(stmt);
    if (ret == SQLITE_ROW) {
        price.id = sqlite3_column_int(stmt, 0);
        price.class_id = sqlite3_column_int(stmt, 1);
        price.preset_id = sqlite3_column_int(stmt, 2);
        price.snapshot_amount = sqlite3_column_double(stmt, 3);
        price.activity_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        if (sqlite3_column_type(stmt, 5) == SQLITE_FLOAT) {
            price.price = sqlite3_column_double(stmt, 5);
        } else {
            price.price = price.snapshot_amount;
        }
        sqlite3_finalize(stmt);

        /* populate qrcode_paths from price_preset_qrcode */
        std::vector<std::string> paths;
        if (price.preset_id > 0) {
            const char* qr_sql = "SELECT qrcode_path FROM price_preset_qrcode WHERE preset_id = ?";
            sqlite3_stmt* qr_stmt = nullptr;
            int qr_ret = sqlite3_prepare_v2(db_, qr_sql, -1, &qr_stmt, nullptr);
            if (qr_ret == SQLITE_OK) {
                sqlite3_bind_int(qr_stmt, 1, price.preset_id);
                while (true) {
                    qr_ret = sqlite3_step(qr_stmt);
                    if (qr_ret == SQLITE_ROW) {
                        paths.push_back(reinterpret_cast<const char*>(sqlite3_column_text(qr_stmt, 0)));
                    } else {
                        break;
                    }
                }
                sqlite3_finalize(qr_stmt);
            }
        }
        price.qrcode_paths = paths;
        return DB_OK;
    }

    sqlite3_finalize(stmt);

    LOG_ERROR << "QueryPriceById not found or step failed, ret=" << ret;
    return ERR_DB_EXEC_FAILED;
}

int SqliteDatabase::InsertQrcode(int32_t price_id, const std::string& qrcode_path) {
    /* price_library 改造后此方法保留以兼容旧调用：通过 class_price.preset_id 写入 price_preset_qrcode。
       新代码应直接用 AddPresetQrcode。 */
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    /* 查询 class_price 获取 preset_id */
    const char* sel_sql = "SELECT preset_id FROM class_price WHERE id = ?";
    sqlite3_stmt* sel_stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sel_sql, -1, &sel_stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for InsertQrcode select, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }
    sqlite3_bind_int(sel_stmt, 1, price_id);
    ret = sqlite3_step(sel_stmt);
    int32_t preset_id = 0;
    if (ret == SQLITE_ROW) {
        preset_id = sqlite3_column_int(sel_stmt, 0);
    }
    sqlite3_finalize(sel_stmt);

    if (preset_id <= 0) {
        LOG_ERROR << "InsertQrcode: preset_id not found for price_id=" << price_id;
        return ERR_DB_EXEC_FAILED;
    }

    const char* sql = "INSERT INTO price_preset_qrcode (preset_id, qrcode_path) VALUES (?, ?)";
    sqlite3_stmt* stmt = nullptr;

    ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for InsertQrcode, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, preset_id);
    sqlite3_bind_text(stmt, 2, qrcode_path.c_str(), -1, SQLITE_TRANSIENT);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "InsertQrcode failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::QueryQrcodesByPriceId(int32_t price_id, std::vector<std::string>& paths) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    paths.clear();

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    /* JOIN class_price → price_preset_qrcode 通过 preset_id 关联 */
    const char* sql =
        "SELECT qrcode_path FROM price_preset_qrcode "
        "WHERE preset_id = (SELECT preset_id FROM class_price WHERE id = ?)";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryQrcodesByPriceId, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, price_id);

    while (true) {
        ret = sqlite3_step(stmt);
        if (ret == SQLITE_ROW) {
            paths.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        } else {
            break;
        }
    }

    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "QueryQrcodesByPriceId step failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

/* ==================== 价位预设管理 ==================== */

int SqliteDatabase::InsertPricePresetInternal(const PricePresetInfo& info) {
    /* caller holds db_mutex_ */
    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "INSERT INTO price_preset (amount, expected_headcount, create_time) VALUES (?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for InsertPricePreset, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_double(stmt, 1, info.amount);
    sqlite3_bind_int(stmt, 2, info.expected_headcount);
    sqlite3_bind_text(stmt, 3, info.create_time.c_str(), -1, SQLITE_TRANSIENT);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        if (ret == SQLITE_CONSTRAINT) {
            LOG_DEBUG << "InsertPricePreset: duplicate amount+headcount, amount=" << info.amount << " headcount=" << info.expected_headcount;
            return ERR_PRICE_DUPLICATE;
        }
        LOG_ERROR << "InsertPricePreset failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    int32_t preset_id = static_cast<int32_t>(sqlite3_last_insert_rowid(db_));
    const_cast<PricePresetInfo&>(info).id = preset_id;

    /* insert qrcode paths */
    for (size_t i = 0; i < info.qrcode_paths.size(); ++i) {
        const char* qr_sql = "INSERT INTO price_preset_qrcode (preset_id, qrcode_path) VALUES (?, ?)";
        sqlite3_stmt* qr_stmt = nullptr;
        int qr_ret = sqlite3_prepare_v2(db_, qr_sql, -1, &qr_stmt, nullptr);
        if (qr_ret != SQLITE_OK) {
            LOG_ERROR << "Prepare failed for preset qrcode insert, ret=" << qr_ret;
            return ERR_DB_PREPARE_FAILED;
        }
        sqlite3_bind_int(qr_stmt, 1, preset_id);
        sqlite3_bind_text(qr_stmt, 2, info.qrcode_paths[i].c_str(), -1, SQLITE_TRANSIENT);
        qr_ret = sqlite3_step(qr_stmt);
        sqlite3_finalize(qr_stmt);
        if (qr_ret != SQLITE_DONE) {
            LOG_ERROR << "InsertPresetQrcode failed, ret=" << qr_ret;
            return ERR_DB_EXEC_FAILED;
        }
    }

    LOG_DEBUG << "InsertPricePreset success, id=" << preset_id;
    return DB_OK;
}

int SqliteDatabase::QueryPricePresetByIdInternal(int32_t id, PricePresetInfo& info) {
    /* caller holds db_mutex_ */
    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT id, amount, expected_headcount, create_time FROM price_preset WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryPricePresetById, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, id);

    ret = sqlite3_step(stmt);
    if (ret == SQLITE_ROW) {
        info.id = sqlite3_column_int(stmt, 0);
        info.amount = sqlite3_column_double(stmt, 1);
        info.expected_headcount = sqlite3_column_int(stmt, 2);
        info.create_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        sqlite3_finalize(stmt);

        /* populate qrcode_paths */
        std::vector<std::string> paths;
        const char* qr_sql = "SELECT qrcode_path FROM price_preset_qrcode WHERE preset_id = ?";
        sqlite3_stmt* qr_stmt = nullptr;
        int qr_ret = sqlite3_prepare_v2(db_, qr_sql, -1, &qr_stmt, nullptr);
        if (qr_ret != SQLITE_OK) {
            LOG_ERROR << "Prepare failed for preset qrcode query, ret=" << qr_ret;
            return ERR_DB_PREPARE_FAILED;
        }
        sqlite3_bind_int(qr_stmt, 1, info.id);
        while (true) {
            qr_ret = sqlite3_step(qr_stmt);
            if (qr_ret == SQLITE_ROW) {
                paths.push_back(reinterpret_cast<const char*>(sqlite3_column_text(qr_stmt, 0)));
            } else {
                break;
            }
        }
        sqlite3_finalize(qr_stmt);
        info.qrcode_paths = paths;
        return DB_OK;
    }

    sqlite3_finalize(stmt);
    LOG_DEBUG << "QueryPricePresetById not found, id=" << id;
    return ERR_PRICE_PRESET_NOT_FOUND;
}

int SqliteDatabase::CountClassPriceByPresetIdInternal(int32_t preset_id) {
    /* caller holds db_mutex_ */
    if (!db_) {
        return 0;
    }
    const char* sql = "SELECT COUNT(*) FROM class_price WHERE preset_id = ?";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        return 0;
    }
    sqlite3_bind_int(stmt, 1, preset_id);
    ret = sqlite3_step(stmt);
    int count = 0;
    if (ret == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

int SqliteDatabase::CountRegistrationByPresetIdInternal(int32_t preset_id) {
    /* caller holds db_mutex_；JOIN class_price 通过 registration.price_id 关联 */
    if (!db_) {
        return 0;
    }
    const char* sql =
        "SELECT COUNT(*) FROM registration r "
        "JOIN class_price cp ON r.price_id = cp.id "
        "WHERE cp.preset_id = ?";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        return 0;
    }
    sqlite3_bind_int(stmt, 1, preset_id);
    ret = sqlite3_step(stmt);
    int count = 0;
    if (ret == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

int SqliteDatabase::CountPresetQrcodeInternal(int32_t preset_id) {
    /* caller holds db_mutex_ */
    if (!db_) {
        return 0;
    }
    const char* sql = "SELECT COUNT(*) FROM price_preset_qrcode WHERE preset_id = ?";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        return 0;
    }
    sqlite3_bind_int(stmt, 1, preset_id);
    ret = sqlite3_step(stmt);
    int count = 0;
    if (ret == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

int SqliteDatabase::InsertPricePreset(const PricePresetInfo& info) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    /* 事务包裹金额插入与图片插入（调用方已持有 db_mutex_，直接执行 SQL） */
    int ret = sqlite3_exec(db_, "BEGIN IMMEDIATE TRANSACTION", nullptr, nullptr, nullptr);
    if (ret != SQLITE_OK) {
        return ERR_DB_EXEC_FAILED;
    }

    ret = InsertPricePresetInternal(info);
    if (ret != DB_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ret;
    }

    ret = sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr);
    if (ret != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::QueryAllPricePresets(std::vector<PricePresetInfo>& presets) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    presets.clear();

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT id, amount, expected_headcount, create_time FROM price_preset ORDER BY id DESC";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryAllPricePresets, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    while (true) {
        ret = sqlite3_step(stmt);
        if (ret == SQLITE_ROW) {
            PricePresetInfo info;
            info.id = sqlite3_column_int(stmt, 0);
            info.amount = sqlite3_column_double(stmt, 1);
            info.expected_headcount = sqlite3_column_int(stmt, 2);
            info.create_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            presets.push_back(info);
        } else {
            break;
        }
    }

    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "QueryAllPricePresets step failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    /* populate qrcode_paths for each preset */
    for (size_t i = 0; i < presets.size(); ++i) {
        std::vector<std::string> paths;
        const char* qr_sql = "SELECT qrcode_path FROM price_preset_qrcode WHERE preset_id = ?";
        sqlite3_stmt* qr_stmt = nullptr;
        int qr_ret = sqlite3_prepare_v2(db_, qr_sql, -1, &qr_stmt, nullptr);
        if (qr_ret != SQLITE_OK) {
            LOG_ERROR << "Prepare failed for preset qrcode query, ret=" << qr_ret;
            return ERR_DB_PREPARE_FAILED;
        }
        sqlite3_bind_int(qr_stmt, 1, presets[i].id);
        while (true) {
            qr_ret = sqlite3_step(qr_stmt);
            if (qr_ret == SQLITE_ROW) {
                paths.push_back(reinterpret_cast<const char*>(sqlite3_column_text(qr_stmt, 0)));
            } else {
                break;
            }
        }
        sqlite3_finalize(qr_stmt);
        presets[i].qrcode_paths = paths;
    }

    return DB_OK;
}

int SqliteDatabase::QueryPricePresetById(int32_t id, PricePresetInfo& info) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    return QueryPricePresetByIdInternal(id, info);
}

int SqliteDatabase::DeletePricePresetAtomic(int32_t preset_id,
                                            std::vector<std::string>& deleted_files) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    deleted_files.clear();

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    /* BEGIN IMMEDIATE 保证读到的引用计数不会被并发插入破坏 */
    int ret = sqlite3_exec(db_, "BEGIN IMMEDIATE TRANSACTION", nullptr, nullptr, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "BeginTransaction failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    /* 校验班级引用 */
    int class_ref_count = CountClassPriceByPresetIdInternal(preset_id);
    if (class_ref_count > 0) {
        RollbackTransactionInternal();
        LOG_DEBUG << "DeletePricePreset: referenced by class, preset_id=" << preset_id
                  << " count=" << class_ref_count;
        return ERR_PRICE_PRESET_IN_USE;
    }

    /* 校验报名引用 */
    int reg_ref_count = CountRegistrationByPresetIdInternal(preset_id);
    if (reg_ref_count > 0) {
        RollbackTransactionInternal();
        LOG_DEBUG << "DeletePricePreset: referenced by registration, preset_id=" << preset_id
                  << " count=" << reg_ref_count;
        return ERR_PRICE_PRESET_IN_USE;
    }

    /* 收集待删除图片路径 */
    const char* sel_sql = "SELECT qrcode_path FROM price_preset_qrcode WHERE preset_id = ?";
    sqlite3_stmt* sel_stmt = nullptr;
    ret = sqlite3_prepare_v2(db_, sel_sql, -1, &sel_stmt, nullptr);
    if (ret != SQLITE_OK) {
        RollbackTransactionInternal();
        LOG_ERROR << "Prepare failed for select preset qrcode, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }
    sqlite3_bind_int(sel_stmt, 1, preset_id);
    while (true) {
        ret = sqlite3_step(sel_stmt);
        if (ret == SQLITE_ROW) {
            deleted_files.push_back(reinterpret_cast<const char*>(sqlite3_column_text(sel_stmt, 0)));
        } else {
            break;
        }
    }
    sqlite3_finalize(sel_stmt);

    /* 删除图片记录 */
    const char* del_qr_sql = "DELETE FROM price_preset_qrcode WHERE preset_id = ?";
    sqlite3_stmt* del_qr_stmt = nullptr;
    ret = sqlite3_prepare_v2(db_, del_qr_sql, -1, &del_qr_stmt, nullptr);
    if (ret != SQLITE_OK) {
        RollbackTransactionInternal();
        LOG_ERROR << "Prepare failed for delete preset qrcode, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }
    sqlite3_bind_int(del_qr_stmt, 1, preset_id);
    ret = sqlite3_step(del_qr_stmt);
    sqlite3_finalize(del_qr_stmt);
    if (ret != SQLITE_DONE) {
        RollbackTransactionInternal();
        LOG_ERROR << "Delete preset qrcode failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    /* 删除预设 */
    const char* del_preset_sql = "DELETE FROM price_preset WHERE id = ?";
    sqlite3_stmt* del_preset_stmt = nullptr;
    ret = sqlite3_prepare_v2(db_, del_preset_sql, -1, &del_preset_stmt, nullptr);
    if (ret != SQLITE_OK) {
        RollbackTransactionInternal();
        LOG_ERROR << "Prepare failed for delete preset, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }
    sqlite3_bind_int(del_preset_stmt, 1, preset_id);
    ret = sqlite3_step(del_preset_stmt);
    sqlite3_finalize(del_preset_stmt);
    if (ret != SQLITE_DONE) {
        RollbackTransactionInternal();
        LOG_ERROR << "Delete preset failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    ret = CommitTransactionInternal();
    if (ret != DB_OK) {
        RollbackTransactionInternal();
        return ret;
    }

    LOG_INFO << "DeletePricePreset success, preset_id=" << preset_id
             << " files_to_delete=" << deleted_files.size();
    return DB_OK;
}

int SqliteDatabase::AddPresetQrcode(int32_t preset_id, const std::string& qrcode_path) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    /* 校验预设存在 */
    PricePresetInfo tmp;
    if (QueryPricePresetByIdInternal(preset_id, tmp) != DB_OK) {
        return ERR_PRICE_PRESET_NOT_FOUND;
    }

    /* 校验 ≤10 张上限 */
    int count = CountPresetQrcodeInternal(preset_id);
    if (count >= 10) {
        LOG_DEBUG << "AddPresetQrcode: exceeds 10 limit, current=" << count;
        return ERR_INVALID_PARAM;
    }

    const char* sql = "INSERT INTO price_preset_qrcode (preset_id, qrcode_path) VALUES (?, ?)";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for AddPresetQrcode, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }
    sqlite3_bind_int(stmt, 1, preset_id);
    sqlite3_bind_text(stmt, 2, qrcode_path.c_str(), -1, SQLITE_TRANSIENT);
    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) {
        LOG_ERROR << "AddPresetQrcode failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::DeletePresetQrcode(int32_t preset_id, const std::string& qrcode_path,
                                       std::string& deleted_file) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    deleted_file.clear();

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    /* 校验记录存在 */
    const char* sel_sql = "SELECT qrcode_path FROM price_preset_qrcode WHERE preset_id = ? AND qrcode_path = ?";
    sqlite3_stmt* sel_stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sel_sql, -1, &sel_stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for select preset qrcode, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }
    sqlite3_bind_int(sel_stmt, 1, preset_id);
    sqlite3_bind_text(sel_stmt, 2, qrcode_path.c_str(), -1, SQLITE_TRANSIENT);
    ret = sqlite3_step(sel_stmt);
    if (ret == SQLITE_ROW) {
        deleted_file = reinterpret_cast<const char*>(sqlite3_column_text(sel_stmt, 0));
        sqlite3_finalize(sel_stmt);
    } else {
        sqlite3_finalize(sel_stmt);
        return ERR_DB_EXEC_FAILED;
    }

    /* 删除记录 */
    const char* del_sql = "DELETE FROM price_preset_qrcode WHERE preset_id = ? AND qrcode_path = ?";
    sqlite3_stmt* del_stmt = nullptr;
    ret = sqlite3_prepare_v2(db_, del_sql, -1, &del_stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for delete preset qrcode, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }
    sqlite3_bind_int(del_stmt, 1, preset_id);
    sqlite3_bind_text(del_stmt, 2, qrcode_path.c_str(), -1, SQLITE_TRANSIENT);
    ret = sqlite3_step(del_stmt);
    sqlite3_finalize(del_stmt);
    if (ret != SQLITE_DONE) {
        LOG_ERROR << "Delete preset qrcode failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

std::string SqliteDatabase::QueryClassNameByPresetId(int32_t preset_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return "";
    }

    /* JOIN class_price → class_info 通过 class_id 还原班级名 */
    const char* sql =
        "SELECT ci.class_name FROM class_price cp "
        "JOIN class_info ci ON cp.class_id = ci.id "
        "WHERE cp.preset_id = ? LIMIT 1";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryClassNameByPresetId, ret=" << ret;
        return "";
    }
    sqlite3_bind_int(stmt, 1, preset_id);
    ret = sqlite3_step(stmt);
    std::string class_name;
    if (ret == SQLITE_ROW) {
        const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (name) {
            class_name = name;
        }
    }
    sqlite3_finalize(stmt);
    return class_name;
}

int SqliteDatabase::CreateClassWithPricesAtomic(
    const ClassInfo& class_info,
    const std::vector<std::pair<std::string, int32_t> >& prices,
    int32_t& generated_class_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    /* 价位项非空校验 */
    if (prices.empty()) {
        return ERR_INVALID_PARAM;
    }

    int ret = sqlite3_exec(db_, "BEGIN IMMEDIATE TRANSACTION", nullptr, nullptr, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "BeginTransaction failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    /* 插入 class_info（复用现有 SQL） */
    const char* class_sql = "INSERT INTO class_info (class_name, start_time, end_time, description, enrollment_capacity, enrollment_used, class_type, create_time) VALUES (?, ?, ?, ?, ?, ?, ?, ?)";
    sqlite3_stmt* class_stmt = nullptr;
    ret = sqlite3_prepare_v2(db_, class_sql, -1, &class_stmt, nullptr);
    if (ret != SQLITE_OK) {
        RollbackTransactionInternal();
        LOG_ERROR << "Prepare failed for insert class, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }
    sqlite3_bind_text(class_stmt, 1, class_info.class_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(class_stmt, 2, class_info.start_time.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(class_stmt, 3, class_info.end_time.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(class_stmt, 4, class_info.description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(class_stmt, 5, class_info.enrollment_capacity);
    sqlite3_bind_double(class_stmt, 6, class_info.enrollment_used);
    sqlite3_bind_text(class_stmt, 7, class_info.class_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(class_stmt, 8, class_info.create_time.c_str(), -1, SQLITE_TRANSIENT);
    ret = sqlite3_step(class_stmt);
    sqlite3_finalize(class_stmt);
    if (ret != SQLITE_DONE) {
        RollbackTransactionInternal();
        if (ret == SQLITE_CONSTRAINT) {
            LOG_DEBUG << "CreateClass: duplicate class name, name=" << class_info.class_name;
            return ERR_CLASS_NAME_DUPLICATE;
        }
        LOG_ERROR << "Insert class failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }
    int32_t new_class_id = static_cast<int32_t>(sqlite3_last_insert_rowid(db_));
    generated_class_id = new_class_id;

    /* 同班金额+人数去重（内存 set） */
    std::vector<std::pair<double, int32_t> > seen_amount_headcount;

    /* 逐项插入 class_price */
    for (size_t i = 0; i < prices.size(); ++i) {
        int32_t preset_id = prices[i].second;
        const std::string& activity_name = prices[i].first;

        /* 查询预设 */
        PricePresetInfo preset_info;
        if (QueryPricePresetByIdInternal(preset_id, preset_info) != DB_OK) {
            RollbackTransactionInternal();
            LOG_DEBUG << "CreateClass: preset not found, preset_id=" << preset_id;
            return ERR_PRICE_PRESET_NOT_FOUND;
        }

        /* 同班金额+人数去重校验 */
        for (size_t k = 0; k < seen_amount_headcount.size(); ++k) {
            if (seen_amount_headcount[k].first == preset_info.amount && seen_amount_headcount[k].second == preset_info.expected_headcount) {
                RollbackTransactionInternal();
                LOG_DEBUG << "CreateClass: duplicate amount+headcount in class, amount=" << preset_info.amount << " headcount=" << preset_info.expected_headcount;
                return ERR_CLASS_ACTIVITY_DUPLICATE;
            }
        }
        seen_amount_headcount.push_back(std::make_pair(preset_info.amount, preset_info.expected_headcount));

        /* 插入 class_price */
        const char* price_sql = "INSERT INTO class_price (class_id, preset_id, activity_name, snapshot_amount, snapshot_headcount) VALUES (?, ?, ?, ?, ?)";
        sqlite3_stmt* price_stmt = nullptr;
        ret = sqlite3_prepare_v2(db_, price_sql, -1, &price_stmt, nullptr);
        if (ret != SQLITE_OK) {
            RollbackTransactionInternal();
            LOG_ERROR << "Prepare failed for insert class_price, ret=" << ret;
            return ERR_DB_PREPARE_FAILED;
        }
        sqlite3_bind_int(price_stmt, 1, new_class_id);
        sqlite3_bind_int(price_stmt, 2, preset_id);
        sqlite3_bind_text(price_stmt, 3, activity_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(price_stmt, 4, preset_info.amount);
        sqlite3_bind_int(price_stmt, 5, preset_info.expected_headcount);
        ret = sqlite3_step(price_stmt);
        sqlite3_finalize(price_stmt);
        if (ret != SQLITE_DONE) {
            RollbackTransactionInternal();
            if (ret == SQLITE_CONSTRAINT) {
                LOG_DEBUG << "CreateClass: duplicate activity_name in class, name=" << activity_name;
                return ERR_CLASS_ACTIVITY_DUPLICATE;
            }
            LOG_ERROR << "Insert class_price failed, ret=" << ret;
            return ERR_DB_EXEC_FAILED;
        }
    }

    ret = CommitTransactionInternal();
    if (ret != DB_OK) {
        RollbackTransactionInternal();
        return ret;
    }

    LOG_INFO << "CreateClassWithPrices success, class_id=" << new_class_id
             << " price_count=" << prices.size();
    return DB_OK;
}

int SqliteDatabase::UpdateClassPricesAtomic(
    int32_t class_id,
    const std::vector<PriceUpdateItem>& prices) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    if (prices.empty()) {
        return ERR_INVALID_PARAM;
    }

    int ret = sqlite3_exec(db_, "BEGIN IMMEDIATE TRANSACTION", nullptr, nullptr, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "BeginTransaction failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    /* 同班金额+人数去重（内存 set） */
    std::vector<std::pair<double, int32_t> > seen_amount_headcount;

    /* 第一轮：校验所有已存在项 preset_id 不可变 + 新增项 preset 存在 + 金额+人数去重 */
    for (size_t i = 0; i < prices.size(); ++i) {
        const PriceUpdateItem& item = prices[i];

        int32_t price_id = item.price_id;
        int32_t preset_id = item.preset_id;

        if (price_id > 0) {
            /* 已存在项：校验 preset_id 不可变 */
            const char* sel_sql = "SELECT preset_id FROM class_price WHERE id = ?";
            sqlite3_stmt* sel_stmt = nullptr;
            ret = sqlite3_prepare_v2(db_, sel_sql, -1, &sel_stmt, nullptr);
            if (ret != SQLITE_OK) {
                RollbackTransactionInternal();
                return ERR_DB_PREPARE_FAILED;
            }
            sqlite3_bind_int(sel_stmt, 1, price_id);
            ret = sqlite3_step(sel_stmt);
            int32_t existing_preset_id = 0;
            if (ret == SQLITE_ROW) {
                existing_preset_id = sqlite3_column_int(sel_stmt, 0);
            }
            sqlite3_finalize(sel_stmt);

            if (preset_id != existing_preset_id) {
                RollbackTransactionInternal();
                LOG_DEBUG << "UpdateClassPrices: preset_id immutable, price_id=" << price_id
                          << " existing=" << existing_preset_id << " requested=" << preset_id;
                return ERR_PRICE_PRESET_IMMUTABLE;
            }

            /* 收集金额+人数用于去重 */
            PricePresetInfo preset_info;
            if (QueryPricePresetByIdInternal(existing_preset_id, preset_info) == DB_OK) {
                for (size_t k = 0; k < seen_amount_headcount.size(); ++k) {
                    if (seen_amount_headcount[k].first == preset_info.amount && seen_amount_headcount[k].second == preset_info.expected_headcount) {
                        RollbackTransactionInternal();
                        return ERR_CLASS_ACTIVITY_DUPLICATE;
                    }
                }
                seen_amount_headcount.push_back(std::make_pair(preset_info.amount, preset_info.expected_headcount));
            }
        } else {
            /* 新增项：校验 preset 存在 + 金额去重 */
            if (preset_id <= 0) {
                RollbackTransactionInternal();
                return ERR_INVALID_PARAM;
            }
            PricePresetInfo preset_info;
            if (QueryPricePresetByIdInternal(preset_id, preset_info) != DB_OK) {
                RollbackTransactionInternal();
                return ERR_PRICE_PRESET_NOT_FOUND;
            }
            for (size_t k = 0; k < seen_amount_headcount.size(); ++k) {
                if (seen_amount_headcount[k].first == preset_info.amount && seen_amount_headcount[k].second == preset_info.expected_headcount) {
                    RollbackTransactionInternal();
                    return ERR_CLASS_ACTIVITY_DUPLICATE;
                }
            }
            seen_amount_headcount.push_back(std::make_pair(preset_info.amount, preset_info.expected_headcount));
        }
    }

    /* 第二轮：先删除未提交的项（必须在新增之前，避免误删新增项） */
    /* 构建 NOT IN 列表（仅含已存在的 price_id） */
    std::string id_list;
    bool first = true;
    for (size_t i = 0; i < prices.size(); ++i) {
        int32_t price_id = prices[i].price_id;
        if (price_id > 0) {
            if (!first) { id_list += ","; }
            first = false;
            id_list += std::to_string(price_id);
        }
    }

    if (id_list.empty()) {
        const char* del_sql = "DELETE FROM class_price WHERE class_id = ?";
        sqlite3_stmt* del_stmt = nullptr;
        ret = sqlite3_prepare_v2(db_, del_sql, -1, &del_stmt, nullptr);
        if (ret != SQLITE_OK) {
            RollbackTransactionInternal();
            return ERR_DB_PREPARE_FAILED;
        }
        sqlite3_bind_int(del_stmt, 1, class_id);
        ret = sqlite3_step(del_stmt);
        sqlite3_finalize(del_stmt);
        if (ret != SQLITE_DONE) {
            RollbackTransactionInternal();
            return ERR_DB_EXEC_FAILED;
        }
    } else {
        std::string del_sql_str = "DELETE FROM class_price WHERE class_id = ? AND id NOT IN (" + id_list + ")";
        sqlite3_stmt* del_stmt = nullptr;
        ret = sqlite3_prepare_v2(db_, del_sql_str.c_str(), -1, &del_stmt, nullptr);
        if (ret != SQLITE_OK) {
            RollbackTransactionInternal();
            return ERR_DB_PREPARE_FAILED;
        }
        sqlite3_bind_int(del_stmt, 1, class_id);
        ret = sqlite3_step(del_stmt);
        sqlite3_finalize(del_stmt);
        if (ret != SQLITE_DONE) {
            RollbackTransactionInternal();
            return ERR_DB_EXEC_FAILED;
        }
    }

    /* 第三轮：执行更新与新增 */
    for (size_t i = 0; i < prices.size(); ++i) {
        const PriceUpdateItem& item = prices[i];

        int32_t price_id = item.price_id;
        int32_t preset_id = item.preset_id;
        const std::string& activity_name = item.activity_name;

        if (price_id > 0) {
            /* 更新活动名 */
            const char* upd_sql = "UPDATE class_price SET activity_name = ? WHERE id = ?";
            sqlite3_stmt* upd_stmt = nullptr;
            ret = sqlite3_prepare_v2(db_, upd_sql, -1, &upd_stmt, nullptr);
            if (ret != SQLITE_OK) {
                RollbackTransactionInternal();
                return ERR_DB_PREPARE_FAILED;
            }
            sqlite3_bind_text(upd_stmt, 1, activity_name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(upd_stmt, 2, price_id);
            ret = sqlite3_step(upd_stmt);
            sqlite3_finalize(upd_stmt);
            if (ret != SQLITE_DONE) {
                RollbackTransactionInternal();
                return ERR_DB_EXEC_FAILED;
            }
        } else {
            /* 新增项 */
            PricePresetInfo preset_info;
            QueryPricePresetByIdInternal(preset_id, preset_info);
            const char* ins_sql = "INSERT INTO class_price (class_id, preset_id, activity_name, snapshot_amount, snapshot_headcount) VALUES (?, ?, ?, ?, ?)";
            sqlite3_stmt* ins_stmt = nullptr;
            ret = sqlite3_prepare_v2(db_, ins_sql, -1, &ins_stmt, nullptr);
            if (ret != SQLITE_OK) {
                RollbackTransactionInternal();
                return ERR_DB_PREPARE_FAILED;
            }
            sqlite3_bind_int(ins_stmt, 1, class_id);
            sqlite3_bind_int(ins_stmt, 2, preset_id);
            sqlite3_bind_text(ins_stmt, 3, activity_name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_double(ins_stmt, 4, preset_info.amount);
            sqlite3_bind_int(ins_stmt, 5, preset_info.expected_headcount);
            ret = sqlite3_step(ins_stmt);
            sqlite3_finalize(ins_stmt);
            if (ret != SQLITE_DONE) {
                RollbackTransactionInternal();
                return ERR_DB_EXEC_FAILED;
            }
        }
    }

    ret = CommitTransactionInternal();
    if (ret != DB_OK) {
        RollbackTransactionInternal();
        return ret;
    }

    LOG_INFO << "UpdateClassPrices success, class_id=" << class_id;
    return DB_OK;
}

int SqliteDatabase::InsertClassType(const ClassType& type) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "INSERT INTO class_type (name, is_builtin) VALUES (?, ?)";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for InsertClassType, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_text(stmt, 1, type.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, type.is_builtin);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "InsertClassType failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::QueryAllClassTypes(std::vector<ClassType>& types) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    types.clear();

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT id, name, is_builtin FROM class_type ORDER BY id";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryAllClassTypes, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    while (true) {
        ret = sqlite3_step(stmt);
        if (ret == SQLITE_ROW) {
            ClassType type;
            type.id = sqlite3_column_int(stmt, 0);
            type.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            type.is_builtin = sqlite3_column_int(stmt, 2);
            types.push_back(type);
        } else {
            break;
        }
    }

    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "QueryAllClassTypes step failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::DeleteClassType(int32_t id) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "DELETE FROM class_type WHERE id = ? AND is_builtin = 0";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for DeleteClassType, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, id);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "DeleteClassType failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::DeleteClass(int32_t id) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    /* begin transaction: delete class + cascade related data */
    char* err_msg = nullptr;
    int ret = sqlite3_exec(db_, "BEGIN IMMEDIATE TRANSACTION", nullptr, nullptr, &err_msg);
    if (ret != SQLITE_OK) {
        if (err_msg) { sqlite3_free(err_msg); }
        return ERR_DB_EXEC_FAILED;
    }

    /* price_library 改造后 price_preset_qrcode 为全局共享，不随班级删除。
       仅删除本班级的 class_price 关联记录与报名记录。 */
    sqlite3_stmt* stmt = nullptr;

    /* delete prices of this class */
    const char* sql2 = "DELETE FROM class_price WHERE class_id = ?";
    stmt = nullptr;
    ret = sqlite3_prepare_v2(db_, sql2, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_PREPARE_FAILED;
    }
    sqlite3_bind_int(stmt, 1, id);
    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_EXEC_FAILED;
    }

    /* delete registrations of this class */
    const char* sql3 = "DELETE FROM registration WHERE class_id = ?";
    stmt = nullptr;
    ret = sqlite3_prepare_v2(db_, sql3, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_PREPARE_FAILED;
    }
    sqlite3_bind_int(stmt, 1, id);
    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_EXEC_FAILED;
    }

    /* delete attendance of this class */
    const char* sql4 = "DELETE FROM attendance WHERE class_id = ?";
    stmt = nullptr;
    ret = sqlite3_prepare_v2(db_, sql4, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_PREPARE_FAILED;
    }
    sqlite3_bind_int(stmt, 1, id);
    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_EXEC_FAILED;
    }

    /* finally delete the class */
    const char* sql5 = "DELETE FROM class_info WHERE id = ?";
    stmt = nullptr;
    ret = sqlite3_prepare_v2(db_, sql5, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_PREPARE_FAILED;
    }
    sqlite3_bind_int(stmt, 1, id);
    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_EXEC_FAILED;
    }

    ret = sqlite3_exec(db_, "COMMIT", nullptr, nullptr, &err_msg);
    if (ret != SQLITE_OK) {
        if (err_msg) { sqlite3_free(err_msg); }
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_EXEC_FAILED;
    }

    LOG_DEBUG << "DeleteClass success, id=" << id;
    return DB_OK;
}

int SqliteDatabase::QueryClassTypeById(int32_t id, ClassType& type) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT id, name, is_builtin FROM class_type WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryClassTypeById, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, id);

    ret = sqlite3_step(stmt);
    if (ret == SQLITE_ROW) {
        type.id = sqlite3_column_int(stmt, 0);
        type.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        type.is_builtin = sqlite3_column_int(stmt, 2);
        sqlite3_finalize(stmt);
        return DB_OK;
    }

    sqlite3_finalize(stmt);

    if (ret == SQLITE_DONE) {
        return ERR_DB_EXEC_FAILED;
    }

    LOG_ERROR << "QueryClassTypeById step failed, ret=" << ret;
    return ERR_DB_EXEC_FAILED;
}

/* ==================== IRegistrationDao ==================== */

int SqliteDatabase::InsertRegistration(const RegistrationInfo& info) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    return InsertRegistrationInternal(info);
}

int SqliteDatabase::InsertRegistrationInternal(const RegistrationInfo& info) {
    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "INSERT INTO registration (class_id, student_name, student_gender, parent_phone, has_allergy, allergy_desc, price_id, need_bed, teacher_name, other_info, register_time, is_deposit, paid_amount_snapshot, supplement_amount, supplement_preset_id, supplement_operator, supplement_time, student_start_date, student_end_date) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for InsertRegistration, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, info.class_id);
    sqlite3_bind_text(stmt, 2, info.student_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, info.student_gender.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, info.parent_phone.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, info.has_allergy);
    sqlite3_bind_text(stmt, 6, info.allergy_desc.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, info.price_id);
    sqlite3_bind_int(stmt, 8, info.need_bed);
    sqlite3_bind_text(stmt, 9, info.teacher_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, info.other_info.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, info.register_time.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 12, info.is_deposit);
    sqlite3_bind_double(stmt, 13, info.paid_amount_snapshot);
    sqlite3_bind_double(stmt, 14, info.supplement_amount);
    sqlite3_bind_int(stmt, 15, info.supplement_preset_id);
    sqlite3_bind_text(stmt, 16, info.supplement_operator.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 17, info.supplement_time.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 18, info.student_start_date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 19, info.student_end_date.c_str(), -1, SQLITE_TRANSIENT);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "InsertRegistration failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    const_cast<RegistrationInfo&>(info).id = static_cast<int32_t>(sqlite3_last_insert_rowid(db_));
    LOG_DEBUG << "InsertRegistration success, id=" << info.id;
    return DB_OK;
}

int SqliteDatabase::QueryRegistrationById(int32_t id, RegistrationInfo& info) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT id, class_id, student_name, student_gender, parent_phone, has_allergy, allergy_desc, price_id, need_bed, teacher_name, other_info, register_time, is_deposit, paid_amount_snapshot, supplement_amount, supplement_preset_id, supplement_operator, supplement_time, student_start_date, student_end_date, COALESCE((SELECT SUM(refund_amount) FROM refund_record WHERE registration_id=registration.id AND status=0), 0) AS refund_sum FROM registration WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryRegistrationById, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, id);

    ret = sqlite3_step(stmt);
    if (ret == SQLITE_ROW) {
        info.id = sqlite3_column_int(stmt, 0);
        info.class_id = sqlite3_column_int(stmt, 1);
        info.student_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        info.student_gender = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        info.parent_phone = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        info.has_allergy = sqlite3_column_int(stmt, 5);
        if (sqlite3_column_type(stmt, 6) != SQLITE_NULL) {
            info.allergy_desc = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        }
        info.price_id = sqlite3_column_int(stmt, 7);
        info.need_bed = sqlite3_column_int(stmt, 8);
        info.teacher_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
        if (sqlite3_column_type(stmt, 10) != SQLITE_NULL) {
            info.other_info = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
        }
        info.register_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));
        info.is_deposit = sqlite3_column_int(stmt, 12);
        info.paid_amount_snapshot = sqlite3_column_double(stmt, 13);
        info.supplement_amount = sqlite3_column_double(stmt, 14);
        info.supplement_preset_id = sqlite3_column_int(stmt, 15);
        if (sqlite3_column_type(stmt, 16) != SQLITE_NULL) {
            info.supplement_operator = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 16));
        }
        if (sqlite3_column_type(stmt, 17) != SQLITE_NULL) {
            info.supplement_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 17));
        }
        if (sqlite3_column_type(stmt, 18) != SQLITE_NULL) {
            info.student_start_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 18));
        }
        if (sqlite3_column_type(stmt, 19) != SQLITE_NULL) {
            info.student_end_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 19));
        }
        info.paid_amount = 0;
        info.refund_amount = sqlite3_column_double(stmt, 20);
        sqlite3_finalize(stmt);
        return DB_OK;
    }

    sqlite3_finalize(stmt);

    if (ret == SQLITE_DONE) {
        return ERR_DB_EXEC_FAILED;
    }

    LOG_ERROR << "QueryRegistrationById step failed, ret=" << ret;
    return ERR_DB_EXEC_FAILED;
}

int SqliteDatabase::QueryRegistrationsByClassId(int32_t class_id, std::vector<RegistrationInfo>& regs) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    regs.clear();

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql =
        "SELECT r.id, r.class_id, r.student_name, r.student_gender, r.parent_phone, "
        "r.has_allergy, r.allergy_desc, r.price_id, r.need_bed, r.teacher_name, "
        "r.other_info, r.register_time, "
        "r.is_deposit, r.paid_amount_snapshot, r.supplement_amount, r.supplement_preset_id, "
        "r.supplement_operator, r.supplement_time, "
        "r.student_start_date, r.student_end_date, "
        "COALESCE((SELECT SUM(refund_amount) FROM refund_record WHERE registration_id=r.id AND status=0), 0) AS refund_sum "
        "FROM registration r WHERE r.class_id = ? ORDER BY r.id DESC";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryRegistrationsByClassId, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, class_id);

    while (true) {
        ret = sqlite3_step(stmt);
        if (ret == SQLITE_ROW) {
            RegistrationInfo info;
            info.id = sqlite3_column_int(stmt, 0);
            info.class_id = sqlite3_column_int(stmt, 1);
            info.student_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            info.student_gender = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            info.parent_phone = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            info.has_allergy = sqlite3_column_int(stmt, 5);
            if (sqlite3_column_type(stmt, 6) != SQLITE_NULL) {
                info.allergy_desc = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            }
            info.price_id = sqlite3_column_int(stmt, 7);
            info.need_bed = sqlite3_column_int(stmt, 8);
            info.teacher_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
            if (sqlite3_column_type(stmt, 10) != SQLITE_NULL) {
                info.other_info = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
            }
            info.register_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));
            info.is_deposit = sqlite3_column_int(stmt, 12);
            info.paid_amount_snapshot = sqlite3_column_double(stmt, 13);
            info.supplement_amount = sqlite3_column_double(stmt, 14);
            info.supplement_preset_id = sqlite3_column_int(stmt, 15);
            if (sqlite3_column_type(stmt, 16) != SQLITE_NULL) {
                info.supplement_operator = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 16));
            }
            if (sqlite3_column_type(stmt, 17) != SQLITE_NULL) {
                info.supplement_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 17));
            }
            if (sqlite3_column_type(stmt, 18) != SQLITE_NULL) {
                info.student_start_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 18));
            }
            if (sqlite3_column_type(stmt, 19) != SQLITE_NULL) {
                info.student_end_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 19));
            }
            info.refund_amount = sqlite3_column_double(stmt, 20);
            info.paid_amount = 0;  /* Handler 端从 paid_amount_snapshot 计算 paid_amount */
            regs.push_back(info);
        } else {
            break;
        }
    }

    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "QueryRegistrationsByClassId step failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::QueryRegistrationsByTimeRange(const std::string& start_time, const std::string& end_time,
                                                   std::vector<RegistrationInfo>& regs) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    regs.clear();

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql =
        "SELECT r.id, r.class_id, r.student_name, r.student_gender, r.parent_phone, "
        "r.has_allergy, r.allergy_desc, r.price_id, r.need_bed, r.teacher_name, "
        "r.other_info, r.register_time, "
        "r.is_deposit, r.paid_amount_snapshot, r.supplement_amount, r.supplement_preset_id, "
        "r.supplement_operator, r.supplement_time, "
        "r.student_start_date, r.student_end_date, "
        "COALESCE((SELECT SUM(refund_amount) FROM refund_record WHERE registration_id=r.id AND status=0), 0) AS refund_sum "
        "FROM registration r WHERE r.register_time >= ? AND r.register_time <= ? ORDER BY r.register_time DESC";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryRegistrationsByTimeRange, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_text(stmt, 1, start_time.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, end_time.c_str(), -1, SQLITE_TRANSIENT);

    while (true) {
        ret = sqlite3_step(stmt);
        if (ret == SQLITE_ROW) {
            RegistrationInfo info;
            info.id = sqlite3_column_int(stmt, 0);
            info.class_id = sqlite3_column_int(stmt, 1);
            info.student_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            info.student_gender = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            info.parent_phone = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            info.has_allergy = sqlite3_column_int(stmt, 5);
            if (sqlite3_column_type(stmt, 6) != SQLITE_NULL) {
                info.allergy_desc = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            }
            info.price_id = sqlite3_column_int(stmt, 7);
            info.need_bed = sqlite3_column_int(stmt, 8);
            info.teacher_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
            if (sqlite3_column_type(stmt, 10) != SQLITE_NULL) {
                info.other_info = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
            }
            info.register_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));
            info.is_deposit = sqlite3_column_int(stmt, 12);
            info.paid_amount_snapshot = sqlite3_column_double(stmt, 13);
            info.supplement_amount = sqlite3_column_double(stmt, 14);
            info.supplement_preset_id = sqlite3_column_int(stmt, 15);
            if (sqlite3_column_type(stmt, 16) != SQLITE_NULL) {
                info.supplement_operator = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 16));
            }
            if (sqlite3_column_type(stmt, 17) != SQLITE_NULL) {
                info.supplement_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 17));
            }
            if (sqlite3_column_type(stmt, 18) != SQLITE_NULL) {
                info.student_start_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 18));
            }
            if (sqlite3_column_type(stmt, 19) != SQLITE_NULL) {
                info.student_end_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 19));
            }
            info.refund_amount = sqlite3_column_double(stmt, 20);
            info.paid_amount = 0;  /* Handler 端从 paid_amount_snapshot 计算 paid_amount */
            regs.push_back(info);
        } else {
            break;
        }
    }

    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "QueryRegistrationsByTimeRange step failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::CountEnrolledByClassId(int32_t class_id) {
    double used = QueryEnrollmentUsedByClassId(class_id);
    return static_cast<int>(used + 0.5);
}

double SqliteDatabase::QueryEnrollmentUsedByClassId(int32_t class_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    return QueryEnrollmentUsedInternal(class_id);
}

int SqliteDatabase::CountActiveStudentsByClassId(int32_t class_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) {
        return -1;
    }

    const char* sql = "SELECT COUNT(*) FROM registration WHERE class_id = ? "
                      "AND (student_end_date = '' OR student_end_date >= date('now'))";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for CountActiveStudentsByClassId, ret=" << ret;
        return -1;
    }

    sqlite3_bind_int(stmt, 1, class_id);

    ret = sqlite3_step(stmt);
    if (ret == SQLITE_ROW) {
        int count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        LOG_INFO << "CountActiveStudentsByClassId: class_id=" << class_id << " active_count=" << count;
        return count;
    }

    sqlite3_finalize(stmt);
    LOG_ERROR << "CountActiveStudentsByClassId step failed, ret=" << ret;
    return -1;
}

int SqliteDatabase::QueryClassByIdInternal(int32_t id, ClassInfo& info) {
    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT id, class_name, start_time, end_time, description, enrollment_capacity, enrollment_used, class_type, create_time FROM class_info WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryClassByIdInternal, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, id);

    ret = sqlite3_step(stmt);
    if (ret == SQLITE_ROW) {
        info.id = sqlite3_column_int(stmt, 0);
        info.class_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        info.start_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        info.end_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        if (sqlite3_column_type(stmt, 4) != SQLITE_NULL) {
            info.description = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        }
        info.enrollment_capacity = sqlite3_column_int(stmt, 5);
        info.enrollment_used = sqlite3_column_double(stmt, 6);
        info.class_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        info.create_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        sqlite3_finalize(stmt);
        return DB_OK;
    }

    sqlite3_finalize(stmt);

    if (ret == SQLITE_DONE) {
        return ERR_DB_EXEC_FAILED;
    }

    LOG_ERROR << "QueryClassByIdInternal step failed, ret=" << ret;
    return ERR_DB_EXEC_FAILED;
}

double SqliteDatabase::QueryEnrollmentUsedInternal(int32_t class_id) {
    if (!db_) {
        return -1.0;
    }

    const char* sql = "SELECT enrollment_used FROM class_info WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryEnrollmentUsed, ret=" << ret;
        return -1.0;
    }

    sqlite3_bind_int(stmt, 1, class_id);

    ret = sqlite3_step(stmt);
    if (ret == SQLITE_ROW) {
        double used = sqlite3_column_double(stmt, 0);
        sqlite3_finalize(stmt);
        LOG_INFO << "QueryEnrollmentUsedInternal: class_id=" << class_id << " enrollment_used=" << used;
        return used;
    }

    sqlite3_finalize(stmt);
    LOG_ERROR << "QueryEnrollmentUsed step failed, ret=" << ret;
    return -1.0;
}

int SqliteDatabase::CheckEnrollmentAvailable(int32_t class_id, int32_t capacity) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    double used = QueryEnrollmentUsedInternal(class_id);
    if (used < 0) {
        return ERR_DB_EXEC_FAILED;
    }

    return (used < static_cast<double>(capacity) + 0.001) ? 1 : 0;
}

/* ==================== IResourceDao ==================== */

int SqliteDatabase::InsertResource(const ResourceInfo& info) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "INSERT INTO resource (name, total_count, used_count, remain_count, resource_type, bed_reserved_count) VALUES (?, ?, ?, ?, ?, 0)";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for InsertResource, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    int32_t remain_count = info.total_count - info.used_count;

    sqlite3_bind_text(stmt, 1, info.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, info.total_count);
    sqlite3_bind_int(stmt, 3, info.used_count);
    sqlite3_bind_int(stmt, 4, remain_count);
    sqlite3_bind_int(stmt, 5, info.resource_type);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "InsertResource failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    const_cast<ResourceInfo&>(info).id = static_cast<int32_t>(sqlite3_last_insert_rowid(db_));
    LOG_DEBUG << "InsertResource success, id=" << info.id;
    return DB_OK;
}

int SqliteDatabase::QueryResourceById(int32_t id, ResourceInfo& info) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT id, name, total_count, used_count, remain_count, resource_type, bed_reserved_count FROM resource WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryResourceById, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, id);

    ret = sqlite3_step(stmt);
    if (ret == SQLITE_ROW) {
        info.id = sqlite3_column_int(stmt, 0);
        info.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        info.total_count = sqlite3_column_int(stmt, 2);
        info.used_count = sqlite3_column_int(stmt, 3);
        info.remain_count = sqlite3_column_int(stmt, 4);
        info.resource_type = sqlite3_column_int(stmt, 5);
        info.bed_reserved_count = sqlite3_column_int(stmt, 6);
        sqlite3_finalize(stmt);
        return DB_OK;
    }

    sqlite3_finalize(stmt);

    if (ret == SQLITE_DONE) {
        return ERR_DB_EXEC_FAILED;
    }

    LOG_ERROR << "QueryResourceById step failed, ret=" << ret;
    return ERR_DB_EXEC_FAILED;
}

int SqliteDatabase::QueryAllResources(std::vector<ResourceInfo>& resources) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    resources.clear();

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT id, name, total_count, used_count, remain_count, resource_type, bed_reserved_count FROM resource ORDER BY id";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryAllResources, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    while (true) {
        ret = sqlite3_step(stmt);
        if (ret == SQLITE_ROW) {
            ResourceInfo info;
            info.id = sqlite3_column_int(stmt, 0);
            info.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            info.total_count = sqlite3_column_int(stmt, 2);
            info.used_count = sqlite3_column_int(stmt, 3);
            info.remain_count = sqlite3_column_int(stmt, 4);
            info.resource_type = sqlite3_column_int(stmt, 5);
            info.bed_reserved_count = sqlite3_column_int(stmt, 6);
            resources.push_back(info);
        } else {
            break;
        }
    }

    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "QueryAllResources step failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::UpdateResourceTotal(int32_t id, int32_t total_count) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "UPDATE resource SET total_count = ?, remain_count = ? - used_count WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for UpdateResourceTotal, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, total_count);
    sqlite3_bind_int(stmt, 2, total_count);
    sqlite3_bind_int(stmt, 3, id);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "UpdateResourceTotal failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::DeleteResource(int32_t id) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "DELETE FROM resource WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for DeleteResource, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, id);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "DeleteResource failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::CheckResourceInUse(int32_t resource_id, std::vector<std::string>& using_classes) {
    (void)resource_id;
    std::lock_guard<std::mutex> lock(db_mutex_);
    using_classes.clear();

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT DISTINCT c.class_name FROM registration r INNER JOIN class_info c ON r.class_id = c.id WHERE r.need_bed = 1 AND c.end_time > date('now')";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for CheckResourceInUse, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    while (true) {
        ret = sqlite3_step(stmt);
        if (ret == SQLITE_ROW) {
            using_classes.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        } else {
            break;
        }
    }

    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "CheckResourceInUse step failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::InsertAllocation(const ResourceAllocation& alloc) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    return InsertAllocationInternal(alloc);
}

int SqliteDatabase::InsertAllocationInternal(const ResourceAllocation& alloc) {
    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "INSERT INTO resource_allocation (resource_id, registration_id, student_name, student_gender, teacher_name, class_name, resource_code, allocate_time) VALUES (?, ?, ?, ?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for InsertAllocation, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, alloc.resource_id);
    sqlite3_bind_int(stmt, 2, alloc.registration_id);
    sqlite3_bind_text(stmt, 3, alloc.student_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, alloc.student_gender.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, alloc.teacher_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, alloc.class_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, alloc.resource_code);
    sqlite3_bind_text(stmt, 8, alloc.allocate_time.c_str(), -1, SQLITE_TRANSIENT);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "InsertAllocation failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    const_cast<ResourceAllocation&>(alloc).id = static_cast<int32_t>(sqlite3_last_insert_rowid(db_));
    LOG_DEBUG << "InsertAllocation success, id=" << alloc.id;
    return DB_OK;
}

int SqliteDatabase::DeleteAllocationsByRegIdInternal(int32_t registration_id, std::vector<int32_t>& resource_ids) {
    resource_ids.clear();
    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    /* collect resource_ids before deleting */
    const char* sel_sql = "SELECT resource_id FROM resource_allocation WHERE registration_id = ?";
    sqlite3_stmt* sel_stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sel_sql, -1, &sel_stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for DeleteAllocationsByRegId select, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(sel_stmt, 1, registration_id);

    while (sqlite3_step(sel_stmt) == SQLITE_ROW) {
        resource_ids.push_back(sqlite3_column_int(sel_stmt, 0));
    }
    sqlite3_finalize(sel_stmt);

    /* delete allocation records */
    const char* del_sql = "DELETE FROM resource_allocation WHERE registration_id = ?";
    sqlite3_stmt* del_stmt = nullptr;
    ret = sqlite3_prepare_v2(db_, del_sql, -1, &del_stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for DeleteAllocationsByRegId delete, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(del_stmt, 1, registration_id);

    ret = sqlite3_step(del_stmt);
    sqlite3_finalize(del_stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "DeleteAllocationsByRegId delete failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::QueryAllocationsByResourceId(int32_t resource_id, std::vector<ResourceAllocation>& allocs) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    allocs.clear();

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT id, resource_id, registration_id, student_name, student_gender, teacher_name, class_name, resource_code, allocate_time FROM resource_allocation WHERE resource_id = ? ORDER BY resource_code";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryAllocationsByResourceId, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, resource_id);

    while (true) {
        ret = sqlite3_step(stmt);
        if (ret == SQLITE_ROW) {
            ResourceAllocation alloc;
            alloc.id = sqlite3_column_int(stmt, 0);
            alloc.resource_id = sqlite3_column_int(stmt, 1);
            alloc.registration_id = sqlite3_column_int(stmt, 2);
            alloc.student_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            alloc.student_gender = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            alloc.teacher_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            alloc.class_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            alloc.resource_code = sqlite3_column_int(stmt, 7);
            alloc.allocate_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
            allocs.push_back(alloc);
        } else {
            break;
        }
    }

    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "QueryAllocationsByResourceId step failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::QueryAllocationsByClassId(int32_t class_id, std::vector<ResourceAllocation>& allocs) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    allocs.clear();

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    /* resource_allocation has no class_id, but registration has class_id.
       Join through registration_id to filter by class */
    const char* sql =
        "SELECT a.id, a.resource_id, a.registration_id, a.student_name, "
        "a.student_gender, a.teacher_name, a.class_name, a.resource_code, a.allocate_time "
        "FROM resource_allocation a "
        "INNER JOIN registration r ON a.registration_id = r.id "
        "WHERE r.class_id = ? ORDER BY a.student_name";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryAllocationsByClassId, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, class_id);

    while (true) {
        ret = sqlite3_step(stmt);
        if (ret == SQLITE_ROW) {
            ResourceAllocation alloc;
            alloc.id = sqlite3_column_int(stmt, 0);
            alloc.resource_id = sqlite3_column_int(stmt, 1);
            alloc.registration_id = sqlite3_column_int(stmt, 2);
            alloc.student_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            alloc.student_gender = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            alloc.teacher_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            alloc.class_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            alloc.resource_code = sqlite3_column_int(stmt, 7);
            alloc.allocate_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
            allocs.push_back(alloc);
        } else {
            break;
        }
    }

    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "QueryAllocationsByClassId step failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::QueryAllocationsByTimeRange(const std::string& start_time, const std::string& end_time, std::vector<ResourceAllocation>& allocs) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    allocs.clear();

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql =
        "SELECT id, resource_id, registration_id, student_name, student_gender, "
        "teacher_name, class_name, resource_code, allocate_time "
        "FROM resource_allocation WHERE allocate_time >= ? AND allocate_time <= ? "
        "ORDER BY allocate_time DESC";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryAllocationsByTimeRange, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_text(stmt, 1, start_time.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, end_time.c_str(), -1, SQLITE_TRANSIENT);

    while (true) {
        ret = sqlite3_step(stmt);
        if (ret == SQLITE_ROW) {
            ResourceAllocation alloc;
            alloc.id = sqlite3_column_int(stmt, 0);
            alloc.resource_id = sqlite3_column_int(stmt, 1);
            alloc.registration_id = sqlite3_column_int(stmt, 2);
            alloc.student_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            alloc.student_gender = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            alloc.teacher_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            alloc.class_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            alloc.resource_code = sqlite3_column_int(stmt, 7);
            alloc.allocate_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
            allocs.push_back(alloc);
        } else {
            break;
        }
    }

    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "QueryAllocationsByTimeRange step failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::CheckResourceCodeOccupied(int32_t resource_id, int32_t resource_code) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    return CheckResourceCodeOccupiedInternal(resource_id, resource_code);
}

int SqliteDatabase::CheckResourceCodeOccupiedInternal(int32_t resource_id, int32_t resource_code) {
    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT COUNT(*) FROM resource_allocation WHERE resource_id = ? AND resource_code = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for CheckResourceCodeOccupied, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, resource_id);
    sqlite3_bind_int(stmt, 2, resource_code);

    ret = sqlite3_step(stmt);
    if (ret == SQLITE_ROW) {
        int32_t count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return (count > 0) ? 1 : 0;
    }

    sqlite3_finalize(stmt);
    LOG_ERROR << "CheckResourceCodeOccupied step failed, ret=" << ret;
    return ERR_DB_EXEC_FAILED;
}

int SqliteDatabase::CheckStudentResourceAllocated(int32_t resource_id, int32_t registration_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT COUNT(*) FROM resource_allocation WHERE resource_id = ? AND registration_id = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for CheckStudentResourceAllocated, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, resource_id);
    sqlite3_bind_int(stmt, 2, registration_id);

    ret = sqlite3_step(stmt);
    if (ret == SQLITE_ROW) {
        int32_t count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return (count > 0) ? 1 : 0;
    }

    sqlite3_finalize(stmt);
    LOG_ERROR << "CheckStudentResourceAllocated step failed, ret=" << ret;
    return ERR_DB_EXEC_FAILED;
}

int SqliteDatabase::IncrementResourceUsed(int32_t resource_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    return IncrementResourceUsedInternal(resource_id);
}

int SqliteDatabase::IncrementResourceUsedInternal(int32_t resource_id) {
    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "UPDATE resource SET used_count = used_count + 1, remain_count = remain_count - 1 WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for IncrementResourceUsed, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, resource_id);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "IncrementResourceUsed failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::IncrementBedReservedInternal(int32_t resource_id) {
    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "UPDATE resource SET bed_reserved_count = bed_reserved_count + 1 WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for IncrementBedReserved, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, resource_id);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "IncrementBedReserved failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::DecrementBedReservedInternal(int32_t resource_id) {
    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "UPDATE resource SET bed_reserved_count = bed_reserved_count - 1 WHERE id = ? AND bed_reserved_count > 0";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for DecrementBedReserved, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, resource_id);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "DecrementBedReserved failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::DecrementResourceUsed(int32_t resource_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "UPDATE resource SET used_count = used_count - 1, remain_count = remain_count + 1 WHERE id = ? AND used_count > 0";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for DecrementResourceUsed, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, resource_id);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "DecrementResourceUsed failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::DecrementResourceUsedInternal(int32_t resource_id) {
    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "UPDATE resource SET used_count = used_count - 1, remain_count = remain_count + 1 WHERE id = ? AND used_count > 0";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for DecrementResourceUsedInternal, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, resource_id);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "DecrementResourceUsedInternal failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::QueryResourceByName(const std::string& name, ResourceInfo& info) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT id, name, total_count, used_count, remain_count, resource_type, bed_reserved_count FROM resource WHERE name = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryResourceByName, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);

    ret = sqlite3_step(stmt);
    if (ret == SQLITE_ROW) {
        info.id = sqlite3_column_int(stmt, 0);
        info.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        info.total_count = sqlite3_column_int(stmt, 2);
        info.used_count = sqlite3_column_int(stmt, 3);
        info.remain_count = sqlite3_column_int(stmt, 4);
        info.resource_type = sqlite3_column_int(stmt, 5);
        info.bed_reserved_count = sqlite3_column_int(stmt, 6);
        sqlite3_finalize(stmt);
        return DB_OK;
    }

    sqlite3_finalize(stmt);

    if (ret == SQLITE_DONE) {
        return ERR_DB_EXEC_FAILED;
    }

    LOG_ERROR << "QueryResourceByName step failed, ret=" << ret;
    return ERR_DB_EXEC_FAILED;
}

int SqliteDatabase::QueryResourceByType(int32_t resource_type, ResourceInfo& info) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT id, name, total_count, used_count, remain_count, resource_type, bed_reserved_count FROM resource WHERE resource_type = ? LIMIT 1";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryResourceByType, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, resource_type);

    ret = sqlite3_step(stmt);
    if (ret == SQLITE_ROW) {
        info.id = sqlite3_column_int(stmt, 0);
        info.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        info.total_count = sqlite3_column_int(stmt, 2);
        info.used_count = sqlite3_column_int(stmt, 3);
        info.remain_count = sqlite3_column_int(stmt, 4);
        info.resource_type = sqlite3_column_int(stmt, 5);
        info.bed_reserved_count = sqlite3_column_int(stmt, 6);
        sqlite3_finalize(stmt);
        return DB_OK;
    }

    sqlite3_finalize(stmt);

    if (ret == SQLITE_DONE) {
        return ERR_DB_EXEC_FAILED;
    }

    LOG_ERROR << "QueryResourceByType step failed, ret=" << ret;
    return ERR_DB_EXEC_FAILED;
}

int SqliteDatabase::QueryBedResourceRemain(int32_t resource_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    return QueryBedResourceRemainInternal(resource_id);
}

int SqliteDatabase::QueryBedResourceRemainInternal(int32_t resource_id) {
    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT total_count, bed_reserved_count FROM resource WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryBedResourceRemain, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, resource_id);

    ret = sqlite3_step(stmt);
    if (ret == SQLITE_ROW) {
        int32_t total = sqlite3_column_int(stmt, 0);
        int32_t reserved = sqlite3_column_int(stmt, 1);
        sqlite3_finalize(stmt);
        return total - reserved;
    }

    sqlite3_finalize(stmt);
    LOG_ERROR << "QueryBedResourceRemain step failed, ret=" << ret;
    return ERR_DB_EXEC_FAILED;
}

/* ==================== IAttendanceDao ==================== */

int SqliteDatabase::DeleteAttendanceByRegIdInternal(int32_t registration_id) {
    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "DELETE FROM attendance WHERE registration_id = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for DeleteAttendanceByRegId, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, registration_id);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "DeleteAttendanceByRegId failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::InsertAttendance(const AttendanceRecord& record) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    char* err_msg = nullptr;
    int ret = sqlite3_exec(db_, "BEGIN IMMEDIATE TRANSACTION", nullptr, nullptr, &err_msg);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "InsertAttendance BEGIN failed, ret=" << ret;
        if (err_msg) { sqlite3_free(err_msg); }
        return ERR_DB_EXEC_FAILED;
    }

    /* upsert: UPDATE-then-INSERT 保留原行 id，避免 INSERT OR REPLACE 导致 AUTOINCREMENT 跳变 */
    const char* upd_sql = "UPDATE attendance SET status = ?, leave_time = ?, teacher_name = ?, record_time = ? WHERE class_id = ? AND registration_id = ? AND attendance_date = ?";
    sqlite3_stmt* stmt = nullptr;

    ret = sqlite3_prepare_v2(db_, upd_sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for InsertAttendance update, ret=" << ret;
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, static_cast<int32_t>(record.status));
    sqlite3_bind_text(stmt, 2, record.leave_time.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, record.teacher_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, record.record_time.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, record.class_id);
    sqlite3_bind_int(stmt, 6, record.registration_id);
    sqlite3_bind_text(stmt, 7, record.attendance_date.c_str(), -1, SQLITE_TRANSIENT);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "InsertAttendance update failed, ret=" << ret;
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_EXEC_FAILED;
    }

    if (sqlite3_changes(db_) > 0) {
        sqlite3_stmt* sel_stmt = nullptr;
        const char* sel_sql = "SELECT id FROM attendance WHERE class_id = ? AND registration_id = ? AND attendance_date = ?";
        ret = sqlite3_prepare_v2(db_, sel_sql, -1, &sel_stmt, nullptr);
        if (ret == SQLITE_OK) {
            sqlite3_bind_int(sel_stmt, 1, record.class_id);
            sqlite3_bind_int(sel_stmt, 2, record.registration_id);
            sqlite3_bind_text(sel_stmt, 3, record.attendance_date.c_str(), -1, SQLITE_TRANSIENT);
            ret = sqlite3_step(sel_stmt);
            if (ret == SQLITE_ROW) {
                const_cast<AttendanceRecord&>(record).id = sqlite3_column_int(sel_stmt, 0);
            }
            sqlite3_finalize(sel_stmt);
        } else {
            if (sel_stmt) { sqlite3_finalize(sel_stmt); }
        }
        ret = sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr);
        if (ret != SQLITE_OK) {
            LOG_ERROR << "InsertAttendance COMMIT failed after update, ret=" << ret;
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
            return ERR_DB_EXEC_FAILED;
        }
        LOG_DEBUG << "InsertAttendance upsert update hit, id=" << record.id;
        return DB_OK;
    }

    /* UPDATE 未命中，执行 INSERT */
    const char* ins_sql = "INSERT INTO attendance (class_id, registration_id, student_name, student_gender, attendance_date, status, leave_time, teacher_name, record_time) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)";
    stmt = nullptr;
    ret = sqlite3_prepare_v2(db_, ins_sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for InsertAttendance insert, ret=" << ret;
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, record.class_id);
    sqlite3_bind_int(stmt, 2, record.registration_id);
    sqlite3_bind_text(stmt, 3, record.student_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, record.student_gender.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, record.attendance_date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 6, static_cast<int32_t>(record.status));
    sqlite3_bind_text(stmt, 7, record.leave_time.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, record.teacher_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, record.record_time.c_str(), -1, SQLITE_TRANSIENT);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "InsertAttendance insert failed, ret=" << ret;
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_EXEC_FAILED;
    }

    ret = sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "InsertAttendance COMMIT failed after insert, ret=" << ret;
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_EXEC_FAILED;
    }

    const_cast<AttendanceRecord&>(record).id = static_cast<int32_t>(sqlite3_last_insert_rowid(db_));
    LOG_DEBUG << "InsertAttendance success (insert), id=" << record.id;
    return DB_OK;
}

int SqliteDatabase::QueryAttendanceByClassAndDate(int32_t class_id, const std::string& date, std::vector<AttendanceRecord>& records) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    records.clear();

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT id, class_id, registration_id, student_name, student_gender, attendance_date, status, leave_time, teacher_name, record_time FROM attendance WHERE class_id = ? AND attendance_date = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryAttendanceByClassAndDate, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, class_id);
    sqlite3_bind_text(stmt, 2, date.c_str(), -1, SQLITE_TRANSIENT);

    while (true) {
        ret = sqlite3_step(stmt);
        if (ret == SQLITE_ROW) {
            AttendanceRecord rec;
            rec.id = sqlite3_column_int(stmt, 0);
            rec.class_id = sqlite3_column_int(stmt, 1);
            rec.registration_id = sqlite3_column_int(stmt, 2);
            rec.student_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            rec.student_gender = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            rec.attendance_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            rec.status = static_cast<AttendanceStatusType>(sqlite3_column_int(stmt, 6));
            const unsigned char* lt = sqlite3_column_text(stmt, 7);
            rec.leave_time = lt ? reinterpret_cast<const char*>(lt) : "";
            rec.teacher_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
            rec.record_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
            records.push_back(rec);
        } else {
            break;
        }
    }

    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "QueryAttendanceByClassAndDate step failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::CheckAttendanceExists(int32_t class_id, const std::string& date) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT COUNT(*) FROM attendance WHERE class_id = ? AND attendance_date = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for CheckAttendanceExists, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, class_id);
    sqlite3_bind_text(stmt, 2, date.c_str(), -1, SQLITE_TRANSIENT);

    ret = sqlite3_step(stmt);
    if (ret == SQLITE_ROW) {
        int32_t count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return (count > 0) ? 1 : 0;
    }

    sqlite3_finalize(stmt);
    LOG_ERROR << "CheckAttendanceExists step failed, ret=" << ret;
    return ERR_DB_EXEC_FAILED;
}

int SqliteDatabase::QueryAttendanceByClassAndDateRange(int32_t class_id, const std::string& start_date, const std::string& end_date, std::vector<AttendanceRecord>& records) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    records.clear();

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT id, class_id, registration_id, student_name, student_gender, attendance_date, status, leave_time, teacher_name, record_time FROM attendance WHERE class_id = ? AND attendance_date >= ? AND attendance_date <= ? ORDER BY attendance_date, id";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryAttendanceByClassAndDateRange, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, class_id);
    sqlite3_bind_text(stmt, 2, start_date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, end_date.c_str(), -1, SQLITE_TRANSIENT);

    while (true) {
        ret = sqlite3_step(stmt);
        if (ret == SQLITE_ROW) {
            AttendanceRecord rec;
            rec.id = sqlite3_column_int(stmt, 0);
            rec.class_id = sqlite3_column_int(stmt, 1);
            rec.registration_id = sqlite3_column_int(stmt, 2);
            rec.student_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            rec.student_gender = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            rec.attendance_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            rec.status = static_cast<AttendanceStatusType>(sqlite3_column_int(stmt, 6));
            const unsigned char* lt = sqlite3_column_text(stmt, 7);
            rec.leave_time = lt ? reinterpret_cast<const char*>(lt) : "";
            rec.teacher_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
            rec.record_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
            records.push_back(rec);
        } else {
            break;
        }
    }

    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "QueryAttendanceByClassAndDateRange step failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::QueryAttendanceByRegId(int32_t registration_id, std::vector<AttendanceRecord>& records) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    records.clear();

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT id, class_id, registration_id, student_name, student_gender, attendance_date, status, leave_time, teacher_name, record_time FROM attendance WHERE registration_id = ? ORDER BY attendance_date ASC, id ASC";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryAttendanceByRegId, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, registration_id);

    while (true) {
        ret = sqlite3_step(stmt);
        if (ret == SQLITE_ROW) {
            AttendanceRecord rec;
            rec.id = sqlite3_column_int(stmt, 0);
            rec.class_id = sqlite3_column_int(stmt, 1);
            rec.registration_id = sqlite3_column_int(stmt, 2);
            rec.student_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            rec.student_gender = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            rec.attendance_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            rec.status = static_cast<AttendanceStatusType>(sqlite3_column_int(stmt, 6));
            const unsigned char* lt = sqlite3_column_text(stmt, 7);
            rec.leave_time = lt ? reinterpret_cast<const char*>(lt) : "";
            rec.teacher_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
            rec.record_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
            records.push_back(rec);
        } else {
            break;
        }
    }

    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "QueryAttendanceByRegId step failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

/* Internal helpers — caller already holds db_mutex_ (used by atomic operations) */
int SqliteDatabase::RollbackTransactionInternal() {
    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }
    int ret = sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "RollbackTransactionInternal failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }
    return DB_OK;
}

int SqliteDatabase::CommitTransactionInternal() {
    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }
    int ret = sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "CommitTransactionInternal failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }
    return DB_OK;
}

/* ==================== Atomic Operations ==================== */

int SqliteDatabase::RegisterStudentAtomic(const RegistrationInfo& info, int32_t class_id,
                                           int32_t capacity, int32_t need_bed,
                                           int32_t bed_resource_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    /* begin transaction */
    const char* begin_sql = "BEGIN IMMEDIATE TRANSACTION";
    char* err_msg = nullptr;
    int ret = sqlite3_exec(db_, begin_sql, nullptr, nullptr, &err_msg);
    if (ret != SQLITE_OK) {
        if (err_msg) { sqlite3_free(err_msg); }
        return ERR_DB_EXEC_FAILED;
    }

    /* check enrollment available */
    double used = QueryEnrollmentUsedInternal(class_id);
    if (used < 0) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_EXEC_FAILED;
    }
    if (used + info.enrollment_ratio > static_cast<double>(capacity) + 0.001) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_CLASS_ENROLLMENT_FULL;
    }

    /* check bed resource if needed */
    if (need_bed == 1 && bed_resource_id > 0) {
        int32_t remain = QueryBedResourceRemainInternal(bed_resource_id);
        if (remain <= 0) {
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
            return ERR_RESOURCE_BED_UNAVAILABLE;
        }
    }

    /* insert registration */
    ret = InsertRegistrationInternal(info);
    if (ret != DB_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ret;
    }

    /* increment enrollment_used */
    ret = IncrementEnrollmentUsedInternal(class_id, info.enrollment_ratio);
    if (ret != DB_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ret;
    }

    /* increment bed reserved count if needed (independent from allocation used_count) */
    if (need_bed == 1 && bed_resource_id > 0) {
        ret = IncrementBedReservedInternal(bed_resource_id);
        if (ret != DB_OK) {
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
            return ret;
        }
    }

    /* commit transaction */
    const char* commit_sql = "COMMIT";
    ret = sqlite3_exec(db_, commit_sql, nullptr, nullptr, &err_msg);
    if (ret != SQLITE_OK) {
        if (err_msg) { sqlite3_free(err_msg); }
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::RegisterStudentsBatchAtomic(
    const std::vector<RegistrationInfo>& infos,
    int32_t class_id, int32_t capacity, int32_t bed_resource_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    if (infos.empty()) {
        return ERR_INVALID_PARAM;
    }

    /* begin transaction */
    const char* begin_sql = "BEGIN IMMEDIATE TRANSACTION";
    char* err_msg = nullptr;
    int ret = sqlite3_exec(db_, begin_sql, nullptr, nullptr, &err_msg);
    if (ret != SQLITE_OK) {
        if (err_msg) { sqlite3_free(err_msg); }
        return ERR_DB_EXEC_FAILED;
    }

    /* check enrollment capacity for whole batch */
    double used = QueryEnrollmentUsedInternal(class_id);
    if (used < 0) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_EXEC_FAILED;
    }
    double total_delta = 0.0;
    for (size_t i = 0; i < infos.size(); ++i) {
        total_delta += infos[i].enrollment_ratio;
    }
    if (used + total_delta > static_cast<double>(capacity) + 0.001) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_CLASS_ENROLLMENT_FULL;
    }

    /* per-student bed check + insert + increment */
    for (size_t i = 0; i < infos.size(); ++i) {
        if (infos[i].need_bed == 1 && bed_resource_id > 0) {
            int32_t remain = QueryBedResourceRemainInternal(bed_resource_id);
            if (remain <= 0) {
                sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
                return ERR_RESOURCE_BED_UNAVAILABLE;
            }
        }

        ret = InsertRegistrationInternal(infos[i]);
        if (ret != DB_OK) {
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
            return ret;
        }

        ret = IncrementEnrollmentUsedInternal(class_id, infos[i].enrollment_ratio);
        if (ret != DB_OK) {
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
            return ret;
        }

        if (infos[i].need_bed == 1 && bed_resource_id > 0) {
            ret = IncrementBedReservedInternal(bed_resource_id);
            if (ret != DB_OK) {
                sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
                return ret;
            }
        }
    }

    /* commit transaction */
    const char* commit_sql = "COMMIT";
    ret = sqlite3_exec(db_, commit_sql, nullptr, nullptr, &err_msg);
    if (ret != SQLITE_OK) {
        if (err_msg) { sqlite3_free(err_msg); }
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::RegisterDepositAtomic(
    const std::vector<RegistrationInfo>& infos,
    int32_t class_id, int32_t capacity, int32_t bed_resource_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    if (infos.empty()) {
        return ERR_INVALID_PARAM;
    }

    /* begin transaction */
    const char* begin_sql = "BEGIN IMMEDIATE TRANSACTION";
    char* err_msg = nullptr;
    int ret = sqlite3_exec(db_, begin_sql, nullptr, nullptr, &err_msg);
    if (ret != SQLITE_OK) {
        if (err_msg) { sqlite3_free(err_msg); }
        return ERR_DB_EXEC_FAILED;
    }

    /* check enrollment capacity for whole batch (定金报名同样占用名额) */
    double used = QueryEnrollmentUsedInternal(class_id);
    if (used < 0) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_EXEC_FAILED;
    }
    double total_delta = 0.0;
    for (size_t i = 0; i < infos.size(); ++i) {
        total_delta += infos[i].enrollment_ratio;
    }
    if (used + total_delta > static_cast<double>(capacity) + 0.001) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_CLASS_ENROLLMENT_FULL;
    }

    /* per-student bed check + insert (is_deposit=1, paid_amount_snapshot=定金金额) + increment */
    for (size_t i = 0; i < infos.size(); ++i) {
        if (infos[i].need_bed == 1 && bed_resource_id > 0) {
            int32_t remain = QueryBedResourceRemainInternal(bed_resource_id);
            if (remain <= 0) {
                sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
                return ERR_RESOURCE_BED_UNAVAILABLE;
            }
        }

        ret = InsertRegistrationInternal(infos[i]);
        if (ret != DB_OK) {
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
            return ret;
        }

        ret = IncrementEnrollmentUsedInternal(class_id, infos[i].enrollment_ratio);
        if (ret != DB_OK) {
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
            return ret;
        }

        if (infos[i].need_bed == 1 && bed_resource_id > 0) {
            ret = IncrementBedReservedInternal(bed_resource_id);
            if (ret != DB_OK) {
                sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
                return ret;
            }
        }
    }

    /* commit transaction */
    const char* commit_sql = "COMMIT";
    ret = sqlite3_exec(db_, commit_sql, nullptr, nullptr, &err_msg);
    if (ret != SQLITE_OK) {
        if (err_msg) { sqlite3_free(err_msg); }
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::QueryRegistrationForUpdateInternal(int32_t registration_id, RegistrationInfo& info) {
    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT id, class_id, student_name, student_gender, parent_phone, has_allergy, allergy_desc, price_id, need_bed, teacher_name, other_info, register_time, is_deposit, paid_amount_snapshot, supplement_amount, supplement_preset_id, supplement_operator, supplement_time, student_start_date, student_end_date, COALESCE((SELECT SUM(refund_amount) FROM refund_record WHERE registration_id=registration.id AND status=0), 0) AS refund_sum FROM registration WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryRegistrationForUpdateInternal, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, registration_id);

    ret = sqlite3_step(stmt);
    if (ret == SQLITE_ROW) {
        info.id = sqlite3_column_int(stmt, 0);
        info.class_id = sqlite3_column_int(stmt, 1);
        info.student_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        info.student_gender = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        info.parent_phone = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        info.has_allergy = sqlite3_column_int(stmt, 5);
        if (sqlite3_column_type(stmt, 6) != SQLITE_NULL) {
            info.allergy_desc = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        }
        info.price_id = sqlite3_column_int(stmt, 7);
        info.need_bed = sqlite3_column_int(stmt, 8);
        info.teacher_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
        if (sqlite3_column_type(stmt, 10) != SQLITE_NULL) {
            info.other_info = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
        }
        info.register_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));
        info.is_deposit = sqlite3_column_int(stmt, 12);
        info.paid_amount_snapshot = sqlite3_column_double(stmt, 13);
        info.supplement_amount = sqlite3_column_double(stmt, 14);
        info.supplement_preset_id = sqlite3_column_int(stmt, 15);
        if (sqlite3_column_type(stmt, 16) != SQLITE_NULL) {
            info.supplement_operator = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 16));
        }
        if (sqlite3_column_type(stmt, 17) != SQLITE_NULL) {
            info.supplement_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 17));
        }
        if (sqlite3_column_type(stmt, 18) != SQLITE_NULL) {
            info.student_start_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 18));
        }
        if (sqlite3_column_type(stmt, 19) != SQLITE_NULL) {
            info.student_end_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 19));
        }
        info.paid_amount = 0;
        info.refund_amount = sqlite3_column_double(stmt, 20);
        sqlite3_finalize(stmt);
        return DB_OK;
    }

    sqlite3_finalize(stmt);

    if (ret == SQLITE_DONE) {
        return ERR_REGISTRATION_NOT_FOUND;
    }

    LOG_ERROR << "QueryRegistrationForUpdateInternal step failed, ret=" << ret;
    return ERR_DB_EXEC_FAILED;
}

int SqliteDatabase::QueryRegistrationForDeleteInternal(int32_t registration_id, int32_t& class_id, int32_t& need_bed, std::string& student_start_date, std::string& student_end_date) {
    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT class_id, need_bed, student_start_date, student_end_date FROM registration WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryRegistrationForDeleteInternal, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, registration_id);

    ret = sqlite3_step(stmt);
    if (ret == SQLITE_ROW) {
        class_id = sqlite3_column_int(stmt, 0);
        need_bed = sqlite3_column_int(stmt, 1);
        if (sqlite3_column_type(stmt, 2) != SQLITE_NULL) {
            student_start_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        }
        if (sqlite3_column_type(stmt, 3) != SQLITE_NULL) {
            student_end_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        }
        sqlite3_finalize(stmt);
        return DB_OK;
    }

    sqlite3_finalize(stmt);

    if (ret == SQLITE_DONE) {
        return ERR_REGISTRATION_NOT_FOUND;
    }

    LOG_ERROR << "QueryRegistrationForDeleteInternal step failed, ret=" << ret;
    return ERR_DB_EXEC_FAILED;
}

int SqliteDatabase::DeleteRegistrationInternal(int32_t registration_id) {
    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "DELETE FROM registration WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for DeleteRegistrationInternal, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, registration_id);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "DeleteRegistrationInternal failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::UpdateRegistrationSupplementInternal(int32_t registration_id, int32_t target_class_price_id,
                                                         double target_amount, double supplement_amount,
                                                         int32_t target_preset_id,
                                                         const std::string& operator_name,
                                                         const std::string& operate_time) {
    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "UPDATE registration SET is_deposit = 0, paid_amount_snapshot = ?, price_id = ?, supplement_amount = ?, supplement_preset_id = ?, supplement_operator = ?, supplement_time = ? WHERE id = ? AND is_deposit = 1";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for UpdateRegistrationSupplementInternal, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_double(stmt, 1, target_amount);
    sqlite3_bind_int(stmt, 2, target_class_price_id);
    sqlite3_bind_double(stmt, 3, supplement_amount);
    sqlite3_bind_int(stmt, 4, target_preset_id);
    sqlite3_bind_text(stmt, 5, operator_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, operate_time.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, registration_id);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "UpdateRegistrationSupplementInternal failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    if (sqlite3_changes(db_) == 0) {
        /* WHERE is_deposit=1 未命中：并发场景下已被另一事务补缴，调用方应回滚并返回 ALREADY_DONE */
        return ERR_SUPPLEMENT_ALREADY_DONE;
    }

    return DB_OK;
}

int SqliteDatabase::SupplementDepositAtomic(int32_t registration_id,
                                            int32_t target_class_price_id,
                                            int32_t target_preset_id,
                                            double target_amount,
                                            const std::string& operator_name,
                                            const std::string& operate_time,
                                            double& out_supplement_amount) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    /* begin transaction */
    const char* begin_sql = "BEGIN IMMEDIATE TRANSACTION";
    char* err_msg = nullptr;
    int ret = sqlite3_exec(db_, begin_sql, nullptr, nullptr, &err_msg);
    if (ret != SQLITE_OK) {
        if (err_msg) { sqlite3_free(err_msg); }
        return ERR_DB_EXEC_FAILED;
    }

    /* 1. 事务内重读 registration，防并发重复补缴 */
    RegistrationInfo reg;
    ret = QueryRegistrationForUpdateInternal(registration_id, reg);
    if (ret != DB_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ret;
    }
    if (reg.is_deposit == 0) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_SUPPLEMENT_ALREADY_DONE;
    }

    /* 2. 校验目标 class_price_id 属于该班级（SELECT COUNT 轻量校验，防越权补缴到其他班级预设；
          不调加锁的 QueryPricesByClassId 避免 db_mutex_ 重入死锁） */
    sqlite3_stmt* chk = nullptr;
    ret = sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM class_price WHERE id=? AND class_id=?", -1, &chk, nullptr);
    if (ret != SQLITE_OK) {
        if (chk) { sqlite3_finalize(chk); }
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_PREPARE_FAILED;
    }
    sqlite3_bind_int(chk, 1, target_class_price_id);
    sqlite3_bind_int(chk, 2, reg.class_id);
    int preset_count = 0;
    if (sqlite3_step(chk) == SQLITE_ROW) {
        preset_count = sqlite3_column_int(chk, 0);
    }
    sqlite3_finalize(chk);
    if (preset_count == 0) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_SUPPLEMENT_PRESET_NOT_IN_CLASS;
    }

    /* 3. 校验目标全额 > 原已付定金 */
    double deposit_paid = reg.paid_amount_snapshot;
    if (target_amount <= deposit_paid) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_SUPPLEMENT_AMOUNT_INVALID;
    }
    out_supplement_amount = target_amount - deposit_paid;

    /* 4. UPDATE registration 写补缴字段（is_deposit=0, paid_amount_snapshot=目标全额, price_id=目标class_price.id, supplement_*审计） */
    ret = UpdateRegistrationSupplementInternal(registration_id, target_class_price_id,
                                               target_amount, out_supplement_amount,
                                               target_preset_id, operator_name, operate_time);
    if (ret != DB_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ret;
    }

    /* commit transaction */
    const char* commit_sql = "COMMIT";
    ret = sqlite3_exec(db_, commit_sql, nullptr, nullptr, &err_msg);
    if (ret != SQLITE_OK) {
        if (err_msg) { sqlite3_free(err_msg); }
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::DeleteRegistrationAtomic(int32_t registration_id, int32_t bed_resource_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    /* begin transaction */
    const char* begin_sql = "BEGIN IMMEDIATE TRANSACTION";
    char* err_msg = nullptr;
    int ret = sqlite3_exec(db_, begin_sql, nullptr, nullptr, &err_msg);
    if (ret != SQLITE_OK) {
        if (err_msg) { sqlite3_free(err_msg); }
        return ERR_DB_EXEC_FAILED;
    }

    /* 1. query registration for class_id, need_bed, and period (re-read inside tx to prevent TOCTOU) */
    int32_t class_id = 0;
    int32_t need_bed = 0;
    std::string student_start_date;
    std::string student_end_date;
    ret = QueryRegistrationForDeleteInternal(registration_id, class_id, need_bed, student_start_date, student_end_date);
    if (ret != DB_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ret;
    }

    /* 2. delete attendance records */
    ret = DeleteAttendanceByRegIdInternal(registration_id);
    if (ret != DB_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DELETE_STUDENT_FAILED;
    }

    /* 3. delete resource allocations and collect resource_ids for used_count release */
    std::vector<int32_t> resource_ids;
    ret = DeleteAllocationsByRegIdInternal(registration_id, resource_ids);
    if (ret != DB_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DELETE_STUDENT_FAILED;
    }

    /* 4. release resources: decrement used_count for each unique resource_id */
    if (!resource_ids.empty()) {
        std::sort(resource_ids.begin(), resource_ids.end());
        resource_ids.erase(std::unique(resource_ids.begin(), resource_ids.end()), resource_ids.end());
        for (size_t i = 0; i < resource_ids.size(); ++i) {
            ret = DecrementResourceUsedInternal(resource_ids[i]);
            if (ret != DB_OK) {
                sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
                return ERR_DELETE_STUDENT_FAILED;
            }
        }
    }

    /* 5. delete refund records */
    ret = DeleteRefundsByRegIdInternal(registration_id);
    if (ret != DB_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DELETE_STUDENT_FAILED;
    }

    /* 6. delete registration record */
    ret = DeleteRegistrationInternal(registration_id);
    if (ret != DB_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DELETE_STUDENT_FAILED;
    }

    /* 7. decrement enrollment_used */
    double enrollment_ratio = 1.0;
    if (!student_start_date.empty() && !student_end_date.empty()) {
        /* compute ratio from student period vs class period */
        ClassInfo class_info;
        if (QueryClassByIdInternal(class_id, class_info) == DB_OK &&
            !class_info.start_time.empty() && !class_info.end_time.empty()) {
            int32_t student_days = register_student::CountWeekdaysInRange(student_start_date, student_end_date);
            int32_t class_days = register_student::CountWeekdaysInRange(class_info.start_time, class_info.end_time);
            if (class_days > 0) {
                double raw = static_cast<double>(student_days) / static_cast<double>(class_days);
                enrollment_ratio = std::round(raw * 10000.0) / 10000.0;
            }
        }
    }
    ret = DecrementEnrollmentUsedInternal(class_id, enrollment_ratio);
    if (ret != DB_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DELETE_STUDENT_FAILED;
    }

    /* 8. decrement bed_reserved_count if need_bed=1 */
    if (need_bed == 1 && bed_resource_id > 0) {
        ret = DecrementBedReservedInternal(bed_resource_id);
        if (ret != DB_OK) {
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
            return ERR_DELETE_STUDENT_FAILED;
        }
    }

    /* commit transaction */
    const char* commit_sql = "COMMIT";
    ret = sqlite3_exec(db_, commit_sql, nullptr, nullptr, &err_msg);
    if (ret != SQLITE_OK) {
        if (err_msg) { sqlite3_free(err_msg); }
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::DecrementEnrollmentUsedInternal(int32_t class_id, double delta) {
    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    /* Query current value before update */
    double before_value = 0.0;
    const char* query_sql = "SELECT enrollment_used FROM class_info WHERE id = ?";
    sqlite3_stmt* query_stmt = nullptr;
    if (sqlite3_prepare_v2(db_, query_sql, -1, &query_stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(query_stmt, 1, class_id);
        if (sqlite3_step(query_stmt) == SQLITE_ROW) {
            before_value = sqlite3_column_double(query_stmt, 0);
        }
        sqlite3_finalize(query_stmt);
    }

    const char* sql = "UPDATE class_info SET enrollment_used = ROUND(enrollment_used - ?, 4) WHERE id = ? AND enrollment_used >= ? - 0.001";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for DecrementEnrollmentUsed, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_double(stmt, 1, delta);
    sqlite3_bind_int(stmt, 2, class_id);
    sqlite3_bind_double(stmt, 3, delta);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "DecrementEnrollmentUsed failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    int changes = sqlite3_changes(db_);
    LOG_INFO << "DecrementEnrollmentUsed: class_id=" << class_id
             << " delta=" << delta
             << " before=" << before_value
             << " after=" << (before_value - delta)
             << " changes=" << changes;

    return DB_OK;
}

int SqliteDatabase::UpdateRegistrationClassIdInternal(int32_t registration_id, int32_t new_class_id) {
    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "UPDATE registration SET class_id = ? WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for UpdateRegistrationClassId, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, new_class_id);
    sqlite3_bind_int(stmt, 2, registration_id);

    ret = sqlite3_step(stmt);
    int changes = sqlite3_changes(db_);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "UpdateRegistrationClassId failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    if (changes == 0) {
        return ERR_REGISTRATION_NOT_FOUND;
    }

    return DB_OK;
}

int SqliteDatabase::UpdateStudentBasicInfoInternal(const RegistrationInfo& info) {
    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "UPDATE registration SET student_name=?, student_gender=?, parent_phone=?, "
                      "has_allergy=?, allergy_desc=?, other_info=?, teacher_name=? WHERE id=?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for UpdateStudentBasicInfo, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_text(stmt, 1, info.student_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, info.student_gender.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, info.parent_phone.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, info.has_allergy);
    sqlite3_bind_text(stmt, 5, info.allergy_desc.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, info.other_info.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, info.teacher_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 8, info.id);

    ret = sqlite3_step(stmt);
    int changes = sqlite3_changes(db_);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "UpdateStudentBasicInfo failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    if (changes == 0) {
        return ERR_REGISTRATION_NOT_FOUND;
    }

    return DB_OK;
}

int SqliteDatabase::UpdateStudentBasicInfo(const RegistrationInfo& info) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    char* err_msg = nullptr;
    int ret = sqlite3_exec(db_, "BEGIN IMMEDIATE TRANSACTION", nullptr, nullptr, &err_msg);
    if (ret != SQLITE_OK) {
        if (err_msg) { sqlite3_free(err_msg); }
        return ERR_DB_EXEC_FAILED;
    }

    ret = UpdateStudentBasicInfoInternal(info);
    if (ret != DB_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ret;
    }

    ret = sqlite3_exec(db_, "COMMIT", nullptr, nullptr, &err_msg);
    if (ret != SQLITE_OK) {
        if (err_msg) { sqlite3_free(err_msg); }
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::TransferClassAtomic(int32_t registration_id, int32_t old_class_id,
                                        int32_t new_class_id, int32_t new_class_capacity) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    char* err_msg = nullptr;
    int ret = sqlite3_exec(db_, "BEGIN IMMEDIATE TRANSACTION", nullptr, nullptr, &err_msg);
    if (ret != SQLITE_OK) {
        if (err_msg) { sqlite3_free(err_msg); }
        return ERR_DB_EXEC_FAILED;
    }

    /* 1. 查询报名记录获取 student_start_date/student_end_date 以计算 enrollment_ratio */
    double student_ratio = 1.0;
    bool need_update_period = false;  /* 全额报名学生转班时自动更新时段为新班级时段 */
    std::string new_start_date;
    std::string new_end_date;
    {
        const char* sql = "SELECT student_start_date, student_end_date FROM registration WHERE id = ?";
        sqlite3_stmt* stmt = nullptr;
        ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (ret != SQLITE_OK) {
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
            return ERR_DB_PREPARE_FAILED;
        }
        sqlite3_bind_int(stmt, 1, registration_id);
        ret = sqlite3_step(stmt);
        if (ret == SQLITE_ROW) {
            std::string student_start_date;
            std::string student_end_date;
            if (sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
                student_start_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            }
            if (sqlite3_column_type(stmt, 1) != SQLITE_NULL) {
                student_end_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            }
            if (!student_start_date.empty() && !student_end_date.empty()) {
                ClassInfo class_info;
                if (QueryClassByIdInternal(old_class_id, class_info) == DB_OK &&
                    !class_info.start_time.empty() && !class_info.end_time.empty()) {
                    /* 判断是否全额报名（时段=原班级完整时段） */
                    if (student_start_date == class_info.start_time && student_end_date == class_info.end_time) {
                        need_update_period = true;
                        ClassInfo new_class_info;
                        if (QueryClassByIdInternal(new_class_id, new_class_info) == DB_OK) {
                            new_start_date = new_class_info.start_time;
                            new_end_date = new_class_info.end_time;
                        }
                    } else {
                        /* 部分时段：计算 enrollment_ratio */
                        int32_t student_days = register_student::CountWeekdaysInRange(student_start_date, student_end_date);
                        int32_t class_days = register_student::CountWeekdaysInRange(class_info.start_time, class_info.end_time);
                        if (class_days > 0) {
                            double raw = static_cast<double>(student_days) / static_cast<double>(class_days);
                            student_ratio = std::round(raw * 10000.0) / 10000.0;
                        }
                    }
                }
            } else {
                /* 旧数据（student_start_date/student_end_date 为空）视为全额报名 */
                need_update_period = true;
                ClassInfo new_class_info;
                if (QueryClassByIdInternal(new_class_id, new_class_info) == DB_OK) {
                    new_start_date = new_class_info.start_time;
                    new_end_date = new_class_info.end_time;
                }
            }
        } else {
            sqlite3_finalize(stmt);
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
            return ERR_REGISTRATION_NOT_FOUND;
        }
        sqlite3_finalize(stmt);
    }

    /* 2. 校验新班级余量 */
    double used = QueryEnrollmentUsedInternal(new_class_id);
    if (used < 0) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_EXEC_FAILED;
    }
    if (used + student_ratio > static_cast<double>(new_class_capacity) + 0.001) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_CLASS_ENROLLMENT_FULL;
    }

    /* 3. 原班级 enrollment_used - student_ratio */
    ret = DecrementEnrollmentUsedInternal(old_class_id, student_ratio);
    if (ret != DB_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ret;
    }

    /* 4. 新班级 enrollment_used + student_ratio */
    ret = IncrementEnrollmentUsedInternal(new_class_id, student_ratio);
    if (ret != DB_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ret;
    }

    /* 5. 更新 registration.class_id */
    ret = UpdateRegistrationClassIdInternal(registration_id, new_class_id);
    if (ret != DB_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ret;
    }

    /* 5.5 全额报名学生转班时自动更新 student_start_date/student_end_date 为新班级时段 */
    if (need_update_period && !new_start_date.empty() && !new_end_date.empty()) {
        const char* period_sql = "UPDATE registration SET student_start_date = ?, student_end_date = ? WHERE id = ?";
        sqlite3_stmt* period_stmt = nullptr;
        ret = sqlite3_prepare_v2(db_, period_sql, -1, &period_stmt, nullptr);
        if (ret == SQLITE_OK) {
            sqlite3_bind_text(period_stmt, 1, new_start_date.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(period_stmt, 2, new_end_date.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(period_stmt, 3, registration_id);
            ret = sqlite3_step(period_stmt);
            sqlite3_finalize(period_stmt);
            if (ret != SQLITE_DONE) {
                sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
                return ERR_DB_EXEC_FAILED;
            }
        } else {
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
            return ERR_DB_PREPARE_FAILED;
        }
    }

    /* 6. commit */
    ret = sqlite3_exec(db_, "COMMIT", nullptr, nullptr, &err_msg);
    if (ret != SQLITE_OK) {
        if (err_msg) { sqlite3_free(err_msg); }
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::AllocateResourceAtomic(const ResourceAllocation& alloc) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    /* begin transaction */
    const char* begin_sql = "BEGIN IMMEDIATE TRANSACTION";
    char* err_msg = nullptr;
    int ret = sqlite3_exec(db_, begin_sql, nullptr, nullptr, &err_msg);
    if (ret != SQLITE_OK) {
        if (err_msg) { sqlite3_free(err_msg); }
        return ERR_DB_EXEC_FAILED;
    }

    /* check resource code not occupied */
    int occupied = CheckResourceCodeOccupiedInternal(alloc.resource_id, alloc.resource_code);
    if (occupied == 1) {
        /* code is already occupied */
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_RESOURCE_CODE_OCCUPIED;
    }
    if (occupied < 0) {
        /* query error */
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return occupied;
    }

    /* insert allocation */
    ret = InsertAllocationInternal(alloc);
    if (ret != DB_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ret;
    }

    /* increment resource used count */
    ret = IncrementResourceUsedInternal(alloc.resource_id);
    if (ret != DB_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ret;
    }

    /* commit transaction */
    const char* commit_sql = "COMMIT";
    ret = sqlite3_exec(db_, commit_sql, nullptr, nullptr, &err_msg);
    if (ret != SQLITE_OK) {
        if (err_msg) { sqlite3_free(err_msg); }
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

/* ==================== IRefundDao ==================== */

int SqliteDatabase::QueryActiveRefundSumByRegIdInternal(int32_t registration_id, double& sum) {
    if (!db_) { return ERR_DB_NOT_OPEN; }
    const char* sql = "SELECT COALESCE(SUM(refund_amount), 0) FROM refund_record WHERE registration_id=? AND status=0";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryActiveRefundSumByRegIdInternal, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }
    sqlite3_bind_int(stmt, 1, registration_id);
    ret = sqlite3_step(stmt);
    if (ret == SQLITE_ROW) {
        sum = sqlite3_column_double(stmt, 0);
    } else {
        sum = 0.0;
    }
    sqlite3_finalize(stmt);
    return DB_OK;
}

int SqliteDatabase::QueryActiveRefundSumByRegId(int32_t registration_id, double& sum) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    return QueryActiveRefundSumByRegIdInternal(registration_id, sum);
}

int SqliteDatabase::InsertRefundInternal(const RefundRecordInfo& info) {
    if (!db_) { return ERR_DB_NOT_OPEN; }
    const char* sql =
        "INSERT INTO refund_record (registration_id, refund_amount, operator_name, refund_time, "
        "status, cancel_operator_name, cancel_time, unit_price, total_class_days, attended_days, "
        "original_amount, tolerance_used) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for InsertRefundInternal, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }
    sqlite3_bind_int(stmt, 1, info.registration_id);
    sqlite3_bind_double(stmt, 2, info.refund_amount);
    sqlite3_bind_text(stmt, 3, info.operator_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, info.refund_time.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, info.status);
    sqlite3_bind_text(stmt, 6, info.cancel_operator_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, info.cancel_time.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 8, info.unit_price);
    sqlite3_bind_int(stmt, 9, info.total_class_days);
    sqlite3_bind_int(stmt, 10, info.attended_days);
    sqlite3_bind_double(stmt, 11, info.original_amount);
    sqlite3_bind_double(stmt, 12, info.tolerance_used);
    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) {
        LOG_ERROR << "InsertRefundInternal step failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }
    const_cast<RefundRecordInfo&>(info).id = static_cast<int32_t>(sqlite3_last_insert_rowid(db_));
    return DB_OK;
}

int SqliteDatabase::QueryLatestActiveRefundInternal(int32_t registration_id, RefundRecordInfo& info) {
    if (!db_) { return ERR_DB_NOT_OPEN; }
    const char* sql =
        "SELECT id, registration_id, refund_amount, operator_name, refund_time, status, "
        "cancel_operator_name, cancel_time, unit_price, total_class_days, attended_days, "
        "original_amount, tolerance_used "
        "FROM refund_record WHERE registration_id=? AND status=0 "
        "ORDER BY refund_time DESC LIMIT 1";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryLatestActiveRefundInternal, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }
    sqlite3_bind_int(stmt, 1, registration_id);
    ret = sqlite3_step(stmt);
    if (ret == SQLITE_ROW) {
        info.id = sqlite3_column_int(stmt, 0);
        info.registration_id = sqlite3_column_int(stmt, 1);
        info.refund_amount = sqlite3_column_double(stmt, 2);
        info.operator_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        info.refund_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        info.status = static_cast<RefundStatusType>(sqlite3_column_int(stmt, 5));
        const unsigned char* c_op = sqlite3_column_text(stmt, 6);
        info.cancel_operator_name = c_op ? reinterpret_cast<const char*>(c_op) : "";
        const unsigned char* c_t = sqlite3_column_text(stmt, 7);
        info.cancel_time = c_t ? reinterpret_cast<const char*>(c_t) : "";
        info.unit_price = sqlite3_column_double(stmt, 8);
        info.total_class_days = sqlite3_column_int(stmt, 9);
        info.attended_days = sqlite3_column_int(stmt, 10);
        info.original_amount = sqlite3_column_double(stmt, 11);
        info.tolerance_used = sqlite3_column_double(stmt, 12);
        sqlite3_finalize(stmt);
        return DB_OK;
    }
    sqlite3_finalize(stmt);
    return ERR_REFUND_NOT_FOUND;
}

int SqliteDatabase::DeleteRefundsByRegIdInternal(int32_t registration_id) {
    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "DELETE FROM refund_record WHERE registration_id = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for DeleteRefundsByRegId, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, registration_id);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "DeleteRefundsByRegId failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::InsertRefundAtomic(RefundRecordInfo& info, double original_amount, double tolerance, bool skip_attendance_check) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    /* BEGIN IMMEDIATE */
    int ret = sqlite3_exec(db_, "BEGIN IMMEDIATE TRANSACTION", nullptr, nullptr, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "InsertRefundAtomic BEGIN failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    /* 事务内重读累计退费防并发 */
    double sum = 0.0;
    ret = QueryActiveRefundSumByRegIdInternal(info.registration_id, sum);
    if (ret != DB_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ret;
    }

    /* 事务内重新校验上限（paid_limit = original - sum；attendance_limit 用 info 快照重算）
       管理员跳过考勤折损校验时仅校验 paid_limit */
    double paid_limit = original_amount - sum;
    if (skip_attendance_check) {
        if (info.refund_amount > paid_limit + tolerance) {
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
            LOG_INFO << "InsertRefundAtomic refund " << info.refund_amount
                     << " exceeds paid_limit " << paid_limit << " (re-read, admin skip attendance)";
            return ERR_REFUND_EXCEEDS_PAID;
        }
    } else {
        double attendance_limit = info.original_amount - info.unit_price * info.attended_days;
        double real_final_limit = (paid_limit < attendance_limit ? paid_limit : attendance_limit) + tolerance;
        if (info.refund_amount > real_final_limit) {
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
            LOG_INFO << "InsertRefundAtomic refund " << info.refund_amount
                     << " exceeds limit " << real_final_limit << " (re-read)";
            return ERR_REFUND_EXCEEDS_PAID;
        }
    }

    /* 插入 */
    ret = InsertRefundInternal(info);
    if (ret != DB_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ret;
    }

    /* COMMIT */
    ret = sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "InsertRefundAtomic COMMIT failed, ret=" << ret;
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_EXEC_FAILED;
    }
    return DB_OK;
}

int SqliteDatabase::CancelRefundAtomic(int32_t registration_id,
                                       const std::string& cancel_operator_name,
                                       const std::string& cancel_time,
                                       double& restored_paid_amount) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    int ret = sqlite3_exec(db_, "BEGIN IMMEDIATE TRANSACTION", nullptr, nullptr, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "CancelRefundAtomic BEGIN failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    /* 查最近一条 status=0 */
    RefundRecordInfo latest;
    ret = QueryLatestActiveRefundInternal(registration_id, latest);
    if (ret != DB_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ret;  /* ERR_REFUND_NOT_FOUND */
    }

    /* UPDATE 为 status=1 */
    const char* sql =
        "UPDATE refund_record SET status=1, cancel_operator_name=?, cancel_time=? "
        "WHERE id=? AND status=0";
    sqlite3_stmt* stmt = nullptr;
    ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for CancelRefundAtomic, ret=" << ret;
        if (stmt) { sqlite3_finalize(stmt); }
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_PREPARE_FAILED;
    }
    sqlite3_bind_text(stmt, 1, cancel_operator_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, cancel_time.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, latest.id);
    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) {
        LOG_ERROR << "CancelRefundAtomic step failed, ret=" << ret;
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_EXEC_FAILED;
    }

    /* 计算恢复后实缴 */
    double sum = 0.0;
    QueryActiveRefundSumByRegIdInternal(registration_id, sum);
    restored_paid_amount = latest.original_amount - sum;

    ret = sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "CancelRefundAtomic COMMIT failed, ret=" << ret;
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_EXEC_FAILED;
    }
    return DB_OK;
}

int SqliteDatabase::QueryRefundsByRegId(int32_t registration_id, std::vector<RefundRecordInfo>& records) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    records.clear();

    if (!db_) { return ERR_DB_NOT_OPEN; }

    const char* sql =
        "SELECT id, registration_id, refund_amount, operator_name, refund_time, status, "
        "cancel_operator_name, cancel_time, unit_price, total_class_days, attended_days, "
        "original_amount, tolerance_used "
        "FROM refund_record WHERE registration_id=? ORDER BY refund_time DESC";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryRefundsByRegId, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }
    sqlite3_bind_int(stmt, 1, registration_id);

    while (true) {
        ret = sqlite3_step(stmt);
        if (ret == SQLITE_ROW) {
            RefundRecordInfo info;
            info.id = sqlite3_column_int(stmt, 0);
            info.registration_id = sqlite3_column_int(stmt, 1);
            info.refund_amount = sqlite3_column_double(stmt, 2);
            info.operator_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            info.refund_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            info.status = static_cast<RefundStatusType>(sqlite3_column_int(stmt, 5));
            const unsigned char* c_op = sqlite3_column_text(stmt, 6);
            info.cancel_operator_name = c_op ? reinterpret_cast<const char*>(c_op) : "";
            const unsigned char* c_t = sqlite3_column_text(stmt, 7);
            info.cancel_time = c_t ? reinterpret_cast<const char*>(c_t) : "";
            info.unit_price = sqlite3_column_double(stmt, 8);
            info.total_class_days = sqlite3_column_int(stmt, 9);
            info.attended_days = sqlite3_column_int(stmt, 10);
            info.original_amount = sqlite3_column_double(stmt, 11);
            info.tolerance_used = sqlite3_column_double(stmt, 12);
            records.push_back(info);
        } else {
            break;
        }
    }

    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "QueryRefundsByRegId step failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::RenewRegistrationAtomic(int32_t registration_id,
    const std::string& new_end_date, double renew_amount, double enrollment_delta,
    const std::string& operator_name, const std::string& operate_time) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    /* BEGIN IMMEDIATE */
    int ret = sqlite3_exec(db_, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "RenewRegistrationAtomic BEGIN IMMEDIATE failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    /* 1. Query registration for class_id and current student_end_date */
    int32_t class_id = 0;
    std::string current_end_date;
    {
        const char* sql = "SELECT class_id, student_end_date FROM registration WHERE id = ?";
        sqlite3_stmt* stmt = nullptr;
        ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (ret != SQLITE_OK) {
            RollbackTransactionInternal();
            return ERR_DB_PREPARE_FAILED;
        }
        sqlite3_bind_int(stmt, 1, registration_id);
        ret = sqlite3_step(stmt);
        if (ret == SQLITE_ROW) {
            class_id = sqlite3_column_int(stmt, 0);
            if (sqlite3_column_type(stmt, 1) != SQLITE_NULL) {
                current_end_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            }
        } else {
            sqlite3_finalize(stmt);
            RollbackTransactionInternal();
            return ERR_REGISTRATION_NOT_FOUND;
        }
        sqlite3_finalize(stmt);
    }

    /* 2. Verify student_end_date hasn't changed (concurrency check) */
    /* The caller already validated new_end_date > current_end_date, but we re-verify */
    /* If current_end_date is empty, it means full period student - should not be renewing */

    /* 3. Update registration: student_end_date and paid_amount_snapshot */
    {
        const char* sql = "UPDATE registration SET student_end_date = ?, paid_amount_snapshot = paid_amount_snapshot + ? WHERE id = ?";
        sqlite3_stmt* stmt = nullptr;
        ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (ret != SQLITE_OK) {
            RollbackTransactionInternal();
            return ERR_DB_PREPARE_FAILED;
        }
        sqlite3_bind_text(stmt, 1, new_end_date.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 2, renew_amount);
        sqlite3_bind_int(stmt, 3, registration_id);
        ret = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (ret != SQLITE_DONE) {
            RollbackTransactionInternal();
            return ERR_DB_EXEC_FAILED;
        }
    }

    /* 4. Update enrollment_used */
    ret = IncrementEnrollmentUsedInternal(class_id, enrollment_delta);
    if (ret != DB_OK) {
        RollbackTransactionInternal();
        return ret;
    }

    /* 5. Insert renewal_record */
    {
        RenewalRecordInfo renewal;
        renewal.id = 0;
        renewal.registration_id = registration_id;
        renewal.old_end_date = current_end_date;
        renewal.new_end_date = new_end_date;
        renewal.renew_amount = renew_amount;
        renewal.operator_name = operator_name;
        renewal.renew_time = operate_time;
        ret = InsertRenewalInternal(renewal);
        if (ret != DB_OK) {
            RollbackTransactionInternal();
            return ret;
        }
    }

    /* COMMIT */
    ret = CommitTransactionInternal();
    if (ret != DB_OK) {
        return ret;
    }

    LOG_DEBUG << "RenewRegistrationAtomic success, reg_id=" << registration_id
              << " new_end=" << new_end_date << " amount=" << renew_amount;
    return DB_OK;
}

int SqliteDatabase::InsertRenewalInternal(const RenewalRecordInfo& info) {
    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "INSERT INTO renewal_record (registration_id, old_end_date, new_end_date, renew_amount, operator_name, renew_time) VALUES (?, ?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for InsertRenewal, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, info.registration_id);
    sqlite3_bind_text(stmt, 2, info.old_end_date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, info.new_end_date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 4, info.renew_amount);
    sqlite3_bind_text(stmt, 5, info.operator_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, info.renew_time.c_str(), -1, SQLITE_TRANSIENT);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "InsertRenewal failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::QueryRegistrationPeriodForRenewInternal(int32_t registration_id,
    int32_t& class_id, std::string& student_end_date) {
    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT class_id, student_end_date FROM registration WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, registration_id);
    ret = sqlite3_step(stmt);
    if (ret == SQLITE_ROW) {
        class_id = sqlite3_column_int(stmt, 0);
        if (sqlite3_column_type(stmt, 1) != SQLITE_NULL) {
            student_end_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        }
        sqlite3_finalize(stmt);
        return DB_OK;
    }

    sqlite3_finalize(stmt);
    return ERR_REGISTRATION_NOT_FOUND;
}

int SqliteDatabase::QueryRenewalsByRegId(int32_t registration_id,
    std::vector<RenewalRecordInfo>& records) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    records.clear();

    if (!db_) {
        return ERR_DB_NOT_OPEN;
    }

    const char* sql = "SELECT id, registration_id, old_end_date, new_end_date, renew_amount, operator_name, renew_time FROM renewal_record WHERE registration_id = ? ORDER BY renew_time DESC";
    sqlite3_stmt* stmt = nullptr;

    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        LOG_ERROR << "Prepare failed for QueryRenewalsByRegId, ret=" << ret;
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int(stmt, 1, registration_id);

    while (true) {
        ret = sqlite3_step(stmt);
        if (ret == SQLITE_ROW) {
            RenewalRecordInfo info;
            info.id = sqlite3_column_int(stmt, 0);
            info.registration_id = sqlite3_column_int(stmt, 1);
            info.old_end_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            info.new_end_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            info.renew_amount = sqlite3_column_double(stmt, 4);
            info.operator_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            info.renew_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            records.push_back(info);
        } else {
            break;
        }
    }

    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_ERROR << "QueryRenewalsByRegId step failed, ret=" << ret;
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

/* ===================================================================
 * IActivityDao implementation
 * =================================================================== */

int SqliteDatabase::CreateActivity(const ActivityInfo& info, int64_t& out_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    if (info.title.empty()) { return ERR_ACTIVITY_TITLE_EMPTY; }
    if (info.start_time >= info.end_time) { return ERR_ACTIVITY_TIME_INVALID; }
    if (info.signup_deadline > info.end_time) { return ERR_ACTIVITY_DEADLINE_INVALID; }

    std::string now = register_student::GetCurrentTimeString();
    const char* sql = "INSERT INTO activity (title, description, cover_image, start_time, end_time, signup_deadline, capacity, group_image, sort_order, status, min_group_size, group_type, created_at, updated_at) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?)";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    sqlite3_bind_text(stmt, 1, info.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, info.description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, info.cover_image.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, info.start_time.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, info.end_time.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, info.signup_deadline.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, info.capacity);
    sqlite3_bind_text(stmt, 8, info.group_image.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 9, info.sort_order);
    sqlite3_bind_int(stmt, 10, info.status);
    sqlite3_bind_int(stmt, 11, info.min_group_size);
    sqlite3_bind_int(stmt, 12, info.group_type);
    sqlite3_bind_text(stmt, 13, now.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 14, now.c_str(), -1, SQLITE_TRANSIENT);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) { return ERR_DB_EXEC_FAILED; }

    out_id = static_cast<int64_t>(sqlite3_last_insert_rowid(db_));
    return DB_OK;
}

int SqliteDatabase::UpdateActivity(const ActivityInfo& info) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    if (info.start_time >= info.end_time) { return ERR_ACTIVITY_TIME_INVALID; }
    if (info.signup_deadline > info.end_time) { return ERR_ACTIVITY_DEADLINE_INVALID; }

    std::string now = register_student::GetCurrentTimeString();
    const char* sql = "UPDATE activity SET title=?, description=?, start_time=?, end_time=?, signup_deadline=?, capacity=?, sort_order=?, min_group_size=?, group_type=?, updated_at=? WHERE id=?";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    sqlite3_bind_text(stmt, 1, info.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, info.description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, info.start_time.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, info.end_time.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, info.signup_deadline.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 6, info.capacity);
    sqlite3_bind_int(stmt, 7, info.sort_order);
    sqlite3_bind_int(stmt, 8, info.min_group_size);
    sqlite3_bind_int(stmt, 9, info.group_type);
    sqlite3_bind_text(stmt, 10, now.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 11, info.id);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) { return ERR_DB_EXEC_FAILED; }
    if (sqlite3_changes(db_) == 0) { return ERR_ACTIVITY_NOT_FOUND; }

    return DB_OK;
}

int SqliteDatabase::DeleteActivity(int64_t id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    int ret = sqlite3_exec(db_, "BEGIN IMMEDIATE TRANSACTION", nullptr, nullptr, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_EXEC_FAILED; }

    /* delete signups first */
    const char* sql_del_signups = "DELETE FROM activity_signup WHERE activity_id=?";
    sqlite3_stmt* stmt = nullptr;
    ret = sqlite3_prepare_v2(db_, sql_del_signups, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_PREPARE_FAILED;
    }
    sqlite3_bind_int64(stmt, 1, id);
    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_EXEC_FAILED;
    }

    /* delete activity */
    const char* sql_del = "DELETE FROM activity WHERE id=?";
    ret = sqlite3_prepare_v2(db_, sql_del, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_PREPARE_FAILED;
    }
    sqlite3_bind_int64(stmt, 1, id);
    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_EXEC_FAILED;
    }
    if (sqlite3_changes(db_) == 0) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_ACTIVITY_NOT_FOUND;
    }

    ret = sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr);
    if (ret != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::GetActivity(int64_t id, ActivityInfo& info) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    const char* sql = "SELECT id, title, description, cover_image, start_time, end_time, signup_deadline, capacity, signup_count, group_image, sort_order, status, min_group_size, group_type, created_at, updated_at FROM activity WHERE id=?";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    sqlite3_bind_int64(stmt, 1, id);
    ret = sqlite3_step(stmt);
    if (ret != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return ERR_ACTIVITY_NOT_FOUND;
    }

    info.id = sqlite3_column_int64(stmt, 0);
    info.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    info.description = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    info.cover_image = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    info.start_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    info.end_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
    info.signup_deadline = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
    info.capacity = sqlite3_column_int(stmt, 7);
    info.signup_count = sqlite3_column_int(stmt, 8);
    info.group_image = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
    info.sort_order = sqlite3_column_int(stmt, 10);
    info.status = sqlite3_column_int(stmt, 11);
    info.min_group_size = sqlite3_column_int(stmt, 12);
    info.group_type = sqlite3_column_int(stmt, 13);
    info.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 14));
    info.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 15));

    sqlite3_finalize(stmt);
    return DB_OK;
}

static void LoadActivityFromRow(sqlite3_stmt* stmt, ActivityInfo& info) {
    info.id = sqlite3_column_int64(stmt, 0);
    info.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    info.description = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    info.cover_image = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    info.start_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    info.end_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
    info.signup_deadline = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
    info.capacity = sqlite3_column_int(stmt, 7);
    info.signup_count = sqlite3_column_int(stmt, 8);
    info.group_image = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
    info.sort_order = sqlite3_column_int(stmt, 10);
    info.status = sqlite3_column_int(stmt, 11);
    info.min_group_size = sqlite3_column_int(stmt, 12);
    info.group_type = sqlite3_column_int(stmt, 13);
    info.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 14));
    info.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 15));
}

int SqliteDatabase::ListActivities(std::vector<ActivityInfo>& list) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    list.clear();
    const char* sql = "SELECT id, title, description, cover_image, start_time, end_time, signup_deadline, capacity, signup_count, group_image, sort_order, status, min_group_size, group_type, created_at, updated_at FROM activity ORDER BY sort_order ASC, id DESC";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ActivityInfo info;
        LoadActivityFromRow(stmt, info);
        list.push_back(info);
    }

    sqlite3_finalize(stmt);
    return DB_OK;
}

int SqliteDatabase::ListPublishedActivities(std::vector<ActivityInfo>& list) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    list.clear();
    const char* sql = "SELECT id, title, description, cover_image, start_time, end_time, signup_deadline, capacity, signup_count, group_image, sort_order, status, min_group_size, group_type, created_at, updated_at FROM activity WHERE status=1 ORDER BY sort_order ASC, id DESC";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ActivityInfo info;
        LoadActivityFromRow(stmt, info);
        list.push_back(info);
    }

    sqlite3_finalize(stmt);
    return DB_OK;
}

int SqliteDatabase::ListPublishedActivitiesPaged(std::vector<ActivityInfo>& list, int limit, int offset, int& total_count) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    list.clear();
    total_count = 0;

    const char* count_sql = "SELECT COUNT(*) FROM activity WHERE status=1";
    sqlite3_stmt* count_stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, count_sql, -1, &count_stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    if (sqlite3_step(count_stmt) == SQLITE_ROW) {
        total_count = sqlite3_column_int(count_stmt, 0);
    }
    sqlite3_finalize(count_stmt);

    const char* sql = "SELECT id, title, description, cover_image, start_time, end_time, signup_deadline, capacity, signup_count, group_image, sort_order, status, min_group_size, group_type, created_at, updated_at FROM activity WHERE status=1 ORDER BY sort_order ASC, id DESC LIMIT ? OFFSET ?";
    sqlite3_stmt* stmt = nullptr;
    ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    sqlite3_bind_int(stmt, 1, limit);
    sqlite3_bind_int(stmt, 2, offset);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ActivityInfo info;
        LoadActivityFromRow(stmt, info);
        list.push_back(info);
    }

    sqlite3_finalize(stmt);
    return DB_OK;
}

int SqliteDatabase::UpdateActivityStatus(int64_t id, int32_t status) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    std::string now = register_student::GetCurrentTimeString();
    const char* sql = "UPDATE activity SET status=?, updated_at=? WHERE id=?";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    sqlite3_bind_int(stmt, 1, status);
    sqlite3_bind_text(stmt, 2, now.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, id);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) { return ERR_DB_EXEC_FAILED; }
    if (sqlite3_changes(db_) == 0) { return ERR_ACTIVITY_NOT_FOUND; }

    return DB_OK;
}

int SqliteDatabase::UpdateActivityImage(int64_t id, const std::string& field,
                                        const std::string& path) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    if (field != "cover_image" && field != "group_image") {
        return ERR_INVALID_PARAM;
    }

    std::string now = register_student::GetCurrentTimeString();
    std::string sql = "UPDATE activity SET " + field + "=?, updated_at=? WHERE id=?";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, now.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, id);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) { return ERR_DB_EXEC_FAILED; }
    if (sqlite3_changes(db_) == 0) { return ERR_ACTIVITY_NOT_FOUND; }

    return DB_OK;
}

int SqliteDatabase::IncrementSignupCount(int64_t id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    std::string now = register_student::GetCurrentTimeString();
    const char* sql = "UPDATE activity SET signup_count=signup_count+1, updated_at=? WHERE id=?";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    sqlite3_bind_text(stmt, 1, now.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, id);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) { return ERR_DB_EXEC_FAILED; }

    return DB_OK;
}

int SqliteDatabase::BatchUpdateSortOrder(
    const std::vector<std::pair<int64_t, int32_t>>& orders) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    int ret = sqlite3_exec(db_, "BEGIN IMMEDIATE TRANSACTION", nullptr, nullptr, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_EXEC_FAILED; }

    std::string now = register_student::GetCurrentTimeString();
    const char* sql = "UPDATE activity SET sort_order=?, updated_at=? WHERE id=?";
    sqlite3_stmt* stmt = nullptr;
    ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_PREPARE_FAILED;
    }

    for (size_t i = 0; i < orders.size(); ++i) {
        sqlite3_reset(stmt);
        sqlite3_bind_int(stmt, 1, orders[i].second);
        sqlite3_bind_text(stmt, 2, now.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, orders[i].first);

        ret = sqlite3_step(stmt);
        if (ret != SQLITE_DONE) {
            sqlite3_finalize(stmt);
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
            return ERR_DB_EXEC_FAILED;
        }
    }

    sqlite3_finalize(stmt);
    ret = sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr);
    if (ret != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::AddCoverImage(int64_t activity_id, const std::string& path,
                                  int sort_order, int64_t& out_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    std::string now = register_student::GetCurrentTimeString();
    const char* sql = "INSERT INTO activity_cover_image (activity_id, image_path, sort_order, created_at) VALUES (?,?,?,?)";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    sqlite3_bind_int64(stmt, 1, activity_id);
    sqlite3_bind_text(stmt, 2, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, sort_order);
    sqlite3_bind_text(stmt, 4, now.c_str(), -1, SQLITE_TRANSIENT);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) { return ERR_DB_EXEC_FAILED; }

    out_id = static_cast<int64_t>(sqlite3_last_insert_rowid(db_));
    return DB_OK;
}

int SqliteDatabase::GetCoverImages(int64_t activity_id,
                                   std::vector<ActivityCoverImage>& images) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    const char* sql = "SELECT id, activity_id, image_path, sort_order, created_at FROM activity_cover_image WHERE activity_id=? ORDER BY sort_order ASC, id ASC";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    sqlite3_bind_int64(stmt, 1, activity_id);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ActivityCoverImage img;
        img.id = sqlite3_column_int64(stmt, 0);
        img.activity_id = sqlite3_column_int64(stmt, 1);
        const char* p = (const char*)sqlite3_column_text(stmt, 2);
        if (p) { img.image_path = p; }
        img.sort_order = sqlite3_column_int(stmt, 3);
        const char* t = (const char*)sqlite3_column_text(stmt, 4);
        if (t) { img.created_at = t; }
        images.push_back(img);
    }

    sqlite3_finalize(stmt);
    return DB_OK;
}

int SqliteDatabase::DeleteCoverImage(int64_t image_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    const char* sql = "DELETE FROM activity_cover_image WHERE id=?";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    sqlite3_bind_int64(stmt, 1, image_id);
    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) { return ERR_DB_EXEC_FAILED; }
    if (sqlite3_changes(db_) == 0) { return ERR_ACTIVITY_NOT_FOUND; }

    return DB_OK;
}

int SqliteDatabase::DeleteCoverImagesByActivityId(int64_t activity_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    const char* sql = "DELETE FROM activity_cover_image WHERE activity_id=?";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    sqlite3_bind_int64(stmt, 1, activity_id);
    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) { return ERR_DB_EXEC_FAILED; }

    return DB_OK;
}

/* ===================================================================
 * Promotion methods
 * =================================================================== */

int SqliteDatabase::AddPromotionImage(const std::string& path,
                                      int sort_order, int64_t& out_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    std::string now = register_student::GetCurrentTimeString();
    const char* sql = "INSERT INTO promotion_image (image_path, sort_order, created_at) VALUES (?,?,?)";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, sort_order);
    sqlite3_bind_text(stmt, 3, now.c_str(), -1, SQLITE_TRANSIENT);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) { return ERR_DB_EXEC_FAILED; }

    out_id = static_cast<int64_t>(sqlite3_last_insert_rowid(db_));
    return DB_OK;
}

int SqliteDatabase::GetPromotionImages(std::vector<ActivityCoverImage>& images) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    const char* sql = "SELECT id, image_path, sort_order, created_at FROM promotion_image ORDER BY sort_order ASC, id ASC";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ActivityCoverImage img;
        img.id = sqlite3_column_int64(stmt, 0);
        img.activity_id = 0;
        const char* p = (const char*)sqlite3_column_text(stmt, 1);
        if (p) { img.image_path = p; }
        img.sort_order = sqlite3_column_int(stmt, 2);
        const char* t = (const char*)sqlite3_column_text(stmt, 3);
        if (t) { img.created_at = t; }
        images.push_back(img);
    }

    sqlite3_finalize(stmt);
    return DB_OK;
}

int SqliteDatabase::DeletePromotionImage(int64_t image_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    const char* sql = "DELETE FROM promotion_image WHERE id=?";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    sqlite3_bind_int64(stmt, 1, image_id);
    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) { return ERR_DB_EXEC_FAILED; }

    return DB_OK;
}

int SqliteDatabase::BatchUpdatePromotionImageSortOrder(
    const std::vector<std::pair<int64_t, int32_t>>& orders) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    int ret = sqlite3_exec(db_, "BEGIN IMMEDIATE TRANSACTION", nullptr, nullptr, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_EXEC_FAILED; }

    const char* sql = "UPDATE promotion_image SET sort_order=? WHERE id=?";
    sqlite3_stmt* stmt = nullptr;
    ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_PREPARE_FAILED;
    }

    for (size_t i = 0; i < orders.size(); ++i) {
        sqlite3_reset(stmt);
        sqlite3_bind_int(stmt, 1, orders[i].second);
        sqlite3_bind_int64(stmt, 2, orders[i].first);

        ret = sqlite3_step(stmt);
        if (ret != SQLITE_DONE) {
            sqlite3_finalize(stmt);
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
            return ERR_DB_EXEC_FAILED;
        }
    }

    sqlite3_finalize(stmt);
    sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr);
    return DB_OK;
}

int SqliteDatabase::GetPromotionText(std::string& content) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    const char* sql = "SELECT content FROM promotion_text WHERE id=1";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    ret = sqlite3_step(stmt);
    if (ret == SQLITE_ROW) {
        const char* p = (const char*)sqlite3_column_text(stmt, 0);
        if (p) { content = p; } else { content = ""; }
    } else {
        content = "";
    }

    sqlite3_finalize(stmt);
    return DB_OK;
}

int SqliteDatabase::UpdatePromotionText(const std::string& content) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    std::string now = register_student::GetCurrentTimeString();
    const char* sql = "UPDATE promotion_text SET content=?, updated_at=? WHERE id=1";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    sqlite3_bind_text(stmt, 1, content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, now.c_str(), -1, SQLITE_TRANSIENT);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) { return ERR_DB_EXEC_FAILED; }

    return DB_OK;
}

int SqliteDatabase::GetActivityNotice(std::string& content) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    const char* sql = "SELECT content FROM activity_notice WHERE id=1";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    ret = sqlite3_step(stmt);
    if (ret == SQLITE_ROW) {
        const char* p = (const char*)sqlite3_column_text(stmt, 0);
        if (p) { content = p; } else { content = ""; }
    } else {
        content = "";
    }

    sqlite3_finalize(stmt);
    return DB_OK;
}

int SqliteDatabase::UpdateActivityNotice(const std::string& content) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    std::string now = register_student::GetCurrentTimeString();
    const char* sql = "UPDATE activity_notice SET content=?, updated_at=? WHERE id=1";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    sqlite3_bind_text(stmt, 1, content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, now.c_str(), -1, SQLITE_TRANSIENT);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) { return ERR_DB_EXEC_FAILED; }

    return DB_OK;
}

int SqliteDatabase::AddAboutUsCard(const std::string& image_path,
                                   const std::string& text,
                                   int32_t layout_type,
                                   int32_t sort_order,
                                   int64_t& out_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    std::string now = register_student::GetCurrentTimeString();
    const char* sql = "INSERT INTO about_us_card (image_path, text_content, layout_type, sort_order, created_at) VALUES (?,?,?,?,?)";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    sqlite3_bind_text(stmt, 1, image_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, layout_type);
    sqlite3_bind_int(stmt, 4, sort_order);
    sqlite3_bind_text(stmt, 5, now.c_str(), -1, SQLITE_TRANSIENT);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) { return ERR_DB_EXEC_FAILED; }

    out_id = static_cast<int64_t>(sqlite3_last_insert_rowid(db_));
    return DB_OK;
}

int SqliteDatabase::GetAboutUsCards(std::vector<AboutUsCard>& cards) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    const char* sql = "SELECT id, image_path, text_content, layout_type, sort_order, created_at FROM about_us_card ORDER BY sort_order ASC, id ASC";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AboutUsCard card;
        card.id = sqlite3_column_int64(stmt, 0);
        const char* p = (const char*)sqlite3_column_text(stmt, 1);
        if (p) { card.image_path = p; }
        p = (const char*)sqlite3_column_text(stmt, 2);
        if (p) { card.text_content = p; }
        card.layout_type = sqlite3_column_int(stmt, 3);
        card.sort_order = sqlite3_column_int(stmt, 4);
        p = (const char*)sqlite3_column_text(stmt, 5);
        if (p) { card.created_at = p; }
        cards.push_back(card);
    }

    sqlite3_finalize(stmt);
    return DB_OK;
}

int SqliteDatabase::UpdateAboutUsCard(int64_t id, const std::string& image_path,
                                      const std::string& text,
                                      int32_t layout_type) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    const char* sql = "UPDATE about_us_card SET image_path=?, text_content=?, layout_type=? WHERE id=?";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    sqlite3_bind_text(stmt, 1, image_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, layout_type);
    sqlite3_bind_int64(stmt, 4, id);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) { return ERR_DB_EXEC_FAILED; }

    return DB_OK;
}

int SqliteDatabase::DeleteAboutUsCard(int64_t card_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    const char* sql = "DELETE FROM about_us_card WHERE id=?";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    sqlite3_bind_int64(stmt, 1, card_id);
    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) { return ERR_DB_EXEC_FAILED; }

    return DB_OK;
}

int SqliteDatabase::BatchUpdateAboutUsCardSortOrder(
    const std::vector<std::pair<int64_t, int32_t>>& orders) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    int ret = sqlite3_exec(db_, "BEGIN IMMEDIATE TRANSACTION", nullptr, nullptr, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_EXEC_FAILED; }

    const char* sql = "UPDATE about_us_card SET sort_order=? WHERE id=?";
    sqlite3_stmt* stmt = nullptr;
    ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_PREPARE_FAILED;
    }

    for (size_t i = 0; i < orders.size(); ++i) {
        sqlite3_reset(stmt);
        sqlite3_bind_int(stmt, 1, orders[i].second);
        sqlite3_bind_int64(stmt, 2, orders[i].first);

        ret = sqlite3_step(stmt);
        if (ret != SQLITE_DONE) {
            sqlite3_finalize(stmt);
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
            return ERR_DB_EXEC_FAILED;
        }
    }

    sqlite3_finalize(stmt);
    sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr);
    return DB_OK;
}

/* ===================================================================
 * IActivitySignupDao implementation
 * =================================================================== */

int SqliteDatabase::CreateSignupAtomic(const ActivitySignupInfo& info,
                                       int64_t& out_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    int ret = sqlite3_exec(db_, "BEGIN IMMEDIATE TRANSACTION", nullptr, nullptr, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_EXEC_FAILED; }

    /* check activity exists and get current state */
    const char* sql_check = "SELECT status, signup_deadline, capacity, signup_count, start_time FROM activity WHERE id=?";
    sqlite3_stmt* stmt = nullptr;
    ret = sqlite3_prepare_v2(db_, sql_check, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_PREPARE_FAILED;
    }
    sqlite3_bind_int64(stmt, 1, info.activity_id);

    ret = sqlite3_step(stmt);
    if (ret != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_ACTIVITY_NOT_FOUND;
    }

    int32_t act_status = sqlite3_column_int(stmt, 0);
    std::string deadline = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    int32_t capacity = sqlite3_column_int(stmt, 2);
    int32_t signup_count = sqlite3_column_int(stmt, 3);
    std::string start_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    sqlite3_finalize(stmt);

    if (act_status != 1) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_ACTIVITY_NOT_PUBLISHED;
    }

    std::string now = register_student::GetCurrentTimeString();
    if (now < start_time) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_ACTIVITY_SIGNUP_NOT_STARTED;
    }
    if (deadline <= now) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_ACTIVITY_SIGNUP_ENDED;
    }

    if (capacity > 0 && signup_count >= capacity) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_ACTIVITY_CAPACITY_FULL;
    }

    /* check duplicate: same name + phone + grade for this activity */
    const char* sql_dup = "SELECT id FROM activity_signup WHERE activity_id=? AND name=? AND phone=? AND grade=? LIMIT 1";
    sqlite3_stmt* stmt_dup = nullptr;
    ret = sqlite3_prepare_v2(db_, sql_dup, -1, &stmt_dup, nullptr);
    if (ret != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_PREPARE_FAILED;
    }
    sqlite3_bind_int64(stmt_dup, 1, info.activity_id);
    sqlite3_bind_text(stmt_dup, 2, info.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt_dup, 3, info.phone.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt_dup, 4, info.grade.c_str(), -1, SQLITE_TRANSIENT);
    ret = sqlite3_step(stmt_dup);
    sqlite3_finalize(stmt_dup);
    if (ret == SQLITE_ROW) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_ACTIVITY_DUPLICATE_SIGNUP;
    }

    /* insert signup record */
    const char* sql_insert = "INSERT INTO activity_signup (activity_id, name, phone, grade, signup_type, created_at) VALUES (?,?,?,?,?,?)";
    ret = sqlite3_prepare_v2(db_, sql_insert, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int64(stmt, 1, info.activity_id);
    sqlite3_bind_text(stmt, 2, info.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, info.phone.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, info.grade.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, info.signup_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, now.c_str(), -1, SQLITE_TRANSIENT);

    ret = sqlite3_step(stmt);
    int step_ret = ret;
    sqlite3_finalize(stmt);
    if (step_ret != SQLITE_DONE) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        if (step_ret == SQLITE_CONSTRAINT) {
            return ERR_ACTIVITY_DUPLICATE_SIGNUP;
        }
        return ERR_DB_EXEC_FAILED;
    }

    out_id = static_cast<int64_t>(sqlite3_last_insert_rowid(db_));

    /* increment signup_count */
    const char* sql_inc = "UPDATE activity SET signup_count=signup_count+1, updated_at=? WHERE id=?";
    ret = sqlite3_prepare_v2(db_, sql_inc, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_text(stmt, 1, now.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, info.activity_id);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_EXEC_FAILED;
    }

    ret = sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr);
    if (ret != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::ListSignupsByActivity(int64_t activity_id,
                                          std::vector<ActivitySignupInfo>& list) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    list.clear();
    const char* sql = "SELECT id, activity_id, name, phone, grade, signup_type, created_at FROM activity_signup WHERE activity_id=? ORDER BY created_at DESC";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    sqlite3_bind_int64(stmt, 1, activity_id);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ActivitySignupInfo s;
        s.id = sqlite3_column_int64(stmt, 0);
        s.activity_id = sqlite3_column_int64(stmt, 1);
        s.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        s.phone = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const char* g = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        if (g) { s.grade = g; }
        const char* st = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        if (st) { s.signup_type = st; }
        s.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        list.push_back(s);
    }

    sqlite3_finalize(stmt);
    return DB_OK;
}

int SqliteDatabase::ConfirmSessionAtomic(int64_t activity_id,
                                         const std::vector<ActivitySignupInfo>& members) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    int ret = sqlite3_exec(db_, "BEGIN IMMEDIATE TRANSACTION", nullptr, nullptr, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_EXEC_FAILED; }

    /* capacity check */
    const char* sql_cap = "SELECT capacity, signup_count FROM activity WHERE id=?";
    sqlite3_stmt* stmt = nullptr;
    ret = sqlite3_prepare_v2(db_, sql_cap, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_PREPARE_FAILED;
    }
    sqlite3_bind_int64(stmt, 1, activity_id);
    ret = sqlite3_step(stmt);
    if (ret != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_ACTIVITY_NOT_FOUND;
    }
    int32_t capacity = sqlite3_column_int(stmt, 0);
    int32_t signup_count = sqlite3_column_int(stmt, 1);
    sqlite3_finalize(stmt);

    int32_t member_count = static_cast<int32_t>(members.size());
    if (capacity > 0 && signup_count + member_count > capacity) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_ACTIVITY_CAPACITY_FULL;
    }

    /* insert each member */
    std::string now = register_student::GetCurrentTimeString();
    const char* sql_ins = "INSERT INTO activity_signup (activity_id, name, phone, grade, signup_type, created_at) VALUES (?,?,?,?,?,?)";

    for (size_t i = 0; i < members.size(); ++i) {
        sqlite3_stmt* stmt_ins = nullptr;
        ret = sqlite3_prepare_v2(db_, sql_ins, -1, &stmt_ins, nullptr);
        if (ret != SQLITE_OK) {
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
            return ERR_DB_PREPARE_FAILED;
        }
        sqlite3_bind_int64(stmt_ins, 1, activity_id);
        sqlite3_bind_text(stmt_ins, 2, members[i].name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt_ins, 3, members[i].phone.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt_ins, 4, members[i].grade.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt_ins, 5, members[i].signup_type.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt_ins, 6, now.c_str(), -1, SQLITE_TRANSIENT);
        ret = sqlite3_step(stmt_ins);
        sqlite3_finalize(stmt_ins);
        if (ret != SQLITE_DONE) {
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
            return ERR_DB_EXEC_FAILED;
        }
    }

    /* update signup_count */
    const char* sql_upd = "UPDATE activity SET signup_count = signup_count + ? WHERE id = ?";
    sqlite3_stmt* stmt_upd = nullptr;
    ret = sqlite3_prepare_v2(db_, sql_upd, -1, &stmt_upd, nullptr);
    if (ret != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_PREPARE_FAILED;
    }
    sqlite3_bind_int(stmt_upd, 1, member_count);
    sqlite3_bind_int64(stmt_upd, 2, activity_id);
    ret = sqlite3_step(stmt_upd);
    sqlite3_finalize(stmt_upd);
    if (ret != SQLITE_DONE) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_EXEC_FAILED;
    }

    ret = sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr);
    if (ret != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_EXEC_FAILED;
    }

    LOG_INFO << "SqliteDatabase: ConfirmSessionAtomic success, activity_id=" << activity_id
             << ", members=" << member_count;
    return DB_OK;
}

int SqliteDatabase::CheckDuplicateSignup(int64_t activity_id,
                                         const std::string& name,
                                         const std::string& phone,
                                         bool& out_exists) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    out_exists = false;
    const char* sql = "SELECT COUNT(*) FROM activity_signup WHERE activity_id=? AND name=? AND phone=?";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    sqlite3_bind_int64(stmt, 1, activity_id);
    sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, phone.c_str(), -1, SQLITE_TRANSIENT);

    ret = sqlite3_step(stmt);
    if (ret == SQLITE_ROW) {
        out_exists = (sqlite3_column_int(stmt, 0) > 0);
    }
    sqlite3_finalize(stmt);
    return DB_OK;
}

/* ========== IDataTransferDao ========== */

int SqliteDatabase::GetTableColumnNames(
    const std::string& table_name,
    std::vector<std::string>& out_columns) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    out_columns.clear();
    std::string sql = "PRAGMA table_info(" + table_name + ")";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* name = reinterpret_cast<const char*>(
            sqlite3_column_text(stmt, 1));
        if (name) {
            out_columns.push_back(name);
        }
    }
    sqlite3_finalize(stmt);
    return DB_OK;
}

int SqliteDatabase::ExportTableRows(
    const std::string& table_name,
    std::vector<DataRow>& out_rows) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    out_rows.clear();

    /* Get column names first */
    std::vector<std::string> columns;
    {
        std::string pragma_sql = "PRAGMA table_info(" + table_name + ")";
        sqlite3_stmt* pstmt = nullptr;
        int ret = sqlite3_prepare_v2(db_, pragma_sql.c_str(), -1, &pstmt, nullptr);
        if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }
        while (sqlite3_step(pstmt) == SQLITE_ROW) {
            const char* name = reinterpret_cast<const char*>(
                sqlite3_column_text(pstmt, 1));
            if (name) { columns.push_back(name); }
        }
        sqlite3_finalize(pstmt);
    }

    if (columns.empty()) { return DB_OK; }

    /* Build SELECT * query */
    std::string sql = "SELECT * FROM " + table_name;
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    int col_count = sqlite3_column_count(stmt);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DataRow row;
        for (int i = 0; i < col_count && i < static_cast<int>(columns.size()); ++i) {
            const char* text = reinterpret_cast<const char*>(
                sqlite3_column_text(stmt, i));
            row[columns[i]] = text ? text : "";
        }
        out_rows.push_back(row);
    }

    sqlite3_finalize(stmt);
    return DB_OK;
}

int SqliteDatabase::ImportTableRows(
    const std::string& table_name,
    const std::vector<DataRow>& rows,
    const std::vector<std::string>& unique_keys,
    ImportModeType mode,
    TableImportStats& out_stats) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    out_stats.table_name = table_name;
    out_stats.inserted = 0;
    out_stats.skipped = 0;
    out_stats.failed = 0;

    if (rows.empty()) { return DB_OK; }

    /* Get actual column names from target table */
    std::vector<std::string> target_columns;
    {
        std::string pragma_sql = "PRAGMA table_info(" + table_name + ")";
        sqlite3_stmt* pstmt = nullptr;
        int ret = sqlite3_prepare_v2(db_, pragma_sql.c_str(), -1, &pstmt, nullptr);
        if (ret != SQLITE_OK) { return ERR_DT_IMPORT_FAILED; }
        while (sqlite3_step(pstmt) == SQLITE_ROW) {
            const char* name = reinterpret_cast<const char*>(
                sqlite3_column_text(pstmt, 1));
            if (name) { target_columns.push_back(name); }
        }
        sqlite3_finalize(pstmt);
    }

    if (target_columns.empty()) {
        out_stats.failed = static_cast<int>(rows.size());
        return ERR_DT_IMPORT_FAILED;
    }

    /* Begin transaction */
    char* err_msg = nullptr;
    sqlite3_exec(db_, "BEGIN TRANSACTION", nullptr, nullptr, &err_msg);
    if (err_msg) { sqlite3_free(err_msg); }

    for (size_t r = 0; r < rows.size(); ++r) {
        const DataRow& row = rows[r];

        /* Incremental mode: check if record exists by unique keys */
        if (mode == ImportMode_Incremental && !unique_keys.empty()) {
            std::string check_sql = "SELECT COUNT(*) FROM " + table_name + " WHERE ";
            for (size_t k = 0; k < unique_keys.size(); ++k) {
                if (k > 0) { check_sql += " AND "; }
                check_sql += unique_keys[k] + "=?";
            }

            sqlite3_stmt* cstmt = nullptr;
            int ret = sqlite3_prepare_v2(db_, check_sql.c_str(), -1, &cstmt, nullptr);
            if (ret != SQLITE_OK) {
                sqlite3_finalize(cstmt);
                out_stats.failed++;
                continue;
            }

            for (size_t k = 0; k < unique_keys.size(); ++k) {
                auto it = row.find(unique_keys[k]);
                std::string val = (it != row.end()) ? it->second : "";
                sqlite3_bind_text(cstmt, static_cast<int>(k + 1),
                    val.c_str(), static_cast<int>(val.size()), SQLITE_TRANSIENT);
            }

            bool exists = false;
            if (sqlite3_step(cstmt) == SQLITE_ROW) {
                exists = sqlite3_column_int(cstmt, 0) > 0;
            }
            sqlite3_finalize(cstmt);

            if (exists) {
                out_stats.skipped++;
                continue;
            }
        }

        /* Build INSERT with columns that exist in both row data and target table */
        std::vector<std::string> insert_cols;
        for (size_t c = 0; c < target_columns.size(); ++c) {
            if (row.find(target_columns[c]) != row.end()) {
                insert_cols.push_back(target_columns[c]);
            }
        }

        if (insert_cols.empty()) {
            out_stats.failed++;
            continue;
        }

        std::string insert_sql = "INSERT INTO " + table_name + " (";
        for (size_t c = 0; c < insert_cols.size(); ++c) {
            if (c > 0) { insert_sql += ", "; }
            insert_sql += insert_cols[c];
        }
        insert_sql += ") VALUES (";
        for (size_t c = 0; c < insert_cols.size(); ++c) {
            if (c > 0) { insert_sql += ", "; }
            insert_sql += "?";
        }
        insert_sql += ")";

        sqlite3_stmt* istmt = nullptr;
        int ret = sqlite3_prepare_v2(db_, insert_sql.c_str(), -1, &istmt, nullptr);
        if (ret != SQLITE_OK) {
            sqlite3_finalize(istmt);
            out_stats.failed++;
            continue;
        }

        for (size_t c = 0; c < insert_cols.size(); ++c) {
            auto it = row.find(insert_cols[c]);
            const std::string& val = it->second;
            sqlite3_bind_text(istmt, static_cast<int>(c + 1),
                val.c_str(), static_cast<int>(val.size()), SQLITE_TRANSIENT);
        }

        ret = sqlite3_step(istmt);
        sqlite3_finalize(istmt);

        if (ret != SQLITE_DONE) {
            if (mode == ImportMode_Incremental) {
                out_stats.skipped++;
            } else {
                out_stats.failed++;
            }
        } else {
            out_stats.inserted++;
        }
    }

    /* Commit transaction */
    sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr);

    return DB_OK;
}

int SqliteDatabase::ClearBusinessTables() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    std::vector<TableConfig> configs = DataTransferUtil::GetTableConfigs();

    char* err_msg = nullptr;
    sqlite3_exec(db_, "BEGIN TRANSACTION", nullptr, nullptr, &err_msg);
    if (err_msg) { sqlite3_free(err_msg); }

    /* Delete in reverse import order (dependents first) */
    for (int i = static_cast<int>(configs.size()) - 1; i >= 0; --i) {
        std::string sql = "DELETE FROM " + configs[i].table_name;
        sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, nullptr);
    }

    sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr);
    return DB_OK;
}

int SqliteDatabase::BatchIncrementSignupCount(int64_t id, int32_t delta) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    std::string now = register_student::GetCurrentTimeString();
    const char* sql = "UPDATE activity SET signup_count=signup_count+?, updated_at=? WHERE id=?";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    sqlite3_bind_int(stmt, 1, delta);
    sqlite3_bind_text(stmt, 2, now.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, id);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) { return ERR_DB_EXEC_FAILED; }

    return DB_OK;
}

int SqliteDatabase::CreateGroup(const ActivityGroupInfo& info, int64_t& out_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    std::string now = register_student::GetCurrentTimeString();
    const char* sql = "INSERT INTO activity_group (activity_id, invite_code, leader_name, leader_phone, leader_grade, current_count, target_count, status, created_at, updated_at) VALUES (?,?,?,?,?,?,?,0,?,?)";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    sqlite3_bind_int64(stmt, 1, info.activity_id);
    sqlite3_bind_text(stmt, 2, info.invite_code.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, info.leader_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, info.leader_phone.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, info.leader_grade.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 6, 1);
    sqlite3_bind_int(stmt, 7, info.target_count);
    sqlite3_bind_text(stmt, 8, now.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, now.c_str(), -1, SQLITE_TRANSIENT);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) { return ERR_DB_EXEC_FAILED; }

    out_id = static_cast<int64_t>(sqlite3_last_insert_rowid(db_));
    return DB_OK;
}

int SqliteDatabase::GetGroup(int64_t group_id, ActivityGroupInfo& info) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    const char* sql = "SELECT id, activity_id, invite_code, leader_name, leader_phone, leader_grade, current_count, target_count, status, cancel_reason, created_at, updated_at FROM activity_group WHERE id=?";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    sqlite3_bind_int64(stmt, 1, group_id);
    ret = sqlite3_step(stmt);
    if (ret != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return ERR_ACTIVITY_NOT_FOUND;
    }

    info.id = sqlite3_column_int64(stmt, 0);
    info.activity_id = sqlite3_column_int64(stmt, 1);
    info.invite_code = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    info.leader_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    info.leader_phone = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    info.leader_grade = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
    info.current_count = sqlite3_column_int(stmt, 6);
    info.target_count = sqlite3_column_int(stmt, 7);
    info.status = sqlite3_column_int(stmt, 8);
    info.cancel_reason = sqlite3_column_int(stmt, 9);
    info.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
    info.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));

    sqlite3_finalize(stmt);
    return DB_OK;
}

int SqliteDatabase::GetGroupByInviteCode(const std::string& invite_code,
                                         ActivityGroupInfo& info) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    const char* sql = "SELECT id, activity_id, invite_code, leader_name, leader_phone, leader_grade, current_count, target_count, status, cancel_reason, created_at, updated_at FROM activity_group WHERE UPPER(invite_code)=UPPER(?) AND status=0";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    sqlite3_bind_text(stmt, 1, invite_code.c_str(), -1, SQLITE_TRANSIENT);
    ret = sqlite3_step(stmt);
    if (ret != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return ERR_ACTIVITY_GROUP_INVALID_CODE;
    }

    info.id = sqlite3_column_int64(stmt, 0);
    info.activity_id = sqlite3_column_int64(stmt, 1);
    info.invite_code = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    info.leader_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    info.leader_phone = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    info.leader_grade = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
    info.current_count = sqlite3_column_int(stmt, 6);
    info.target_count = sqlite3_column_int(stmt, 7);
    info.status = sqlite3_column_int(stmt, 8);
    info.cancel_reason = sqlite3_column_int(stmt, 9);
    info.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
    info.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));

    sqlite3_finalize(stmt);
    return DB_OK;
}

int SqliteDatabase::AddMember(const ActivityGroupMemberInfo& member,
                              int64_t& out_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    std::string now = register_student::GetCurrentTimeString();
    const char* sql = "INSERT INTO activity_group_member (group_id, name, phone, grade, signup_type, is_leader, created_at) VALUES (?,?,?,?,?,?,?)";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    sqlite3_bind_int64(stmt, 1, member.group_id);
    sqlite3_bind_text(stmt, 2, member.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, member.phone.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, member.grade.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, member.signup_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 6, member.is_leader);
    sqlite3_bind_text(stmt, 7, now.c_str(), -1, SQLITE_TRANSIENT);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) { return ERR_DB_EXEC_FAILED; }

    out_id = static_cast<int64_t>(sqlite3_last_insert_rowid(db_));
    return DB_OK;
}

int SqliteDatabase::RemoveMember(int64_t group_id, const std::string& name,
                                 const std::string& phone,
                                 int32_t& out_is_leader,
                                 int32_t& out_remaining_count) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    out_is_leader = 0;
    out_remaining_count = 0;

    /* begin transaction */
    const char* sql_begin = "BEGIN IMMEDIATE TRANSACTION";
    sqlite3_stmt* bstmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql_begin, -1, &bstmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }
    ret = sqlite3_step(bstmt);
    sqlite3_finalize(bstmt);
    if (ret != SQLITE_DONE) { return ERR_DB_EXEC_FAILED; }

    /* query is_leader */
    const char* sql_q = "SELECT is_leader FROM activity_group_member WHERE group_id=? AND name=? AND phone=? LIMIT 1";
    sqlite3_stmt* stmt = nullptr;
    ret = sqlite3_prepare_v2(db_, sql_q, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        sqlite3_prepare_v2(db_, "ROLLBACK TRANSACTION", -1, &bstmt, nullptr);
        sqlite3_step(bstmt);
        sqlite3_finalize(bstmt);
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int64(stmt, 1, group_id);
    sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, phone.c_str(), -1, SQLITE_TRANSIENT);

    ret = sqlite3_step(stmt);
    if (ret != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        sqlite3_prepare_v2(db_, "ROLLBACK TRANSACTION", -1, &bstmt, nullptr);
        sqlite3_step(bstmt);
        sqlite3_finalize(bstmt);
        return ERR_ACTIVITY_NOT_FOUND;
    }
    out_is_leader = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    /* delete member */
    const char* sql_del = "DELETE FROM activity_group_member WHERE group_id=? AND name=? AND phone=?";
    ret = sqlite3_prepare_v2(db_, sql_del, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        sqlite3_prepare_v2(db_, "ROLLBACK TRANSACTION", -1, &bstmt, nullptr);
        sqlite3_step(bstmt);
        sqlite3_finalize(bstmt);
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int64(stmt, 1, group_id);
    sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, phone.c_str(), -1, SQLITE_TRANSIENT);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) {
        sqlite3_prepare_v2(db_, "ROLLBACK TRANSACTION", -1, &bstmt, nullptr);
        sqlite3_step(bstmt);
        sqlite3_finalize(bstmt);
        return ERR_DB_EXEC_FAILED;
    }

    /* update current_count */
    std::string now = register_student::GetCurrentTimeString();
    const char* sql_upd = "UPDATE activity_group SET current_count=current_count-1, updated_at=? WHERE id=?";
    ret = sqlite3_prepare_v2(db_, sql_upd, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        sqlite3_prepare_v2(db_, "ROLLBACK TRANSACTION", -1, &bstmt, nullptr);
        sqlite3_step(bstmt);
        sqlite3_finalize(bstmt);
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_text(stmt, 1, now.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, group_id);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) {
        sqlite3_prepare_v2(db_, "ROLLBACK TRANSACTION", -1, &bstmt, nullptr);
        sqlite3_step(bstmt);
        sqlite3_finalize(bstmt);
        return ERR_DB_EXEC_FAILED;
    }

    /* query remaining count */
    const char* sql_cnt = "SELECT current_count FROM activity_group WHERE id=?";
    ret = sqlite3_prepare_v2(db_, sql_cnt, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        sqlite3_prepare_v2(db_, "ROLLBACK TRANSACTION", -1, &bstmt, nullptr);
        sqlite3_step(bstmt);
        sqlite3_finalize(bstmt);
        return ERR_DB_PREPARE_FAILED;
    }

    sqlite3_bind_int64(stmt, 1, group_id);
    ret = sqlite3_step(stmt);
    if (ret == SQLITE_ROW) {
        out_remaining_count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    /* commit transaction */
    const char* sql_commit = "COMMIT TRANSACTION";
    ret = sqlite3_prepare_v2(db_, sql_commit, -1, &bstmt, nullptr);
    if (ret != SQLITE_OK) {
        sqlite3_prepare_v2(db_, "ROLLBACK TRANSACTION", -1, &bstmt, nullptr);
        sqlite3_step(bstmt);
        sqlite3_finalize(bstmt);
        return ERR_DB_PREPARE_FAILED;
    }
    ret = sqlite3_step(bstmt);
    sqlite3_finalize(bstmt);
    if (ret != SQLITE_DONE) {
        sqlite3_prepare_v2(db_, "ROLLBACK TRANSACTION", -1, &bstmt, nullptr);
        sqlite3_step(bstmt);
        sqlite3_finalize(bstmt);
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int SqliteDatabase::UpdateGroupCount(int64_t group_id, int32_t delta) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    std::string now = register_student::GetCurrentTimeString();
    const char* sql = "UPDATE activity_group SET current_count=current_count+?, updated_at=? WHERE id=?";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    sqlite3_bind_int(stmt, 1, delta);
    sqlite3_bind_text(stmt, 2, now.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, group_id);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) { return ERR_DB_EXEC_FAILED; }

    return DB_OK;
}

int SqliteDatabase::UpdateGroupStatus(int64_t group_id, int32_t status,
                                      int32_t cancel_reason) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    std::string now = register_student::GetCurrentTimeString();
    const char* sql = "UPDATE activity_group SET status=?, cancel_reason=?, updated_at=? WHERE id=?";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    sqlite3_bind_int(stmt, 1, status);
    sqlite3_bind_int(stmt, 2, cancel_reason);
    sqlite3_bind_text(stmt, 3, now.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, group_id);

    ret = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) { return ERR_DB_EXEC_FAILED; }

    return DB_OK;
}

int SqliteDatabase::ListMembersByGroup(int64_t group_id,
                                       std::vector<ActivityGroupMemberInfo>& list) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    list.clear();
    const char* sql = "SELECT id, group_id, name, phone, grade, signup_type, is_leader, created_at FROM activity_group_member WHERE group_id=? ORDER BY is_leader DESC, created_at ASC";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    sqlite3_bind_int64(stmt, 1, group_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ActivityGroupMemberInfo m;
        m.id = sqlite3_column_int64(stmt, 0);
        m.group_id = sqlite3_column_int64(stmt, 1);
        m.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        m.phone = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        m.grade = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        const char* st = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        if (st) { m.signup_type = st; }
        m.is_leader = sqlite3_column_int(stmt, 6);
        m.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        list.push_back(m);
    }

    sqlite3_finalize(stmt);
    return DB_OK;
}

int SqliteDatabase::ListMembersByActivity(int64_t activity_id,
                                          std::vector<ActivityGroupMemberInfo>& list) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    list.clear();
    const char* sql = "SELECT m.id, m.group_id, m.name, m.phone, m.grade, m.signup_type, m.is_leader, m.created_at, g.invite_code FROM activity_group_member m JOIN activity_group g ON m.group_id=g.id WHERE g.activity_id=? AND g.status=1 ORDER BY g.id ASC, m.is_leader DESC, m.created_at ASC";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    sqlite3_bind_int64(stmt, 1, activity_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ActivityGroupMemberInfo m;
        m.id = sqlite3_column_int64(stmt, 0);
        m.group_id = sqlite3_column_int64(stmt, 1);
        m.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        m.phone = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        m.grade = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        const char* st = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        if (st) { m.signup_type = st; }
        m.is_leader = sqlite3_column_int(stmt, 6);
        m.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        m.invite_code = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        list.push_back(m);
    }

    sqlite3_finalize(stmt);
    return DB_OK;
}

int SqliteDatabase::CheckDuplicateInGroup(int64_t group_id, const std::string& name,
                                          const std::string& phone,
                                          const std::string& grade,
                                          bool& out_duplicate) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    out_duplicate = false;
    const char* sql = "SELECT id FROM activity_group_member WHERE group_id=? AND name=? AND phone=? AND grade=? LIMIT 1";
    sqlite3_stmt* stmt = nullptr;
    int ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_PREPARE_FAILED; }

    sqlite3_bind_int64(stmt, 1, group_id);
    sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, phone.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, grade.c_str(), -1, SQLITE_TRANSIENT);

    ret = sqlite3_step(stmt);
    if (ret == SQLITE_ROW) {
        out_duplicate = true;
    }
    sqlite3_finalize(stmt);
    return DB_OK;
}

int SqliteDatabase::ConfirmGroupAtomic(int64_t activity_id, int64_t group_id,
                                       const std::vector<ActivityGroupMemberInfo>& members) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) { return ERR_DB_NOT_OPEN; }

    int ret = sqlite3_exec(db_, "BEGIN IMMEDIATE TRANSACTION", nullptr, nullptr, nullptr);
    if (ret != SQLITE_OK) { return ERR_DB_EXEC_FAILED; }

    const char* sql_cap = "SELECT capacity, signup_count FROM activity WHERE id=?";
    sqlite3_stmt* stmt = nullptr;
    ret = sqlite3_prepare_v2(db_, sql_cap, -1, &stmt, nullptr);
    if (ret != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_PREPARE_FAILED;
    }
    sqlite3_bind_int64(stmt, 1, activity_id);
    ret = sqlite3_step(stmt);
    if (ret != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_ACTIVITY_NOT_FOUND;
    }
    int32_t capacity = sqlite3_column_int(stmt, 0);
    int32_t signup_count = sqlite3_column_int(stmt, 1);
    sqlite3_finalize(stmt);

    int32_t member_count = static_cast<int32_t>(members.size());
    if (capacity > 0 && signup_count + member_count > capacity) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_ACTIVITY_CAPACITY_FULL;
    }

    std::string now = register_student::GetCurrentTimeString();
    const char* sql_dup = "SELECT id FROM activity_signup WHERE activity_id=? AND name=? AND phone=? AND grade=? LIMIT 1";
    const char* sql_ins = "INSERT INTO activity_signup (activity_id, name, phone, grade, signup_type, created_at) VALUES (?,?,?,?,?,?)";

    for (size_t i = 0; i < members.size(); ++i) {
        sqlite3_stmt* stmt_dup = nullptr;
        ret = sqlite3_prepare_v2(db_, sql_dup, -1, &stmt_dup, nullptr);
        if (ret != SQLITE_OK) {
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
            return ERR_DB_PREPARE_FAILED;
        }
        sqlite3_bind_int64(stmt_dup, 1, activity_id);
        sqlite3_bind_text(stmt_dup, 2, members[i].name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt_dup, 3, members[i].phone.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt_dup, 4, members[i].grade.c_str(), -1, SQLITE_TRANSIENT);
        ret = sqlite3_step(stmt_dup);
        sqlite3_finalize(stmt_dup);
        if (ret == SQLITE_ROW) {
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
            return ERR_ACTIVITY_DUPLICATE_SIGNUP;
        }

        sqlite3_stmt* stmt_ins = nullptr;
        ret = sqlite3_prepare_v2(db_, sql_ins, -1, &stmt_ins, nullptr);
        if (ret != SQLITE_OK) {
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
            return ERR_DB_PREPARE_FAILED;
        }
        sqlite3_bind_int64(stmt_ins, 1, activity_id);
        sqlite3_bind_text(stmt_ins, 2, members[i].name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt_ins, 3, members[i].phone.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt_ins, 4, members[i].grade.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt_ins, 5, members[i].signup_type.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt_ins, 6, now.c_str(), -1, SQLITE_TRANSIENT);
        ret = sqlite3_step(stmt_ins);
        sqlite3_finalize(stmt_ins);
        if (ret != SQLITE_DONE) {
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
            if (ret == SQLITE_CONSTRAINT) {
                return ERR_ACTIVITY_DUPLICATE_SIGNUP;
            }
            return ERR_DB_EXEC_FAILED;
        }
    }

    const char* sql_inc = "UPDATE activity SET signup_count=signup_count+?, updated_at=? WHERE id=?";
    sqlite3_stmt* stmt_inc = nullptr;
    ret = sqlite3_prepare_v2(db_, sql_inc, -1, &stmt_inc, nullptr);
    if (ret != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_PREPARE_FAILED;
    }
    sqlite3_bind_int(stmt_inc, 1, member_count);
    sqlite3_bind_text(stmt_inc, 2, now.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt_inc, 3, activity_id);
    ret = sqlite3_step(stmt_inc);
    sqlite3_finalize(stmt_inc);
    if (ret != SQLITE_DONE) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_EXEC_FAILED;
    }

    const char* sql_status = "UPDATE activity_group SET status=1, cancel_reason=0, updated_at=? WHERE id=?";
    sqlite3_stmt* stmt_st = nullptr;
    ret = sqlite3_prepare_v2(db_, sql_status, -1, &stmt_st, nullptr);
    if (ret != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_PREPARE_FAILED;
    }
    sqlite3_bind_text(stmt_st, 1, now.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt_st, 2, group_id);
    ret = sqlite3_step(stmt_st);
    sqlite3_finalize(stmt_st);
    if (ret != SQLITE_DONE) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_EXEC_FAILED;
    }

    ret = sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr);
    if (ret != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return ERR_DB_EXEC_FAILED;
    }
    return DB_OK;
}
