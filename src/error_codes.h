#ifndef __ERROR_CODES_H__
#define __ERROR_CODES_H__

enum ErrorCodeType {
    DB_OK                           = 0,
    ERR_INVALID_PARAM               = 1001,
    /* Activity errors (1100-1199) */
    ERR_ACTIVITY_NOT_FOUND          = 1101,
    ERR_ACTIVITY_CAPACITY_FULL      = 1102,
    ERR_ACTIVITY_DUPLICATE_SIGNUP   = 1103,
    ERR_ACTIVITY_SIGNUP_ENDED       = 1104,
    ERR_ACTIVITY_NOT_PUBLISHED      = 1105,
    ERR_ACTIVITY_TITLE_EMPTY        = 1106,
    ERR_ACTIVITY_TIME_INVALID       = 1107,
    ERR_ACTIVITY_PHONE_INVALID      = 1108,
    ERR_ACTIVITY_NAME_EMPTY         = 1109,
    ERR_ACTIVITY_COVER_REQUIRED     = 1110,
    ERR_ACTIVITY_DEADLINE_INVALID   = 1111,
    ERR_ACTIVITY_GROUP_INVALID_CODE = 1112,
    ERR_ACTIVITY_GROUP_CONFIRMED    = 1113,
    ERR_ACTIVITY_GROUP_CANCELLED    = 1114,
    ERR_ACTIVITY_GROUP_TIMEOUT      = 1115,
    ERR_ACTIVITY_GROUP_NOT_LEADER   = 1116,
    ERR_ACTIVITY_NOT_GROUP_MODE     = 1117,
    ERR_ACTIVITY_SIGNUP_NOT_STARTED = 1118,
    ERR_ACTIVITY_GROUP_SESSION_EXPIRED  = 1119,  /* session group expired (type=1 unified) */
    ERR_ACTIVITY_GROUP_CAPACITY_INSUFFICIENT = 1120,  /* activity capacity insufficient for group */
    ERR_HANDLER_NULL_DAO            = 2001,
    ERR_AUTH_INVALID_CREDENTIALS    = 2002,
    ERR_AUTH_ADMIN_EXISTS           = 2003,
    ERR_AUTH_SESSION_EXPIRED        = 2004,
    ERR_AUTH_PERMISSION_DENIED      = 2005,
    ERR_AUTH_RESET_PENDING          = 2006,
    ERR_AUTH_USER_NOT_FOUND         = 2007,
    ERR_AUTH_ADMIN_NOT_EXISTS       = 2008,  /* admin not exists, cannot register teacher */
    ERR_AUTH_ACCOUNT_PENDING        = 2009,  /* account pending review */
    ERR_AUTH_ACCOUNT_REJECTED       = 2010,  /* registration request rejected */
    ERR_REG_REQ_NOT_FOUND           = 2011,  /* registration request not found */
    ERR_REG_REQ_DUPLICATE           = 2012,  /* username already exists or pending review */
    ERR_DB_NOT_OPEN                 = 3001,
    ERR_DB_PREPARE_FAILED           = 3002,
    ERR_DB_EXEC_FAILED              = 3003,
    ERR_CLASS_NAME_DUPLICATE        = 4001,
    ERR_CLASS_ACTIVITY_DUPLICATE    = 4002,
    ERR_CLASS_TYPE_BUILTIN          = 4003,
    ERR_CLASS_ENROLLMENT_FULL       = 4004,
    ERR_PRICE_DUPLICATE             = 4005,  /* 预设金额+成团人数组合重复 */
    ERR_PRICE_PRESET_IN_USE         = 4006,  /* 预设被班级或报名引用 */
    ERR_PRICE_PRESET_NOT_FOUND      = 4007,  /* 预设不存在 */
    ERR_PRICE_PRESET_IMMUTABLE      = 4008,  /* 编辑时不可更换预设 */
    ERR_REGISTRATION_PHONE_INVALID  = 5001,
    ERR_REGISTRATION_ALLERGY_REQUIRED = 5002,
    ERR_REGISTRATION_NOT_FOUND      = 5003,  /* 报名记录不存在 */
    ERR_REGISTRATION_HEADCOUNT_MISMATCH = 5004,  /* 报名学生数与价位期望人数不符 */
    ERR_REFUND_NOT_FOUND              = 5005,  /* 撤销退费时找不到未撤销退费记录 */
    ERR_REFUND_EXCEEDS_PAID           = 5006,  /* 退费金额超过实缴上限 */
    ERR_REFUND_EXCEEDS_ATTENDANCE_CAP = 5007,  /* 退费金额超过考勤折损上限 */
    ERR_DEPOSIT_AMOUNT_INVALID         = 5008,  /* 定金金额非法（为负） */
    ERR_ZERO_PRESET_NOT_FOUND          = 5009,  /* 0 元预设不存在（后端兜底校验） */
    ERR_SUPPLEMENT_AMOUNT_INVALID      = 5010,  /* 补缴金额不符合班级要求（目标全额 <= 已付定金） */
    ERR_SUPPLEMENT_ALREADY_DONE        = 5011,  /* 该学生已补缴，不可重复补缴 */
    ERR_SUPPLEMENT_PRESET_NOT_IN_CLASS = 5012,  /* 补缴目标预设不属于该班级 */
    ERR_DELETE_STUDENT_FAILED          = 5013,  /* 删除学生事务失败 */
    ERR_REGISTRATION_DATE_OUT_OF_RANGE = 5014,  /* 上课日期超出班级时段范围 */
    ERR_REGISTRATION_DATE_INVALID      = 5015,  /* 上课起止日期无效（开始>结束等） */
    ERR_RENEWAL_DATE_INVALID           = 5016,  /* 续费日期无效（new_end_date <= 原 end_date 等） */
    ERR_RENEWAL_NOT_ALLOWED            = 5017,  /* 该学生不允许续费（全额报名/已全额退费等） */
    ERR_RENEWAL_AMOUNT_INVALID         = 5018,  /* 续费金额非法（为负） */
    ERR_TRANSFER_PERIOD_NOT_COVERED    = 5019,  /* 转班失败-新班级时段不覆盖学生上课时段 */
    ERR_RESOURCE_IN_USE             = 6001,
    ERR_RESOURCE_CODE_OCCUPIED      = 6002,
    ERR_RESOURCE_BED_UNAVAILABLE    = 6003,
    ERR_RESOURCE_ALREADY_ALLOCATED  = 6004,
    ERR_ATTENDANCE_ALREADY_DONE     = 7001,
    ERR_UPLOAD_SIZE_EXCEEDED        = 8001,
    ERR_UPLOAD_FORMAT_INVALID       = 8002,
    ERR_UPLOAD_PATH_NOT_CONFIGURED  = 8003,
    ERR_LOG_DB_NOT_OPEN             = 9001,
    /* Platform infrastructure errors (Windows only, 9100-9199) */
    ERR_PLATFORM_INTERNAL           = 9101,  /* Windows 平台基础设施错误 */
    ERR_PLATFORM_PORT_IN_USE        = 9102,  /* 端口被占用 */
    ERR_PLATFORM_CONFIG_CREATE      = 9103,  /* 配置文件创建失败 */
    ERR_PLATFORM_TRAY_INIT          = 9104,  /* 系统托盘初始化失败 */
    /* Network info errors (9200-9299) */
    ERR_NETWORK_INFO_UNAVAILABLE    = 9201,  /* 获取网络信息失败 */
    /* Data transfer errors (10000-10099) */
    ERR_DT_EXPORT_FAILED            = 10001, /* 导出操作失败 */
    ERR_DT_IMPORT_FAILED            = 10002, /* 导入操作失败 */
    ERR_DT_INVALID_FORMAT           = 10003, /* 上传文件不是合法 .dtz 格式 */
    ERR_DT_VERSION_UNSUPPORTED      = 10004, /* 数据包格式版本不支持 */
    ERR_DT_FILE_TOO_LARGE           = 10005, /* 上传文件超过大小限制 */
    ERR_DT_PACK_FAILED              = 10006, /* ZIP 打包失败 */
    ERR_DT_UNPACK_FAILED            = 10007, /* ZIP 解包失败 */
    ERR_DT_BACKUP_FAILED            = 10008  /* 覆盖前自动备份失败 */
};

#endif /* __ERROR_CODES_H__ */
