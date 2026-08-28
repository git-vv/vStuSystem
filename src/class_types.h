#ifndef __CLASS_TYPES_H__
#define __CLASS_TYPES_H__

#include <string>
#include <cstdint>
#include <vector>

struct ClassInfo {
    int32_t id;
    std::string class_name;
    std::string start_time;
    std::string end_time;
    std::string description;
    int32_t enrollment_capacity;
    double enrollment_used;  /* 已用名额（浮点比例累计），全额报名=1.0，部分时段=学生工作日数/班级总工作日数 */
    std::string class_type;
    std::string create_time;
};

/* 价位预设（全局公共资源，被多个班级引用） */
struct PricePresetInfo {
    int32_t id;                              /* 主键 */
    double amount;                           /* 金额，单位元（REAL） */
    int32_t expected_headcount;              /* 必须同时报名的学生数，默认 1，>=1 */
    std::string create_time;                 /* 创建时间 */
    std::vector<std::string> qrcode_paths;   /* 二维码图片路径列表，0-10 张 */
};

struct PriceInfo {
    int32_t id;                              /* class_price.id */
    int32_t class_id;                        /* 所属班级 */
    int32_t preset_id;                       /* 引用 price_preset.id */
    double snapshot_amount;                  /* 金额快照（元），创建时写入 preset.amount，用于预设删除后回退 */
    int32_t snapshot_headcount;              /* 成团人数快照，创建时写入 preset.expected_headcount，预设删除后回退用 */
    double price;                            /* 运行时金额（元），由 JOIN preset.amount 或回退 snapshot_amount 填充 */
    std::string activity_name;               /* 用户自起活动名称 */
    std::vector<std::string> qrcode_paths;   /* 来自 price_preset_qrcode */
};

struct ClassType {
    int32_t id;
    std::string name;
    int32_t is_builtin;  /* 1=内置不可删, 0=自定义可删 */
};

/* 编辑班级价位时提交的单个价位项（解耦 Handler 层 JSON 类型与 Dao 层接口） */
struct PriceUpdateItem {
    int32_t price_id;      /* >0 已存在项，=0 新增项 */
    int32_t preset_id;     /* 引用 price_preset.id */
    std::string activity_name;
};

#endif /* __CLASS_TYPES_H__ */
