#ifndef __CLASS_CREATE_HANDLER_H__
#define __CLASS_CREATE_HANDLER_H__

#include "crow_safe.h"
#include "i_class_dao.h"
#include "i_operation_log_dao.h"
#include "session_manager.h"

class ClassCreateHandler {
public:
    explicit ClassCreateHandler(IClassDao* class_dao, IOperationLogDao* log_dao, SessionManager* session_mgr);
    ~ClassCreateHandler();

    /* @brief 注册Crow路由 */
    void RegisterRoutes(crow::SimpleApp& app);

private:
    /* @brief 创建班级 */
    crow::response HandleCreateClass(const crow::request& req);

    /* @brief 获取所有班级类型 */
    crow::response HandleGetClassTypes();

    /* @brief 添加自定义班级类型 */
    crow::response HandleAddClassType(const crow::request& req);

    /* @brief 删除自定义班级类型 */
    crow::response HandleDeleteClassType(const crow::request& req);

    /* @brief 删除班级 */
    crow::response HandleDeleteClass(const crow::request& req);

    /* @brief 上传二维码图片 */
    crow::response HandleUploadQrcode(const crow::request& req);

    /* @brief 查询所有价位预设（含图片路径） */
    crow::response HandleGetPricePresets();

    /* @brief 新增价位预设（金额 + 图片路径数组） */
    crow::response HandleAddPricePreset(const crow::request& req);

    /* @brief 删除价位预设（未被引用时 + 物理文件联动删除） */
    crow::response HandleDeletePricePreset(const crow::request& req);

    /* @brief 对已有预设追加二维码图片 */
    crow::response HandleAddPresetQrcode(const crow::request& req);

    /* @brief 删除预设下某张二维码图片记录 + 物理文件联动删除 */
    crow::response HandleDeletePresetQrcode(const crow::request& req);

    /* @brief 校验管理员权限，成功返回0 */
    int CheckAdminPermission(const crow::request& req, SessionInfo& info);

    /* @brief 从Cookie中解析session_id */
    std::string GetSessionIdFromCookie(const crow::request& req);

    /* @brief 根据起止日期、类型和描述生成班级名称 YYMMDD-YYMMDD-<type>-<desc> */
    std::string GenerateClassName(const std::string& start_time, const std::string& end_time,
                                  const std::string& class_type, const std::string& description);

    IClassDao* class_dao_;
    IOperationLogDao* log_dao_;
    SessionManager* session_mgr_;
};

#endif /* __CLASS_CREATE_HANDLER_H__ */
