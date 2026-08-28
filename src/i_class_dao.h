#ifndef __I_CLASS_DAO_H__
#define __I_CLASS_DAO_H__

#include "class_types.h"
#include <vector>
#include <utility>
#include <string>

class IClassDao {
public:
    virtual ~IClassDao() {}

    virtual int InsertClass(const ClassInfo& info) = 0;
    virtual int QueryClassById(int32_t id, ClassInfo& info) = 0;
    virtual int QueryClassByName(const std::string& name, ClassInfo& info) = 0;
    virtual int QueryAllClasses(std::vector<ClassInfo>& classes) = 0;
    virtual int QueryActiveClasses(std::vector<ClassInfo>& classes) = 0;
    virtual int SearchClassesByName(const std::string& keyword, std::vector<ClassInfo>& classes) = 0;
    virtual int SearchActiveClassesByName(const std::string& keyword, std::vector<ClassInfo>& classes) = 0;
    virtual int UpdateEnrollment(int32_t class_id, int32_t capacity) = 0;
    /**
     * @brief 增加 enrollment_used 增量（浮点）
     * @param class_id 班级 ID
     * @param delta 增量（全额报名=1.0，部分时段=比例）
     * @return DB_OK=成功, ERR_DB_*=失败
     */
    virtual int IncrementEnrollmentUsed(int32_t class_id, double delta) = 0;
    virtual int InsertPrice(const PriceInfo& info) = 0;
    virtual int QueryPricesByClassId(int32_t class_id, std::vector<PriceInfo>& prices) = 0;
    virtual int QueryPriceById(int32_t price_id, PriceInfo& price) = 0;
    virtual int InsertQrcode(int32_t price_id, const std::string& qrcode_path) = 0;
    virtual int QueryQrcodesByPriceId(int32_t price_id, std::vector<std::string>& paths) = 0;
    virtual int InsertClassType(const ClassType& type) = 0;
    virtual int QueryAllClassTypes(std::vector<ClassType>& types) = 0;
    virtual int DeleteClassType(int32_t id) = 0;
    virtual int QueryClassTypeById(int32_t id, ClassType& type) = 0;
    virtual int DeleteClass(int32_t id) = 0;

    /* === 价位预设管理 === */

    /* @brief 新增价位预设（金额 + 图片路径），原子事务
     * @param info 预设信息（amount, expected_headcount, qrcode_paths, create_time）
     * @return DB_OK=成功, ERR_PRICE_DUPLICATE=金额+成团人数组合重复, 其他=失败 */
    virtual int InsertPricePreset(const PricePresetInfo& info) = 0;

    /* @brief 查询所有价位预设（含图片路径列表） */
    virtual int QueryAllPricePresets(std::vector<PricePresetInfo>& presets) = 0;

    /* @brief 按 id 查询单个预设含图片 */
    virtual int QueryPricePresetById(int32_t id, PricePresetInfo& info) = 0;

    /* @brief 原子删除预设（校验 class_price + registration 引用 + 删图片记录 + 删预设）
     * @param preset_id 待删除预设 id
     * @param deleted_files 输出待删物理文件路径列表（事务 COMMIT 后由调用方 unlink）
     * @return DB_OK=成功, ERR_PRICE_PRESET_IN_USE=被引用, 其他=失败 */
    virtual int DeletePricePresetAtomic(int32_t preset_id,
                                        std::vector<std::string>& deleted_files) = 0;

    /* @brief 对已有预设追加一张二维码图片（校验 ≤10 张上限） */
    virtual int AddPresetQrcode(int32_t preset_id, const std::string& qrcode_path) = 0;

    /* @brief 删除预设下某张图片记录
     * @param deleted_file 输出待删物理文件路径
     * @return DB_OK=成功, 其他=失败 */
    virtual int DeletePresetQrcode(int32_t preset_id, const std::string& qrcode_path,
                                   std::string& deleted_file) = 0;

    /* @brief 查询引用某预设的班级名（用于删除预设时错误提示）
     * @return 班级名（若多个引用取第一个），无引用返回空字符串 */
    virtual std::string QueryClassNameByPresetId(int32_t preset_id) = 0;

    /* === 创建班级 + 关联预设（原子） === */

    /* @brief 原子创建班级 + 关联预设价位项
     * @param class_info 班级基本信息（id 由调用方设为 0，函数内回填）
     * @param prices 价位项列表：(activity_name, preset_id)
     * @param generated_class_id 输出生成的班级 id
     * @return DB_OK=成功, ERR_INVALID_PARAM=价位项为空, ERR_CLASS_NAME_DUPLICATE,
     *         ERR_CLASS_ACTIVITY_DUPLICATE=同班金额重复, ERR_PRICE_PRESET_NOT_FOUND */
    virtual int CreateClassWithPricesAtomic(
        const ClassInfo& class_info,
        const std::vector<std::pair<std::string, int32_t> >& prices,
        int32_t& generated_class_id) = 0;

    /* === 编辑班级价位（不换 preset_id，原子） === */

    /* @brief 原子更新班级价位（活动名修改 + 新增项 + 删除项；不换 preset_id）
     * @param class_id 班级 id
     * @param prices 提交的价位项列表，每项含 price_id（>0 已存在项，=0 新增项）、activity_name、preset_id
     * @return DB_OK=成功, ERR_PRICE_PRESET_IMMUTABLE=已存在项尝试换预设,
     *         ERR_CLASS_ACTIVITY_DUPLICATE, ERR_PRICE_PRESET_NOT_FOUND */
    virtual int UpdateClassPricesAtomic(
        int32_t class_id,
        const std::vector<PriceUpdateItem>& prices) = 0;
};

#endif /* __I_CLASS_DAO_H__ */
