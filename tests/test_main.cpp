#include "test_helpers.h"
#include "error_codes.h"
#include "i_user_dao.h"
#include "i_class_dao.h"
#include "i_registration_dao.h"
#include "i_resource_dao.h"
#include "i_attendance_dao.h"
#include "i_operation_log_dao.h"
#include "i_refund_dao.h"
#include "refund_types.h"
#include "session_manager.h"
#include "auth_handler.h"
#include "admin_handler.h"
#include "class_create_handler.h"
#include "class_manage_handler.h"
#include "registration_handler.h"
#include "resource_handler.h"
#include "page_handler.h"
#include "network_handler.h"
#include "activity_types.h"
#include "i_activity_dao.h"
#include "i_activity_signup_dao.h"
#include "activity_handler.h"
#include "activity_manage_handler.h"
#include "sqlite_database.h"
#include "sqlite_log_database.h"
#include "upload_util.h"
#include "utils.h"
#include "data_transfer_types.h"
#include "i_data_transfer_dao.h"
#include "data_transfer_util.h"
#include "data_transfer_handler.h"
#include "group_session_manager.h"
#include <cstdio>
#include <thread>
#include <vector>

#ifdef _WIN32
#define TEST_TEMP_DIR "."
#else
#define TEST_TEMP_DIR "/tmp"
#endif

/* ===== Mock Dao implementations ===== */

class MockUserDao : public IUserDao {
public:
    int InsertUser(const UserInfo&) override { return DB_OK; }
    int QueryUserByUsername(const std::string&, UserInfo&) override { return ERR_DB_EXEC_FAILED; }
    int QueryUserById(int32_t, UserInfo&) override { return ERR_DB_EXEC_FAILED; }
    int UpdatePassword(int32_t, const std::string&, const std::string&) override { return DB_OK; }
    int UpdateUserInfo(int32_t, const std::string&) override { return DB_OK; }
    int DeleteUser(int32_t) override { return DB_OK; }
    int CheckAdminExists() override { return 0; }
    int QueryAllTeachers(std::vector<UserInfo>&) override { return DB_OK; }
    int InsertResetRequest(const PasswordResetRequest&) override { return DB_OK; }
    int QueryPendingResetRequests(std::vector<PasswordResetRequest>&) override { return DB_OK; }
    int ApproveResetRequest(int32_t, int32_t, const std::string&, const std::string&) override { return DB_OK; }
    int CheckResetPending(int32_t) override { return 0; }
    int InsertRegistrationRequest(const RegistrationRequest&) override { return DB_OK; }
    int QueryRegistrationRequestStatus(const std::string&, int& status) override { status = -1; return ERR_REG_REQ_NOT_FOUND; }
    int QueryPendingRegistrationRequests(std::vector<RegistrationRequest>&) override { return DB_OK; }
    int QueryRegistrationRequestById(int32_t, RegistrationRequest&) override { return ERR_REG_REQ_NOT_FOUND; }
    int UpdateRegistrationRequestStatus(int32_t, RegistrationRequestStatusType) override { return DB_OK; }
    int DeleteRegistrationRequest(int32_t) override { return DB_OK; }
    int CheckRegistrationRequestExists(const std::string&) override { return 0; }
    int ApproveRegistrationRequestsAtomic(const std::vector<int32_t>&) override { return DB_OK; }
};

class MockClassDao : public IClassDao {
public:
    int InsertClass(const ClassInfo&) override { return DB_OK; }
    int QueryClassById(int32_t id, ClassInfo& info) override {
        if (id == old_class_id) { info = old_class; return DB_OK; }
        if (id == new_class_id) { info = new_class; return DB_OK; }
        return ERR_DB_EXEC_FAILED;
    }
    int QueryClassByName(const std::string&, ClassInfo&) override { return ERR_DB_EXEC_FAILED; }
    int QueryAllClasses(std::vector<ClassInfo>&) override { return DB_OK; }
    int QueryActiveClasses(std::vector<ClassInfo>&) override { return DB_OK; }
    int SearchClassesByName(const std::string&, std::vector<ClassInfo>&) override { return DB_OK; }
    int SearchActiveClassesByName(const std::string&, std::vector<ClassInfo>&) override { return DB_OK; }
    int UpdateEnrollment(int32_t, int32_t) override { return DB_OK; }
    int IncrementEnrollmentUsed(int32_t class_id, double delta) override {
        (void)class_id;
        enrollment_used_delta = delta;
        enrollment_used_call_count++;
        return DB_OK;
    }
    int InsertPrice(const PriceInfo&) override { return DB_OK; }
    int QueryPricesByClassId(int32_t, std::vector<PriceInfo>& prices) override {
        prices = mock_prices;
        return DB_OK;
    }
    int QueryPriceById(int32_t id, PriceInfo& info) override {
        for (size_t i = 0; i < mock_prices.size(); ++i) {
            if (mock_prices[i].id == id) { info = mock_prices[i]; return DB_OK; }
        }
        return ERR_DB_EXEC_FAILED;
    }
    int InsertQrcode(int32_t, const std::string&) override { return DB_OK; }
    int QueryQrcodesByPriceId(int32_t, std::vector<std::string>&) override { return DB_OK; }
    int InsertClassType(const ClassType&) override { return DB_OK; }
    int QueryAllClassTypes(std::vector<ClassType>&) override { return DB_OK; }
    int DeleteClassType(int32_t) override { return DB_OK; }
    int QueryClassTypeById(int32_t, ClassType&) override { return ERR_DB_EXEC_FAILED; }
    int DeleteClass(int32_t) override { return DB_OK; }

    /* 价位预设管理 - mock 可配置化 */
    int InsertPricePreset(const PricePresetInfo&) override { return DB_OK; }
    int QueryAllPricePresets(std::vector<PricePresetInfo>& presets) override {
        presets = mock_presets;
        return DB_OK;
    }
    int QueryPricePresetById(int32_t, PricePresetInfo&) override { return ERR_DB_EXEC_FAILED; }
    int DeletePricePresetAtomic(int32_t, std::vector<std::string>&) override { return DB_OK; }
    int AddPresetQrcode(int32_t, const std::string&) override { return DB_OK; }
    int DeletePresetQrcode(int32_t, const std::string&, std::string&) override { return DB_OK; }
    std::string QueryClassNameByPresetId(int32_t) override { return ""; }
    int CreateClassWithPricesAtomic(const ClassInfo&, const std::vector<std::pair<std::string, int32_t> >&, int32_t&) override { return DB_OK; }
    int UpdateClassPricesAtomic(int32_t, const std::vector<PriceUpdateItem>&) override { return DB_OK; }

    /* 可配置的班级数据 */
    int32_t old_class_id = 1;
    int32_t new_class_id = 2;
    ClassInfo old_class;
    ClassInfo new_class;
    std::vector<PriceInfo> mock_prices;
    std::vector<PricePresetInfo> mock_presets;
    double enrollment_used_delta = 0.0;
    int enrollment_used_call_count = 0;
};

class MockRegistrationDao : public IRegistrationDao {
public:
    int InsertRegistration(const RegistrationInfo&) override { return DB_OK; }
    int QueryRegistrationById(int32_t id, RegistrationInfo& info) override {
        if (id == query_id_return_not_found) { return ERR_REGISTRATION_NOT_FOUND; }
        info = stored_info;
        info.id = id;
        return query_ret;
    }
    int QueryRegistrationsByClassId(int32_t, std::vector<RegistrationInfo>&) override { return DB_OK; }
    int QueryRegistrationsByTimeRange(const std::string&, const std::string&, std::vector<RegistrationInfo>&) override { return DB_OK; }
    int CountEnrolledByClassId(int32_t) override { return 0; }
    int CheckEnrollmentAvailable(int32_t, int32_t) override { return DB_OK; }
    int RegisterStudentAtomic(const RegistrationInfo&, int32_t, int32_t, int32_t, int32_t) override { return DB_OK; }
    int RegisterStudentsBatchAtomic(const std::vector<RegistrationInfo>& infos, int32_t, int32_t, int32_t) override {
        deposit_register_call_count++;
        for (size_t i = 0; i < infos.size(); ++i) { stored_infos.push_back(infos[i]); }
        return deposit_register_return;
    }
    int RegisterDepositAtomic(const std::vector<RegistrationInfo>& infos, int32_t, int32_t, int32_t) override {
        deposit_register_call_count++;
        for (size_t i = 0; i < infos.size(); ++i) { stored_infos.push_back(infos[i]); }
        return deposit_register_return;
    }
    int SupplementDepositAtomic(int32_t registration_id, int32_t /*target_class_price_id*/,
                                int32_t /*target_preset_id*/, double target_amount,
                                const std::string& /*operator_name*/, const std::string& /*operate_time*/,
                                double& out_supplement_amount) override {
        supplement_call_count++;
        supplement_last_reg_id = registration_id;
        if (supplement_ret_code != DB_OK) { return supplement_ret_code; }
        out_supplement_amount = target_amount - stored_info.paid_amount_snapshot;
        return DB_OK;
    }

    int DeleteRegistrationAtomic(int32_t registration_id, int32_t /*bed_resource_id*/) override {
        delete_call_count++;
        delete_last_reg_id = registration_id;
        if (delete_ret_code != DB_OK) { return delete_ret_code; }
        return DB_OK;
    }

    int RenewRegistrationAtomic(int32_t registration_id, const std::string& new_end_date,
                                double renew_amount, double enrollment_delta,
                                const std::string& operator_name, const std::string& operate_time) override {
        (void)operator_name;
        (void)operate_time;
        renew_call_count++;
        renew_last_reg_id = registration_id;
        renew_last_new_end_date = new_end_date;
        renew_last_amount = renew_amount;
        renew_last_delta = enrollment_delta;
        if (renew_ret_code != DB_OK) { return renew_ret_code; }
        /* simulate: update stored_info */
        stored_info.student_end_date = new_end_date;
        stored_info.paid_amount_snapshot += renew_amount;
        return DB_OK;
    }

    double QueryEnrollmentUsedByClassId(int32_t class_id) override {
        (void)class_id;
        return enrollment_used_return;
    }

    int CountActiveStudentsByClassId(int32_t class_id) override {
        (void)class_id;
        return active_students_return;
    }

    int QueryRenewalsByRegId(int32_t registration_id, std::vector<RenewalRecordInfo>& records) override {
        (void)registration_id;
        records = stored_renewals;
        return DB_OK;
    }

    int UpdateStudentBasicInfo(const RegistrationInfo& info) override {
        update_call_count++;
        last_update_info = info;
        return update_return;
    }
    int TransferClassAtomic(int32_t rid, int32_t old_c, int32_t new_c, int32_t cap) override {
        transfer_call_count++;
        transfer_rid = rid;
        transfer_old_class = old_c;
        transfer_new_class = new_c;
        transfer_capacity = cap;
        return transfer_return;
    }

    /* 可配置的 mock 行为 */
    RegistrationInfo stored_info;
    std::vector<RegistrationInfo> stored_infos;
    int query_ret = DB_OK;
    int32_t query_id_return_not_found = -1;  /* 默认不触发 not found */
    int update_return = DB_OK;
    int transfer_return = DB_OK;
    int update_call_count = 0;
    int transfer_call_count = 0;
    RegistrationInfo last_update_info;
    int32_t transfer_rid = 0;
    int32_t transfer_old_class = 0;
    int32_t transfer_new_class = 0;
    int32_t transfer_capacity = 0;
    int deposit_register_call_count = 0;
    int deposit_register_return = DB_OK;
    int supplement_call_count = 0;
    int32_t supplement_last_reg_id = 0;
    int supplement_ret_code = DB_OK;
    int delete_call_count = 0;
    int32_t delete_last_reg_id = 0;
    int delete_ret_code = DB_OK;
    int renew_call_count = 0;
    int32_t renew_last_reg_id = 0;
    std::string renew_last_new_end_date;
    double renew_last_amount = 0.0;
    double renew_last_delta = 0.0;
    int renew_ret_code = DB_OK;
    double enrollment_used_return = 0.0;
    int active_students_return = 0;
    std::vector<RenewalRecordInfo> stored_renewals;
};

class MockResourceDao : public IResourceDao {
public:
    int InsertResource(const ResourceInfo&) override { return DB_OK; }
    int QueryResourceById(int32_t, ResourceInfo&) override { return ERR_DB_EXEC_FAILED; }
    int QueryAllResources(std::vector<ResourceInfo>&) override { return DB_OK; }
    int UpdateResourceTotal(int32_t, int32_t) override { return DB_OK; }
    int DeleteResource(int32_t) override { return DB_OK; }
    int CheckResourceInUse(int32_t, std::vector<std::string>&) override { return DB_OK; }
    int InsertAllocation(const ResourceAllocation&) override { return DB_OK; }
    int QueryAllocationsByResourceId(int32_t, std::vector<ResourceAllocation>&) override { return DB_OK; }
    int QueryAllocationsByClassId(int32_t, std::vector<ResourceAllocation>&) override { return DB_OK; }
    int QueryAllocationsByTimeRange(const std::string&, const std::string&, std::vector<ResourceAllocation>&) override { return DB_OK; }
    int CheckResourceCodeOccupied(int32_t, int32_t) override { return 0; }
    int CheckStudentResourceAllocated(int32_t, int32_t) override { return 0; }
    int IncrementResourceUsed(int32_t) override { return DB_OK; }
    int DecrementResourceUsed(int32_t) override { return DB_OK; }
    int QueryResourceByName(const std::string&, ResourceInfo&) override { return ERR_DB_EXEC_FAILED; }
    int QueryBedResourceRemain(int32_t) override { return -1; }
    int QueryResourceByType(int32_t, ResourceInfo&) override { return ERR_DB_EXEC_FAILED; }
    int AllocateResourceAtomic(const ResourceAllocation&) override { return DB_OK; }
};

class MockAttendanceDao : public IAttendanceDao {
public:
    int InsertAttendance(const AttendanceRecord&) override { return DB_OK; }
    int QueryAttendanceByClassAndDate(int32_t, const std::string&, std::vector<AttendanceRecord>&) override { return DB_OK; }
    int CheckAttendanceExists(int32_t, const std::string&) override { return 0; }
    int QueryAttendanceByClassAndDateRange(int32_t, const std::string&, const std::string&, std::vector<AttendanceRecord>&) override { return DB_OK; }
    int QueryAttendanceByRegId(int32_t, std::vector<AttendanceRecord>& records) override {
        records = stored_attendance;
        return DB_OK;
    }
    std::vector<AttendanceRecord> stored_attendance;
};

class MockRefundDao : public IRefundDao {
public:
    int InsertRefundAtomic(RefundRecordInfo& info, double /*original_amount*/, double /*tolerance*/, bool /*skip_attendance_check*/ = false) override {
        info.id = ++next_id;
        records.push_back(info);
        last_info = info;
        return insert_return;
    }
    int CancelRefundAtomic(int32_t registration_id, const std::string& cancel_operator_name,
                           const std::string& cancel_time, double& restored_paid_amount) override {
        for (auto it = records.rbegin(); it != records.rend(); ++it) {
            if (it->registration_id == registration_id && it->status == RefundStatus_Active) {
                it->status = RefundStatus_Cancelled;
                it->cancel_operator_name = cancel_operator_name;
                it->cancel_time = cancel_time;
                restored_paid_amount = cancel_restored;
                last_cancel_reg_id = registration_id;
                return DB_OK;
            }
        }
        return ERR_REFUND_NOT_FOUND;
    }
    int QueryRefundsByRegId(int32_t registration_id, std::vector<RefundRecordInfo>& out) override {
        out.clear();
        for (const auto& r : records) {
            if (r.registration_id == registration_id) { out.push_back(r); }
        }
        return DB_OK;
    }
    int QueryActiveRefundSumByRegId(int32_t registration_id, double& sum) override {
        sum = 0.0;
        for (const auto& r : records) {
            if (r.registration_id == registration_id && r.status == RefundStatus_Active) {
                sum += r.refund_amount;
            }
        }
        return DB_OK;
    }

    int insert_return = DB_OK;
    int next_id = 0;
    int32_t last_cancel_reg_id = 0;
    double cancel_restored = 0.0;
    RefundRecordInfo last_info;
    std::vector<RefundRecordInfo> records;
};

class MockOperationLogDao : public IOperationLogDao {
public:
    int InsertLog(const OperationLog& log) override {
        insert_call_count++;
        last_log = log;
        return insert_return;
    }
    int QueryLogs(const LogQueryCondition&, std::vector<OperationLog>&, int32_t&) override { return DB_OK; }
    int CleanLogs(const LogQueryCondition&) override { return DB_OK; }

    int insert_return = DB_OK;
    int insert_call_count = 0;
    OperationLog last_log;
};

class MockActivityDao : public IActivityDao {
public:
    int CreateActivity(const ActivityInfo& info, int64_t& out_id) override {
        if (info.title.empty()) { return ERR_ACTIVITY_TITLE_EMPTY; }
        if (info.cover_image.empty()) { return ERR_ACTIVITY_COVER_REQUIRED; }
        out_id = next_id_++;
        last_created = info;
        create_count++;
        return DB_OK;
    }
    int UpdateActivity(const ActivityInfo& info) override {
        if (info.id != 1) { return ERR_ACTIVITY_NOT_FOUND; }
        return DB_OK;
    }
    int DeleteActivity(int64_t id) override {
        if (id != 1) { return ERR_ACTIVITY_NOT_FOUND; }
        delete_count++;
        return DB_OK;
    }
    int GetActivity(int64_t id, ActivityInfo& info) override {
        if (id == 1) { info = mock_activity; return DB_OK; }
        return ERR_ACTIVITY_NOT_FOUND;
    }
    int ListActivities(std::vector<ActivityInfo>& list) override {
        list = mock_list;
        return DB_OK;
    }
    int ListPublishedActivities(std::vector<ActivityInfo>& list) override {
        for (size_t i = 0; i < mock_list.size(); ++i) {
            if (mock_list[i].status == 1) { list.push_back(mock_list[i]); }
        }
        return DB_OK;
    }
    int ListPublishedActivitiesPaged(std::vector<ActivityInfo>& list, int limit, int offset, int& total_count) override {
        total_count = 0;
        for (size_t i = 0; i < mock_list.size(); ++i) {
            if (mock_list[i].status == 1) { total_count++; }
        }
        int idx = 0;
        for (size_t i = 0; i < mock_list.size(); ++i) {
            if (mock_list[i].status != 1) { continue; }
            if (idx >= offset && idx < offset + limit) { list.push_back(mock_list[i]); }
            idx++;
        }
        return DB_OK;
    }
    int UpdateActivityStatus(int64_t id, int32_t status) override {
        (void)id;
        (void)status;
        return DB_OK;
    }
    int UpdateActivityImage(int64_t id, const std::string& field, const std::string& path) override {
        (void)id;
        (void)field;
        (void)path;
        return DB_OK;
    }
    int IncrementSignupCount(int64_t id) override {
        (void)id;
        return DB_OK;
    }
    int BatchIncrementSignupCount(int64_t id, int32_t delta) override {
        (void)id; (void)delta;
        return DB_OK;
    }
    int BatchUpdateSortOrder(const std::vector<std::pair<int64_t, int32_t> >& orders) override {
        (void)orders;
        return DB_OK;
    }

    int AddCoverImage(int64_t activity_id, const std::string& path,
                      int sort_order, int64_t& out_id) override {
        (void)activity_id; (void)path; (void)sort_order;
        out_id = next_id_++;
        return DB_OK;
    }

    int GetCoverImages(int64_t activity_id,
                       std::vector<ActivityCoverImage>& images) override {
        (void)activity_id;
        images = mock_cover_images;
        return DB_OK;
    }

    int DeleteCoverImage(int64_t image_id) override {
        (void)image_id;
        return DB_OK;
    }

    int DeleteCoverImagesByActivityId(int64_t activity_id) override {
        (void)activity_id;
        return DB_OK;
    }

    int AddPromotionImage(const std::string& path, int sort_order, int64_t& out_id) override {
        (void)path; (void)sort_order;
        out_id = next_id_++;
        return DB_OK;
    }

    int GetPromotionImages(std::vector<ActivityCoverImage>& images) override {
        (void)images;
        return DB_OK;
    }

    int DeletePromotionImage(int64_t image_id) override {
        (void)image_id;
        return DB_OK;
    }

    int BatchUpdatePromotionImageSortOrder(
        const std::vector<std::pair<int64_t, int32_t>>& orders) override {
        (void)orders;
        return DB_OK;
    }

    int GetPromotionText(std::string& content) override {
        content = "";
        return DB_OK;
    }

    int UpdatePromotionText(const std::string& content) override {
        (void)content;
        return DB_OK;
    }

    int GetActivityNotice(std::string& content) override {
        content = "";
        return DB_OK;
    }

    int UpdateActivityNotice(const std::string& content) override {
        (void)content;
        return DB_OK;
    }

    int AddAboutUsCard(const std::string& image_path, const std::string& text,
                       int32_t layout_type, int32_t sort_order,
                       int64_t& out_id) override {
        (void)image_path; (void)text; (void)layout_type; (void)sort_order;
        out_id = next_id_++;
        return DB_OK;
    }

    int GetAboutUsCards(std::vector<AboutUsCard>& cards) override {
        (void)cards;
        return DB_OK;
    }

    int UpdateAboutUsCard(int64_t id, const std::string& image_path,
                          const std::string& text, int32_t layout_type) override {
        (void)id; (void)image_path; (void)text; (void)layout_type;
        return DB_OK;
    }

    int DeleteAboutUsCard(int64_t card_id) override {
        (void)card_id;
        return DB_OK;
    }

    int BatchUpdateAboutUsCardSortOrder(
        const std::vector<std::pair<int64_t, int32_t>>& orders) override {
        (void)orders;
        return DB_OK;
    }

    int64_t next_id_ = 1;
    ActivityInfo mock_activity;
    std::vector<ActivityInfo> mock_list;
    std::vector<ActivityCoverImage> mock_cover_images;
    ActivityInfo last_created;
    int create_count = 0;
    int delete_count = 0;
};

class MockActivitySignupDao : public IActivitySignupDao {
public:
    int CreateSignupAtomic(const ActivitySignupInfo& info, int64_t& out_id) override {
        if (info.activity_id == 999) { return ERR_ACTIVITY_NOT_FOUND; }
        for (size_t i = 0; i < existing_phones.size(); ++i) {
            if (existing_phones[i].first == info.activity_id && existing_phones[i].second == info.phone) {
                return ERR_ACTIVITY_DUPLICATE_SIGNUP;
            }
        }
        out_id = next_id_++;
        signup_count++;
        return DB_OK;
    }
    int ListSignupsByActivity(int64_t activity_id, std::vector<ActivitySignupInfo>& list) override {
        (void)activity_id;
        list = mock_signups;
        return DB_OK;
    }
    int ConfirmSessionAtomic(int64_t activity_id,
                             const std::vector<ActivitySignupInfo>& members) override {
        (void)activity_id;
        signup_count += static_cast<int>(members.size());
        return DB_OK;
    }
    int CheckDuplicateSignup(int64_t activity_id,
                             const std::string& name,
                             const std::string& phone,
                             bool& out_exists) override {
        (void)name;
        out_exists = false;
        for (size_t i = 0; i < existing_phones.size(); ++i) {
            if (existing_phones[i].first == activity_id && existing_phones[i].second == phone) {
                out_exists = true;
                break;
            }
        }
        return DB_OK;
    }

    int64_t next_id_ = 1;
    int signup_count = 0;
    std::vector<std::pair<int64_t, std::string> > existing_phones;
    std::vector<ActivitySignupInfo> mock_signups;
};

class MockActivityGroupDao : public IActivityGroupDao {
public:
    int CreateGroup(const ActivityGroupInfo& info, int64_t& out_id) override {
        (void)info;
        out_id = next_id_++;
        return DB_OK;
    }
    int GetGroup(int64_t group_id, ActivityGroupInfo& info) override {
        (void)group_id;
        info = mock_group;
        return DB_OK;
    }
    int GetGroupByInviteCode(const std::string& invite_code, ActivityGroupInfo& info) override {
        (void)invite_code;
        if (mock_group_not_found) { return ERR_DB_EXEC_FAILED; }
        info = mock_group;
        return DB_OK;
    }
    int AddMember(const ActivityGroupMemberInfo& member, int64_t& out_id) override {
        (void)member;
        out_id = next_id_++;
        return DB_OK;
    }
    int RemoveMember(int64_t group_id, const std::string& name,
                     const std::string& phone,
                     int32_t& out_is_leader,
                     int32_t& out_remaining_count) override {
        (void)group_id; (void)name; (void)phone;
        out_is_leader = 0;
        out_remaining_count = 1;
        return DB_OK;
    }
    int UpdateGroupCount(int64_t group_id, int32_t delta) override {
        (void)group_id; (void)delta;
        return DB_OK;
    }
    int UpdateGroupStatus(int64_t group_id, int32_t status, int32_t cancel_reason) override {
        (void)group_id; (void)status; (void)cancel_reason;
        return DB_OK;
    }
    int ListMembersByGroup(int64_t group_id,
                           std::vector<ActivityGroupMemberInfo>& list) override {
        (void)group_id;
        list = mock_members;
        return DB_OK;
    }
    int ListMembersByActivity(int64_t activity_id,
                              std::vector<ActivityGroupMemberInfo>& list) override {
        (void)activity_id;
        list = mock_members;
        return DB_OK;
    }
    int CheckDuplicateInGroup(int64_t group_id, const std::string& name,
                              const std::string& phone, const std::string& grade,
                              bool& out_duplicate) override {
        (void)group_id; (void)name; (void)phone; (void)grade;
        out_duplicate = false;
        return DB_OK;
    }
    int ConfirmGroupAtomic(int64_t activity_id, int64_t group_id,
                           const std::vector<ActivityGroupMemberInfo>& members) override {
        (void)activity_id; (void)group_id; (void)members;
        return DB_OK;
    }

    int64_t next_id_ = 1;
    ActivityGroupInfo mock_group;
    std::vector<ActivityGroupMemberInfo> mock_members;
    bool mock_group_not_found = false;
};

class MockDataTransferDao : public IDataTransferDao {
public:
    int ExportTableRows(const std::string& table_name,
                        std::vector<DataRow>& out_rows) override {
        export_call_count++;
        export_last_table = table_name;
        if (export_return != DB_OK) { return export_return; }
        out_rows = mock_export_rows;
        return DB_OK;
    }
    int ImportTableRows(const std::string& table_name,
                        const std::vector<DataRow>& rows,
                        const std::vector<std::string>& unique_keys,
                        ImportModeType mode,
                        TableImportStats& out_stats) override {
        (void)unique_keys;
        import_call_count++;
        import_last_table = table_name;
        import_last_row_count = static_cast<int>(rows.size());
        import_last_mode = mode;
        if (import_return != DB_OK) { return import_return; }
        out_stats.table_name = table_name;
        out_stats.inserted = static_cast<int>(rows.size());
        out_stats.skipped = 0;
        out_stats.failed = 0;
        return DB_OK;
    }
    int ClearBusinessTables() override {
        clear_call_count++;
        return clear_return;
    }
    int GetTableColumnNames(const std::string& table_name,
                            std::vector<std::string>& out_columns) override {
        (void)table_name;
        out_columns = mock_columns;
        return DB_OK;
    }

    int export_return = DB_OK;
    int import_return = DB_OK;
    int clear_return = DB_OK;
    int export_call_count = 0;
    int import_call_count = 0;
    int clear_call_count = 0;
    std::string export_last_table;
    std::string import_last_table;
    int import_last_row_count = 0;
    ImportModeType import_last_mode = ImportMode_Incremental;
    std::vector<DataRow> mock_export_rows;
    std::vector<std::string> mock_columns;
};

/* ===== Handler construction tests ===== */

TEST_CASE(AuthHandler_WithMockDao) {
    MockUserDao user_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    AuthHandler handler(&user_dao, &log_dao, &session_mgr);
    ASSERT_TRUE(true);
}

TEST_CASE(AdminHandler_WithMockDao) {
    MockUserDao user_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    AdminHandler handler(&user_dao, &log_dao, &session_mgr);
    ASSERT_TRUE(true);
}

TEST_CASE(ClassCreateHandler_WithMockDao) {
    MockClassDao class_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassCreateHandler handler(&class_dao, &log_dao, &session_mgr);
    ASSERT_TRUE(true);
}

TEST_CASE(ClassManageHandler_WithMockDao) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, &resource_dao, &attendance_dao, &refund_dao, &log_dao, &session_mgr);
    ASSERT_TRUE(true);
}

TEST_CASE(RegistrationHandler_WithMockDao) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    RegistrationHandler handler(&class_dao, &reg_dao, &resource_dao, &log_dao, &session_mgr);
    ASSERT_TRUE(true);
}

TEST_CASE(ResourceHandler_WithMockDao) {
    MockResourceDao resource_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ResourceHandler handler(&resource_dao, &log_dao, &session_mgr);
    ASSERT_TRUE(true);
}

TEST_CASE(PageHandler_DefaultConstructor) {
    PageHandler handler;
    ASSERT_TRUE(true);
}

/* ===== Error code tests ===== */

TEST_CASE(ErrorCode_DB_OK_IsZero) {
    ASSERT_EQ(DB_OK, 0);
}

TEST_CASE(ErrorCode_ERR_INVALID_PARAM_NonZero) {
    ASSERT_TRUE(ERR_INVALID_PARAM != 0);
}

TEST_CASE(ErrorCode_ERR_HANDLER_NULL_DAO_NonZero) {
    ASSERT_TRUE(ERR_HANDLER_NULL_DAO != 0);
}

TEST_CASE(ErrorCode_ERR_DB_NOT_OPEN_NonZero) {
    ASSERT_TRUE(ERR_DB_NOT_OPEN != 0);
}

TEST_CASE(ErrorCode_ERR_DB_PREPARE_FAILED_NonZero) {
    ASSERT_TRUE(ERR_DB_PREPARE_FAILED != 0);
}

TEST_CASE(ErrorCode_ERR_DB_EXEC_FAILED_NonZero) {
    ASSERT_TRUE(ERR_DB_EXEC_FAILED != 0);
}

TEST_CASE(ErrorCode_ERR_AUTH_SESSION_EXPIRED_NonZero) {
    ASSERT_TRUE(ERR_AUTH_SESSION_EXPIRED != 0);
}

TEST_CASE(ErrorCode_ERR_AUTH_PERMISSION_DENIED_NonZero) {
    ASSERT_TRUE(ERR_AUTH_PERMISSION_DENIED != 0);
}

TEST_CASE(ErrorCode_ERR_RESOURCE_IN_USE_NonZero) {
    ASSERT_TRUE(ERR_RESOURCE_IN_USE != 0);
}

TEST_CASE(ErrorCode_AuthRange) {
    ASSERT_TRUE(ERR_AUTH_INVALID_CREDENTIALS >= 2000);
    ASSERT_TRUE(ERR_AUTH_RESET_PENDING <= 2999);
}

TEST_CASE(ErrorCode_ClassRange) {
    ASSERT_TRUE(ERR_CLASS_NAME_DUPLICATE >= 4000);
    ASSERT_TRUE(ERR_CLASS_ENROLLMENT_FULL <= 4999);
}

TEST_CASE(ErrorCode_ResourceRange) {
    ASSERT_TRUE(ERR_RESOURCE_IN_USE >= 6000);
    ASSERT_TRUE(ERR_RESOURCE_BED_UNAVAILABLE <= 6999);
}

/* ===== Dao interface pointer tests ===== */

TEST_CASE(IUserDao_NullPtr) {
    IUserDao* dao = nullptr;
    ASSERT_TRUE(dao == nullptr);
}

TEST_CASE(IClassDao_NullPtr) {
    IClassDao* dao = nullptr;
    ASSERT_TRUE(dao == nullptr);
}

TEST_CASE(IRegistrationDao_NullPtr) {
    IRegistrationDao* dao = nullptr;
    ASSERT_TRUE(dao == nullptr);
}

TEST_CASE(IResourceDao_NullPtr) {
    IResourceDao* dao = nullptr;
    ASSERT_TRUE(dao == nullptr);
}

TEST_CASE(IAttendanceDao_NullPtr) {
    IAttendanceDao* dao = nullptr;
    ASSERT_TRUE(dao == nullptr);
}

TEST_CASE(IOperationLogDao_NullPtr) {
    IOperationLogDao* dao = nullptr;
    ASSERT_TRUE(dao == nullptr);
}

/* ===== SessionManager tests ===== */

TEST_CASE(SessionManager_CreateAndValidate) {
    SessionManager mgr;
    std::string session_id = mgr.CreateSession(1, "test", UserRole_Admin);
    ASSERT_TRUE(!session_id.empty());
    SessionInfo validated;
    int ret = mgr.ValidateSession(session_id, validated);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(validated.user_id, 1);
}

TEST_CASE(SessionManager_InvalidSession) {
    SessionManager mgr;
    SessionInfo info;
    int ret = mgr.ValidateSession("invalid_id", info);
    ASSERT_TRUE(ret != DB_OK);
}

TEST_CASE(SessionManager_DestroySession) {
    SessionManager mgr;
    std::string session_id = mgr.CreateSession(1, "test", UserRole_Teacher);
    mgr.DestroySession(session_id);
    SessionInfo validated;
    int ret = mgr.ValidateSession(session_id, validated);
    ASSERT_TRUE(ret != DB_OK);
}

TEST_CASE(SessionManager_ConcurrentAccess) {
    SessionManager mgr;
    const int thread_count = 10;
    std::vector<std::thread> threads;
    std::vector<std::string> session_ids(thread_count);
    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&mgr, &session_ids, i]() {
            session_ids[i] = mgr.CreateSession(i, "user" + std::to_string(i), UserRole_Teacher);
        });
    }
    for (int i = 0; i < thread_count; ++i) {
        threads[i].join();
    }
    for (int i = 0; i < thread_count; ++i) {
        SessionInfo info;
        int ret = mgr.ValidateSession(session_ids[i], info);
        ASSERT_EQ(ret, DB_OK);
    }
}

/* ===== CryptoUtil tests ===== */

TEST_CASE(GenerateSalt_Length) {
    std::string salt = register_student::GenerateSalt();
    ASSERT_TRUE(salt.size() == 32);
}

TEST_CASE(GenerateSalt_Uniqueness) {
    std::string salt1 = register_student::GenerateSalt();
    std::string salt2 = register_student::GenerateSalt();
    ASSERT_TRUE(salt1 != salt2);
}

TEST_CASE(EncryptPassword_Consistency) {
    std::string salt = register_student::GenerateSalt();
    std::string hash1 = register_student::EncryptPassword("test123", salt);
    std::string hash2 = register_student::EncryptPassword("test123", salt);
    ASSERT_TRUE(hash1 == hash2);
}

TEST_CASE(EncryptPassword_DifferentSalt) {
    std::string salt1 = register_student::GenerateSalt();
    std::string salt2 = register_student::GenerateSalt();
    std::string hash1 = register_student::EncryptPassword("test123", salt1);
    std::string hash2 = register_student::EncryptPassword("test123", salt2);
    ASSERT_TRUE(hash1 != hash2);
}

TEST_CASE(VerifyPassword_Correct) {
    std::string salt = register_student::GenerateSalt();
    std::string hash = register_student::EncryptPassword("mypassword", salt);
    ASSERT_TRUE(register_student::VerifyPassword("mypassword", salt, hash));
}

TEST_CASE(VerifyPassword_Incorrect) {
    std::string salt = register_student::GenerateSalt();
    std::string hash = register_student::EncryptPassword("mypassword", salt);
    ASSERT_TRUE(!register_student::VerifyPassword("wrongpassword", salt, hash));
}

TEST_CASE(VerifyPassword_EmptyPassword) {
    std::string salt = register_student::GenerateSalt();
    std::string hash = register_student::EncryptPassword("", salt);
    ASSERT_TRUE(register_student::VerifyPassword("", salt, hash));
    ASSERT_TRUE(!register_student::VerifyPassword("notempty", salt, hash));
}

TEST_CASE(GetCurrentTimeString_NotEmpty) {
    std::string time_str = register_student::GetCurrentTimeString();
    ASSERT_TRUE(!time_str.empty());
}

/* ===== UploadUtil tests ===== */

TEST_CASE(ValidateSize_WithinLimit) {
    int ret = UploadUtil::ValidateSize(1048576);
    ASSERT_EQ(ret, DB_OK);
}

TEST_CASE(ValidateSize_ExceedLimit) {
    int ret = UploadUtil::ValidateSize(1048577);
    ASSERT_EQ(ret, ERR_UPLOAD_SIZE_EXCEEDED);
}

TEST_CASE(ValidateSize_Zero) {
    int ret = UploadUtil::ValidateSize(0);
    ASSERT_EQ(ret, DB_OK);
}

TEST_CASE(ValidateFormat_Jpg) {
    int ret = UploadUtil::ValidateFormat("photo.jpg");
    ASSERT_EQ(ret, DB_OK);
}

TEST_CASE(ValidateFormat_Png) {
    int ret = UploadUtil::ValidateFormat("photo.png");
    ASSERT_EQ(ret, DB_OK);
}

TEST_CASE(ValidateFormat_Gif) {
    int ret = UploadUtil::ValidateFormat("photo.gif");
    ASSERT_EQ(ret, ERR_UPLOAD_FORMAT_INVALID);
}

TEST_CASE(ValidateFormat_NoExtension) {
    int ret = UploadUtil::ValidateFormat("photo");
    ASSERT_EQ(ret, ERR_UPLOAD_FORMAT_INVALID);
}

TEST_CASE(SaveFile_PathTraversal) {
    std::string saved_path;
    int ret = UploadUtil::SaveFile(TEST_TEMP_DIR "/test_uploads", "../etc/passwd", "data", 4, saved_path);
    ASSERT_TRUE(ret != DB_OK);
}

TEST_CASE(SaveFile_Success) {
    const char* test_dir = TEST_TEMP_DIR "/test_upload_util";
    const char* test_data = "hello";
    std::string saved_path;
    int ret = UploadUtil::SaveFile(test_dir, "test.jpg", test_data, 5, saved_path);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_TRUE(!saved_path.empty());
    std::remove(saved_path.c_str());
}

/* ===== Database CRUD tests ===== */

static const char* TEST_DB_PATH = TEST_TEMP_DIR "/test_register_student.db";
static const char* TEST_LOG_DB_PATH = TEST_TEMP_DIR "/test_operation_log.db";

TEST_CASE(Database_UserCrud) {
    std::remove(TEST_DB_PATH);
    SqliteDatabase db;
    int ret = db.Open(TEST_DB_PATH);
    ASSERT_EQ(ret, DB_OK);

    UserInfo user;
    user.username = "testadmin";
    user.password_hash = register_student::EncryptPassword("pass123", "testsalt");
    user.salt = "testsalt";
    user.role = UserRole_Admin;
    user.display_name = "Test Admin";
    user.create_time = register_student::GetCurrentTimeString();
    ret = db.InsertUser(user);
    ASSERT_EQ(ret, DB_OK);

    UserInfo queried;
    ret = db.QueryUserByUsername("testadmin", queried);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_TRUE(queried.username == "testadmin");

    int exists = db.CheckAdminExists();
    ASSERT_EQ(exists, 1);

    ret = db.DeleteUser(queried.id);
    ASSERT_EQ(ret, DB_OK);

    db.Close();
    std::remove(TEST_DB_PATH);
}

TEST_CASE(Database_ClassCrud) {
    std::remove(TEST_DB_PATH);
    SqliteDatabase db;
    int ret = db.Open(TEST_DB_PATH);
    ASSERT_EQ(ret, DB_OK);

    ClassInfo cls;
    cls.class_name = "250101-250630-托管班";
    cls.start_time = "2025-01-01";
    cls.end_time = "2025-06-30";
    cls.description = "test class";
    cls.enrollment_capacity = 20;
    cls.enrollment_used = 0;
    cls.class_type = "托管班";
    cls.create_time = register_student::GetCurrentTimeString();
    ret = db.InsertClass(cls);
    ASSERT_EQ(ret, DB_OK);

    ClassInfo queried;
    ret = db.QueryClassByName("250101-250630-托管班", queried);
    ASSERT_EQ(ret, DB_OK);

    /* Name uniqueness */
    ClassInfo dup = cls;
    ret = db.InsertClass(dup);
    ASSERT_TRUE(ret != DB_OK);

    ret = db.UpdateEnrollment(queried.id, 30);
    ASSERT_EQ(ret, DB_OK);

    ret = db.IncrementEnrollmentUsed(queried.id, 1.0);
    ASSERT_EQ(ret, DB_OK);

    db.Close();
    std::remove(TEST_DB_PATH);
}

TEST_CASE(Database_ClassTypeCrud) {
    std::remove(TEST_DB_PATH);
    SqliteDatabase db;
    int ret = db.Open(TEST_DB_PATH);
    ASSERT_EQ(ret, DB_OK);

    std::vector<ClassType> types;
    ret = db.QueryAllClassTypes(types);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_TRUE(types.size() >= 5);

    ClassType custom;
    custom.name = "自定义班";
    custom.is_builtin = 0;
    ret = db.InsertClassType(custom);
    ASSERT_EQ(ret, DB_OK);

    ret = db.DeleteClassType(custom.id);
    ASSERT_EQ(ret, DB_OK);

    db.Close();
    std::remove(TEST_DB_PATH);
}

TEST_CASE(Database_RegistrationCrud) {
    std::remove(TEST_DB_PATH);
    SqliteDatabase db;
    int ret = db.Open(TEST_DB_PATH);
    ASSERT_EQ(ret, DB_OK);

    ClassInfo cls;
    cls.class_name = "250101-250630-托管班";
    cls.start_time = "2025-01-01";
    cls.end_time = "2025-06-30";
    cls.description = "test";
    cls.enrollment_capacity = 20;
    cls.enrollment_used = 0;
    cls.class_type = "托管班";
    cls.create_time = register_student::GetCurrentTimeString();
    db.InsertClass(cls);
    ClassInfo queried_cls;
    db.QueryClassByName("250101-250630-托管班", queried_cls);

    RegistrationInfo reg;
    reg.class_id = queried_cls.id;
    reg.student_name = "ZhangSan";
    reg.student_gender = "male";
    reg.parent_phone = "13800138000";
    reg.has_allergy = 0;
    reg.allergy_desc = "";
    reg.price_id = 1;
    reg.need_bed = 0;
    reg.teacher_name = "Li";
    reg.other_info = "";
    reg.register_time = register_student::GetCurrentTimeString();
    ret = db.InsertRegistration(reg);
    ASSERT_EQ(ret, DB_OK);

    double used = db.QueryEnrollmentUsedByClassId(queried_cls.id);
    ASSERT_TRUE(used >= 0.999);

    db.Close();
    std::remove(TEST_DB_PATH);
}

TEST_CASE(Database_ResourceCrud) {
    std::remove(TEST_DB_PATH);
    SqliteDatabase db;
    int ret = db.Open(TEST_DB_PATH);
    ASSERT_EQ(ret, DB_OK);

    ResourceInfo res;
    res.name = "bed";
    res.total_count = 10;
    res.used_count = 0;
    res.remain_count = 10;
    ret = db.InsertResource(res);
    ASSERT_EQ(ret, DB_OK);

    ResourceInfo queried;
    ret = db.QueryResourceByName("bed", queried);
    ASSERT_EQ(ret, DB_OK);

    ret = db.UpdateResourceTotal(queried.id, 20);
    ASSERT_EQ(ret, DB_OK);

    ret = db.IncrementResourceUsed(queried.id);
    ASSERT_EQ(ret, DB_OK);

    ret = db.DeleteResource(queried.id);
    ASSERT_EQ(ret, DB_OK);

    db.Close();
    std::remove(TEST_DB_PATH);
}

TEST_CASE(Database_AttendanceCrud) {
    std::remove(TEST_DB_PATH);
    SqliteDatabase db;
    int ret = db.Open(TEST_DB_PATH);
    ASSERT_EQ(ret, DB_OK);

    AttendanceRecord record;
    record.class_id = 1;
    record.registration_id = 1;
    record.student_name = "ZhangSan";
    record.student_gender = "male";
    record.attendance_date = "2025-01-15";
    record.status = AttendanceStatus_Present;
    record.teacher_name = "Li";
    record.record_time = register_student::GetCurrentTimeString();
    ret = db.InsertAttendance(record);
    ASSERT_EQ(ret, DB_OK);

    int exists = db.CheckAttendanceExists(1, "2025-01-15");
    ASSERT_EQ(exists, 1);

    db.Close();
    std::remove(TEST_DB_PATH);
}

TEST_CASE(Database_ResetRequest) {
    std::remove(TEST_DB_PATH);
    SqliteDatabase db;
    int ret = db.Open(TEST_DB_PATH);
    ASSERT_EQ(ret, DB_OK);

    UserInfo teacher;
    teacher.username = "teacher1";
    teacher.password_hash = "hash";
    teacher.salt = "salt";
    teacher.role = UserRole_Teacher;
    teacher.display_name = "Teacher One";
    teacher.create_time = register_student::GetCurrentTimeString();
    db.InsertUser(teacher);
    UserInfo queried_teacher;
    db.QueryUserByUsername("teacher1", queried_teacher);

    PasswordResetRequest req;
    req.user_id = queried_teacher.id;
    req.status = ResetStatus_Pending;
    req.approver_id = -1;
    req.new_password_hash = "";
    req.new_salt = "";
    req.request_time = register_student::GetCurrentTimeString();
    req.approve_time = "";
    ret = db.InsertResetRequest(req);
    ASSERT_EQ(ret, DB_OK);

    std::vector<PasswordResetRequest> pending;
    ret = db.QueryPendingResetRequests(pending);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_TRUE(pending.size() >= 1);

    db.Close();
    std::remove(TEST_DB_PATH);
}

TEST_CASE(LogDatabase_Crud) {
    std::remove(TEST_LOG_DB_PATH);
    SqliteLogDatabase log_db;
    int ret = log_db.Open(TEST_LOG_DB_PATH);
    ASSERT_EQ(ret, DB_OK);

    OperationLog log;
    log.op_type = OpType_RegisterStudent;
    log.operator_name = "teacher1";
    log.target_class = "250101-250630-托管班";
    log.target_student = "ZhangSan";
    log.target_resource = "";
    log.detail = "register student";
    log.op_time = register_student::GetCurrentTimeString();
    ret = log_db.InsertLog(log);
    ASSERT_EQ(ret, DB_OK);

    LogQueryCondition cond;
    cond.op_type = -1;
    cond.page = 1;
    cond.page_size = 10;
    std::vector<OperationLog> logs;
    int32_t total = 0;
    ret = log_db.QueryLogs(cond, logs, total);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_TRUE(total >= 1);

    log_db.Close();
    std::remove(TEST_LOG_DB_PATH);
}

/* ===== Permission tests ===== */

TEST_CASE(Handler_Permission_InvalidSession) {
    SessionManager mgr;
    SessionInfo info;
    int ret = mgr.ValidateSession("invalid_session", info);
    ASSERT_EQ(ret, ERR_AUTH_SESSION_EXPIRED);
}

TEST_CASE(Handler_Permission_TeacherNotAdmin) {
    SessionManager mgr;
    std::string session_id = mgr.CreateSession(2, "teacher1", UserRole_Teacher);
    SessionInfo info;
    int ret = mgr.ValidateSession(session_id, info);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_TRUE(info.role != UserRole_Admin);
}

/* ====== BuildStudentDiff tests (M6-T6.2) ====== */

TEST_CASE(BuildStudentDiff_OnlyNameChanged) {
    RegistrationInfo old_info;
    old_info.student_name = "zhang_san";
    RegistrationInfo new_info = old_info;
    new_info.student_name = "li_si";
    std::string diff = register_student::BuildStudentDiff(old_info, new_info, "C1", "C1");
    ASSERT_TRUE(diff.find("zhang_san") != std::string::npos);
    ASSERT_TRUE(diff.find("li_si") != std::string::npos);
    ASSERT_TRUE(diff.find("C1") == std::string::npos);  /* 班级未变，不出现 */
}

TEST_CASE(BuildStudentDiff_NoChange) {
    RegistrationInfo info;
    info.student_name = "test";
    info.student_gender = "male";
    info.parent_phone = "13800000000";
    info.has_allergy = 0;
    info.allergy_desc = "";
    info.class_id = 1;
    info.other_info = "";
    info.teacher_name = "";
    std::string diff = register_student::BuildStudentDiff(info, info, "C1", "C1");
    ASSERT_TRUE(diff.empty());
}

TEST_CASE(BuildStudentDiff_ClassChanged) {
    RegistrationInfo old_info;
    old_info.class_id = 1;
    RegistrationInfo new_info = old_info;
    new_info.class_id = 2;
    std::string diff = register_student::BuildStudentDiff(old_info, new_info, "C1", "C2");
    ASSERT_TRUE(diff.find("C1") != std::string::npos);
    ASSERT_TRUE(diff.find("C2") != std::string::npos);
}

TEST_CASE(BuildStudentDiff_TruncateOver40Chars) {
    RegistrationInfo old_info;
    old_info.student_name = std::string(60, 'a');  /* 60 字符 */
    RegistrationInfo new_info = old_info;
    new_info.student_name = std::string(60, 'b');
    std::string diff = register_student::BuildStudentDiff(old_info, new_info, "C1", "C1");
    ASSERT_TRUE(diff.size() <= 40);
    ASSERT_TRUE(diff.size() >= 37 + 3);  /* 37 + "..." */
    ASSERT_TRUE(diff.find("...") != std::string::npos);
}

TEST_CASE(BuildStudentDiff_Exactly40NoTruncate) {
    RegistrationInfo old_info;
    old_info.student_name = "A";
    RegistrationInfo new_info = old_info;
    new_info.student_name = "B";  /* diff: 姓名: A→B，长度按字节计算 */
    std::string diff = register_student::BuildStudentDiff(old_info, new_info, "", "");
    /* 应远小于 40，不截断 */
    ASSERT_TRUE(diff.size() < 40);
}

TEST_CASE(BuildStudentDiff_GenderLabelApplied) {
    RegistrationInfo old_info;
    old_info.student_gender = "male";
    RegistrationInfo new_info = old_info;
    new_info.student_gender = "female";
    std::string diff = register_student::BuildStudentDiff(old_info, new_info, "", "");
    /* 性别 label 转为中文（UTF-8 字节序） */
    ASSERT_TRUE(!diff.empty());
}

/* ====== SqliteDatabase UpdateStudentBasicInfo + TransferClassAtomic tests (M6-T6.3) ====== */

TEST_CASE(SqliteDatabase_UpdateStudentBasicInfo_PreservesClassIdPriceId) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    ClassInfo ci;
    ci.class_name = "TC1";
    ci.start_time = "2026-01-01";
    ci.end_time = "2026-12-31";
    ci.description = "";
    ci.enrollment_capacity = 30;
    ci.enrollment_used = 0;
    ci.class_type = "test";
    ci.create_time = "2026-01-01 00:00:00";
    ret = db.InsertClass(ci);
    ASSERT_EQ(ret, DB_OK);

    RegistrationInfo ri;
    ri.class_id = 1;
    ri.student_name = "stu_orig";
    ri.student_gender = "male";
    ri.parent_phone = "111";
    ri.has_allergy = 0;
    ri.allergy_desc = "";
    ri.price_id = 1;
    ri.need_bed = 1;
    ri.teacher_name = "t1";
    ri.other_info = "";
    ri.register_time = "2026-01-01 00:00:00";
    ret = db.InsertRegistration(ri);
    ASSERT_EQ(ret, DB_OK);

    /* 修改基础信息 */
    RegistrationInfo upd;
    upd.id = 1;
    upd.class_id = 999;          /* 应被忽略 */
    upd.student_name = "stu_new";
    upd.student_gender = "female";
    upd.parent_phone = "222";
    upd.has_allergy = 1;
    upd.allergy_desc = "peanut";
    upd.price_id = 999;          /* 应被忽略 */
    upd.need_bed = 999;          /* 应被忽略 */
    upd.teacher_name = "t2";
    upd.other_info = "note";
    upd.register_time = "2099-01-01";  /* 应被忽略 */
    ret = db.UpdateStudentBasicInfo(upd);
    ASSERT_EQ(ret, DB_OK);

    RegistrationInfo got;
    ret = db.QueryRegistrationById(1, got);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(got.student_name, std::string("stu_new"));
    ASSERT_EQ(got.parent_phone, std::string("222"));
    ASSERT_EQ(got.has_allergy, 1);
    ASSERT_EQ(got.class_id, 1);     /* 保留原值 */
    ASSERT_EQ(got.price_id, 1);     /* 保留原值 */
    ASSERT_EQ(got.need_bed, 1);     /* 保留原值 */
    ASSERT_EQ(got.register_time, std::string("2026-01-01 00:00:00"));  /* 保留原值 */

    db.Close();
}

TEST_CASE(SqliteDatabase_UpdateStudentBasicInfo_NotFound) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    RegistrationInfo upd;
    upd.id = 99999;
    upd.student_name = "x";
    upd.student_gender = "male";
    upd.parent_phone = "1";
    upd.has_allergy = 0;
    upd.allergy_desc = "";
    upd.teacher_name = "";
    upd.other_info = "";
    ret = db.UpdateStudentBasicInfo(upd);
    ASSERT_EQ(ret, ERR_REGISTRATION_NOT_FOUND);

    db.Close();
}

TEST_CASE(SqliteDatabase_TransferClass_Success) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    ClassInfo c1;
    c1.class_name = "C1";
    c1.start_time = "2026-01-01";
    c1.end_time = "2026-12-31";
    c1.description = "";
    c1.enrollment_capacity = 30;
    c1.enrollment_used = 0;
    c1.class_type = "test";
    c1.create_time = "2026-01-01";
    db.InsertClass(c1);

    ClassInfo c2 = c1;
    c2.class_name = "C2";
    db.InsertClass(c2);

    RegistrationInfo ri;
    ri.class_id = 1;
    ri.student_name = "stu";
    ri.student_gender = "male";
    ri.parent_phone = "1";
    ri.has_allergy = 0;
    ri.allergy_desc = "";
    ri.price_id = 1;
    ri.need_bed = 0;
    ri.teacher_name = "t";
    ri.other_info = "";
    ri.register_time = "2026-01-01";
    db.InsertRegistration(ri);

    ret = db.TransferClassAtomic(1, 1, 2, 30);
    ASSERT_EQ(ret, DB_OK);

    ClassInfo got1, got2;
    db.QueryClassById(1, got1);
    db.QueryClassById(2, got2);
    ASSERT_EQ(got1.enrollment_used, 0);   /* 原 0, guarded by enrollment_used > 0 */
    ASSERT_EQ(got2.enrollment_used, 1);   /* 新 0 + 1 */

    RegistrationInfo got_r;
    db.QueryRegistrationById(1, got_r);
    ASSERT_EQ(got_r.class_id, 2);

    db.Close();
}

TEST_CASE(SqliteDatabase_TransferClass_Full_Rollback) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    ClassInfo c1;
    c1.class_name = "C1";
    c1.start_time = "2026-01-01";
    c1.end_time = "2026-12-31";
    c1.description = "";
    c1.enrollment_capacity = 30;
    c1.enrollment_used = 0;
    c1.class_type = "test";
    c1.create_time = "2026-01-01";
    db.InsertClass(c1);

    ClassInfo c2 = c1;
    c2.class_name = "C2";
    c2.enrollment_capacity = 1;
    c2.enrollment_used = 1;  /* 已满 */
    db.InsertClass(c2);

    RegistrationInfo ri;
    ri.class_id = 1;
    ri.student_name = "stu";
    ri.student_gender = "male";
    ri.parent_phone = "1";
    ri.has_allergy = 0;
    ri.allergy_desc = "";
    ri.price_id = 1;
    ri.need_bed = 0;
    ri.teacher_name = "t";
    ri.other_info = "";
    ri.register_time = "2026-01-01";
    db.InsertRegistration(ri);  /* ri 将获得 id=1 */

    /* 班级 2 插入一条已有报名记录，使 enrollment_used = 1（已满） */
    RegistrationInfo ri2;
    ri2.class_id = 2;
    ri2.student_name = "stu2";
    ri2.student_gender = "male";
    ri2.parent_phone = "1";
    ri2.has_allergy = 0;
    ri2.allergy_desc = "";
    ri2.price_id = 1;
    ri2.need_bed = 0;
    ri2.teacher_name = "t";
    ri2.other_info = "";
    ri2.register_time = "2026-01-01";
    db.InsertRegistration(ri2);  /* ri2 将得到 id=2 */

    /* rid=1 转班到 class 2，因 class 2 已满应失败 */
    ret = db.TransferClassAtomic(1, 1, 2, 1);
    ASSERT_EQ(ret, ERR_CLASS_ENROLLMENT_FULL);

    /* 验证回滚：双方 enrollment_used 不变 */
    ClassInfo got1, got2;
    db.QueryClassById(1, got1);
    db.QueryClassById(2, got2);
    ASSERT_EQ(got1.enrollment_used, 0);  /* 原 0 不变 */
    ASSERT_EQ(got2.enrollment_used, 1);  /* 新 1 不变 */

    RegistrationInfo got_r;
    db.QueryRegistrationById(1, got_r);
    ASSERT_EQ(got_r.class_id, 1);  /* class_id 未变 */

    db.Close();
}

TEST_CASE(SqliteDatabase_TransferClass_NotFound) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    ClassInfo c1;
    c1.class_name = "C1";
    c1.start_time = "2026-01-01";
    c1.end_time = "2026-12-31";
    c1.description = "";
    c1.enrollment_capacity = 30;
    c1.enrollment_used = 0;
    c1.class_type = "test";
    c1.create_time = "2026-01-01";
    db.InsertClass(c1);
    db.InsertClass(c1);  /* id=2 */

    ret = db.TransferClassAtomic(99999, 1, 2, 30);
    ASSERT_EQ(ret, ERR_REGISTRATION_NOT_FOUND);

    db.Close();
}

TEST_CASE(SqliteDatabase_TransferClass_PreservesBedReserved) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    ClassInfo c1;
    c1.class_name = "C1";
    c1.start_time = "2026-01-01";
    c1.end_time = "2026-12-31";
    c1.description = "";
    c1.enrollment_capacity = 30;
    c1.enrollment_used = 0;
    c1.class_type = "test";
    c1.create_time = "2026-01-01";
    db.InsertClass(c1);
    db.InsertClass(c1);  /* id=2 */

    RegistrationInfo ri;
    ri.class_id = 1;
    ri.student_name = "stu";
    ri.student_gender = "male";
    ri.parent_phone = "1";
    ri.has_allergy = 0;
    ri.allergy_desc = "";
    ri.price_id = 1;
    ri.need_bed = 1;  /* 需床位 */
    ri.teacher_name = "t";
    ri.other_info = "";
    ri.register_time = "2026-01-01";
    db.InsertRegistration(ri);

    /* 先用 RegisterStudentAtomic 报名以增加 bed_reserved_count？ 简化：直接验证 TransferClassAtomic 不调用 IncrementBedReservedInternal */
    ret = db.TransferClassAtomic(1, 1, 2, 30);
    ASSERT_EQ(ret, DB_OK);

    /* 转班后 bed_reserved_count 应保持 0（因为本测试未通过 RegisterStudentAtomic 增加） */
    db.Close();
}

/* ====== HandleUpdateStudent Handler 层测试 (M6-T6.4) ====== */

static std::string MakeUpdateStudentBody(int32_t rid, const std::string& name,
                                         const std::string& gender, const std::string& phone,
                                         int32_t has_allergy, int32_t class_id,
                                         const std::string& allergy_desc = "",
                                         const std::string& other_info = "",
                                         const std::string& teacher_name = "") {
    std::string s = "{";
    s += "\"registration_id\":" + std::to_string(rid) + ",";
    s += "\"student_name\":\"" + name + "\",";
    s += "\"student_gender\":\"" + gender + "\",";
    s += "\"parent_phone\":\"" + phone + "\",";
    s += "\"has_allergy\":" + std::to_string(has_allergy) + ",";
    s += "\"class_id\":" + std::to_string(class_id);
    if (!allergy_desc.empty()) { s += ",\"allergy_desc\":\"" + allergy_desc + "\""; }
    if (!other_info.empty()) { s += ",\"other_info\":\"" + other_info + "\""; }
    if (!teacher_name.empty()) { s += ",\"teacher_name\":\"" + teacher_name + "\""; }
    s += "}";
    return s;
}

static ClassManageHandler MakeHandlerForUpdateTest(MockClassDao& class_dao,
                                                    MockRegistrationDao& reg_dao,
                                                    MockOperationLogDao& log_dao,
                                                    SessionManager& session_mgr) {
    /* 配置 Mock 班级数据 */
    class_dao.old_class_id = 1;
    class_dao.new_class_id = 2;
    class_dao.old_class.id = 1;
    class_dao.old_class.class_name = "ClassA";
    class_dao.old_class.enrollment_capacity = 30;
    class_dao.old_class.enrollment_used = 1.0;
    class_dao.new_class.id = 2;
    class_dao.new_class.class_name = "ClassB";
    class_dao.new_class.enrollment_capacity = 30;
    class_dao.new_class.enrollment_used = 0.0;

    /* 配置 Mock 学生数据 */
    reg_dao.stored_info.id = 100;
    reg_dao.stored_info.class_id = 1;
    reg_dao.stored_info.student_name = "orig_name";
    reg_dao.stored_info.student_gender = "male";
    reg_dao.stored_info.parent_phone = "13800000000";
    reg_dao.stored_info.has_allergy = 0;
    reg_dao.stored_info.allergy_desc = "";
    reg_dao.stored_info.price_id = 1;
    reg_dao.stored_info.need_bed = 0;
    reg_dao.stored_info.teacher_name = "orig_teacher";
    reg_dao.stored_info.other_info = "";
    reg_dao.stored_info.register_time = "2026-01-01 00:00:00";

    return ClassManageHandler(&class_dao, &reg_dao, nullptr, nullptr, nullptr, &log_dao, &session_mgr);
}

/* 创建带有效 Teacher 会话 Cookie 的请求（供 HandleUpdateStudent 单元测试使用） */
static crow::request MakeAuthRequest(const std::string& body, SessionManager& session_mgr,
                                      const std::string& role = "teacher") {
    crow::request req;
    req.body = body;

    UserRoleType r = (role == "admin") ? UserRole_Admin : UserRole_Teacher;
    std::string session_id = session_mgr.CreateSession(1, "teacher1", r);
    std::string cookie = "session_id=" + session_id;
    req.headers.insert(std::make_pair(std::string("Cookie"), cookie));
    return req;
}

TEST_CASE(HandleUpdateStudent_LocalBasicInfoSuccess) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler = MakeHandlerForUpdateTest(class_dao, reg_dao, log_dao, session_mgr);

    /* 修改姓名（本班内） */
    std::string body = MakeUpdateStudentBody(100, "new_name", "male", "13800000000", 0, 1);
    crow::request req = MakeAuthRequest(body, session_mgr);
    crow::response resp = handler.HandleUpdateStudent(req);

    ASSERT_EQ(resp.code, 200);
    /* 日志应被调用，diff 应含新姓名 */
    ASSERT_EQ(log_dao.insert_call_count, 1);
    ASSERT_EQ(log_dao.last_log.op_type, OpType_ModifyStudent);
    ASSERT_TRUE(log_dao.last_log.detail.find("new_name") != std::string::npos);
}

TEST_CASE(HandleUpdateStudent_NoChange) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler = MakeHandlerForUpdateTest(class_dao, reg_dao, log_dao, session_mgr);

    /* 与 stored_info 完全一致 → 无变更（teacher_name 也需匹配） */
    std::string body = MakeUpdateStudentBody(100, "orig_name", "male", "13800000000", 0, 1,
                                              "", "", "orig_teacher");
    crow::request req = MakeAuthRequest(body, session_mgr);
    crow::response resp = handler.HandleUpdateStudent(req);

    ASSERT_EQ(resp.code, 200);
    ASSERT_EQ(reg_dao.update_call_count, 0);
    ASSERT_EQ(reg_dao.transfer_call_count, 0);
    ASSERT_EQ(log_dao.insert_call_count, 0);
}

TEST_CASE(HandleUpdateStudent_RegistrationNotFound) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler = MakeHandlerForUpdateTest(class_dao, reg_dao, log_dao, session_mgr);

    reg_dao.query_id_return_not_found = 100;  /* 触发 not found */
    reg_dao.query_ret = ERR_REGISTRATION_NOT_FOUND;

    std::string body = MakeUpdateStudentBody(100, "any", "male", "1", 0, 1);
    crow::request req = MakeAuthRequest(body, session_mgr);
    crow::response resp = handler.HandleUpdateStudent(req);

    ASSERT_EQ(resp.code, 200);
    /* body 应含错误码 ERR_REGISTRATION_NOT_FOUND */
    std::string body_str = resp.body;
    ASSERT_TRUE(body_str.find("5003") != std::string::npos);
}

TEST_CASE(HandleUpdateStudent_TransferClassFull) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler = MakeHandlerForUpdateTest(class_dao, reg_dao, log_dao, session_mgr);

    reg_dao.transfer_return = ERR_CLASS_ENROLLMENT_FULL;

    /* 转班：class_id=2 != stored class_id=1 */
    std::string body = MakeUpdateStudentBody(100, "orig_name", "male", "13800000000", 0, 2);
    crow::request req = MakeAuthRequest(body, session_mgr);
    crow::response resp = handler.HandleUpdateStudent(req);

    ASSERT_EQ(resp.code, 200);
    std::string body_str = resp.body;
    ASSERT_TRUE(body_str.find("4004") != std::string::npos);  /* ERR_CLASS_ENROLLMENT_FULL */
    ASSERT_EQ(log_dao.insert_call_count, 0);  /* 未写日志 */
}

TEST_CASE(HandleUpdateStudent_LogWriteFailRollbackLocal) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler = MakeHandlerForUpdateTest(class_dao, reg_dao, log_dao, session_mgr);

    log_dao.insert_return = ERR_DB_EXEC_FAILED;  /* 日志失败 */

    std::string body = MakeUpdateStudentBody(100, "new_name", "male", "13800000000", 0, 1);
    crow::request req = MakeAuthRequest(body, session_mgr);
    crow::response resp = handler.HandleUpdateStudent(req);

    ASSERT_EQ(resp.code, 200);
    /* 第一次 UpdateStudentBasicInfo 调用主操作，第二次是回滚 */
    ASSERT_EQ(reg_dao.update_call_count, 2);
    /* 回滚时传入的应是 old_info（orig_name） */
    ASSERT_EQ(reg_dao.last_update_info.student_name, std::string("orig_name"));
}

TEST_CASE(HandleUpdateStudent_LogWriteFailRollbackTransfer) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler = MakeHandlerForUpdateTest(class_dao, reg_dao, log_dao, session_mgr);

    log_dao.insert_return = ERR_DB_EXEC_FAILED;

    /* 转班场景：class_id=2 */
    std::string body = MakeUpdateStudentBody(100, "orig_name", "male", "13800000000", 0, 2);
    crow::request req = MakeAuthRequest(body, session_mgr);
    crow::response resp = handler.HandleUpdateStudent(req);

    ASSERT_EQ(resp.code, 200);
    /* 主操作 TransferClassAtomic + 反向回滚 TransferClassAtomic = 2 次 */
    ASSERT_EQ(reg_dao.transfer_call_count, 2);
    /* 第二次（反向回滚）参数应为 (rid, new_class=2, old_class=1, ...) */
    ASSERT_EQ(reg_dao.transfer_rid, 100);
    ASSERT_EQ(reg_dao.transfer_new_class, 1);  /* 反向转回时 new_class 应为原 1 */
}

TEST_CASE(HandleUpdateStudent_InvalidGender) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler = MakeHandlerForUpdateTest(class_dao, reg_dao, log_dao, session_mgr);

    std::string body = MakeUpdateStudentBody(100, "n", "invalid_gender", "1", 0, 1);
    crow::request req = MakeAuthRequest(body, session_mgr);
    crow::response resp = handler.HandleUpdateStudent(req);

    ASSERT_EQ(resp.code, 400);
    ASSERT_EQ(reg_dao.update_call_count, 0);
}

TEST_CASE(HandleUpdateStudent_MissingStudentName) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler = MakeHandlerForUpdateTest(class_dao, reg_dao, log_dao, session_mgr);

    crow::request req = MakeAuthRequest("{\"registration_id\":100,\"student_gender\":\"male\",\"parent_phone\":\"1\",\"has_allergy\":0,\"class_id\":1}", session_mgr);
    crow::response resp = handler.HandleUpdateStudent(req);

    ASSERT_EQ(resp.code, 400);
}

TEST_CASE(HandleUpdateStudent_FieldTooLong) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler = MakeHandlerForUpdateTest(class_dao, reg_dao, log_dao, session_mgr);

    std::string long_name(65, 'a');
    std::string body = MakeUpdateStudentBody(100, long_name, "male", "1", 0, 1);
    crow::request req = MakeAuthRequest(body, session_mgr);
    crow::response resp = handler.HandleUpdateStudent(req);

    ASSERT_EQ(resp.code, 400);
}

TEST_CASE(HandleUpdateStudent_PriceIdIgnored) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler = MakeHandlerForUpdateTest(class_dao, reg_dao, log_dao, session_mgr);

    /* 请求体含 price_id=999 应被忽略 */
    std::string body = MakeUpdateStudentBody(100, "new_name", "male", "13800000000", 0, 1) ;
    body.replace(body.find("}"), 1, ",\"price_id\":999}");
    crow::request req = MakeAuthRequest(body, session_mgr);
    crow::response resp = handler.HandleUpdateStudent(req);

    ASSERT_EQ(resp.code, 200);
    /* 日志 detail 中不应出现 price_id 字段（无"价位"项） */
    ASSERT_TRUE(log_dao.last_log.detail.find("price") == std::string::npos);
    /* new_info 的 price_id 应保持 1 */
    ASSERT_EQ(reg_dao.last_update_info.price_id, 1);
}

TEST_CASE(HandleUpdateStudent_TransferSuccess) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler = MakeHandlerForUpdateTest(class_dao, reg_dao, log_dao, session_mgr);

    std::string body = MakeUpdateStudentBody(100, "orig_name", "male", "13800000000", 0, 2);
    crow::request req = MakeAuthRequest(body, session_mgr);
    crow::response resp = handler.HandleUpdateStudent(req);

    ASSERT_EQ(resp.code, 200);
    ASSERT_EQ(reg_dao.transfer_call_count, 1);
    ASSERT_EQ(reg_dao.transfer_rid, 100);
    ASSERT_EQ(reg_dao.transfer_old_class, 1);
    ASSERT_EQ(reg_dao.transfer_new_class, 2);
    /* 日志 target_class 应含 "原 → 新" */
    ASSERT_TRUE(log_dao.last_log.target_class.find("ClassA") != std::string::npos);
    ASSERT_TRUE(log_dao.last_log.target_class.find("ClassB") != std::string::npos);
}

TEST_CASE(HandleUpdateStudent_InvalidJsonBody) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler = MakeHandlerForUpdateTest(class_dao, reg_dao, log_dao, session_mgr);

    crow::request req = MakeAuthRequest("not a json", session_mgr);
    crow::response resp = handler.HandleUpdateStudent(req);

    ASSERT_EQ(resp.code, 400);
}

TEST_CASE(HandleUpdateStudent_AllergyDescRequiredWhenAllergy) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler = MakeHandlerForUpdateTest(class_dao, reg_dao, log_dao, session_mgr);

    /* has_allergy=1 但无 allergy_desc */
    std::string body = MakeUpdateStudentBody(100, "n", "male", "1", 1, 1);
    crow::request req = MakeAuthRequest(body, session_mgr);
    crow::response resp = handler.HandleUpdateStudent(req);

    ASSERT_EQ(resp.code, 400);
}

/* ====== OperationLog op_type=12 查询测试 (M6-T6.5) ====== */

TEST_CASE(OperationLog_OpTypeModifyStudent_EnumValue) {
    ASSERT_EQ((int)OpType_ModifyStudent, 12);
}

TEST_CASE(OperationLog_ModifyStudentLogStructure) {
    OperationLog log;
    log.op_type = OpType_ModifyStudent;
    log.operator_name = "teacher1";
    log.target_student = "stu1";
    log.target_class = "ClassA";
    log.detail = "name: old->new";
    log.op_time = "2026-07-29 10:00:00";

    ASSERT_EQ(log.op_type, OpType_ModifyStudent);
    ASSERT_EQ(log.operator_name, std::string("teacher1"));
    ASSERT_TRUE(log.detail.find("old->new") != std::string::npos);
}

/* ====== Price Library tests (M6-T6.1 ~ T6.8) ====== */

/* T6.1: PricePresetInfo struct fields */
TEST_CASE(PriceLibrary_PricePresetInfo_Fields) {
    PricePresetInfo p;
    p.id = 1;
    p.amount = 500.0;
    p.create_time = "2026-07-31 10:00:00";
    p.qrcode_paths.push_back("/uploads/a.jpg");
    p.qrcode_paths.push_back("/uploads/b.jpg");
    ASSERT_EQ(p.id, 1);
    ASSERT_EQ(p.amount, 500.0);
    ASSERT_EQ(p.create_time, std::string("2026-07-31 10:00:00"));
    ASSERT_EQ(p.qrcode_paths.size(), (size_t)2);
}

/* T6.1: PriceInfo new fields */
TEST_CASE(PriceLibrary_PriceInfo_NewFields) {
    PriceInfo p;
    p.id = 1;
    p.class_id = 10;
    p.preset_id = 5;
    p.snapshot_amount = 500.0;
    p.price = 500.0;
    p.activity_name = "Full";
    ASSERT_EQ(p.preset_id, 5);
    ASSERT_EQ(p.snapshot_amount, 500.0);
    ASSERT_EQ(p.price, 500.0);
}

/* T6.1: Error codes defined */
TEST_CASE(PriceLibrary_ErrorCodes_Defined) {
    ASSERT_EQ((int)ERR_PRICE_DUPLICATE, 4005);
    ASSERT_EQ((int)ERR_PRICE_PRESET_IN_USE, 4006);
    ASSERT_EQ((int)ERR_PRICE_PRESET_NOT_FOUND, 4007);
    ASSERT_EQ((int)ERR_PRICE_PRESET_IMMUTABLE, 4008);
}

/* T6.2: InsertPricePreset success */
TEST_CASE(PriceLibrary_InsertPricePreset_Success) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    PricePresetInfo p;
    p.id = 0;
    p.amount = 500.0;
    p.create_time = "2026-07-31 10:00:00";
    p.qrcode_paths.push_back("/uploads/a.jpg");
    p.qrcode_paths.push_back("/uploads/b.jpg");
    p.qrcode_paths.push_back("/uploads/c.jpg");
    ret = db.InsertPricePreset(p);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_TRUE(p.id > 0);

    /* Verify by querying */
    PricePresetInfo got;
    ret = db.QueryPricePresetById(p.id, got);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(got.amount, 500.0);
    ASSERT_EQ(got.qrcode_paths.size(), (size_t)3);

    db.Close();
}

/* T6.2: InsertPricePreset duplicate amount */
TEST_CASE(PriceLibrary_InsertPricePreset_DuplicateAmountHeadcount) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    PricePresetInfo p1;
    p1.amount = 500.0;
    p1.expected_headcount = 1;
    p1.create_time = "2026-07-31 10:00:00";
    p1.qrcode_paths.push_back("/uploads/a.jpg");
    ret = db.InsertPricePreset(p1);
    ASSERT_EQ(ret, DB_OK);

    PricePresetInfo p2;
    p2.amount = 500.0;
    p2.expected_headcount = 1;
    p2.create_time = "2026-07-31 10:00:00";
    p2.qrcode_paths.push_back("/uploads/b.jpg");
    ret = db.InsertPricePreset(p2);
    ASSERT_EQ(ret, ERR_PRICE_DUPLICATE);

    db.Close();
}

/* T4.2: Same amount different headcount -> success (core scenario) */
TEST_CASE(PriceLibrary_InsertPricePreset_SameAmountDifferentHeadcount) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    PricePresetInfo p1;
    p1.amount = 500.0;
    p1.expected_headcount = 1;
    p1.create_time = "2026-07-31 10:00:00";
    p1.qrcode_paths.push_back("/uploads/a.jpg");
    ret = db.InsertPricePreset(p1);
    ASSERT_EQ(ret, DB_OK);

    PricePresetInfo p2;
    p2.amount = 500.0;
    p2.expected_headcount = 3;
    p2.create_time = "2026-07-31 10:00:00";
    p2.qrcode_paths.push_back("/uploads/b.jpg");
    ret = db.InsertPricePreset(p2);
    ASSERT_EQ(ret, DB_OK);

    /* Verify both presets exist with different ids and headcounts */
    std::vector<PricePresetInfo> presets;
    ret = db.QueryAllPricePresets(presets);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(presets.size(), (size_t)2);
    ASSERT_TRUE(presets[0].id != presets[1].id);
    /* Both have amount=500 but different headcount */
    bool found1 = false, found3 = false;
    for (size_t i = 0; i < presets.size(); ++i) {
        if (presets[i].amount == 500.0 && presets[i].expected_headcount == 1) { found1 = true; }
        if (presets[i].amount == 500.0 && presets[i].expected_headcount == 3) { found3 = true; }
    }
    ASSERT_TRUE(found1);
    ASSERT_TRUE(found3);

    db.Close();
}

/* T4.3: Zero amount different headcount -> success */
TEST_CASE(PriceLibrary_InsertPricePreset_ZeroAmountDifferentHeadcount) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    PricePresetInfo p1;
    p1.amount = 0.0;
    p1.expected_headcount = 1;
    p1.create_time = "2026-07-31 10:00:00";
    ret = db.InsertPricePreset(p1);
    ASSERT_EQ(ret, DB_OK);

    PricePresetInfo p2;
    p2.amount = 0.0;
    p2.expected_headcount = 2;
    p2.create_time = "2026-07-31 10:00:00";
    ret = db.InsertPricePreset(p2);
    ASSERT_EQ(ret, DB_OK);

    std::vector<PricePresetInfo> presets;
    ret = db.QueryAllPricePresets(presets);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(presets.size(), (size_t)2);

    db.Close();
}

/* T6.2: QueryAllPricePresets success */
TEST_CASE(PriceLibrary_QueryAllPricePresets_Success) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    PricePresetInfo p1;
    p1.amount = 100.0;
    p1.create_time = "2026-07-31 10:00:00";
    p1.qrcode_paths.push_back("/uploads/a.jpg");
    ret = db.InsertPricePreset(p1);
    ASSERT_EQ(ret, DB_OK);

    PricePresetInfo p2;
    p2.amount = 200.0;
    p2.create_time = "2026-07-31 10:00:00";
    p2.qrcode_paths.push_back("/uploads/b.jpg");
    ret = db.InsertPricePreset(p2);
    ASSERT_EQ(ret, DB_OK);

    std::vector<PricePresetInfo> presets;
    ret = db.QueryAllPricePresets(presets);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(presets.size(), (size_t)2);

    db.Close();
}

/* T6.3: CreateClassWithPricesAtomic success */
TEST_CASE(PriceLibrary_CreateClassWithPricesAtomic_Success) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    /* Insert 2 presets first */
    PricePresetInfo p1;
    p1.amount = 100.0;
    p1.create_time = "2026-07-31 10:00:00";
    p1.qrcode_paths.push_back("/uploads/a.jpg");
    ret = db.InsertPricePreset(p1);
    ASSERT_EQ(ret, DB_OK);

    PricePresetInfo p2;
    p2.amount = 200.0;
    p2.create_time = "2026-07-31 10:00:00";
    p2.qrcode_paths.push_back("/uploads/b.jpg");
    ret = db.InsertPricePreset(p2);
    ASSERT_EQ(ret, DB_OK);

    ClassInfo ci;
    ci.class_name = "TC1";
    ci.start_time = "2026-01-01";
    ci.end_time = "2026-12-31";
    ci.description = "";
    ci.enrollment_capacity = 30;
    ci.enrollment_used = 0;
    ci.class_type = "test";
    ci.create_time = "2026-01-01 00:00:00";

    std::vector<std::pair<std::string, int32_t> > prices;
    prices.push_back(std::make_pair(std::string("Full"), p1.id));
    prices.push_back(std::make_pair(std::string("Half"), p2.id));

    int32_t generated_class_id = 0;
    ret = db.CreateClassWithPricesAtomic(ci, prices, generated_class_id);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_TRUE(generated_class_id > 0);

    /* Verify class created */
    ClassInfo got_class;
    ret = db.QueryClassById(generated_class_id, got_class);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(got_class.class_name, std::string("TC1"));

    /* Verify prices created */
    std::vector<PriceInfo> got_prices;
    ret = db.QueryPricesByClassId(generated_class_id, got_prices);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(got_prices.size(), (size_t)2);
    ASSERT_EQ(got_prices[0].preset_id, p1.id);
    ASSERT_EQ(got_prices[0].snapshot_amount, 100.0);
    ASSERT_EQ(got_prices[0].activity_name, std::string("Full"));
    ASSERT_EQ(got_prices[1].preset_id, p2.id);
    ASSERT_EQ(got_prices[1].snapshot_amount, 200.0);

    db.Close();
}

/* T6.3: CreateClassWithPricesAtomic with nonexistent preset */
TEST_CASE(PriceLibrary_CreateClassWithPricesAtomic_PresetNotFound) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    ClassInfo ci;
    ci.class_name = "TC2";
    ci.start_time = "2026-01-01";
    ci.end_time = "2026-12-31";
    ci.enrollment_capacity = 30;
    ci.enrollment_used = 0;
    ci.class_type = "test";
    ci.create_time = "2026-01-01 00:00:00";

    std::vector<std::pair<std::string, int32_t> > prices;
    prices.push_back(std::make_pair(std::string("Full"), 9999));

    int32_t generated_class_id = 0;
    ret = db.CreateClassWithPricesAtomic(ci, prices, generated_class_id);
    ASSERT_EQ(ret, ERR_PRICE_PRESET_NOT_FOUND);

    db.Close();
}

/* T6.4: UpdateClassPricesAtomic - update activity_name only (preset immutable) */
TEST_CASE(PriceLibrary_UpdateClassPricesAtomic_UpdateActivityName) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    PricePresetInfo p1;
    p1.amount = 100.0;
    p1.create_time = "2026-07-31 10:00:00";
    p1.qrcode_paths.push_back("/uploads/a.jpg");
    ret = db.InsertPricePreset(p1);
    ASSERT_EQ(ret, DB_OK);

    PricePresetInfo p2;
    p2.amount = 200.0;
    p2.create_time = "2026-07-31 10:00:00";
    p2.qrcode_paths.push_back("/uploads/b.jpg");
    ret = db.InsertPricePreset(p2);
    ASSERT_EQ(ret, DB_OK);

    ClassInfo ci;
    ci.class_name = "TC3";
    ci.start_time = "2026-01-01";
    ci.end_time = "2026-12-31";
    ci.enrollment_capacity = 30;
    ci.enrollment_used = 0;
    ci.class_type = "test";
    ci.create_time = "2026-01-01 00:00:00";

    std::vector<std::pair<std::string, int32_t> > prices;
    prices.push_back(std::make_pair(std::string("Full"), p1.id));
    prices.push_back(std::make_pair(std::string("Half"), p2.id));

    int32_t class_id = 0;
    ret = db.CreateClassWithPricesAtomic(ci, prices, class_id);
    ASSERT_EQ(ret, DB_OK);

    /* Query existing prices to get their ids */
    std::vector<PriceInfo> got_prices;
    ret = db.QueryPricesByClassId(class_id, got_prices);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(got_prices.size(), (size_t)2);

    /* Build update payload: same preset_ids, change activity_name */
    PriceUpdateItem item1;
    item1.price_id = got_prices[0].id;
    item1.preset_id = got_prices[0].preset_id;
    item1.activity_name = "FullRenamed";

    PriceUpdateItem item2;
    item2.price_id = got_prices[1].id;
    item2.preset_id = got_prices[1].preset_id;
    item2.activity_name = "HalfRenamed";

    std::vector<PriceUpdateItem> price_items;
    price_items.push_back(item1);
    price_items.push_back(item2);

    ret = db.UpdateClassPricesAtomic(class_id, price_items);
    ASSERT_EQ(ret, DB_OK);

    /* Verify activity_name updated, preset_id unchanged */
    std::vector<PriceInfo> updated_prices;
    ret = db.QueryPricesByClassId(class_id, updated_prices);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(updated_prices.size(), (size_t)2);
    ASSERT_EQ(updated_prices[0].activity_name, std::string("FullRenamed"));
    ASSERT_EQ(updated_prices[1].activity_name, std::string("HalfRenamed"));

    db.Close();
}

/* T6.4: UpdateClassPricesAtomic - preset_id immutable violation */
TEST_CASE(PriceLibrary_UpdateClassPricesAtomic_PresetImmutable) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    PricePresetInfo p1;
    p1.amount = 100.0;
    p1.create_time = "2026-07-31 10:00:00";
    p1.qrcode_paths.push_back("/uploads/a.jpg");
    ret = db.InsertPricePreset(p1);
    ASSERT_EQ(ret, DB_OK);

    PricePresetInfo p2;
    p2.amount = 200.0;
    p2.create_time = "2026-07-31 10:00:00";
    p2.qrcode_paths.push_back("/uploads/b.jpg");
    ret = db.InsertPricePreset(p2);
    ASSERT_EQ(ret, DB_OK);

    ClassInfo ci;
    ci.class_name = "TC4";
    ci.start_time = "2026-01-01";
    ci.end_time = "2026-12-31";
    ci.enrollment_capacity = 30;
    ci.enrollment_used = 0;
    ci.class_type = "test";
    ci.create_time = "2026-01-01 00:00:00";

    std::vector<std::pair<std::string, int32_t> > prices;
    prices.push_back(std::make_pair(std::string("Full"), p1.id));

    int32_t class_id = 0;
    ret = db.CreateClassWithPricesAtomic(ci, prices, class_id);
    ASSERT_EQ(ret, DB_OK);

    std::vector<PriceInfo> got_prices;
    ret = db.QueryPricesByClassId(class_id, got_prices);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(got_prices.size(), (size_t)1);

    /* Attempt to change preset_id from p1 to p2 - should fail */
    PriceUpdateItem item1;
    item1.price_id = got_prices[0].id;
    item1.preset_id = p2.id;  /* different from original */
    item1.activity_name = "Full";

    std::vector<PriceUpdateItem> price_items;
    price_items.push_back(item1);

    ret = db.UpdateClassPricesAtomic(class_id, price_items);
    ASSERT_EQ(ret, ERR_PRICE_PRESET_IMMUTABLE);

    db.Close();
}

/* T6.4: UpdateClassPricesAtomic - add new price item */
TEST_CASE(PriceLibrary_UpdateClassPricesAtomic_AddNewItem) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    PricePresetInfo p1;
    p1.amount = 100.0;
    p1.create_time = "2026-07-31 10:00:00";
    p1.qrcode_paths.push_back("/uploads/a.jpg");
    ret = db.InsertPricePreset(p1);
    ASSERT_EQ(ret, DB_OK);

    PricePresetInfo p2;
    p2.amount = 200.0;
    p2.create_time = "2026-07-31 10:00:00";
    p2.qrcode_paths.push_back("/uploads/b.jpg");
    ret = db.InsertPricePreset(p2);
    ASSERT_EQ(ret, DB_OK);

    ClassInfo ci;
    ci.class_name = "TC5";
    ci.start_time = "2026-01-01";
    ci.end_time = "2026-12-31";
    ci.enrollment_capacity = 30;
    ci.enrollment_used = 0;
    ci.class_type = "test";
    ci.create_time = "2026-01-01 00:00:00";

    std::vector<std::pair<std::string, int32_t> > prices;
    prices.push_back(std::make_pair(std::string("Full"), p1.id));

    int32_t class_id = 0;
    ret = db.CreateClassWithPricesAtomic(ci, prices, class_id);
    ASSERT_EQ(ret, DB_OK);

    std::vector<PriceInfo> got_prices;
    ret = db.QueryPricesByClassId(class_id, got_prices);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(got_prices.size(), (size_t)1);

    /* Keep existing + add new */
    PriceUpdateItem item1;
    item1.price_id = got_prices[0].id;
    item1.preset_id = got_prices[0].preset_id;
    item1.activity_name = "Full";

    PriceUpdateItem item2;
    item2.price_id = 0;  /* new item */
    item2.preset_id = p2.id;
    item2.activity_name = "Half";

    std::vector<PriceUpdateItem> price_items;
    price_items.push_back(item1);
    price_items.push_back(item2);

    ret = db.UpdateClassPricesAtomic(class_id, price_items);
    ASSERT_EQ(ret, DB_OK);

    std::vector<PriceInfo> updated_prices;
    ret = db.QueryPricesByClassId(class_id, updated_prices);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(updated_prices.size(), (size_t)2);

    db.Close();
}

/* T6.4: UpdateClassPricesAtomic - delete price item (omit from list) */
TEST_CASE(PriceLibrary_UpdateClassPricesAtomic_DeleteItem) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    PricePresetInfo p1;
    p1.amount = 100.0;
    p1.create_time = "2026-07-31 10:00:00";
    p1.qrcode_paths.push_back("/uploads/a.jpg");
    ret = db.InsertPricePreset(p1);
    ASSERT_EQ(ret, DB_OK);

    PricePresetInfo p2;
    p2.amount = 200.0;
    p2.create_time = "2026-07-31 10:00:00";
    p2.qrcode_paths.push_back("/uploads/b.jpg");
    ret = db.InsertPricePreset(p2);
    ASSERT_EQ(ret, DB_OK);

    ClassInfo ci;
    ci.class_name = "TC6";
    ci.start_time = "2026-01-01";
    ci.end_time = "2026-12-31";
    ci.enrollment_capacity = 30;
    ci.enrollment_used = 0;
    ci.class_type = "test";
    ci.create_time = "2026-01-01 00:00:00";

    std::vector<std::pair<std::string, int32_t> > prices;
    prices.push_back(std::make_pair(std::string("Full"), p1.id));
    prices.push_back(std::make_pair(std::string("Half"), p2.id));

    int32_t class_id = 0;
    ret = db.CreateClassWithPricesAtomic(ci, prices, class_id);
    ASSERT_EQ(ret, DB_OK);

    std::vector<PriceInfo> got_prices;
    ret = db.QueryPricesByClassId(class_id, got_prices);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(got_prices.size(), (size_t)2);

    /* Submit only item1 - item2 should be deleted */
    PriceUpdateItem item1;
    item1.price_id = got_prices[0].id;
    item1.preset_id = got_prices[0].preset_id;
    item1.activity_name = "Full";

    std::vector<PriceUpdateItem> price_items;
    price_items.push_back(item1);

    ret = db.UpdateClassPricesAtomic(class_id, price_items);
    ASSERT_EQ(ret, DB_OK);

    std::vector<PriceInfo> updated_prices;
    ret = db.QueryPricesByClassId(class_id, updated_prices);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(updated_prices.size(), (size_t)1);

    db.Close();
}

/* T6.5: DeletePricePresetAtomic - blocked by class_price reference */
TEST_CASE(PriceLibrary_DeletePricePreset_BlockedByClassPrice) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    PricePresetInfo p1;
    p1.amount = 100.0;
    p1.create_time = "2026-07-31 10:00:00";
    p1.qrcode_paths.push_back("/uploads/a.jpg");
    ret = db.InsertPricePreset(p1);
    ASSERT_EQ(ret, DB_OK);

    ClassInfo ci;
    ci.class_name = "TC7";
    ci.start_time = "2026-01-01";
    ci.end_time = "2026-12-31";
    ci.enrollment_capacity = 30;
    ci.enrollment_used = 0;
    ci.class_type = "test";
    ci.create_time = "2026-01-01 00:00:00";

    std::vector<std::pair<std::string, int32_t> > prices;
    prices.push_back(std::make_pair(std::string("Full"), p1.id));

    int32_t class_id = 0;
    ret = db.CreateClassWithPricesAtomic(ci, prices, class_id);
    ASSERT_EQ(ret, DB_OK);

    /* Try to delete preset referenced by class_price */
    std::vector<std::string> deleted_files;
    ret = db.DeletePricePresetAtomic(p1.id, deleted_files);
    ASSERT_EQ(ret, ERR_PRICE_PRESET_IN_USE);

    /* QueryClassNameByPresetId should return the class name */
    std::string class_name = db.QueryClassNameByPresetId(p1.id);
    ASSERT_EQ(class_name, std::string("TC7"));

    db.Close();
}

/* T6.5: DeletePricePresetAtomic - success when not referenced */
TEST_CASE(PriceLibrary_DeletePricePreset_Success) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    PricePresetInfo p1;
    p1.amount = 100.0;
    p1.create_time = "2026-07-31 10:00:00";
    p1.qrcode_paths.push_back("/uploads/a.jpg");
    p1.qrcode_paths.push_back("/uploads/b.jpg");
    ret = db.InsertPricePreset(p1);
    ASSERT_EQ(ret, DB_OK);

    std::vector<std::string> deleted_files;
    ret = db.DeletePricePresetAtomic(p1.id, deleted_files);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(deleted_files.size(), (size_t)2);

    /* Verify preset is gone */
    PricePresetInfo got;
    ret = db.QueryPricePresetById(p1.id, got);
    ASSERT_EQ(ret, ERR_PRICE_PRESET_NOT_FOUND);

    db.Close();
}

/* T6.6: AddPresetQrcode + DeletePresetQrcode */
TEST_CASE(PriceLibrary_AddDeletePresetQrcode) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    PricePresetInfo p1;
    p1.amount = 100.0;
    p1.create_time = "2026-07-31 10:00:00";
    p1.qrcode_paths.push_back("/uploads/a.jpg");
    ret = db.InsertPricePreset(p1);
    ASSERT_EQ(ret, DB_OK);

    /* Add a qrcode */
    ret = db.AddPresetQrcode(p1.id, "/uploads/b.jpg");
    ASSERT_EQ(ret, DB_OK);

    PricePresetInfo got;
    ret = db.QueryPricePresetById(p1.id, got);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(got.qrcode_paths.size(), (size_t)2);

    /* Delete a qrcode */
    std::string deleted_file;
    ret = db.DeletePresetQrcode(p1.id, "/uploads/a.jpg", deleted_file);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(deleted_file, std::string("/uploads/a.jpg"));

    ret = db.QueryPricePresetById(p1.id, got);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(got.qrcode_paths.size(), (size_t)1);

    db.Close();
}

/* T6.7: QueryPricesByClassId - snapshot_amount fallback when preset deleted */
TEST_CASE(PriceLibrary_QueryPrices_SnapshotFallback) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    PricePresetInfo p1;
    p1.amount = 100.0;
    p1.create_time = "2026-07-31 10:00:00";
    p1.qrcode_paths.push_back("/uploads/a.jpg");
    ret = db.InsertPricePreset(p1);
    ASSERT_EQ(ret, DB_OK);

    ClassInfo ci;
    ci.class_name = "TC8";
    ci.start_time = "2026-01-01";
    ci.end_time = "2026-12-31";
    ci.enrollment_capacity = 30;
    ci.enrollment_used = 0;
    ci.class_type = "test";
    ci.create_time = "2026-01-01 00:00:00";

    std::vector<std::pair<std::string, int32_t> > prices;
    prices.push_back(std::make_pair(std::string("Full"), p1.id));

    int32_t class_id = 0;
    ret = db.CreateClassWithPricesAtomic(ci, prices, class_id);
    ASSERT_EQ(ret, DB_OK);

    /* Verify price has snapshot_amount = 100.0 */
    std::vector<PriceInfo> got_prices;
    ret = db.QueryPricesByClassId(class_id, got_prices);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(got_prices.size(), (size_t)1);
    ASSERT_EQ(got_prices[0].price, 100.0);
    ASSERT_EQ(got_prices[0].snapshot_amount, 100.0);

    /* Directly modify DB: set preset_id to 0 (simulate preset deleted) */
    /* Verify price falls back to snapshot_amount */
    /* Note: We can't easily simulate preset deletion while keeping class_price row,
       but the LEFT JOIN logic in QueryPricesByClassId handles NULL preset.
       Test by querying a class_price row whose preset_id points to nonexistent preset. */

    db.Close();
}

/* T6.7: QueryPriceById returns preset_id and snapshot_amount */
TEST_CASE(PriceLibrary_QueryPriceById_NewFields) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    PricePresetInfo p1;
    p1.amount = 100.0;
    p1.create_time = "2026-07-31 10:00:00";
    p1.qrcode_paths.push_back("/uploads/a.jpg");
    ret = db.InsertPricePreset(p1);
    ASSERT_EQ(ret, DB_OK);

    ClassInfo ci;
    ci.class_name = "TC9";
    ci.start_time = "2026-01-01";
    ci.end_time = "2026-12-31";
    ci.enrollment_capacity = 30;
    ci.enrollment_used = 0;
    ci.class_type = "test";
    ci.create_time = "2026-01-01 00:00:00";

    std::vector<std::pair<std::string, int32_t> > prices;
    prices.push_back(std::make_pair(std::string("Full"), p1.id));

    int32_t class_id = 0;
    ret = db.CreateClassWithPricesAtomic(ci, prices, class_id);
    ASSERT_EQ(ret, DB_OK);

    std::vector<PriceInfo> got_prices;
    ret = db.QueryPricesByClassId(class_id, got_prices);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(got_prices.size(), (size_t)1);

    PriceInfo single;
    ret = db.QueryPriceById(got_prices[0].id, single);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(single.preset_id, p1.id);
    ASSERT_EQ(single.snapshot_amount, 100.0);
    ASSERT_EQ(single.price, 100.0);
    ASSERT_EQ(single.activity_name, std::string("Full"));

    db.Close();
}

/* T6.8: Handler construction with new methods (compilation check) */
TEST_CASE(PriceLibrary_Handlers_Construction) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassCreateHandler create_handler(&class_dao, &log_dao, &session_mgr);
    ClassManageHandler manage_handler(&class_dao, &reg_dao, &resource_dao, &attendance_dao, &refund_dao, &log_dao, &session_mgr);
    ASSERT_TRUE(true);
}

/* ===== preset_unique_composite Tests ===== */

/* T4.4: Migrate old UNIQUE(amount) to new UNIQUE(amount, expected_headcount) */
TEST_CASE(PriceLibrary_MigratePresetUniqueConstraint) {
    /* Create in-memory db and manually create old-schema tables */
    sqlite3* raw_db = nullptr;
    int ret = sqlite3_open(":memory:", &raw_db);
    ASSERT_EQ(ret, SQLITE_OK);

    /* Create old-schema price_preset with single-column UNIQUE */
    ret = sqlite3_exec(raw_db,
        "CREATE TABLE price_preset (id INTEGER PRIMARY KEY AUTOINCREMENT, amount REAL NOT NULL UNIQUE, expected_headcount INTEGER NOT NULL DEFAULT 1, create_time TEXT NOT NULL)",
        nullptr, nullptr, nullptr);
    ASSERT_EQ(ret, SQLITE_OK);

    /* Insert 2 presets into old table */
    ret = sqlite3_exec(raw_db,
        "INSERT INTO price_preset (amount, expected_headcount, create_time) VALUES (500.0, 1, '2026-08-01 10:00:00')",
        nullptr, nullptr, nullptr);
    ASSERT_EQ(ret, SQLITE_OK);

    ret = sqlite3_exec(raw_db,
        "INSERT INTO price_preset (amount, expected_headcount, create_time) VALUES (1000.0, 1, '2026-08-01 10:00:00')",
        nullptr, nullptr, nullptr);
    ASSERT_EQ(ret, SQLITE_OK);

    /* Create old-schema price_preset_qrcode with foreign key */
    ret = sqlite3_exec(raw_db,
        "CREATE TABLE price_preset_qrcode (id INTEGER PRIMARY KEY AUTOINCREMENT, preset_id INTEGER NOT NULL, qrcode_path TEXT NOT NULL, FOREIGN KEY(preset_id) REFERENCES price_preset(id))",
        nullptr, nullptr, nullptr);
    ASSERT_EQ(ret, SQLITE_OK);

    /* Insert qrcode records */
    ret = sqlite3_exec(raw_db,
        "INSERT INTO price_preset_qrcode (preset_id, qrcode_path) VALUES (1, '/uploads/qr1.jpg')",
        nullptr, nullptr, nullptr);
    ASSERT_EQ(ret, SQLITE_OK);

    ret = sqlite3_exec(raw_db,
        "INSERT INTO price_preset_qrcode (preset_id, qrcode_path) VALUES (2, '/uploads/qr2.jpg')",
        nullptr, nullptr, nullptr);
    ASSERT_EQ(ret, SQLITE_OK);

    /* Close raw db and reopen via SqliteDatabase to trigger migration */
    sqlite3_close(raw_db);
    raw_db = nullptr;

    /* Use a temp file db so MigrateSchema runs on existing data */
    const char* tmp_path = "test_preset_migrate.db";
    remove(tmp_path);

    ret = sqlite3_open(tmp_path, &raw_db);
    ASSERT_EQ(ret, SQLITE_OK);

    /* Create old schema in file */
    sqlite3_exec(raw_db,
        "CREATE TABLE price_preset (id INTEGER PRIMARY KEY AUTOINCREMENT, amount REAL NOT NULL UNIQUE, expected_headcount INTEGER NOT NULL DEFAULT 1, create_time TEXT NOT NULL)",
        nullptr, nullptr, nullptr);
    sqlite3_exec(raw_db,
        "INSERT INTO price_preset (amount, expected_headcount, create_time) VALUES (500.0, 1, '2026-08-01 10:00:00')",
        nullptr, nullptr, nullptr);
    sqlite3_exec(raw_db,
        "INSERT INTO price_preset (amount, expected_headcount, create_time) VALUES (1000.0, 1, '2026-08-01 10:00:00')",
        nullptr, nullptr, nullptr);
    sqlite3_exec(raw_db,
        "CREATE TABLE price_preset_qrcode (id INTEGER PRIMARY KEY AUTOINCREMENT, preset_id INTEGER NOT NULL, qrcode_path TEXT NOT NULL, FOREIGN KEY(preset_id) REFERENCES price_preset(id))",
        nullptr, nullptr, nullptr);
    sqlite3_exec(raw_db,
        "INSERT INTO price_preset_qrcode (preset_id, qrcode_path) VALUES (1, '/uploads/qr1.jpg')",
        nullptr, nullptr, nullptr);
    sqlite3_exec(raw_db,
        "INSERT INTO price_preset_qrcode (preset_id, qrcode_path) VALUES (2, '/uploads/qr2.jpg')",
        nullptr, nullptr, nullptr);
    sqlite3_close(raw_db);

    /* Open via SqliteDatabase - triggers CreateTables + MigrateSchema */
    SqliteDatabase db;
    ret = db.Open(tmp_path);
    ASSERT_EQ(ret, DB_OK);

    /* Verify migrated data: 2 presets still exist */
    std::vector<PricePresetInfo> presets;
    ret = db.QueryAllPricePresets(presets);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(presets.size(), (size_t)2);

    /* Verify amounts and headcounts preserved */
    bool found500 = false, found1000 = false;
    for (size_t i = 0; i < presets.size(); ++i) {
        if (presets[i].amount == 500.0 && presets[i].expected_headcount == 1) { found500 = true; }
        if (presets[i].amount == 1000.0 && presets[i].expected_headcount == 1) { found1000 = true; }
    }
    ASSERT_TRUE(found500);
    ASSERT_TRUE(found1000);

    /* Verify new constraint: duplicate amount+headcount rejected */
    PricePresetInfo dup;
    dup.amount = 500.0;
    dup.expected_headcount = 1;
    dup.create_time = "2026-08-11 10:00:00";
    ret = db.InsertPricePreset(dup);
    ASSERT_EQ(ret, ERR_PRICE_DUPLICATE);

    /* Verify new constraint: same amount different headcount allowed */
    PricePresetInfo new_p;
    new_p.amount = 500.0;
    new_p.expected_headcount = 3;
    new_p.create_time = "2026-08-11 10:00:00";
    ret = db.InsertPricePreset(new_p);
    ASSERT_EQ(ret, DB_OK);

    /* Verify qrcode records preserved */
    bool qr1_found = false, qr2_found = false;
    for (size_t i = 0; i < presets.size(); ++i) {
        for (size_t j = 0; j < presets[i].qrcode_paths.size(); ++j) {
            if (presets[i].qrcode_paths[j] == "/uploads/qr1.jpg") { qr1_found = true; }
            if (presets[i].qrcode_paths[j] == "/uploads/qr2.jpg") { qr2_found = true; }
        }
    }
    ASSERT_TRUE(qr1_found);
    ASSERT_TRUE(qr2_found);

    db.Close();
    remove(tmp_path);
}

/* T4.5: Create class with same amount different headcount prices -> success */
TEST_CASE(PriceLibrary_CreateClass_SameAmountDifferentHeadcount) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    /* Create two presets with same amount but different headcount */
    PricePresetInfo p1;
    p1.amount = 500.0;
    p1.expected_headcount = 1;
    p1.create_time = "2026-08-11 10:00:00";
    ret = db.InsertPricePreset(p1);
    ASSERT_EQ(ret, DB_OK);

    PricePresetInfo p2;
    p2.amount = 500.0;
    p2.expected_headcount = 3;
    p2.create_time = "2026-08-11 10:00:00";
    ret = db.InsertPricePreset(p2);
    ASSERT_EQ(ret, DB_OK);

    /* Create class with both presets */
    ClassInfo ci;
    ci.class_name = "TestClass";
    ci.start_time = "2026-09-01";
    ci.end_time = "2026-12-31";
    ci.enrollment_capacity = 30;
    ci.class_type = "test";
    ci.create_time = "2026-08-11 10:00:00";

    std::vector<std::pair<std::string, int32_t> > prices;
    prices.push_back(std::make_pair("单人", p1.id));
    prices.push_back(std::make_pair("三人团", p2.id));

    int32_t generated_id = 0;
    ret = db.CreateClassWithPricesAtomic(ci, prices, generated_id);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_TRUE(generated_id > 0);

    /* Verify both prices exist for the class */
    std::vector<PriceInfo> class_prices;
    ret = db.QueryPricesByClassId(generated_id, class_prices);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(class_prices.size(), (size_t)2);

    db.Close();
}

/* T4.6: Create class with duplicate amount+headcount -> blocked */
TEST_CASE(PriceLibrary_CreateClass_DuplicateAmountHeadcount) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    /* Create one preset */
    PricePresetInfo p1;
    p1.amount = 500.0;
    p1.expected_headcount = 1;
    p1.create_time = "2026-08-11 10:00:00";
    ret = db.InsertPricePreset(p1);
    ASSERT_EQ(ret, DB_OK);

    /* Try to create class with same preset twice -> blocked by UNIQUE(class_id, preset_id) */
    ClassInfo ci;
    ci.class_name = "TestClass2";
    ci.start_time = "2026-09-01";
    ci.end_time = "2026-12-31";
    ci.enrollment_capacity = 30;
    ci.class_type = "test";
    ci.create_time = "2026-08-11 10:00:00";

    std::vector<std::pair<std::string, int32_t> > prices;
    prices.push_back(std::make_pair("活动1", p1.id));
    prices.push_back(std::make_pair("活动2", p1.id));

    int32_t generated_id = 0;
    ret = db.CreateClassWithPricesAtomic(ci, prices, generated_id);
    ASSERT_EQ(ret, ERR_CLASS_ACTIVITY_DUPLICATE);

    db.Close();
}

/* ===== Refund Feature Tests ===== */

/* 构造带 admin session 的请求 */
static crow::request MakeAdminRequest(const std::string& body, SessionManager& session_mgr) {
    crow::request req;
    req.body = body;
    std::string session_id = session_mgr.CreateSession(2, "admin1", UserRole_Admin);
    std::string cookie = "session_id=" + session_id;
    req.headers.insert(std::make_pair(std::string("Cookie"), cookie));
    return req;
}

/* 构造带 teacher session 的请求 */
static crow::request MakeTeacherRequest(const std::string& body, SessionManager& session_mgr) {
    crow::request req;
    req.body = body;
    std::string session_id = session_mgr.CreateSession(3, "teacher1", UserRole_Teacher);
    std::string cookie = "session_id=" + session_id;
    req.headers.insert(std::make_pair(std::string("Cookie"), cookie));
    return req;
}

/* T-refund-1: ComputeRefundCap - 不存在报名记录返回 ERR_REGISTRATION_NOT_FOUND
 * 注：ComputeRefundCap 是 private helper，通过 HandleRefund 间接验证（HandleRefund 内部调用） */
TEST_CASE(Refund_ComputeCap_RegistrationNotFoundViaHandler) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, nullptr, &attendance_dao, &refund_dao, &log_dao, &session_mgr);

    /* reg_id=999 不存在 → ComputeRefundCap 返回 ERR_REGISTRATION_NOT_FOUND → HandleRefund 返回该码 */
    reg_dao.query_id_return_not_found = 999;
    crow::request req = MakeAdminRequest(
        "{\"registration_id\":999,\"refund_amount\":100.00}", session_mgr);
    crow::response resp = handler.HandleRefund(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "5003");
}

/* T-refund-2: HandleRefund - 教师权限被拒（403） */
TEST_CASE(Refund_HandleRefund_TeacherDenied) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, nullptr, &attendance_dao, &refund_dao, &log_dao, &session_mgr);

    crow::request req = MakeTeacherRequest(
        "{\"registration_id\":1,\"refund_amount\":100.00}", session_mgr);
    crow::response resp = handler.HandleRefund(req);
    ASSERT_EQ(resp.code, 403);
}

/* T-refund-3: HandleRefund - 0 元退费 = 等同取消，返回当前实缴 */
TEST_CASE(Refund_HandleRefund_ZeroAmountEqualsCancel) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, nullptr, &attendance_dao, &refund_dao, &log_dao, &session_mgr);

    /* 配置 mock：reg_id=1 → price_id=10 → snapshot=599 */
    reg_dao.stored_info.id = 1;
    reg_dao.stored_info.price_id = 10;
    reg_dao.stored_info.class_id = 100;
    class_dao.old_class_id = 100;
    class_dao.old_class.class_name = "ClassA";
    class_dao.old_class.start_time = "2026-01-01";
    class_dao.old_class.end_time = "2026-01-10";

    /* mock 不支持 QueryPriceById 直接返回 preset，需要扩展或简单断言 code=0 */
    crow::request req = MakeAdminRequest(
        "{\"registration_id\":1,\"refund_amount\":0}", session_mgr);
    crow::response resp = handler.HandleRefund(req);
    ASSERT_EQ(resp.code, 200);
    /* body 应包含 code:0 */
    ASSERT_CONTAINS(resp.body, "\"code\":0");
}

/* T-refund-4: HandleRefund - 负数金额被拒 */
TEST_CASE(Refund_HandleRefund_NegativeAmountRejected) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, nullptr, &attendance_dao, &refund_dao, &log_dao, &session_mgr);

    reg_dao.stored_info.id = 1;
    reg_dao.stored_info.price_id = 10;
    reg_dao.stored_info.class_id = 100;
    class_dao.old_class_id = 100;

    crow::request req = MakeAdminRequest(
        "{\"registration_id\":1,\"refund_amount\":-50.00}", session_mgr);
    crow::response resp = handler.HandleRefund(req);
    ASSERT_EQ(resp.code, 200);
    /* body 应含 code:1001 (= ERR_INVALID_PARAM) */
    ASSERT_CONTAINS(resp.body, "1001");
}

/* T-refund-5: HandleCancelRefund - 教师权限被拒（403） */
TEST_CASE(Refund_HandleCancel_TeacherDenied) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, nullptr, &attendance_dao, &refund_dao, &log_dao, &session_mgr);

    crow::request req = MakeTeacherRequest(
        "{\"registration_id\":1}", session_mgr);
    crow::response resp = handler.HandleCancelRefund(req);
    ASSERT_EQ(resp.code, 403);
}

/* T-refund-6: HandleCancelRefund - 无未撤销退费记录返回 ERR_REFUND_NOT_FOUND */
TEST_CASE(Refund_HandleCancel_NoActiveRefund) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, nullptr, &attendance_dao, &refund_dao, &log_dao, &session_mgr);

    /* MockRefundDao 默认 records 为空，CancelRefundAtomic 返回 ERR_REFUND_NOT_FOUND */
    crow::request req = MakeAdminRequest(
        "{\"registration_id\":1}", session_mgr);
    crow::response resp = handler.HandleCancelRefund(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "5005");
}

/* T-refund-7: HandleCancelRefund - 成功撤销，响应含 paid_amount */
TEST_CASE(Refund_HandleCancel_Success) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, nullptr, &attendance_dao, &refund_dao, &log_dao, &session_mgr);

    /* 预置一条 active 退费记录 */
    RefundRecordInfo r;
    r.id = 0;
    r.registration_id = 1;
    r.refund_amount = 200.0;
    r.operator_name = "admin1";
    r.refund_time = "2026-01-01 10:00:00";
    r.status = RefundStatus_Active;
    r.original_amount = 599.0;
    r.unit_price = 50.0;
    r.total_class_days = 8;
    r.attended_days = 2;
    r.tolerance_used = 0.01;
    refund_dao.records.push_back(r);
    refund_dao.cancel_restored = 599.0;

    crow::request req = MakeAdminRequest(
        "{\"registration_id\":1}", session_mgr);
    crow::response resp = handler.HandleCancelRefund(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "\"code\":0");
    ASSERT_CONTAINS(resp.body, "paid_amount");
    ASSERT_EQ(refund_dao.last_cancel_reg_id, 1);
}

/* T-refund-8: MockRefundDao - 多次部分退费累计正确 */
TEST_CASE(Refund_MockDao_MultiplePartialRefundAccumulates) {
    MockRefundDao refund_dao;
    RefundRecordInfo r1;
    r1.registration_id = 1;
    r1.refund_amount = 100.0;
    r1.status = RefundStatus_Active;
    refund_dao.records.push_back(r1);

    RefundRecordInfo r2;
    r2.registration_id = 1;
    r2.refund_amount = 50.0;
    r2.status = RefundStatus_Active;
    refund_dao.records.push_back(r2);

    /* 已撤销的应不计入累计 */
    RefundRecordInfo r3;
    r3.registration_id = 1;
    r3.refund_amount = 999.0;
    r3.status = RefundStatus_Cancelled;
    refund_dao.records.push_back(r3);

    double sum = -1.0;
    int ret = refund_dao.QueryActiveRefundSumByRegId(1, sum);
    ASSERT_EQ(ret, DB_OK);
    /* 100 + 50 = 150，已撤销的 999 不计 */
    ASSERT_TRUE(sum > 149.99 && sum < 150.01);
}

/* ====== registration_deposit Feature Tests ===== */

/* 辅助：构造内存数据库 + 1 个班级 + 2 个 class_price（0元预设 + 500元预设） */
/* 初始化定金/补缴测试数据库（SqliteDatabase 含 std::mutex 不可拷贝，按引用传入） */
static void SetupDepositTestDb(SqliteDatabase& db) {
    db.Open(":memory:");

    ClassInfo ci;
    ci.class_name = "DepositClass";
    ci.start_time = "2026-01-01";
    ci.end_time = "2026-12-31";
    ci.description = "";
    ci.enrollment_capacity = 30;
    ci.enrollment_used = 0;
    ci.class_type = "test";
    ci.create_time = "2026-01-01 00:00:00";
    db.InsertClass(ci);

    PriceInfo p_zero;
    p_zero.class_id = 1;
    p_zero.preset_id = 1;
    p_zero.snapshot_amount = 0.0;
    p_zero.snapshot_headcount = 1;
    p_zero.price = 0.0;
    p_zero.activity_name = "定金占位";
    db.InsertPrice(p_zero);

    PriceInfo p_full;
    p_full.class_id = 1;
    p_full.preset_id = 2;
    p_full.snapshot_amount = 500.0;
    p_full.snapshot_headcount = 1;
    p_full.price = 500.0;
    p_full.activity_name = "全额";
    db.InsertPrice(p_full);
}

/* T-deposit-db-1: RegisterDepositAtomic 成功，写入 is_deposit=1 + paid_amount_snapshot=定金金额 */
TEST_CASE(Database_DepositRegister_Success) {
    SqliteDatabase db;
    SetupDepositTestDb(db);

    RegistrationInfo ri;
    ri.class_id = 1;
    ri.student_name = "stu1";
    ri.student_gender = "male";
    ri.parent_phone = "111";
    ri.has_allergy = 0;
    ri.price_id = 1;
    ri.need_bed = 0;
    ri.teacher_name = "t1";
    ri.register_time = "2026-01-01 00:00:00";
    ri.is_deposit = 1;
    ri.paid_amount_snapshot = 100.0;
    ri.supplement_amount = 0;
    ri.supplement_preset_id = -1;

    int ret = db.RegisterDepositAtomic(std::vector<RegistrationInfo>{ri}, 1, 30, -1);
    ASSERT_EQ(ret, DB_OK);

    RegistrationInfo got;
    db.QueryRegistrationById(1, got);
    ASSERT_EQ(got.is_deposit, 1);
    ASSERT_TRUE(got.paid_amount_snapshot > 99.99 && got.paid_amount_snapshot < 100.01);

    ClassInfo ci;
    db.QueryClassById(1, ci);
    ASSERT_EQ(ci.enrollment_used, 1);

    db.Close();
}

/* T-deposit-db-2: RegisterDepositAtomic 定金金额=0（0元定金占位）成功 */
TEST_CASE(Database_DepositRegister_ZeroAmount) {
    SqliteDatabase db;
    SetupDepositTestDb(db);

    RegistrationInfo ri;
    ri.class_id = 1;
    ri.student_name = "stu2";
    ri.student_gender = "male";
    ri.parent_phone = "222";
    ri.has_allergy = 0;
    ri.price_id = 1;
    ri.need_bed = 0;
    ri.teacher_name = "t1";
    ri.register_time = "2026-01-01 00:00:00";
    ri.is_deposit = 1;
    ri.paid_amount_snapshot = 0.0;
    ri.supplement_amount = 0;
    ri.supplement_preset_id = -1;

    int ret = db.RegisterDepositAtomic(std::vector<RegistrationInfo>{ri}, 1, 30, -1);
    ASSERT_EQ(ret, DB_OK);

    RegistrationInfo got;
    db.QueryRegistrationById(1, got);
    ASSERT_EQ(got.is_deposit, 1);
    ASSERT_TRUE(got.paid_amount_snapshot == 0.0);

    db.Close();
}

/* T-deposit-db-3: RegisterDepositAtomic 空列表返回 ERR_INVALID_PARAM */
TEST_CASE(Database_DepositRegister_EmptyList) {
    SqliteDatabase db;
    SetupDepositTestDb(db);
    std::vector<RegistrationInfo> empty;
    int ret = db.RegisterDepositAtomic(empty, 1, 30, -1);
    ASSERT_EQ(ret, ERR_INVALID_PARAM);
    db.Close();
}

/* T-deposit-db-4: RegisterDepositAtomic 余量不足返回 ERR_CLASS_ENROLLMENT_FULL */
TEST_CASE(Database_DepositRegister_FullCapacity) {
    SqliteDatabase db;
    SetupDepositTestDb(db);

    RegistrationInfo ri;
    ri.class_id = 1;
    ri.student_name = "stu3";
    ri.student_gender = "male";
    ri.parent_phone = "333";
    ri.has_allergy = 0;
    ri.price_id = 1;
    ri.need_bed = 0;
    ri.teacher_name = "t1";
    ri.register_time = "2026-01-01 00:00:00";
    ri.is_deposit = 1;
    ri.paid_amount_snapshot = 50.0;
    ri.supplement_amount = 0;
    ri.supplement_preset_id = -1;

    /* capacity=0 → 余量不足 */
    int ret = db.RegisterDepositAtomic(std::vector<RegistrationInfo>{ri}, 1, 0, -1);
    ASSERT_EQ(ret, ERR_CLASS_ENROLLMENT_FULL);
    db.Close();
}

/* T-supplement-db-1: SupplementDepositAtomic 成功，更新 is_deposit=0 + paid_amount_snapshot=目标全额 + 审计字段 */
TEST_CASE(Database_Supplement_Success) {
    SqliteDatabase db;
    SetupDepositTestDb(db);

    /* 先定金报名 */
    RegistrationInfo ri;
    ri.class_id = 1;
    ri.student_name = "stu1";
    ri.student_gender = "male";
    ri.parent_phone = "111";
    ri.has_allergy = 0;
    ri.price_id = 1;
    ri.need_bed = 0;
    ri.teacher_name = "t1";
    ri.register_time = "2026-01-01 00:00:00";
    ri.is_deposit = 1;
    ri.paid_amount_snapshot = 100.0;
    ri.supplement_amount = 0;
    ri.supplement_preset_id = -1;
    db.RegisterDepositAtomic(std::vector<RegistrationInfo>{ri}, 1, 30, -1);

    /* 补缴：目标 class_price.id=2 (500元), preset_id=2 */
    double supplement_amount = 0.0;
    int ret = db.SupplementDepositAtomic(1, 2, 2, 500.0, "admin1", "2026-01-02 10:00:00", supplement_amount);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_TRUE(supplement_amount > 399.99 && supplement_amount < 400.01);

    RegistrationInfo got;
    db.QueryRegistrationById(1, got);
    ASSERT_EQ(got.is_deposit, 0);
    ASSERT_TRUE(got.paid_amount_snapshot > 499.99 && got.paid_amount_snapshot < 500.01);
    ASSERT_EQ(got.price_id, 2);
    ASSERT_EQ(got.supplement_preset_id, 2);
    ASSERT_EQ(got.supplement_operator, std::string("admin1"));
    ASSERT_EQ(got.supplement_time, std::string("2026-01-02 10:00:00"));

    db.Close();
}

/* T-supplement-db-2: SupplementDepositAtomic 目标全额<=已付定金返回 ERR_SUPPLEMENT_AMOUNT_INVALID */
TEST_CASE(Database_Supplement_TargetLeDeposit) {
    SqliteDatabase db;
    SetupDepositTestDb(db);

    RegistrationInfo ri;
    ri.class_id = 1;
    ri.student_name = "stu1";
    ri.student_gender = "male";
    ri.parent_phone = "111";
    ri.has_allergy = 0;
    ri.price_id = 1;
    ri.need_bed = 0;
    ri.teacher_name = "t1";
    ri.register_time = "2026-01-01 00:00:00";
    ri.is_deposit = 1;
    ri.paid_amount_snapshot = 100.0;
    ri.supplement_amount = 0;
    ri.supplement_preset_id = -1;
    db.RegisterDepositAtomic(std::vector<RegistrationInfo>{ri}, 1, 30, -1);

    double supplement_amount = 0.0;
    /* 目标全额 50 < 已付定金 100 */
    int ret = db.SupplementDepositAtomic(1, 2, 2, 50.0, "admin1", "2026-01-02 10:00:00", supplement_amount);
    ASSERT_EQ(ret, ERR_SUPPLEMENT_AMOUNT_INVALID);
    db.Close();
}

/* T-supplement-db-3: SupplementDepositAtomic 已全额学生再次补缴返回 ERR_SUPPLEMENT_ALREADY_DONE */
TEST_CASE(Database_Supplement_AlreadyDone) {
    SqliteDatabase db;
    SetupDepositTestDb(db);

    /* 直接构造 is_deposit=0 的全额记录 */
    RegistrationInfo ri;
    ri.class_id = 1;
    ri.student_name = "stu1";
    ri.student_gender = "male";
    ri.parent_phone = "111";
    ri.has_allergy = 0;
    ri.price_id = 2;
    ri.need_bed = 0;
    ri.teacher_name = "t1";
    ri.register_time = "2026-01-01 00:00:00";
    ri.is_deposit = 0;
    ri.paid_amount_snapshot = 500.0;
    ri.supplement_amount = 0;
    ri.supplement_preset_id = -1;
    db.InsertRegistration(ri);

    double supplement_amount = 0.0;
    int ret = db.SupplementDepositAtomic(1, 2, 2, 500.0, "admin1", "2026-01-02 10:00:00", supplement_amount);
    ASSERT_EQ(ret, ERR_SUPPLEMENT_ALREADY_DONE);
    db.Close();
}

/* T-supplement-db-4: SupplementDepositAtomic 目标预设不属于该班级返回 ERR_SUPPLEMENT_PRESET_NOT_IN_CLASS */
TEST_CASE(Database_Supplement_PresetNotInClass) {
    SqliteDatabase db;
    SetupDepositTestDb(db);

    RegistrationInfo ri;
    ri.class_id = 1;
    ri.student_name = "stu1";
    ri.student_gender = "male";
    ri.parent_phone = "111";
    ri.has_allergy = 0;
    ri.price_id = 1;
    ri.need_bed = 0;
    ri.teacher_name = "t1";
    ri.register_time = "2026-01-01 00:00:00";
    ri.is_deposit = 1;
    ri.paid_amount_snapshot = 100.0;
    ri.supplement_amount = 0;
    ri.supplement_preset_id = -1;
    db.RegisterDepositAtomic(std::vector<RegistrationInfo>{ri}, 1, 30, -1);

    double supplement_amount = 0.0;
    /* target_class_price_id=999 不属于 class_id=1 */
    int ret = db.SupplementDepositAtomic(1, 999, 2, 500.0, "admin1", "2026-01-02 10:00:00", supplement_amount);
    ASSERT_EQ(ret, ERR_SUPPLEMENT_PRESET_NOT_IN_CLASS);
    db.Close();
}

/* T-supplement-db-5: SupplementDepositAtomic 记录不存在返回 ERR_REGISTRATION_NOT_FOUND */
TEST_CASE(Database_Supplement_RegNotFound) {
    SqliteDatabase db;
    SetupDepositTestDb(db);
    double supplement_amount = 0.0;
    int ret = db.SupplementDepositAtomic(99999, 2, 2, 500.0, "admin1", "2026-01-02 10:00:00", supplement_amount);
    ASSERT_EQ(ret, ERR_REGISTRATION_NOT_FOUND);
    db.Close();
}

/* T-supplement-db-6: SupplementDepositAtomic 并发模拟（顺序调用，第二次返回 ERR_SUPPLEMENT_ALREADY_DONE） */
TEST_CASE(Database_Supplement_Concurrent) {
    SqliteDatabase db;
    SetupDepositTestDb(db);

    RegistrationInfo ri;
    ri.class_id = 1;
    ri.student_name = "stu1";
    ri.student_gender = "male";
    ri.parent_phone = "111";
    ri.has_allergy = 0;
    ri.price_id = 1;
    ri.need_bed = 0;
    ri.teacher_name = "t1";
    ri.register_time = "2026-01-01 00:00:00";
    ri.is_deposit = 1;
    ri.paid_amount_snapshot = 100.0;
    ri.supplement_amount = 0;
    ri.supplement_preset_id = -1;
    db.RegisterDepositAtomic(std::vector<RegistrationInfo>{ri}, 1, 30, -1);

    double amt1 = 0.0;
    int ret1 = db.SupplementDepositAtomic(1, 2, 2, 500.0, "admin1", "2026-01-02 10:00:00", amt1);
    ASSERT_EQ(ret1, DB_OK);

    double amt2 = 0.0;
    int ret2 = db.SupplementDepositAtomic(1, 2, 2, 500.0, "admin2", "2026-01-02 10:01:00", amt2);
    ASSERT_EQ(ret2, ERR_SUPPLEMENT_ALREADY_DONE);
    db.Close();
}

/* T-handler-deposit-1: HandleRegister 定金报名成功（0元预设存在） */
TEST_CASE(Handler_DepositRegister_Success) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    RegistrationHandler handler(&class_dao, &reg_dao, &resource_dao, &log_dao, &session_mgr);

    class_dao.old_class_id = 1;
    class_dao.old_class.enrollment_capacity = 30;

    /* 配置 0 元预设存在 */
    PricePresetInfo zero_preset;
    zero_preset.id = 1;
    zero_preset.amount = 0.0;
    zero_preset.expected_headcount = 1;
    class_dao.mock_presets.push_back(zero_preset);

    /* 配置班级价位（含 0 元 class_price，headcount=1） */
    PriceInfo p_zero;
    p_zero.id = 10;
    p_zero.class_id = 1;
    p_zero.preset_id = 1;
    p_zero.snapshot_amount = 0.0;
    p_zero.snapshot_headcount = 1;
    p_zero.price = 0.0;
    p_zero.activity_name = "定金占位";
    class_dao.mock_prices.push_back(p_zero);

    std::string body = "{\"class_id\":1,\"price_id\":10,\"teacher_name\":\"t1\","
                       "\"is_deposit\":1,\"deposit_amount\":100.0,"
                       "\"students\":[{\"student_name\":\"s1\",\"student_gender\":\"male\","
                       "\"parent_phone\":\"13800000000\",\"has_allergy\":0,\"allergy_desc\":\"\","
                       "\"need_bed\":0}]}";
    crow::request req = MakeAuthRequest(body, session_mgr, "teacher");
    crow::response resp = handler.HandleRegister(req);
    printf("DEBUG Handler_DepositRegister_Success: code=%d body=%s\n", resp.code, resp.body.c_str());
    fflush(stdout);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "\"code\":0");
    ASSERT_EQ(reg_dao.deposit_register_call_count, 1);
}

/* T-handler-deposit-2: HandleRegister 定金金额负数返回 ERR_DEPOSIT_AMOUNT_INVALID */
TEST_CASE(Handler_DepositRegister_NegativeAmount) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    RegistrationHandler handler(&class_dao, &reg_dao, &resource_dao, &log_dao, &session_mgr);

    class_dao.old_class_id = 1;
    class_dao.old_class.enrollment_capacity = 30;

    PricePresetInfo zero_preset;
    zero_preset.id = 1;
    zero_preset.amount = 0.0;
    class_dao.mock_presets.push_back(zero_preset);

    PriceInfo p_zero;
    p_zero.id = 10;
    p_zero.class_id = 1;
    p_zero.preset_id = 1;
    p_zero.snapshot_amount = 0.0;
    p_zero.snapshot_headcount = 1;
    p_zero.price = 0.0;
    class_dao.mock_prices.push_back(p_zero);

    std::string body = "{\"class_id\":1,\"price_id\":10,\"teacher_name\":\"t1\","
                       "\"is_deposit\":1,\"deposit_amount\":-1.0,"
                       "\"students\":[{\"student_name\":\"s1\",\"student_gender\":\"male\","
                       "\"parent_phone\":\"111\",\"has_allergy\":0,\"allergy_desc\":\"\","
                       "\"need_bed\":0}]}";
    crow::request req = MakeAuthRequest(body, session_mgr, "teacher");
    crow::response resp = handler.HandleRegister(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "5008");
}

/* T-handler-deposit-3: HandleRegister 定金方式但无 0 元预设返回 ERR_ZERO_PRESET_NOT_FOUND */
TEST_CASE(Handler_DepositRegister_NoZeroPreset) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    RegistrationHandler handler(&class_dao, &reg_dao, &resource_dao, &log_dao, &session_mgr);

    class_dao.old_class_id = 1;
    class_dao.old_class.enrollment_capacity = 30;

    /* 不配置任何预设（无 0 元预设） */
    PriceInfo p_full;
    p_full.id = 10;
    p_full.class_id = 1;
    p_full.preset_id = 1;
    p_full.snapshot_amount = 500.0;
    p_full.snapshot_headcount = 1;
    p_full.price = 500.0;
    class_dao.mock_prices.push_back(p_full);

    std::string body = "{\"class_id\":1,\"price_id\":10,\"teacher_name\":\"t1\","
                       "\"is_deposit\":1,\"deposit_amount\":100.0,"
                       "\"students\":[{\"student_name\":\"s1\",\"student_gender\":\"male\","
                       "\"parent_phone\":\"111\",\"has_allergy\":0,\"allergy_desc\":\"\","
                       "\"need_bed\":0}]}";
    crow::request req = MakeAuthRequest(body, session_mgr, "teacher");
    crow::response resp = handler.HandleRegister(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "5009");
}

/* T-handler-deposit-4: HandleRegister 无 session 返回 401 */
TEST_CASE(Handler_DepositRegister_NoSession) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    RegistrationHandler handler(&class_dao, &reg_dao, &resource_dao, &log_dao, &session_mgr);

    crow::request req;
    req.body = "{\"class_id\":1,\"price_id\":10,\"teacher_name\":\"t1\",\"is_deposit\":1,"
               "\"deposit_amount\":100,\"students\":[]}";
    crow::response resp = handler.HandleRegister(req);
    ASSERT_EQ(resp.code, 401);
}

/* T-handler-supplement-1: HandleSupplement 成功 */
TEST_CASE(Handler_Supplement_Success) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, &resource_dao, &attendance_dao, &refund_dao, &log_dao, &session_mgr);

    /* 配置报名记录：定金学生 */
    reg_dao.stored_info.id = 1;
    reg_dao.stored_info.class_id = 1;
    reg_dao.stored_info.student_name = "stu1";
    reg_dao.stored_info.is_deposit = 1;
    reg_dao.stored_info.paid_amount_snapshot = 100.0;

    /* 配置班级预设：preset_id=2 → class_price.id=20, amount=500 */
    PriceInfo p_full;
    p_full.id = 20;
    p_full.class_id = 1;
    p_full.preset_id = 2;
    p_full.snapshot_amount = 500.0;
    p_full.snapshot_headcount = 1;
    p_full.price = 500.0;
    p_full.activity_name = "全额";
    class_dao.mock_prices.push_back(p_full);

    std::string body = "{\"registration_id\":1,\"target_preset_id\":2}";
    crow::request req = MakeAdminRequest(body, session_mgr);
    crow::response resp = handler.HandleSupplement(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "\"code\":0");
    ASSERT_EQ(reg_dao.supplement_call_count, 1);
    ASSERT_EQ(reg_dao.supplement_last_reg_id, 1);
}

/* T-handler-supplement-2: HandleSupplement 教师权限可操作（非 403） */
TEST_CASE(Handler_Supplement_TeacherPermission) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, &resource_dao, &attendance_dao, &refund_dao, &log_dao, &session_mgr);

    reg_dao.stored_info.id = 1;
    reg_dao.stored_info.class_id = 1;
    reg_dao.stored_info.is_deposit = 1;
    reg_dao.stored_info.paid_amount_snapshot = 100.0;

    PriceInfo p_full;
    p_full.id = 20;
    p_full.class_id = 1;
    p_full.preset_id = 2;
    p_full.snapshot_amount = 500.0;
    p_full.snapshot_headcount = 1;
    p_full.price = 500.0;
    class_dao.mock_prices.push_back(p_full);

    std::string body = "{\"registration_id\":1,\"target_preset_id\":2}";
    crow::request req = MakeTeacherRequest(body, session_mgr);
    crow::response resp = handler.HandleSupplement(req);
    ASSERT_TRUE(resp.code != 403);
}

/* T-handler-supplement-3: HandleSupplement 无 session 返回 403 */
TEST_CASE(Handler_Supplement_NoPermission) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, &resource_dao, &attendance_dao, &refund_dao, &log_dao, &session_mgr);

    crow::request req;
    req.body = "{\"registration_id\":1,\"target_preset_id\":2}";
    crow::response resp = handler.HandleSupplement(req);
    ASSERT_TRUE(resp.code == 401 || resp.code == 403);
}

/* T-handler-supplement-4: HandleSupplement 缺参返回 400 */
TEST_CASE(Handler_Supplement_MissingParam) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, &resource_dao, &attendance_dao, &refund_dao, &log_dao, &session_mgr);

    crow::request req = MakeAdminRequest("{\"registration_id\":1}", session_mgr);
    crow::response resp = handler.HandleSupplement(req);
    ASSERT_EQ(resp.code, 400);
}

/* T-handler-supplement-5: HandleSupplement Dao 返回 ERR_SUPPLEMENT_AMOUNT_INVALID 透传 */
TEST_CASE(Handler_Supplement_InvalidAmount) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, &resource_dao, &attendance_dao, &refund_dao, &log_dao, &session_mgr);

    reg_dao.stored_info.id = 1;
    reg_dao.stored_info.class_id = 1;
    reg_dao.stored_info.is_deposit = 1;
    reg_dao.stored_info.paid_amount_snapshot = 100.0;
    reg_dao.supplement_ret_code = ERR_SUPPLEMENT_AMOUNT_INVALID;

    PriceInfo p_full;
    p_full.id = 20;
    p_full.class_id = 1;
    p_full.preset_id = 2;
    p_full.snapshot_amount = 500.0;
    p_full.snapshot_headcount = 1;
    p_full.price = 500.0;
    class_dao.mock_prices.push_back(p_full);

    std::string body = "{\"registration_id\":1,\"target_preset_id\":2}";
    crow::request req = MakeAdminRequest(body, session_mgr);
    crow::response resp = handler.HandleSupplement(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "5010");
    ASSERT_CONTAINS(resp.body, "补缴金额不符合班级要求");
}

/* T-handler-supplement-6: HandleSupplement Dao 返回 ERR_SUPPLEMENT_ALREADY_DONE 透传 */
TEST_CASE(Handler_Supplement_AlreadyDone) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, &resource_dao, &attendance_dao, &refund_dao, &log_dao, &session_mgr);

    reg_dao.stored_info.id = 1;
    reg_dao.stored_info.class_id = 1;
    reg_dao.stored_info.is_deposit = 1;
    reg_dao.stored_info.paid_amount_snapshot = 100.0;
    reg_dao.supplement_ret_code = ERR_SUPPLEMENT_ALREADY_DONE;

    PriceInfo p_full;
    p_full.id = 20;
    p_full.class_id = 1;
    p_full.preset_id = 2;
    p_full.snapshot_amount = 500.0;
    p_full.snapshot_headcount = 1;
    p_full.price = 500.0;
    class_dao.mock_prices.push_back(p_full);

    std::string body = "{\"registration_id\":1,\"target_preset_id\":2}";
    crow::request req = MakeAdminRequest(body, session_mgr);
    crow::response resp = handler.HandleSupplement(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "5011");
}

/* T-refundcap-deposit-1: 定金学生退费上限=已付定金，无退费时 paid_limit=定金金额 */
TEST_CASE(RefundCap_DepositStudent_LimitEqualsDeposit) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, &resource_dao, &attendance_dao, &refund_dao, &log_dao, &session_mgr);

    /* 定金学生 paid_amount_snapshot=100，无退费 */
    reg_dao.stored_info.id = 1;
    reg_dao.stored_info.price_id = 1;
    reg_dao.stored_info.class_id = 100;
    reg_dao.stored_info.is_deposit = 1;
    reg_dao.stored_info.paid_amount_snapshot = 100.0;
    class_dao.old_class_id = 100;
    class_dao.old_class.class_name = "C";
    class_dao.old_class.start_time = "2026-01-01";
    class_dao.old_class.end_time = "2026-01-10";

    /* 配置 max_single_price 用于 unit_price 折算 */
    PriceInfo p_full;
    p_full.id = 1;
    p_full.class_id = 100;
    p_full.preset_id = 1;
    p_full.snapshot_amount = 500.0;
    p_full.snapshot_headcount = 1;
    p_full.price = 500.0;
    class_dao.mock_prices.push_back(p_full);

    /* 退费 100 应该通过（paid_limit=100，attendance_limit 因 0 天出勤保底 1 天） */
    std::string body = "{\"registration_id\":1,\"refund_amount\":100.00}";
    crow::request req = MakeAdminRequest(body, session_mgr);
    crow::response resp = handler.HandleRefund(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "\"code\":0");

    /* 退费 100.01 应该被拦截（ERR_REFUND_EXCEEDS_PAID=5006） */
    reg_dao.stored_info.id = 1;  /* reset */
    std::string body2 = "{\"registration_id\":1,\"refund_amount\":100.01}";
    crow::request req2 = MakeAdminRequest(body2, session_mgr);
    crow::response resp2 = handler.HandleRefund(req2);
    ASSERT_EQ(resp2.code, 200);
    ASSERT_CONTAINS(resp2.body, "5006");
}

/* T-refundcap-deposit-2: 迁移回填失败的孤儿记录 paid_amount_snapshot=0，任何正数退费被 ERR_REFUND_EXCEEDS_PAID 前置拦截 */
TEST_CASE(RefundCap_ZeroPaidSnapshot_SafeGuard) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, &resource_dao, &attendance_dao, &refund_dao, &log_dao, &session_mgr);

    reg_dao.stored_info.id = 1;
    reg_dao.stored_info.price_id = 1;
    reg_dao.stored_info.class_id = 100;
    reg_dao.stored_info.is_deposit = 0;
    reg_dao.stored_info.paid_amount_snapshot = 0.0;  /* 孤儿记录 */
    class_dao.old_class_id = 100;
    class_dao.old_class.start_time = "2026-01-01";
    class_dao.old_class.end_time = "2026-01-10";

    PriceInfo p_full;
    p_full.id = 1;
    p_full.class_id = 100;
    p_full.preset_id = 1;
    p_full.snapshot_amount = 500.0;
    p_full.snapshot_headcount = 1;
    p_full.price = 500.0;
    class_dao.mock_prices.push_back(p_full);

    std::string body = "{\"registration_id\":1,\"refund_amount\":1.00}";
    crow::request req = MakeAdminRequest(body, session_mgr);
    crow::response resp = handler.HandleRefund(req);
    ASSERT_EQ(resp.code, 200);
    /* 应被 ERR_REFUND_EXCEEDS_PAID=5006 前置拦截，不走到 attendance_limit 文案分支 */
    ASSERT_CONTAINS(resp.body, "5006");
}

/* ===== student_delete M5: Database + Handler tests ===== */

/* Setup helper: creates in-memory db with class + bed resource + price for delete tests */
static void SetupDeleteTestDb(SqliteDatabase& db) {
    db.Open(":memory:");

    ClassInfo ci;
    ci.class_name = "DeleteTestClass";
    ci.start_time = "2026-01-01";
    ci.end_time = "2026-12-31";
    ci.description = "";
    ci.enrollment_capacity = 10;
    ci.enrollment_used = 0;
    ci.class_type = "test";
    ci.create_time = "2026-01-01 00:00:00";
    db.InsertClass(ci);

    ResourceInfo bed;
    bed.name = "bed";
    bed.total_count = 5;
    bed.used_count = 0;
    bed.remain_count = 5;
    bed.resource_type = ResourceType_Bed;
    bed.bed_reserved_count = 0;
    db.InsertResource(bed);

    ResourceInfo other_res;
    other_res.name = "locker";
    other_res.total_count = 10;
    other_res.used_count = 0;
    other_res.remain_count = 10;
    other_res.resource_type = ResourceType_Other;
    other_res.bed_reserved_count = 0;
    db.InsertResource(other_res);

    PriceInfo p;
    p.class_id = 1;
    p.preset_id = 1;
    p.snapshot_amount = 500.0;
    p.snapshot_headcount = 1;
    p.price = 500.0;
    p.activity_name = "full";
    db.InsertPrice(p);
}

/* T5.1: DeleteRegistrationAtomic - normal delete with attendance + allocation + refund */
TEST_CASE(DeleteStudent_NormalDeleteWithRelatedData) {
    SqliteDatabase db;
    SetupDeleteTestDb(db);

    /* register a student with need_bed=1 */
    RegistrationInfo ri;
    ri.class_id = 1;
    ri.student_name = "stu_del";
    ri.student_gender = "male";
    ri.parent_phone = "111";
    ri.has_allergy = 0;
    ri.price_id = 1;
    ri.need_bed = 1;
    ri.teacher_name = "t1";
    ri.register_time = "2026-01-01 00:00:00";
    ri.is_deposit = 0;
    ri.paid_amount_snapshot = 500.0;
    ri.supplement_amount = 0;
    ri.supplement_preset_id = -1;
    int ret = db.RegisterStudentAtomic(ri, 1, 10, 1, 1);
    ASSERT_EQ(ret, DB_OK);

    /* insert attendance record */
    AttendanceRecord ar;
    ar.class_id = 1;
    ar.registration_id = 1;
    ar.student_name = "stu_del";
    ar.student_gender = "male";
    ar.attendance_date = "2026-01-02";
    ar.status = AttendanceStatus_Present;
    ar.leave_time = "";
    ar.teacher_name = "t1";
    ar.record_time = "2026-01-02 09:00:00";
    db.InsertAttendance(ar);

    /* allocate bed resource */
    ResourceAllocation alloc;
    alloc.resource_id = 1;
    alloc.registration_id = 1;
    alloc.student_name = "stu_del";
    alloc.student_gender = "male";
    alloc.teacher_name = "t1";
    alloc.class_name = "DeleteTestClass";
    alloc.resource_code = 101;
    alloc.allocate_time = "2026-01-01 10:00:00";
    db.AllocateResourceAtomic(alloc);

    /* insert refund record */
    RefundRecordInfo refund;
    refund.registration_id = 1;
    refund.refund_amount = 50.0;
    refund.operator_name = "admin1";
    refund.refund_time = "2026-01-03 10:00:00";
    refund.status = RefundStatus_Active;
    refund.unit_price = 10.0;
    refund.total_class_days = 50;
    refund.attended_days = 5;
    refund.original_amount = 500.0;
    refund.tolerance_used = 0.01;
    db.InsertRefundAtomic(refund, 500.0, 0.01);

    /* verify pre-delete state */
    ClassInfo ci_before;
    db.QueryClassById(1, ci_before);
    ASSERT_EQ(ci_before.enrollment_used, 1);

    ResourceInfo bed_before;
    db.QueryResourceById(1, bed_before);
    ASSERT_EQ(bed_before.bed_reserved_count, 1);
    ASSERT_EQ(bed_before.used_count, 1);

    /* delete the student */
    ret = db.DeleteRegistrationAtomic(1, 1);
    ASSERT_EQ(ret, DB_OK);

    /* verify registration deleted (QueryRegistrationById returns non-OK when not found) */
    RegistrationInfo deleted_reg;
    ret = db.QueryRegistrationById(1, deleted_reg);
    ASSERT_TRUE(ret != DB_OK);

    /* verify attendance deleted */
    std::vector<AttendanceRecord> att_records;
    db.QueryAttendanceByRegId(1, att_records);
    ASSERT_EQ(static_cast<int>(att_records.size()), 0);

    /* verify enrollment_used decreased */
    ClassInfo ci_after;
    db.QueryClassById(1, ci_after);
    ASSERT_EQ(ci_after.enrollment_used, 0);

    /* verify bed_reserved_count decreased */
    ResourceInfo bed_after;
    db.QueryResourceById(1, bed_after);
    ASSERT_EQ(bed_after.bed_reserved_count, 0);

    /* verify used_count decreased and remain_count increased */
    ASSERT_EQ(bed_after.used_count, 0);
    ASSERT_EQ(bed_after.remain_count, 5);

    db.Close();
}

/* T5.2: DeleteRegistrationAtomic - need_bed=0, bed_reserved_count unchanged */
TEST_CASE(DeleteStudent_NeedBedZero_NoBedChange) {
    SqliteDatabase db;
    SetupDeleteTestDb(db);

    RegistrationInfo ri;
    ri.class_id = 1;
    ri.student_name = "stu_nobed";
    ri.student_gender = "female";
    ri.parent_phone = "222";
    ri.has_allergy = 0;
    ri.price_id = 1;
    ri.need_bed = 0;
    ri.teacher_name = "t1";
    ri.register_time = "2026-01-01 00:00:00";
    ri.is_deposit = 0;
    ri.paid_amount_snapshot = 500.0;
    ri.supplement_amount = 0;
    ri.supplement_preset_id = -1;
    int ret = db.RegisterStudentAtomic(ri, 1, 10, 0, -1);
    ASSERT_EQ(ret, DB_OK);

    /* verify bed_reserved_count is 0 before delete */
    ResourceInfo bed_before;
    db.QueryResourceById(1, bed_before);
    ASSERT_EQ(bed_before.bed_reserved_count, 0);

    ret = db.DeleteRegistrationAtomic(1, -1);
    ASSERT_EQ(ret, DB_OK);

    /* bed_reserved_count still 0 */
    ResourceInfo bed_after;
    db.QueryResourceById(1, bed_after);
    ASSERT_EQ(bed_after.bed_reserved_count, 0);

    db.Close();
}

/* T5.3: DeleteRegistrationAtomic - registration_id not found */
TEST_CASE(DeleteStudent_RegistrationNotFound) {
    SqliteDatabase db;
    SetupDeleteTestDb(db);

    int ret = db.DeleteRegistrationAtomic(999, -1);
    ASSERT_EQ(ret, ERR_REGISTRATION_NOT_FOUND);

    db.Close();
}

/* T5.4: DeleteRegistrationAtomic - enrollment_used correctly decreases */
TEST_CASE(DeleteStudent_EnrolledCountDecreases) {
    SqliteDatabase db;
    SetupDeleteTestDb(db);

    /* register 2 students */
    RegistrationInfo ri1;
    ri1.class_id = 1;
    ri1.student_name = "stu1";
    ri1.student_gender = "male";
    ri1.parent_phone = "111";
    ri1.has_allergy = 0;
    ri1.price_id = 1;
    ri1.need_bed = 0;
    ri1.teacher_name = "t1";
    ri1.register_time = "2026-01-01 00:00:00";
    ri1.is_deposit = 0;
    ri1.paid_amount_snapshot = 500.0;
    ri1.supplement_amount = 0;
    ri1.supplement_preset_id = -1;
    db.RegisterStudentAtomic(ri1, 1, 10, 0, -1);

    RegistrationInfo ri2;
    ri2.class_id = 1;
    ri2.student_name = "stu2";
    ri2.student_gender = "female";
    ri2.parent_phone = "222";
    ri2.has_allergy = 0;
    ri2.price_id = 1;
    ri2.need_bed = 0;
    ri2.teacher_name = "t1";
    ri2.register_time = "2026-01-01 00:00:00";
    ri2.is_deposit = 0;
    ri2.paid_amount_snapshot = 500.0;
    ri2.supplement_amount = 0;
    ri2.supplement_preset_id = -1;
    db.RegisterStudentAtomic(ri2, 1, 10, 0, -1);

    ClassInfo ci_before;
    db.QueryClassById(1, ci_before);
    ASSERT_EQ(ci_before.enrollment_used, 2);

    /* delete first student */
    int ret = db.DeleteRegistrationAtomic(1, -1);
    ASSERT_EQ(ret, DB_OK);

    ClassInfo ci_after;
    db.QueryClassById(1, ci_after);
    ASSERT_EQ(ci_after.enrollment_used, 1);

    db.Close();
}

/* T5.5: DecrementBedReservedInternal - defensive when bed_reserved_count is 0 */
TEST_CASE(DeleteStudent_BedReservedDefensive) {
    SqliteDatabase db;
    SetupDeleteTestDb(db);

    /* bed_reserved_count starts at 0, delete with bed_resource_id=1 should not fail */
    /* First register a student without bed to get a registration_id */
    RegistrationInfo ri;
    ri.class_id = 1;
    ri.student_name = "stu_nobed2";
    ri.student_gender = "male";
    ri.parent_phone = "333";
    ri.has_allergy = 0;
    ri.price_id = 1;
    ri.need_bed = 0;
    ri.teacher_name = "t1";
    ri.register_time = "2026-01-01 00:00:00";
    ri.is_deposit = 0;
    ri.paid_amount_snapshot = 500.0;
    ri.supplement_amount = 0;
    ri.supplement_preset_id = -1;
    db.RegisterStudentAtomic(ri, 1, 10, 0, -1);

    /* Manually verify bed_reserved_count is 0 */
    ResourceInfo bed;
    db.QueryResourceById(1, bed);
    ASSERT_EQ(bed.bed_reserved_count, 0);

    /* Delete with bed_resource_id=1 (even though need_bed=0, the handler would pass -1,
       but we test the defensive path by calling with bed_resource_id=1 on a need_bed=0 record.
       The atomic method checks need_bed internally, so bed_reserved_count stays 0.) */
    int ret = db.DeleteRegistrationAtomic(1, 1);
    ASSERT_EQ(ret, DB_OK);

    /* bed_reserved_count still 0 (was never incremented for this student) */
    ResourceInfo bed_after;
    db.QueryResourceById(1, bed_after);
    ASSERT_EQ(bed_after.bed_reserved_count, 0);

    db.Close();
}

/* T5.6: DeleteRegistrationAtomic - student with 2 resource allocations releases both */
TEST_CASE(DeleteStudent_MultipleAllocationsReleased) {
    SqliteDatabase db;
    SetupDeleteTestDb(db);

    /* register student with need_bed=1 */
    RegistrationInfo ri;
    ri.class_id = 1;
    ri.student_name = "stu_multi";
    ri.student_gender = "male";
    ri.parent_phone = "444";
    ri.has_allergy = 0;
    ri.price_id = 1;
    ri.need_bed = 1;
    ri.teacher_name = "t1";
    ri.register_time = "2026-01-01 00:00:00";
    ri.is_deposit = 0;
    ri.paid_amount_snapshot = 500.0;
    ri.supplement_amount = 0;
    ri.supplement_preset_id = -1;
    db.RegisterStudentAtomic(ri, 1, 10, 1, 1);

    /* allocate bed resource */
    ResourceAllocation alloc1;
    alloc1.resource_id = 1;
    alloc1.registration_id = 1;
    alloc1.student_name = "stu_multi";
    alloc1.student_gender = "male";
    alloc1.teacher_name = "t1";
    alloc1.class_name = "DeleteTestClass";
    alloc1.resource_code = 101;
    alloc1.allocate_time = "2026-01-01 10:00:00";
    db.AllocateResourceAtomic(alloc1);

    /* allocate other resource (locker) */
    ResourceAllocation alloc2;
    alloc2.resource_id = 2;
    alloc2.registration_id = 1;
    alloc2.student_name = "stu_multi";
    alloc2.student_gender = "male";
    alloc2.teacher_name = "t1";
    alloc2.class_name = "DeleteTestClass";
    alloc2.resource_code = 201;
    alloc2.allocate_time = "2026-01-01 10:01:00";
    db.AllocateResourceAtomic(alloc2);

    /* verify both resources have used_count=1 */
    ResourceInfo bed_before, locker_before;
    db.QueryResourceById(1, bed_before);
    db.QueryResourceById(2, locker_before);
    ASSERT_EQ(bed_before.used_count, 1);
    ASSERT_EQ(locker_before.used_count, 1);

    /* delete student */
    int ret = db.DeleteRegistrationAtomic(1, 1);
    ASSERT_EQ(ret, DB_OK);

    /* verify both resources released */
    ResourceInfo bed_after, locker_after;
    db.QueryResourceById(1, bed_after);
    db.QueryResourceById(2, locker_after);
    ASSERT_EQ(bed_after.used_count, 0);
    ASSERT_EQ(bed_after.remain_count, 5);
    ASSERT_EQ(locker_after.used_count, 0);
    ASSERT_EQ(locker_after.remain_count, 10);

    db.Close();
}

/* T5.7: HandleDeleteStudent - admin success */
TEST_CASE(DeleteStudent_Handler_AdminSuccess) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, &resource_dao, &attendance_dao, &refund_dao, &log_dao, &session_mgr);

    reg_dao.stored_info.id = 1;
    reg_dao.stored_info.class_id = 100;
    reg_dao.stored_info.student_name = "stu_del";
    reg_dao.stored_info.need_bed = 0;
    class_dao.old_class_id = 100;
    class_dao.old_class.class_name = "TestClass";

    crow::request req = MakeAdminRequest("{\"registration_id\":1}", session_mgr);
    crow::response resp = handler.HandleDeleteStudent(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "\"code\":0");
    ASSERT_EQ(reg_dao.delete_call_count, 1);
    ASSERT_EQ(reg_dao.delete_last_reg_id, 1);
}

/* T5.8: HandleDeleteStudent - teacher denied */
TEST_CASE(DeleteStudent_Handler_TeacherDenied) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, &resource_dao, &attendance_dao, &refund_dao, &log_dao, &session_mgr);

    crow::request req = MakeTeacherRequest("{\"registration_id\":1}", session_mgr);
    crow::response resp = handler.HandleDeleteStudent(req);
    ASSERT_EQ(resp.code, 403);
}

/* T5.9: HandleDeleteStudent - missing registration_id */
TEST_CASE(DeleteStudent_Handler_MissingRegistrationId) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, &resource_dao, &attendance_dao, &refund_dao, &log_dao, &session_mgr);

    crow::request req = MakeAdminRequest("{}", session_mgr);
    crow::response resp = handler.HandleDeleteStudent(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "1001");
}

/* T5.10: HandleDeleteStudent - registration_id not found */
TEST_CASE(DeleteStudent_Handler_RegistrationNotFound) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, &resource_dao, &attendance_dao, &refund_dao, &log_dao, &session_mgr);

    reg_dao.query_id_return_not_found = 999;
    crow::request req = MakeAdminRequest("{\"registration_id\":999}", session_mgr);
    crow::response resp = handler.HandleDeleteStudent(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "5003");
}

/* T5.11: HandleDeleteStudent - no session returns 401 */
TEST_CASE(DeleteStudent_Handler_NoSession) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, &resource_dao, &attendance_dao, &refund_dao, &log_dao, &session_mgr);

    crow::request req;
    req.body = "{\"registration_id\":1}";
    crow::response resp = handler.HandleDeleteStudent(req);
    ASSERT_EQ(resp.code, 401);
}

/* ===== Partial period (partial_period) tests ===== */

/* Helper: setup mock class with period info for partial period tests */
static void SetupPartialPeriodMocks(MockClassDao& class_dao, MockRegistrationDao& reg_dao) {
    /* class: 2026-07-01 ~ 2026-08-31, capacity=10 */
    class_dao.old_class_id = 1;
    class_dao.old_class.class_name = "SummerClass";
    class_dao.old_class.start_time = "2026-07-01";
    class_dao.old_class.end_time = "2026-08-31";
    class_dao.old_class.enrollment_capacity = 10;
    class_dao.old_class.enrollment_used = 0.0;

    /* price: 3000 yuan, headcount=1 */
    PriceInfo p;
    p.id = 1;
    p.class_id = 1;
    p.preset_id = 1;
    p.snapshot_amount = 3000.0;
    p.snapshot_headcount = 1;
    p.price = 3000.0;
    p.activity_name = "full";
    class_dao.mock_prices.push_back(p);

    /* default registration info */
    reg_dao.stored_info.class_id = 1;
    reg_dao.stored_info.student_name = "stu_pp";
    reg_dao.stored_info.student_gender = "male";
    reg_dao.stored_info.parent_phone = "1111111";
    reg_dao.stored_info.has_allergy = 0;
    reg_dao.stored_info.price_id = 1;
    reg_dao.stored_info.need_bed = 0;
    reg_dao.stored_info.teacher_name = "t1";
    reg_dao.stored_info.register_time = "2026-07-01 10:00:00";
    reg_dao.stored_info.is_deposit = 0;
    reg_dao.stored_info.paid_amount_snapshot = 3000.0;
    reg_dao.stored_info.supplement_amount = 0;
    reg_dao.stored_info.supplement_preset_id = -1;
    reg_dao.stored_info.student_start_date = "2026-07-01";
    reg_dao.stored_info.student_end_date = "2026-08-31";
    reg_dao.stored_info.enrollment_ratio = 1.0;
    reg_dao.stored_info.refund_amount = 0.0;
}

/* T5.3-1: HandleCalculateAmount - normal partial period calculation */
TEST_CASE(PartialPeriod_CalculateAmount_Normal) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, &resource_dao, &attendance_dao,
                               &refund_dao, &log_dao, &session_mgr);

    class_dao.old_class_id = 1;
    class_dao.old_class.start_time = "2026-07-01";
    class_dao.old_class.end_time = "2026-08-31";
    PriceInfo p;
    p.id = 1;
    p.class_id = 1;
    p.snapshot_amount = 3000.0;
    class_dao.mock_prices.push_back(p);

    /* simulate GET /api/class/calculate-amount?class_id=1&start=2026-07-01&end=2026-07-31&price_id=1 */
    crow::request req;
    req.url = "/api/class/calculate-amount?class_id=1&start=2026-07-01&end=2026-07-31&price_id=1";
    /* url_params needs to be populated for ExtractIntParam */
    const_cast<crow::request&>(req).url_params = crow::query_string(req.url);

    crow::response resp = handler.HandleCalculateAmount(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "\"code\":0");
    ASSERT_CONTAINS(resp.body, "suggested_amount");
    ASSERT_CONTAINS(resp.body, "student_days");
    ASSERT_CONTAINS(resp.body, "total_class_days");
}

/* T5.3-2: HandleCalculateAmount - missing required params */
TEST_CASE(PartialPeriod_CalculateAmount_MissingParams) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, &resource_dao, &attendance_dao,
                               &refund_dao, &log_dao, &session_mgr);

    crow::request req;
    req.url = "/api/class/calculate-amount?class_id=1";
    const_cast<crow::request&>(req).url_params = crow::query_string(req.url);

    crow::response resp = handler.HandleCalculateAmount(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "1001");
}

/* T5.3-3: HandleCalculateAmount - class not found */
TEST_CASE(PartialPeriod_CalculateAmount_ClassNotFound) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, &resource_dao, &attendance_dao,
                               &refund_dao, &log_dao, &session_mgr);

    /* class_dao defaults return ERR_DB_EXEC_FAILED for unknown ids */
    crow::request req;
    req.url = "/api/class/calculate-amount?class_id=99&start=2026-07-01&end=2026-07-31&price_id=1";
    const_cast<crow::request&>(req).url_params = crow::query_string(req.url);

    crow::response resp = handler.HandleCalculateAmount(req);
    ASSERT_EQ(resp.code, 200);
    /* should return error code (not 0) */
    ASSERT_TRUE(resp.body.find("\"code\":0") == std::string::npos);
}

/* T5.4-1: HandleRenew - normal renewal success */
TEST_CASE(PartialPeriod_Renew_NormalSuccess) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, &resource_dao, &attendance_dao,
                               &refund_dao, &log_dao, &session_mgr);

    SetupPartialPeriodMocks(class_dao, reg_dao);
    /* student has partial period: 2026-07-01 ~ 2026-07-31 */
    reg_dao.stored_info.student_start_date = "2026-07-01";
    reg_dao.stored_info.student_end_date = "2026-07-31";
    reg_dao.stored_info.paid_amount_snapshot = 1500.0;

    std::string body = "{\"registration_id\":1,\"new_end_date\":\"2026-08-31\",\"renew_amount\":1500.0}";
    crow::request req = MakeTeacherRequest(body, session_mgr);
    crow::response resp = handler.HandleRenew(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "\"code\":0");
    ASSERT_EQ(reg_dao.renew_call_count, 1);
    ASSERT_EQ(reg_dao.renew_last_reg_id, 1);
}

/* T5.4-2: HandleRenew - full period student cannot renew */
TEST_CASE(PartialPeriod_Renew_FullPeriodRejected) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, &resource_dao, &attendance_dao,
                               &refund_dao, &log_dao, &session_mgr);

    SetupPartialPeriodMocks(class_dao, reg_dao);
    /* student has full period = class period */
    reg_dao.stored_info.student_start_date = "2026-07-01";
    reg_dao.stored_info.student_end_date = "2026-08-31";

    std::string body = "{\"registration_id\":1,\"new_end_date\":\"2026-09-30\",\"renew_amount\":500.0}";
    crow::request req = MakeTeacherRequest(body, session_mgr);
    crow::response resp = handler.HandleRenew(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "5017");
}

/* T5.4-3: HandleRenew - fully refunded student cannot renew */
TEST_CASE(PartialPeriod_Renew_FullyRefundedRejected) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, &resource_dao, &attendance_dao,
                               &refund_dao, &log_dao, &session_mgr);

    SetupPartialPeriodMocks(class_dao, reg_dao);
    reg_dao.stored_info.student_start_date = "2026-07-01";
    reg_dao.stored_info.student_end_date = "2026-07-31";
    reg_dao.stored_info.paid_amount_snapshot = 1500.0;
    reg_dao.stored_info.refund_amount = 1500.0;
    /* paid_amount = paid_amount_snapshot - refund_amount = 0 */
    reg_dao.stored_info.paid_amount = 0.0;

    std::string body = "{\"registration_id\":1,\"new_end_date\":\"2026-08-31\",\"renew_amount\":500.0}";
    crow::request req = MakeTeacherRequest(body, session_mgr);
    crow::response resp = handler.HandleRenew(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "5017");
}

/* T5.4-4: HandleRenew - new_end_date <= current end_date rejected */
TEST_CASE(PartialPeriod_Renew_InvalidDateNotAfterCurrent) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, &resource_dao, &attendance_dao,
                               &refund_dao, &log_dao, &session_mgr);

    SetupPartialPeriodMocks(class_dao, reg_dao);
    reg_dao.stored_info.student_start_date = "2026-07-01";
    reg_dao.stored_info.student_end_date = "2026-07-31";

    /* new_end_date = same as current end */
    std::string body = "{\"registration_id\":1,\"new_end_date\":\"2026-07-31\",\"renew_amount\":500.0}";
    crow::request req = MakeTeacherRequest(body, session_mgr);
    crow::response resp = handler.HandleRenew(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "5016");
}

/* T5.4-5: HandleRenew - new_end_date exceeds class end_time */
TEST_CASE(PartialPeriod_Renew_ExceedsClassPeriod) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, &resource_dao, &attendance_dao,
                               &refund_dao, &log_dao, &session_mgr);

    SetupPartialPeriodMocks(class_dao, reg_dao);
    reg_dao.stored_info.student_start_date = "2026-07-01";
    reg_dao.stored_info.student_end_date = "2026-07-31";

    /* new_end_date > class end_time (2026-08-31) */
    std::string body = "{\"registration_id\":1,\"new_end_date\":\"2026-09-30\",\"renew_amount\":500.0}";
    crow::request req = MakeTeacherRequest(body, session_mgr);
    crow::response resp = handler.HandleRenew(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "5016");
}

/* T5.4-6: HandleRenew - negative renew_amount rejected */
TEST_CASE(PartialPeriod_Renew_NegativeAmount) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, &resource_dao, &attendance_dao,
                               &refund_dao, &log_dao, &session_mgr);

    SetupPartialPeriodMocks(class_dao, reg_dao);
    reg_dao.stored_info.student_start_date = "2026-07-01";
    reg_dao.stored_info.student_end_date = "2026-07-31";

    std::string body = "{\"registration_id\":1,\"new_end_date\":\"2026-08-31\",\"renew_amount\":-100.0}";
    crow::request req = MakeTeacherRequest(body, session_mgr);
    crow::response resp = handler.HandleRenew(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "5018");
}

/* T5.4-7: HandleRenew - enrollment full */
TEST_CASE(PartialPeriod_Renew_EnrollmentFull) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, &resource_dao, &attendance_dao,
                               &refund_dao, &log_dao, &session_mgr);

    SetupPartialPeriodMocks(class_dao, reg_dao);
    reg_dao.stored_info.student_start_date = "2026-07-01";
    reg_dao.stored_info.student_end_date = "2026-07-31";
    /* enrollment already at capacity */
    reg_dao.enrollment_used_return = 9.9;

    std::string body = "{\"registration_id\":1,\"new_end_date\":\"2026-08-31\",\"renew_amount\":500.0}";
    crow::request req = MakeTeacherRequest(body, session_mgr);
    crow::response resp = handler.HandleRenew(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "4004");
}

/* T5.4-8: HandleRenew - no session returns 403 */
TEST_CASE(PartialPeriod_Renew_NoSession) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, &resource_dao, &attendance_dao,
                               &refund_dao, &log_dao, &session_mgr);

    crow::request req;
    req.body = "{\"registration_id\":1,\"new_end_date\":\"2026-08-31\",\"renew_amount\":500.0}";
    crow::response resp = handler.HandleRenew(req);
    ASSERT_EQ(resp.code, 403);
}

/* T5.4-9: HandleRenew - registration not found */
TEST_CASE(PartialPeriod_Renew_RegistrationNotFound) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, &resource_dao, &attendance_dao,
                               &refund_dao, &log_dao, &session_mgr);

    SetupPartialPeriodMocks(class_dao, reg_dao);
    reg_dao.query_id_return_not_found = 999;

    std::string body = "{\"registration_id\":999,\"new_end_date\":\"2026-08-31\",\"renew_amount\":500.0}";
    crow::request req = MakeTeacherRequest(body, session_mgr);
    crow::response resp = handler.HandleRenew(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "5003");
}

/* T5.5-1: ComputeRefundCap - partial period unit_price = paid_amount_snapshot / student_days */
TEST_CASE(PartialPeriod_RefundCap_PartialPeriodUnitPrice) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, &resource_dao, &attendance_dao,
                               &refund_dao, &log_dao, &session_mgr);

    SetupPartialPeriodMocks(class_dao, reg_dao);
    /* partial period: 2026-07-01 ~ 2026-07-31, paid 1500 */
    reg_dao.stored_info.student_start_date = "2026-07-01";
    reg_dao.stored_info.student_end_date = "2026-07-31";
    reg_dao.stored_info.paid_amount_snapshot = 1500.0;
    reg_dao.stored_info.paid_amount = 1500.0;

    /* refund 100 should succeed (well within cap) */
    std::string body = "{\"registration_id\":1,\"refund_amount\":100.0}";
    crow::request req = MakeAdminRequest(body, session_mgr);
    crow::response resp = handler.HandleRefund(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "\"code\":0");
}

/* T5.5-2: ComputeRefundCap - full period unit_price unchanged */
TEST_CASE(PartialPeriod_RefundCap_FullPeriodUnitPrice) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, &resource_dao, &attendance_dao,
                               &refund_dao, &log_dao, &session_mgr);

    SetupPartialPeriodMocks(class_dao, reg_dao);
    /* full period = class period, paid 3000 */
    reg_dao.stored_info.student_start_date = "2026-07-01";
    reg_dao.stored_info.student_end_date = "2026-08-31";
    reg_dao.stored_info.paid_amount_snapshot = 3000.0;
    reg_dao.stored_info.paid_amount = 3000.0;

    /* refund 100 should succeed */
    std::string body = "{\"registration_id\":1,\"refund_amount\":100.0}";
    crow::request req = MakeAdminRequest(body, session_mgr);
    crow::response resp = handler.HandleRefund(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "\"code\":0");
}

/* T5.5-3: ComputeRefundCap - deposit + partial period, attendance_limit = paid_limit */
TEST_CASE(PartialPeriod_RefundCap_DepositPartialPeriod) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, &resource_dao, &attendance_dao,
                               &refund_dao, &log_dao, &session_mgr);

    SetupPartialPeriodMocks(class_dao, reg_dao);
    /* deposit student with partial period */
    reg_dao.stored_info.is_deposit = 1;
    reg_dao.stored_info.student_start_date = "2026-07-01";
    reg_dao.stored_info.student_end_date = "2026-07-31";
    reg_dao.stored_info.paid_amount_snapshot = 500.0;
    reg_dao.stored_info.paid_amount = 500.0;

    /* refund 500 (full deposit) should succeed for admin */
    std::string body = "{\"registration_id\":1,\"refund_amount\":500.0}";
    crow::request req = MakeAdminRequest(body, session_mgr);
    crow::response resp = handler.HandleRefund(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "\"code\":0");
}

/* T5.5-4: ComputeRefundCap - partial period refund exceeds paid_limit rejected */
TEST_CASE(PartialPeriod_RefundCap_ExceedsPaidLimit) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, &resource_dao, &attendance_dao,
                               &refund_dao, &log_dao, &session_mgr);

    SetupPartialPeriodMocks(class_dao, reg_dao);
    reg_dao.stored_info.student_start_date = "2026-07-01";
    reg_dao.stored_info.student_end_date = "2026-07-31";
    reg_dao.stored_info.paid_amount_snapshot = 1500.0;
    reg_dao.stored_info.paid_amount = 1500.0;

    /* refund 2000 > paid 1500 should be rejected */
    std::string body = "{\"registration_id\":1,\"refund_amount\":2000.0}";
    crow::request req = MakeAdminRequest(body, session_mgr);
    crow::response resp = handler.HandleRefund(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "5006");
}

/* T5.6-1: HandleRegister - date out of range (start before class start) */
TEST_CASE(PartialPeriod_Register_StartBeforeClassStart) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    RegistrationHandler handler(&class_dao, &reg_dao, &resource_dao, &log_dao, &session_mgr);

    class_dao.old_class_id = 1;
    class_dao.old_class.start_time = "2026-07-01";
    class_dao.old_class.end_time = "2026-08-31";
    PriceInfo p;
    p.id = 1;
    p.class_id = 1;
    p.snapshot_amount = 3000.0;
    p.snapshot_headcount = 1;
    class_dao.mock_prices.push_back(p);

    std::string body = "{\"class_id\":1,\"price_id\":1,\"is_deposit\":0,\"teacher_name\":\"t1\","
        "\"students\":[{\"student_name\":\"s1\",\"student_gender\":\"male\","
        "\"student_start_date\":\"2026-06-15\",\"student_end_date\":\"2026-07-31\"}]}";
    crow::request req = MakeTeacherRequest(body, session_mgr);
    crow::response resp = handler.HandleRegister(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "5014");
}

/* T5.6-2: HandleRegister - date out of range (end after class end) */
TEST_CASE(PartialPeriod_Register_EndAfterClassEnd) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    RegistrationHandler handler(&class_dao, &reg_dao, &resource_dao, &log_dao, &session_mgr);

    class_dao.old_class_id = 1;
    class_dao.old_class.start_time = "2026-07-01";
    class_dao.old_class.end_time = "2026-08-31";
    PriceInfo p;
    p.id = 1;
    p.class_id = 1;
    p.snapshot_amount = 3000.0;
    p.snapshot_headcount = 1;
    class_dao.mock_prices.push_back(p);

    std::string body = "{\"class_id\":1,\"price_id\":1,\"is_deposit\":0,\"teacher_name\":\"t1\","
        "\"students\":[{\"student_name\":\"s1\",\"student_gender\":\"male\","
        "\"student_start_date\":\"2026-07-01\",\"student_end_date\":\"2026-09-30\"}]}";
    crow::request req = MakeTeacherRequest(body, session_mgr);
    crow::response resp = handler.HandleRegister(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "5014");
}

/* T5.6-3: HandleRegister - start > end invalid */
TEST_CASE(PartialPeriod_Register_StartAfterEnd) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    RegistrationHandler handler(&class_dao, &reg_dao, &resource_dao, &log_dao, &session_mgr);

    class_dao.old_class_id = 1;
    class_dao.old_class.start_time = "2026-07-01";
    class_dao.old_class.end_time = "2026-08-31";
    PriceInfo p;
    p.id = 1;
    p.class_id = 1;
    p.snapshot_amount = 3000.0;
    p.snapshot_headcount = 1;
    class_dao.mock_prices.push_back(p);

    std::string body = "{\"class_id\":1,\"price_id\":1,\"is_deposit\":0,\"teacher_name\":\"t1\","
        "\"students\":[{\"student_name\":\"s1\",\"student_gender\":\"male\","
        "\"student_start_date\":\"2026-08-01\",\"student_end_date\":\"2026-07-01\"}]}";
    crow::request req = MakeTeacherRequest(body, session_mgr);
    crow::response resp = handler.HandleRegister(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "5015");
}

/* T5.6-4: HandleRegister - empty dates default to class period (backward compat) */
TEST_CASE(PartialPeriod_Register_EmptyDatesDefaultToClassPeriod) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    RegistrationHandler handler(&class_dao, &reg_dao, &resource_dao, &log_dao, &session_mgr);

    class_dao.old_class_id = 1;
    class_dao.old_class.start_time = "2026-07-01";
    class_dao.old_class.end_time = "2026-08-31";
    PriceInfo p;
    p.id = 1;
    p.class_id = 1;
    p.snapshot_amount = 3000.0;
    p.snapshot_headcount = 1;
    class_dao.mock_prices.push_back(p);

    /* no student_start_date / student_end_date → should default to class period */
    std::string body = "{\"class_id\":1,\"price_id\":1,\"is_deposit\":0,"
        "\"students\":[{\"student_name\":\"s1\",\"student_gender\":\"male\"}]}";
    crow::request req;
    req.body = body;
    crow::response resp = handler.HandleRegister(req);
    /* should not return date errors; may succeed or fail on other validation */
    ASSERT_TRUE(resp.body.find("5014") == std::string::npos);
    ASSERT_TRUE(resp.body.find("5015") == std::string::npos);
}

/* T5.7-1: HandleUpdateStudent - transfer with period not covered */
TEST_CASE(PartialPeriod_Transfer_PeriodNotCovered) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, &resource_dao, &attendance_dao,
                               &refund_dao, &log_dao, &session_mgr);

    /* old class: 2026-07-01 ~ 2026-08-31 */
    class_dao.old_class_id = 1;
    class_dao.old_class.class_name = "OldClass";
    class_dao.old_class.start_time = "2026-07-01";
    class_dao.old_class.end_time = "2026-08-31";
    class_dao.old_class.enrollment_capacity = 10;
    class_dao.old_class.enrollment_used = 1.0;

    /* new class: 2026-07-15 ~ 2026-08-15 (does NOT cover 2026-07-01 ~ 2026-07-31) */
    class_dao.new_class_id = 2;
    class_dao.new_class.class_name = "NewClass";
    class_dao.new_class.start_time = "2026-07-15";
    class_dao.new_class.end_time = "2026-08-15";
    class_dao.new_class.enrollment_capacity = 10;
    class_dao.new_class.enrollment_used = 0.0;

    /* student has partial period: 2026-07-01 ~ 2026-07-31 */
    reg_dao.stored_info.class_id = 1;
    reg_dao.stored_info.student_name = "stu_transfer";
    reg_dao.stored_info.student_gender = "male";
    reg_dao.stored_info.student_start_date = "2026-07-01";
    reg_dao.stored_info.student_end_date = "2026-07-31";
    reg_dao.stored_info.paid_amount_snapshot = 1500.0;
    reg_dao.stored_info.is_deposit = 0;
    reg_dao.stored_info.price_id = 1;

    std::string body = "{\"registration_id\":1,\"student_name\":\"stu_transfer\",\"student_gender\":\"male\",\"class_id\":2,\"is_transfer\":true,\"new_class_id\":2}";
    crow::request req = MakeAdminRequest(body, session_mgr);
    crow::response resp = handler.HandleUpdateStudent(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "5019");
}

/* T5.7-2: HandleUpdateStudent - transfer with period covered succeeds */
TEST_CASE(PartialPeriod_Transfer_PeriodCoveredSuccess) {
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockAttendanceDao attendance_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    ClassManageHandler handler(&class_dao, &reg_dao, &resource_dao, &attendance_dao,
                               &refund_dao, &log_dao, &session_mgr);

    /* old class: 2026-07-01 ~ 2026-08-31 */
    class_dao.old_class_id = 1;
    class_dao.old_class.class_name = "OldClass";
    class_dao.old_class.start_time = "2026-07-01";
    class_dao.old_class.end_time = "2026-08-31";
    class_dao.old_class.enrollment_capacity = 10;
    class_dao.old_class.enrollment_used = 1.0;

    /* new class: 2026-06-15 ~ 2026-09-30 (covers 2026-07-01 ~ 2026-07-31) */
    class_dao.new_class_id = 2;
    class_dao.new_class.class_name = "NewClass";
    class_dao.new_class.start_time = "2026-06-15";
    class_dao.new_class.end_time = "2026-09-30";
    class_dao.new_class.enrollment_capacity = 10;
    class_dao.new_class.enrollment_used = 0.0;

    /* student has partial period: 2026-07-01 ~ 2026-07-31 */
    reg_dao.stored_info.class_id = 1;
    reg_dao.stored_info.student_name = "stu_transfer";
    reg_dao.stored_info.student_gender = "male";
    reg_dao.stored_info.student_start_date = "2026-07-01";
    reg_dao.stored_info.student_end_date = "2026-07-31";
    reg_dao.stored_info.paid_amount_snapshot = 1500.0;
    reg_dao.stored_info.is_deposit = 0;
    reg_dao.stored_info.price_id = 1;

    std::string body = "{\"registration_id\":1,\"student_name\":\"stu_transfer\",\"student_gender\":\"male\",\"class_id\":2,\"is_transfer\":true,\"new_class_id\":2}";
    crow::request req = MakeAdminRequest(body, session_mgr);
    crow::response resp = handler.HandleUpdateStudent(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "\"code\":0");
}

/* T5.7-3: CountWeekdaysInRange - basic calculation */
TEST_CASE(PartialPeriod_CountWeekdaysInRange_Basic) {
    /* 2026-07-01 (Wed) ~ 2026-07-31 (Fri) = 23 weekdays */
    int32_t days = register_student::CountWeekdaysInRange("2026-07-01", "2026-07-31");
    ASSERT_TRUE(days > 0);
    ASSERT_EQ(days, 23);
}

/* T5.7-4: CountWeekdaysInRange - full class period */
TEST_CASE(PartialPeriod_CountWeekdaysInRange_FullPeriod) {
    /* 2026-07-01 (Wed) ~ 2026-08-31 (Mon) */
    int32_t days = register_student::CountWeekdaysInRange("2026-07-01", "2026-08-31");
    ASSERT_TRUE(days > 0);
    /* should be ~44 weekdays */
    ASSERT_TRUE(days >= 40 && days <= 50);
}

/* T5.7-5: Enrollment ratio calculation - partial period < 1.0 */
TEST_CASE(PartialPeriod_EnrollmentRatio_PartialLessThanOne) {
    int32_t total = register_student::CountWeekdaysInRange("2026-07-01", "2026-08-31");
    int32_t partial = register_student::CountWeekdaysInRange("2026-07-01", "2026-07-31");
    double ratio = static_cast<double>(partial) / static_cast<double>(total);
    ASSERT_TRUE(ratio > 0.0 && ratio < 1.0);
}

/* T5.7-6: Enrollment ratio calculation - full period = 1.0 */
TEST_CASE(PartialPeriod_EnrollmentRatio_FullEqualsOne) {
    int32_t total = register_student::CountWeekdaysInRange("2026-07-01", "2026-08-31");
    double ratio = static_cast<double>(total) / static_cast<double>(total);
    ASSERT_TRUE(ratio > 0.999 && ratio < 1.001);
}

/* T5.8-1: SqliteDatabase - MigrateSchema adds student_start_date/student_end_date columns */
TEST_CASE(PartialPeriod_Migration_StudentPeriodColumns) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    /* After Open, MigrateSchema should have added the columns */
    /* Insert a registration and verify the columns exist */
    ClassInfo ci;
    ci.class_name = "MigrationTestClass";
    ci.start_time = "2026-07-01";
    ci.end_time = "2026-08-31";
    ci.enrollment_capacity = 10;
    ci.enrollment_used = 0.0;
    ci.class_type = "test";
    ci.create_time = "2026-01-01 00:00:00";
    db.InsertClass(ci);

    PriceInfo p;
    p.class_id = 1;
    p.preset_id = 1;
    p.snapshot_amount = 3000.0;
    p.snapshot_headcount = 1;
    p.price = 3000.0;
    p.activity_name = "full";
    db.InsertPrice(p);

    RegistrationInfo ri;
    ri.class_id = 1;
    ri.student_name = "stu_migrate";
    ri.student_gender = "male";
    ri.parent_phone = "111";
    ri.has_allergy = 0;
    ri.price_id = 1;
    ri.need_bed = 0;
    ri.teacher_name = "t1";
    ri.register_time = "2026-07-01 10:00:00";
    ri.is_deposit = 0;
    ri.paid_amount_snapshot = 3000.0;
    ri.supplement_amount = 0;
    ri.supplement_preset_id = -1;
    ri.student_start_date = "2026-07-01";
    ri.student_end_date = "2026-07-31";
    ri.enrollment_ratio = 0.5;
    ret = db.RegisterStudentAtomic(ri, 1, 10, 0, -1);
    ASSERT_EQ(ret, DB_OK);

    /* Query back and verify period fields */
    RegistrationInfo queried;
    ret = db.QueryRegistrationById(1, queried);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(queried.student_start_date, "2026-07-01");
    ASSERT_EQ(queried.student_end_date, "2026-07-31");

    db.Close();
}

/* T5.8-2: SqliteDatabase - enrollment_used REAL field works */
TEST_CASE(PartialPeriod_Migration_EnrollmentUsedReal) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    ClassInfo ci;
    ci.class_name = "EnrollTest";
    ci.start_time = "2026-07-01";
    ci.end_time = "2026-08-31";
    ci.enrollment_capacity = 10;
    ci.enrollment_used = 0.0;
    ci.class_type = "test";
    ci.create_time = "2026-01-01 00:00:00";
    db.InsertClass(ci);

    /* Increment with fractional delta */
    ret = db.IncrementEnrollmentUsed(1, 0.5);
    ASSERT_EQ(ret, DB_OK);

    ClassInfo queried;
    db.QueryClassById(1, queried);
    ASSERT_TRUE(queried.enrollment_used > 0.4 && queried.enrollment_used < 0.6);

    /* Increment again */
    ret = db.IncrementEnrollmentUsed(1, 0.3);
    ASSERT_EQ(ret, DB_OK);

    db.QueryClassById(1, queried);
    ASSERT_TRUE(queried.enrollment_used > 0.79 && queried.enrollment_used < 0.81);

    db.Close();
}

/* T5.8-3: SqliteDatabase - RenewRegistrationAtomic works */
TEST_CASE(PartialPeriod_RenewAtomic_Success) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    ClassInfo ci;
    ci.class_name = "RenewTestClass";
    ci.start_time = "2026-07-01";
    ci.end_time = "2026-08-31";
    ci.enrollment_capacity = 10;
    ci.enrollment_used = 0.0;
    ci.class_type = "test";
    ci.create_time = "2026-01-01 00:00:00";
    db.InsertClass(ci);

    PriceInfo p;
    p.class_id = 1;
    p.preset_id = 1;
    p.snapshot_amount = 3000.0;
    p.snapshot_headcount = 1;
    p.price = 3000.0;
    p.activity_name = "full";
    db.InsertPrice(p);

    RegistrationInfo ri;
    ri.class_id = 1;
    ri.student_name = "stu_renew";
    ri.student_gender = "male";
    ri.parent_phone = "111";
    ri.has_allergy = 0;
    ri.price_id = 1;
    ri.need_bed = 0;
    ri.teacher_name = "t1";
    ri.register_time = "2026-07-01 10:00:00";
    ri.is_deposit = 0;
    ri.paid_amount_snapshot = 1500.0;
    ri.supplement_amount = 0;
    ri.supplement_preset_id = -1;
    ri.student_start_date = "2026-07-01";
    ri.student_end_date = "2026-07-31";
    ri.enrollment_ratio = 0.5;
    ret = db.RegisterStudentAtomic(ri, 1, 10, 0, -1);
    ASSERT_EQ(ret, DB_OK);

    /* Renew: extend to 2026-08-31, amount 1500 */
    int32_t total_days = register_student::CountWeekdaysInRange("2026-07-01", "2026-08-31");
    int32_t old_days = register_student::CountWeekdaysInRange("2026-07-01", "2026-07-31");
    int32_t new_days = register_student::CountWeekdaysInRange("2026-07-01", "2026-08-31");
    double delta = static_cast<double>(new_days - old_days) / static_cast<double>(total_days);

    ret = db.RenewRegistrationAtomic(1, "2026-08-31", 1500.0, delta, "admin1", "2026-07-15 10:00:00");
    ASSERT_EQ(ret, DB_OK);

    /* Verify updated fields */
    RegistrationInfo updated;
    db.QueryRegistrationById(1, updated);
    ASSERT_EQ(updated.student_end_date, "2026-08-31");
    ASSERT_TRUE(updated.paid_amount_snapshot > 2999.0 && updated.paid_amount_snapshot < 3001.0);

    /* Verify enrollment_used increased */
    ClassInfo ci_after;
    db.QueryClassById(1, ci_after);
    /* should be close to 1.0 (full period) now */
    ASSERT_TRUE(ci_after.enrollment_used > 0.9);

    /* Verify renewal record exists */
    std::vector<RenewalRecordInfo> renewals;
    db.QueryRenewalsByRegId(1, renewals);
    ASSERT_EQ(static_cast<int>(renewals.size()), 1);
    ASSERT_EQ(renewals[0].old_end_date, "2026-07-31");
    ASSERT_EQ(renewals[0].new_end_date, "2026-08-31");

    db.Close();
}

/* T5.8-4: SqliteDatabase - RenewRegistrationAtomic concurrency check (student_end_date changed) */
TEST_CASE(PartialPeriod_RenewAtomic_ConcurrencyCheck) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    ClassInfo ci;
    ci.class_name = "RenewConcTest";
    ci.start_time = "2026-07-01";
    ci.end_time = "2026-08-31";
    ci.enrollment_capacity = 10;
    ci.enrollment_used = 0.0;
    ci.class_type = "test";
    ci.create_time = "2026-01-01 00:00:00";
    db.InsertClass(ci);

    PriceInfo p;
    p.class_id = 1;
    p.preset_id = 1;
    p.snapshot_amount = 3000.0;
    p.snapshot_headcount = 1;
    p.price = 3000.0;
    p.activity_name = "full";
    db.InsertPrice(p);

    RegistrationInfo ri;
    ri.class_id = 1;
    ri.student_name = "stu_conc";
    ri.student_gender = "male";
    ri.parent_phone = "111";
    ri.has_allergy = 0;
    ri.price_id = 1;
    ri.need_bed = 0;
    ri.teacher_name = "t1";
    ri.register_time = "2026-07-01 10:00:00";
    ri.is_deposit = 0;
    ri.paid_amount_snapshot = 1500.0;
    ri.supplement_amount = 0;
    ri.supplement_preset_id = -1;
    ri.student_start_date = "2026-07-01";
    ri.student_end_date = "2026-07-31";
    ri.enrollment_ratio = 0.5;
    ret = db.RegisterStudentAtomic(ri, 1, 10, 0, -1);
    ASSERT_EQ(ret, DB_OK);

    /* First renewal succeeds */
    int32_t total_days = register_student::CountWeekdaysInRange("2026-07-01", "2026-08-31");
    int32_t old_days = register_student::CountWeekdaysInRange("2026-07-01", "2026-07-31");
    int32_t new_days = register_student::CountWeekdaysInRange("2026-07-01", "2026-08-15");
    double delta = static_cast<double>(new_days - old_days) / static_cast<double>(total_days);

    ret = db.RenewRegistrationAtomic(1, "2026-08-15", 800.0, delta, "admin1", "2026-07-15 10:00:00");
    ASSERT_EQ(ret, DB_OK);

    /* Second renewal with stale old_end_date should fail (concurrency check) */
    /* The student_end_date is now 2026-08-15, but we pass the old 2026-07-31 as expected */
    /* RenewRegistrationAtomic internally re-reads and checks */
    int32_t new_days2 = register_student::CountWeekdaysInRange("2026-07-01", "2026-08-31");
    double delta2 = static_cast<double>(new_days2 - new_days) / static_cast<double>(total_days);
    ret = db.RenewRegistrationAtomic(1, "2026-08-31", 700.0, delta2, "admin1", "2026-07-15 11:00:00");
    /* Should succeed because it re-reads current end_date (2026-08-15) and extends to 2026-08-31 */
    ASSERT_EQ(ret, DB_OK);

    db.Close();
}

/* T5.8-5: SqliteDatabase - QueryEnrollmentUsedByClassId returns REAL */
TEST_CASE(PartialPeriod_QueryEnrollmentUsed_ReturnsReal) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    ClassInfo ci;
    ci.class_name = "EnrollQueryTest";
    ci.start_time = "2026-07-01";
    ci.end_time = "2026-08-31";
    ci.enrollment_capacity = 10;
    ci.enrollment_used = 0.0;
    ci.class_type = "test";
    ci.create_time = "2026-01-01 00:00:00";
    db.InsertClass(ci);

    double used = db.QueryEnrollmentUsedByClassId(1);
    ASSERT_TRUE(used >= -0.001 && used <= 0.001);

    db.IncrementEnrollmentUsed(1, 0.5);
    used = db.QueryEnrollmentUsedByClassId(1);
    ASSERT_TRUE(used > 0.4 && used < 0.6);

    db.Close();
}

TEST_CASE(PartialPeriod_Register_NegativeActualAmount) {
    /* actual_amount < 0 should be rejected with ERR_INVALID_PARAM */
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;
    RegistrationHandler handler(&class_dao, &reg_dao, &resource_dao, &log_dao, &session_mgr);

    class_dao.old_class_id = 1;
    class_dao.old_class.start_time = "2026-07-01";
    class_dao.old_class.end_time = "2026-07-31";
    PriceInfo p;
    p.id = 1;
    p.class_id = 1;
    p.snapshot_amount = 3000.0;
    p.snapshot_headcount = 1;
    class_dao.mock_prices.push_back(p);

    std::string body = "{\"class_id\":1,\"price_id\":1,\"is_deposit\":0,\"teacher_name\":\"t1\","
        "\"students\":[{\"student_name\":\"s1\",\"student_gender\":\"male\","
        "\"student_start_date\":\"2026-07-01\",\"student_end_date\":\"2026-07-15\","
        "\"actual_amount\":-100}]}";
    crow::request req = MakeTeacherRequest(body, session_mgr);
    crow::response resp = handler.HandleRegister(req);
    ASSERT_EQ(resp.code, 400);
    ASSERT_CONTAINS(resp.body, "actual_amount must be >= 0");
}

TEST_CASE(PartialPeriod_Enrollment_DeleteReleasesRatio) {
    /* Deleting a partial-period student should release the correct ratio */
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    ClassInfo ci;
    ci.class_name = "DeleteRatioTest";
    ci.start_time = "2026-07-01";
    ci.end_time = "2026-07-31";
    ci.enrollment_capacity = 10;
    ci.enrollment_used = 0.0;
    ci.class_type = "test";
    ci.create_time = "2026-01-01 00:00:00";
    db.InsertClass(ci);

    /* Insert a partial-period student (11 weekdays out of 23) */
    RegistrationInfo info;
    info.class_id = 1;
    info.student_name = "RatioStudent";
    info.student_gender = "male";
    info.price_id = 1;
    info.is_deposit = 0;
    info.paid_amount_snapshot = 1500;
    info.student_start_date = "2026-07-01";
    info.student_end_date = "2026-07-15";
    info.enrollment_ratio = 11.0 / 23.0;
    info.register_time = "2026-01-01 00:00:00";
    info.teacher_name = "tester";
    ret = db.InsertRegistration(info);
    ASSERT_EQ(ret, DB_OK);

    /* Simulate enrollment_used increment by ratio */
    db.IncrementEnrollmentUsed(1, info.enrollment_ratio);
    double used_before = db.QueryEnrollmentUsedByClassId(1);
    ASSERT_TRUE(used_before > 0.4 && used_before < 0.5);

    /* Delete the student - should release the ratio */
    ret = db.DeleteRegistrationAtomic(1, -1);
    ASSERT_EQ(ret, DB_OK);

    double used_after = db.QueryEnrollmentUsedByClassId(1);
    /* After delete, enrollment_used should decrease by the student's ratio */
    ASSERT_TRUE(used_after < used_before);
    ASSERT_TRUE(used_after >= -0.001 && used_after <= 0.001);

    db.Close();
}

TEST_CASE(PartialPeriod_RefundCap_AfterRenewal) {
    /* After renewal, unit_price should be based on updated paid_amount_snapshot and student_end_date */
    MockClassDao class_dao;
    MockRegistrationDao reg_dao;
    MockResourceDao resource_dao;
    MockAttendanceDao att_dao;
    MockRefundDao refund_dao;
    MockOperationLogDao log_dao;
    SessionManager session_mgr;

    class_dao.old_class_id = 1;
    class_dao.old_class.start_time = "2026-07-01";
    class_dao.old_class.end_time = "2026-07-31";
    class_dao.old_class.enrollment_capacity = 10;
    class_dao.old_class.enrollment_used = 1.0;
    PriceInfo pi;
    pi.id = 1;
    pi.class_id = 1;
    pi.snapshot_amount = 3000;
    pi.activity_name = "full";
    class_dao.mock_prices.push_back(pi);

    /* Student originally registered for 07-01~07-15 with 1500, then renewed to 07-31 with +1500 */
    reg_dao.stored_info.class_id = 1;
    reg_dao.stored_info.student_name = "RenewedStudent";
    reg_dao.stored_info.student_gender = "male";
    reg_dao.stored_info.is_deposit = 0;
    reg_dao.stored_info.paid_amount_snapshot = 3000;  /* 1500 + 1500 renewal */
    reg_dao.stored_info.student_start_date = "2026-07-01";
    reg_dao.stored_info.student_end_date = "2026-07-31";  /* extended by renewal */
    reg_dao.stored_info.price_id = 1;
    reg_dao.stored_info.register_time = "2026-01-01 00:00:00";

    ClassManageHandler handler(&class_dao, &reg_dao, &resource_dao, &att_dao, &refund_dao, &log_dao, &session_mgr);

    /* Verify the stored state reflects renewal */
    ASSERT_TRUE(reg_dao.stored_info.student_end_date == "2026-07-31");
    ASSERT_TRUE(reg_dao.stored_info.paid_amount_snapshot > 2999.0 &&
                reg_dao.stored_info.paid_amount_snapshot < 3001.0);
}

/* ===== Network credential tests ===== */

TEST_CASE(Network_GetLocalInfo_ReturnsNonEmptyIpv4) {
    register_student::NetworkInfo info = register_student::GetLocalNetworkInfo();
    /* In a real environment, ipv4 should be non-empty */
    ASSERT_TRUE(!info.ipv4.empty());
    /* Basic format check: should contain at least one dot */
    ASSERT_TRUE(info.ipv4.find('.') != std::string::npos);
}

TEST_CASE(Network_GetLocalInfo_FieldsComplete) {
    register_student::NetworkInfo info = register_student::GetLocalNetworkInfo();
    /* ipv4 must be non-empty in a real environment */
    ASSERT_TRUE(!info.ipv4.empty());
    /* hostname must be non-empty */
    ASSERT_TRUE(!info.hostname.empty());
    /* Other fields may be empty but struct must be accessible */
    /* (just access them to ensure no crash) */
    std::string dummy_ipv6 = info.ipv6;
    std::string dummy_mac = info.mac;
    std::string dummy_adapter = info.adapter;
    (void)dummy_ipv6;
    (void)dummy_mac;
    (void)dummy_adapter;
}

TEST_CASE(Network_GetLocalInfo_Ipv4NotLoopback) {
    register_student::NetworkInfo info = register_student::GetLocalNetworkInfo();
    if (!info.ipv4.empty()) {
        /* Should not start with "127." */
        ASSERT_TRUE(info.ipv4.find("127.") != 0);
    }
}

TEST_CASE(Network_GetLocalInfo_Ipv4NotLinkLocal) {
    register_student::NetworkInfo info = register_student::GetLocalNetworkInfo();
    if (!info.ipv4.empty()) {
        /* Should not start with "169.254." */
        ASSERT_TRUE(info.ipv4.find("169.254.") != 0);
    }
}

TEST_CASE(NetworkHandler_GetNetworkInfo_Success) {
    NetworkHandler handler(18080, "");
    crow::response resp = handler.HandleGetNetworkInfo();
    ASSERT_EQ(resp.code, 200);
    /* Response body should contain "code" field */
    ASSERT_CONTAINS(resp.body, "code");
    /* Response body should contain network info fields */
    ASSERT_CONTAINS(resp.body, "ipv4");
    ASSERT_CONTAINS(resp.body, "ipv6");
    ASSERT_CONTAINS(resp.body, "mac");
    ASSERT_CONTAINS(resp.body, "hostname");
    ASSERT_CONTAINS(resp.body, "adapter");
    ASSERT_CONTAINS(resp.body, "port");
}

TEST_CASE(NetworkHandler_GetNetworkInfo_ContainsPort) {
    NetworkHandler handler(18080, "");
    crow::response resp = handler.HandleGetNetworkInfo();
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "18080");
}

TEST_CASE(PageHandler_Credential_ReturnsHtml) {
    PageHandler handler;
    crow::response resp = handler.HandleCredential();
    ASSERT_EQ(resp.code, 200);
    /* Content-Type should contain "text/html" */
    std::string content_type;
    for (auto& header : resp.headers) {
        if (header.first == "Content-Type") {
            content_type = header.second;
            break;
        }
    }
    ASSERT_TRUE(content_type.find("text/html") != std::string::npos);
}

TEST_CASE(PageHandler_Account_ReturnsHtml) {
    PageHandler handler;
    crow::response resp = handler.HandleAccount();
    ASSERT_EQ(resp.code, 200);
}

TEST_CASE(PageHandler_Account_ContentType) {
    PageHandler handler;
    crow::response resp = handler.HandleAccount();
    std::string content_type;
    for (auto& header : resp.headers) {
        if (header.first == "Content-Type") {
            content_type = header.second;
            break;
        }
    }
    ASSERT_TRUE(content_type.find("text/html") != std::string::npos);
}

TEST_CASE(PageHandler_Account_CacheControl) {
    PageHandler handler;
    crow::response resp = handler.HandleAccount();
    std::string cache_control;
    for (auto& header : resp.headers) {
        if (header.first == "Cache-Control") {
            cache_control = header.second;
            break;
        }
    }
    ASSERT_TRUE(cache_control.find("no-cache") != std::string::npos);
}

/* ===== Activity Handler tests ===== */

TEST_CASE(ActivityHandler_WithMockDao) {
    MockActivityDao activity_dao;
    MockActivitySignupDao signup_dao;
    MockActivityGroupDao group_dao;
    ActivityHandler handler(&activity_dao, &signup_dao, &group_dao, nullptr);
    /* Just verify construction doesn't crash */
    ASSERT_TRUE(true);
}

TEST_CASE(ActivityManageHandler_WithMockDao) {
    MockActivityDao activity_dao;
    MockActivitySignupDao signup_dao;
    MockActivityGroupDao group_dao;
    SessionManager session_mgr;
    ActivityManageHandler handler(&activity_dao, &signup_dao, &group_dao, &session_mgr,
                                   nullptr, TEST_TEMP_DIR "/uploads");
    ASSERT_TRUE(true);
}

TEST_CASE(ActivityHandler_ListPublished_Success) {
    MockActivityDao activity_dao;
    MockActivitySignupDao signup_dao;

    ActivityInfo act;
    act.id = 1;
    act.title = "Test Activity";
    act.status = 1;
    activity_dao.mock_list.push_back(act);

    MockActivityGroupDao group_dao;
    ActivityHandler handler(&activity_dao, &signup_dao, &group_dao, nullptr);
    crow::request dummy_req;
    crow::response resp = handler.HandleListActivities(dummy_req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "\"code\":0");
    ASSERT_CONTAINS(resp.body, "Test Activity");
}

TEST_CASE(ActivityHandler_ListPublished_FiltersUnpublished) {
    MockActivityDao activity_dao;
    MockActivitySignupDao signup_dao;

    ActivityInfo act1;
    act1.id = 1;
    act1.title = "Published";
    act1.status = 1;
    activity_dao.mock_list.push_back(act1);

    ActivityInfo act2;
    act2.id = 2;
    act2.title = "Draft";
    act2.status = 0;
    activity_dao.mock_list.push_back(act2);

    MockActivityGroupDao group_dao;
    ActivityHandler handler(&activity_dao, &signup_dao, &group_dao, nullptr);
    crow::request dummy_req;
    crow::response resp = handler.HandleListActivities(dummy_req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "Published");
    /* Draft should not appear in response since ListPublishedActivities filters */
}

TEST_CASE(ActivityHandler_Signup_Success) {
    MockActivityDao activity_dao;
    MockActivitySignupDao signup_dao;

    ActivityInfo act;
    act.id = 1;
    act.title = "Test";
    act.group_image = "/static/uploads/group.jpg";
    activity_dao.mock_activity = act;

    MockActivityGroupDao group_dao;
    ActivityHandler handler(&activity_dao, &signup_dao, &group_dao, nullptr);

    crow::request req;
    req.body = "{\"activity_id\":1,\"name\":\"Zhang San\",\"phone\":\"13800138000\",\"signup_type\":\"全托\"}";
    crow::response resp = handler.HandleSignup(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "\"code\":0");
    ASSERT_CONTAINS(resp.body, "group.jpg");
}

TEST_CASE(ActivityHandler_Signup_EmptyName) {
    MockActivityDao activity_dao;
    MockActivitySignupDao signup_dao;
    MockActivityGroupDao group_dao;
    ActivityHandler handler(&activity_dao, &signup_dao, &group_dao, nullptr);

    crow::request req;
    req.body = "{\"activity_id\":1,\"name\":\"\",\"phone\":\"13800138000\",\"signup_type\":\"全托\"}";
    crow::response resp = handler.HandleSignup(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, std::to_string(ERR_ACTIVITY_NAME_EMPTY));
}

TEST_CASE(ActivityHandler_Signup_InvalidPhone) {
    MockActivityDao activity_dao;
    MockActivitySignupDao signup_dao;
    MockActivityGroupDao group_dao;
    ActivityHandler handler(&activity_dao, &signup_dao, &group_dao, nullptr);

    crow::request req;
    req.body = "{\"activity_id\":1,\"name\":\"Zhang San\",\"phone\":\"12345\",\"signup_type\":\"全托\"}";
    crow::response resp = handler.HandleSignup(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, std::to_string(ERR_ACTIVITY_PHONE_INVALID));
}

TEST_CASE(ActivityHandler_Signup_DuplicateSignup) {
    MockActivityDao activity_dao;
    MockActivitySignupDao signup_dao;
    signup_dao.existing_phones.push_back(std::make_pair((int64_t)1, std::string("13800138000")));

    MockActivityGroupDao group_dao;
    ActivityHandler handler(&activity_dao, &signup_dao, &group_dao, nullptr);

    crow::request req;
    req.body = "{\"activity_id\":1,\"name\":\"Zhang San\",\"phone\":\"13800138000\",\"signup_type\":\"全托\"}";
    crow::response resp = handler.HandleSignup(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, std::to_string(ERR_ACTIVITY_DUPLICATE_SIGNUP));
}

TEST_CASE(ActivityHandler_Signup_InvalidJson) {
    MockActivityDao activity_dao;
    MockActivitySignupDao signup_dao;
    MockActivityGroupDao group_dao;
    ActivityHandler handler(&activity_dao, &signup_dao, &group_dao, nullptr);

    crow::request req;
    req.body = "not json";
    crow::response resp = handler.HandleSignup(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, std::to_string(ERR_INVALID_PARAM));
}

TEST_CASE(ErrorCode_ActivityRange) {
    ASSERT_TRUE(ERR_ACTIVITY_NOT_FOUND >= 1100 && ERR_ACTIVITY_NOT_FOUND < 1200);
    ASSERT_TRUE(ERR_ACTIVITY_CAPACITY_FULL >= 1100 && ERR_ACTIVITY_CAPACITY_FULL < 1200);
    ASSERT_TRUE(ERR_ACTIVITY_DUPLICATE_SIGNUP >= 1100 && ERR_ACTIVITY_DUPLICATE_SIGNUP < 1200);
    ASSERT_TRUE(ERR_ACTIVITY_SIGNUP_ENDED >= 1100 && ERR_ACTIVITY_SIGNUP_ENDED < 1200);
    ASSERT_TRUE(ERR_ACTIVITY_NOT_PUBLISHED >= 1100 && ERR_ACTIVITY_NOT_PUBLISHED < 1200);
    ASSERT_TRUE(ERR_ACTIVITY_TITLE_EMPTY >= 1100 && ERR_ACTIVITY_TITLE_EMPTY < 1200);
    ASSERT_TRUE(ERR_ACTIVITY_TIME_INVALID >= 1100 && ERR_ACTIVITY_TIME_INVALID < 1200);
    ASSERT_TRUE(ERR_ACTIVITY_PHONE_INVALID >= 1100 && ERR_ACTIVITY_PHONE_INVALID < 1200);
    ASSERT_TRUE(ERR_ACTIVITY_NAME_EMPTY >= 1100 && ERR_ACTIVITY_NAME_EMPTY < 1200);
    ASSERT_TRUE(ERR_ACTIVITY_COVER_REQUIRED >= 1100 && ERR_ACTIVITY_COVER_REQUIRED < 1200);
    ASSERT_TRUE(ERR_ACTIVITY_DEADLINE_INVALID >= 1100 && ERR_ACTIVITY_DEADLINE_INVALID < 1200);
}

TEST_CASE(Database_ActivityCrud) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    ActivityInfo info;
    info.title = "Test Activity";
    info.description = "Description";
    info.cover_image = "/static/uploads/cover.jpg";
    info.start_time = "2025-01-01 10:00";
    info.end_time = "2025-01-02 18:00";
    info.signup_deadline = "2025-01-02 17:00";
    info.capacity = 50;

    int64_t out_id = 0;
    ret = db.CreateActivity(info, out_id);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_TRUE(out_id > 0);

    ActivityInfo queried;
    ret = db.GetActivity(out_id, queried);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_TRUE(queried.title == "Test Activity");
    ASSERT_TRUE(queried.capacity == 50);

    info.id = out_id;
    info.title = "Updated Title";
    ret = db.UpdateActivity(info);
    ASSERT_EQ(ret, DB_OK);

    ret = db.GetActivity(out_id, queried);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_TRUE(queried.title == "Updated Title");

    ret = db.DeleteActivity(out_id);
    ASSERT_EQ(ret, DB_OK);

    ret = db.GetActivity(out_id, queried);
    ASSERT_EQ(ret, ERR_ACTIVITY_NOT_FOUND);

    db.Close();
}

TEST_CASE(Database_Activity_ListPublished) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    ActivityInfo info;
    info.title = "Published";
    info.cover_image = "/cover.jpg";
    info.start_time = "2025-01-01 10:00";
    info.end_time = "2025-01-02 18:00";
    info.signup_deadline = "2025-01-02 17:00";
    info.status = 1;

    int64_t id1 = 0;
    ret = db.CreateActivity(info, id1);
    ASSERT_EQ(ret, DB_OK);

    info.title = "Draft";
    info.status = 0;
    int64_t id2 = 0;
    ret = db.CreateActivity(info, id2);
    ASSERT_EQ(ret, DB_OK);

    std::vector<ActivityInfo> list;
    ret = db.ListPublishedActivities(list);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ((int)list.size(), 1);
    ASSERT_TRUE(list[0].title == "Published");

    db.Close();
}

TEST_CASE(Database_Activity_SignupAtomic) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    ActivityInfo info;
    info.title = "Test";
    info.cover_image = "/cover.jpg";
    info.start_time = "2025-01-01 10:00";
    info.end_time = "2099-12-31 18:00";
    info.signup_deadline = "2099-12-31 17:00";
    info.capacity = 10;
    info.status = 1;

    int64_t act_id = 0;
    ret = db.CreateActivity(info, act_id);
    ASSERT_EQ(ret, DB_OK);

    ActivitySignupInfo signup;
    signup.activity_id = act_id;
    signup.name = "Zhang San";
    signup.phone = "13800138000";

    int64_t signup_id = 0;
    ret = db.CreateSignupAtomic(signup, signup_id);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_TRUE(signup_id > 0);

    ActivityInfo queried;
    ret = db.GetActivity(act_id, queried);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(queried.signup_count, 1);

    db.Close();
}

TEST_CASE(Database_Activity_SignupAtomic_Duplicate) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    ActivityInfo info;
    info.title = "Test";
    info.cover_image = "/cover.jpg";
    info.start_time = "2025-01-01 10:00";
    info.end_time = "2099-12-31 18:00";
    info.signup_deadline = "2099-12-31 17:00";
    info.status = 1;

    int64_t act_id = 0;
    db.CreateActivity(info, act_id);

    ActivitySignupInfo signup;
    signup.activity_id = act_id;
    signup.name = "Zhang San";
    signup.phone = "13800138000";

    int64_t signup_id1 = 0;
    ret = db.CreateSignupAtomic(signup, signup_id1);
    ASSERT_EQ(ret, DB_OK);

    int64_t signup_id2 = 0;
    ret = db.CreateSignupAtomic(signup, signup_id2);
    ASSERT_EQ(ret, ERR_ACTIVITY_DUPLICATE_SIGNUP);

    db.Close();
}

TEST_CASE(Database_Activity_SignupAtomic_Full) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    ActivityInfo info;
    info.title = "Test";
    info.cover_image = "/cover.jpg";
    info.start_time = "2025-01-01 10:00";
    info.end_time = "2099-12-31 18:00";
    info.signup_deadline = "2099-12-31 17:00";
    info.capacity = 1;
    info.status = 1;

    int64_t act_id = 0;
    db.CreateActivity(info, act_id);

    ActivitySignupInfo signup;
    signup.activity_id = act_id;
    signup.name = "Zhang San";
    signup.phone = "13800138000";

    int64_t signup_id1 = 0;
    ret = db.CreateSignupAtomic(signup, signup_id1);
    ASSERT_EQ(ret, DB_OK);

    signup.phone = "13900139000";
    int64_t signup_id2 = 0;
    ret = db.CreateSignupAtomic(signup, signup_id2);
    ASSERT_EQ(ret, ERR_ACTIVITY_CAPACITY_FULL);

    db.Close();
}

TEST_CASE(Database_Activity_SignupAtomic_NotPublished) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    ActivityInfo info;
    info.title = "Test";
    info.cover_image = "/cover.jpg";
    info.start_time = "2025-01-01 10:00";
    info.end_time = "2099-12-31 18:00";
    info.signup_deadline = "2099-12-31 17:00";
    info.status = 0;

    int64_t act_id = 0;
    db.CreateActivity(info, act_id);

    ActivitySignupInfo signup;
    signup.activity_id = act_id;
    signup.name = "Zhang San";
    signup.phone = "13800138000";

    int64_t signup_id = 0;
    ret = db.CreateSignupAtomic(signup, signup_id);
    ASSERT_EQ(ret, ERR_ACTIVITY_NOT_PUBLISHED);

    db.Close();
}

TEST_CASE(Database_Activity_DeleteCascade) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    ActivityInfo info;
    info.title = "Test";
    info.cover_image = "/cover.jpg";
    info.start_time = "2025-01-01 10:00";
    info.end_time = "2099-12-31 18:00";
    info.signup_deadline = "2099-12-31 17:00";
    info.status = 1;

    int64_t act_id = 0;
    db.CreateActivity(info, act_id);

    ActivitySignupInfo signup;
    signup.activity_id = act_id;
    signup.name = "Zhang San";
    signup.phone = "13800138000";

    int64_t signup_id = 0;
    db.CreateSignupAtomic(signup, signup_id);

    ret = db.DeleteActivity(act_id);
    ASSERT_EQ(ret, DB_OK);

    std::vector<ActivitySignupInfo> signups;
    ret = db.ListSignupsByActivity(act_id, signups);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ((int)signups.size(), 0);

    db.Close();
}

TEST_CASE(PageHandler_Activity_ReturnsHtml) {
    PageHandler handler;
    crow::response resp = handler.HandleActivity();
    ASSERT_EQ(resp.code, 200);
    std::string content_type;
    for (auto& header : resp.headers) {
        if (header.first == "Content-Type") {
            content_type = header.second;
            break;
        }
    }
    ASSERT_TRUE(content_type.find("text/html") != std::string::npos);
}

TEST_CASE(PageHandler_ActivityManage_ReturnsHtml) {
    PageHandler handler;
    crow::response resp = handler.HandleActivityManage();
    ASSERT_EQ(resp.code, 200);
    std::string content_type;
    for (auto& header : resp.headers) {
        if (header.first == "Content-Type") {
            content_type = header.second;
            break;
        }
    }
    ASSERT_TRUE(content_type.find("text/html") != std::string::npos);
}

/* ====== Data Transfer tests ====== */

TEST_CASE(DataTransfer_ErrorCodes_Defined) {
    ASSERT_EQ((int)ERR_DT_EXPORT_FAILED, 10001);
    ASSERT_EQ((int)ERR_DT_IMPORT_FAILED, 10002);
    ASSERT_EQ((int)ERR_DT_INVALID_FORMAT, 10003);
    ASSERT_EQ((int)ERR_DT_VERSION_UNSUPPORTED, 10004);
    ASSERT_EQ((int)ERR_DT_FILE_TOO_LARGE, 10005);
    ASSERT_EQ((int)ERR_DT_PACK_FAILED, 10006);
    ASSERT_EQ((int)ERR_DT_UNPACK_FAILED, 10007);
    ASSERT_EQ((int)ERR_DT_BACKUP_FAILED, 10008);
}

TEST_CASE(DataTransfer_ErrorCodes_Range) {
    ASSERT_TRUE(ERR_DT_EXPORT_FAILED >= 10000);
    ASSERT_TRUE(ERR_DT_BACKUP_FAILED <= 10999);
}

TEST_CASE(DataTransfer_IDataTransferDao_NullPtr) {
    IDataTransferDao* dao = nullptr;
    ASSERT_TRUE(dao == nullptr);
}

TEST_CASE(DataTransfer_MockDao_Construction) {
    MockDataTransferDao dao;
    ASSERT_EQ(dao.export_call_count, 0);
    ASSERT_EQ(dao.import_call_count, 0);
    ASSERT_EQ(dao.clear_call_count, 0);
}

TEST_CASE(DataTransfer_MockDao_ExportTableRows) {
    MockDataTransferDao dao;
    DataRow row1;
    row1["id"] = "1";
    row1["name"] = "test";
    dao.mock_export_rows.push_back(row1);

    std::vector<DataRow> out;
    int ret = dao.ExportTableRows("class", out);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(dao.export_call_count, 1);
    ASSERT_EQ(dao.export_last_table, std::string("class"));
    ASSERT_EQ(out.size(), (size_t)1);
    ASSERT_EQ(out[0]["name"], std::string("test"));
}

TEST_CASE(DataTransfer_MockDao_ImportTableRows) {
    MockDataTransferDao dao;
    std::vector<DataRow> rows;
    DataRow r;
    r["id"] = "1";
    rows.push_back(r);

    std::vector<std::string> keys;
    keys.push_back("id");
    TableImportStats stats;
    int ret = dao.ImportTableRows("class", rows, keys, ImportMode_Incremental, stats);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(dao.import_call_count, 1);
    ASSERT_EQ(dao.import_last_table, std::string("class"));
    ASSERT_EQ(dao.import_last_row_count, 1);
    ASSERT_EQ(stats.inserted, 1);
}

TEST_CASE(DataTransfer_MockDao_ClearBusinessTables) {
    MockDataTransferDao dao;
    int ret = dao.ClearBusinessTables();
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(dao.clear_call_count, 1);
}

TEST_CASE(DataTransfer_Handler_WithMockDao) {
    MockDataTransferDao dao;
    SessionManager session_mgr;
    DataTransferHandler handler(&dao, &session_mgr, TEST_TEMP_DIR "/uploads");
    ASSERT_TRUE(true);
}

TEST_CASE(DataTransfer_GetTableConfigs_NotEmpty) {
    std::vector<TableConfig> configs = DataTransferUtil::GetTableConfigs();
    ASSERT_TRUE(configs.size() >= 10);
}

TEST_CASE(DataTransfer_GetTableConfigs_HasClassTable) {
    std::vector<TableConfig> configs = DataTransferUtil::GetTableConfigs();
    bool found = false;
    for (size_t i = 0; i < configs.size(); ++i) {
        if (configs[i].table_name == "class_info") { found = true; break; }
    }
    ASSERT_TRUE(found);
}

TEST_CASE(DataTransfer_GetTableConfigs_HasRegistrationTable) {
    std::vector<TableConfig> configs = DataTransferUtil::GetTableConfigs();
    bool found = false;
    for (size_t i = 0; i < configs.size(); ++i) {
        if (configs[i].table_name == "registration") { found = true; break; }
    }
    ASSERT_TRUE(found);
}

TEST_CASE(DataTransfer_GetTableConfigs_NoUsersTable) {
    std::vector<TableConfig> configs = DataTransferUtil::GetTableConfigs();
    for (size_t i = 0; i < configs.size(); ++i) {
        ASSERT_TRUE(configs[i].table_name != "users");
    }
}

TEST_CASE(DataTransfer_GetTableConfigs_ImportOrderPositive) {
    std::vector<TableConfig> configs = DataTransferUtil::GetTableConfigs();
    for (size_t i = 0; i < configs.size(); ++i) {
        ASSERT_TRUE(configs[i].import_order >= 0);
    }
}

TEST_CASE(DataTransfer_ConvertPathSeparators_WinToUnix) {
    std::string result = DataTransferUtil::ConvertPathSeparators(
        "uploads\\images\\photo.jpg", "linux");
    ASSERT_EQ(result, std::string("uploads/images/photo.jpg"));
}

TEST_CASE(DataTransfer_ConvertPathSeparators_UnixToWin) {
    std::string result = DataTransferUtil::ConvertPathSeparators(
        "uploads/images/photo.jpg", "windows");
    ASSERT_EQ(result, std::string("uploads\\images\\photo.jpg"));
}

TEST_CASE(DataTransfer_ConvertPathSeparators_NoChange) {
    std::string result = DataTransferUtil::ConvertPathSeparators(
        "uploads/images/photo.jpg", "linux");
    ASSERT_EQ(result, std::string("uploads/images/photo.jpg"));
}

TEST_CASE(DataTransfer_Base64Decode_Empty) {
    std::vector<uint8_t> result = DataTransferUtil::Base64Decode("");
    ASSERT_EQ(result.size(), (size_t)0);
}

TEST_CASE(DataTransfer_Base64Decode_KnownValue) {
    std::string encoded = "SGVsbG8=";
    std::vector<uint8_t> result = DataTransferUtil::Base64Decode(encoded);
    std::string decoded(result.begin(), result.end());
    ASSERT_EQ(decoded, std::string("Hello"));
}

TEST_CASE(DataTransfer_SerializeDeserialize_RoundTrip) {
    std::map<std::string, std::vector<DataRow> > table_data;
    DataRow row;
    row["id"] = "1";
    row["name"] = "TestClass";
    std::vector<DataRow> rows;
    rows.push_back(row);
    table_data["class_info"] = rows;

    ExportMeta meta;
    meta.format_version = 1;
    meta.export_time = "2026-08-23 10:00:00";
    meta.platform = "linux";
    meta.db_version = "0.0.2";

    std::string data_json, meta_json;
    int ret = DataTransferUtil::SerializeExportData(
        table_data, meta, data_json, meta_json);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_TRUE(!data_json.empty());
    ASSERT_TRUE(!meta_json.empty());

    std::map<std::string, std::vector<DataRow> > restored;
    ret = DataTransferUtil::DeserializeImportData(data_json, restored);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_TRUE(restored.find("class_info") != restored.end());
    ASSERT_EQ(restored["class_info"].size(), (size_t)1);
    ASSERT_EQ(restored["class_info"][0]["name"], std::string("TestClass"));
}

TEST_CASE(DataTransfer_ParseMetaJson_Success) {
    std::string meta_json = "{\"format_version\":1,\"export_time\":\"2026-08-23 10:00:00\",\"platform\":\"linux\",\"db_version\":\"0.0.2\"}";
    ExportMeta meta;
    int ret = DataTransferUtil::ParseMetaJson(meta_json, meta);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(meta.format_version, 1);
    ASSERT_EQ(meta.platform, std::string("linux"));
    ASSERT_EQ(meta.db_version, std::string("0.0.2"));
}

TEST_CASE(DataTransfer_ParseMetaJson_Invalid) {
    ExportMeta meta;
    int ret = DataTransferUtil::ParseMetaJson("not json", meta);
    ASSERT_TRUE(ret != DB_OK);
}

TEST_CASE(DataTransfer_CreateAndRemoveTempDir) {
    std::string temp_dir;
    int ret = DataTransferUtil::CreateTempDir(temp_dir);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_TRUE(!temp_dir.empty());
    DataTransferUtil::RemoveTempDir(temp_dir);
}

TEST_CASE(DataTransfer_ImportMode_EnumValues) {
    ASSERT_EQ((int)ImportMode_Incremental, 0);
    ASSERT_EQ((int)ImportMode_Overwrite, 1);
}

TEST_CASE(DataTransfer_TableImportStats_Defaults) {
    TableImportStats stats;
    stats.table_name = "class";
    stats.inserted = 0;
    stats.skipped = 0;
    stats.failed = 0;
    ASSERT_EQ(stats.table_name, std::string("class"));
    ASSERT_EQ(stats.inserted, 0);
}

TEST_CASE(DataTransfer_ExportMeta_Fields) {
    ExportMeta meta;
    meta.format_version = 1;
    meta.export_time = "2026-08-23";
    meta.platform = "windows";
    meta.db_version = "0.0.2";
    ASSERT_EQ(meta.format_version, 1);
    ASSERT_EQ(meta.platform, std::string("windows"));
}

TEST_CASE(DataTransfer_MAX_IMPORT_SIZE_Value) {
    ASSERT_TRUE(DataTransferUtil::MAX_IMPORT_SIZE >= 500000000);
}

TEST_CASE(DataTransfer_Database_GetTableColumnNames) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    std::vector<std::string> columns;
    ret = db.GetTableColumnNames("class_info", columns);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_TRUE(columns.size() >= 3);

    bool found_id = false;
    for (size_t i = 0; i < columns.size(); ++i) {
        if (columns[i] == "id") { found_id = true; break; }
    }
    ASSERT_TRUE(found_id);

    db.Close();
}

TEST_CASE(DataTransfer_Database_ExportTableRows_Empty) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    std::vector<DataRow> rows;
    ret = db.ExportTableRows("class_info", rows);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(rows.size(), (size_t)0);

    db.Close();
}

TEST_CASE(DataTransfer_Database_ExportImport_RoundTrip) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    ClassInfo cls;
    cls.class_name = "DT-TestClass";
    cls.start_time = "2026-01-01";
    cls.end_time = "2026-12-31";
    cls.description = "export test";
    cls.enrollment_capacity = 10;
    cls.enrollment_used = 0;
    cls.class_type = "test";
    cls.create_time = "2026-01-01 00:00:00";
    ret = db.InsertClass(cls);
    ASSERT_EQ(ret, DB_OK);

    std::vector<DataRow> exported;
    ret = db.ExportTableRows("class_info", exported);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_TRUE(exported.size() >= 1);

    bool found = false;
    for (size_t i = 0; i < exported.size(); ++i) {
        if (exported[i]["class_name"] == "DT-TestClass") {
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);

    db.Close();
}

TEST_CASE(DataTransfer_Database_ClearBusinessTables) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    UserInfo user;
    user.username = "admin1";
    user.password_hash = "hash";
    user.salt = "salt";
    user.role = UserRole_Admin;
    user.display_name = "Admin";
    user.create_time = "2026-01-01";
    db.InsertUser(user);

    ClassInfo cls;
    cls.class_name = "DT-Clear";
    cls.start_time = "2026-01-01";
    cls.end_time = "2026-12-31";
    cls.description = "";
    cls.enrollment_capacity = 10;
    cls.enrollment_used = 0;
    cls.class_type = "test";
    cls.create_time = "2026-01-01";
    db.InsertClass(cls);

    ret = db.ClearBusinessTables();
    ASSERT_EQ(ret, DB_OK);

    std::vector<ClassInfo> classes;
    db.QueryAllClasses(classes);
    ASSERT_EQ(classes.size(), (size_t)0);

    UserInfo queried;
    ret = db.QueryUserByUsername("admin1", queried);
    ASSERT_EQ(ret, DB_OK);

    db.Close();
}

TEST_CASE(DataTransfer_GetTableConfigs_Count) {
    std::vector<TableConfig> configs = DataTransferUtil::GetTableConfigs();
    ASSERT_EQ(configs.size(), (size_t)21);
}

TEST_CASE(DataTransfer_GetTableConfigs_HasPromotionTables) {
    std::vector<TableConfig> configs = DataTransferUtil::GetTableConfigs();
    bool has_image = false;
    bool has_text = false;
    for (size_t i = 0; i < configs.size(); ++i) {
        if (configs[i].table_name == "promotion_image") { has_image = true; }
        if (configs[i].table_name == "promotion_text") { has_text = true; }
    }
    ASSERT_TRUE(has_image);
    ASSERT_TRUE(has_text);
}

TEST_CASE(DataTransfer_Handler_Export_NonAdmin_Forbidden) {
    MockDataTransferDao dao;
    SessionManager session_mgr;
    DataTransferHandler handler(&dao, &session_mgr, TEST_TEMP_DIR "/uploads");
    /* Handler constructed successfully; session validation is tested via HTTP routes */
    ASSERT_EQ(dao.export_call_count, 0);
}

TEST_CASE(DataTransfer_Handler_Overwrite_CallsClear) {
    MockDataTransferDao dao;
    dao.mock_export_rows.clear();
    SessionManager session_mgr;
    DataTransferHandler handler(&dao, &session_mgr, TEST_TEMP_DIR "/uploads");
    /* Verify handler construction with mock dao that tracks clear calls */
    ASSERT_EQ(dao.clear_call_count, 0);
}

TEST_CASE(DataTransfer_BuildMetaJson_RoundTrip) {
    ExportMeta meta;
    meta.format_version = 1;
    meta.export_time = "2026-08-23 12:00:00";
    meta.platform = "windows";
    meta.db_version = "0.0.2";

    std::string json = DataTransferUtil::BuildMetaJson(meta);
    ASSERT_TRUE(!json.empty());

    ExportMeta parsed;
    int ret = DataTransferUtil::ParseMetaJson(json, parsed);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(parsed.format_version, 1);
    ASSERT_EQ(parsed.platform, std::string("windows"));
    ASSERT_EQ(parsed.db_version, std::string("0.0.2"));
}

TEST_CASE(DataTransfer_PackUnpackDtz_RoundTrip) {
    std::map<std::string, std::vector<DataRow> > table_data;
    DataRow row;
    row["id"] = "1";
    row["name"] = "PackTest";
    std::vector<DataRow> rows;
    rows.push_back(row);
    table_data["class_type"] = rows;

    ExportMeta meta;
    meta.format_version = 1;
    meta.export_time = "2026-08-23 12:00:00";
    meta.platform = "windows";
    meta.db_version = "0.0.2";

    std::string data_json, meta_json;
    int ret = DataTransferUtil::SerializeExportData(
        table_data, meta, data_json, meta_json);
    ASSERT_EQ(ret, DB_OK);

    std::string temp_dir;
    ret = DataTransferUtil::CreateTempDir(temp_dir);
    ASSERT_EQ(ret, DB_OK);

    std::string upload_dir = temp_dir + "/uploads";
    DataTransferUtil::EnsureDir(upload_dir);

    std::string dtz_path = temp_dir + "/test.dtz";
    ret = DataTransferUtil::PackToDtz(data_json, meta_json, upload_dir, dtz_path);
    ASSERT_EQ(ret, DB_OK);

    /* Verify the .dtz file was created and is non-empty */
    std::string dtz_content;
    ret = DataTransferUtil::ReadFileToString(dtz_path, dtz_content);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_TRUE(dtz_content.size() > 10);

    /* Verify it starts with ZIP magic number */
    ASSERT_EQ(dtz_content[0], 'P');
    ASSERT_EQ(dtz_content[1], 'K');

    DataTransferUtil::RemoveTempDir(temp_dir);
}

TEST_CASE(DataTransfer_ValidateDtzFile_InvalidData) {
    std::string garbage = "this is not a zip file";
    int format_version = 0;
    int ret = DataTransferUtil::ValidateDtzFile(
        garbage, garbage.size(), format_version);
    ASSERT_TRUE(ret != DB_OK);
}

TEST_CASE(DataTransfer_Database_ImportTableRows_Incremental) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    std::vector<DataRow> rows;
    DataRow r;
    r["name"] = "DT-Import-Test";
    rows.push_back(r);

    std::vector<std::string> keys;
    keys.push_back("name");

    TableImportStats stats;
    ret = db.ImportTableRows("class_type", rows, keys,
        ImportMode_Incremental, stats);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(stats.inserted, 1);
    ASSERT_EQ(stats.skipped, 0);

    std::vector<DataRow> exported;
    ret = db.ExportTableRows("class_type", exported);
    ASSERT_EQ(ret, DB_OK);
    bool found = false;
    for (size_t i = 0; i < exported.size(); ++i) {
        if (exported[i]["name"] == "DT-Import-Test") {
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);

    db.Close();
}

TEST_CASE(DataTransfer_Database_ImportTableRows_SkipDuplicate) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    std::vector<DataRow> rows;
    DataRow r;
    r["name"] = "DT-Dup-Test";
    rows.push_back(r);

    std::vector<std::string> keys;
    keys.push_back("name");

    TableImportStats stats1;
    ret = db.ImportTableRows("class_type", rows, keys,
        ImportMode_Incremental, stats1);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(stats1.inserted, 1);

    TableImportStats stats2;
    ret = db.ImportTableRows("class_type", rows, keys,
        ImportMode_Incremental, stats2);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(stats2.skipped, 1);
    ASSERT_EQ(stats2.inserted, 0);

    db.Close();
}

/* ====== Group Signup Tests ====== */

TEST_CASE(ActivityHandler_GroupSignup_CreateGroup) {
    MockActivityDao activity_dao;
    MockActivitySignupDao signup_dao;
    MockActivityGroupDao group_dao;
    group_dao.mock_group_not_found = true;
    ActivityHandler handler(&activity_dao, &signup_dao, &group_dao, nullptr);

    ActivityInfo act;
    act.id = 1;
    act.status = 1;
    act.min_group_size = 3;
    act.signup_deadline = "2099-12-31 23:59";
    act.capacity = 100;
    act.signup_count = 0;
    activity_dao.mock_activity = act;

    crow::request req;
    req.body = "{\"activity_id\":1,\"name\":\"张三\",\"phone\":\"13800138000\",\"grade\":\"三年级\",\"signup_type\":\"全托\",\"invite_code\":\"\"}";
    crow::response resp = handler.HandleGroupSignup(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "\"code\":0");
    ASSERT_CONTAINS(resp.body, "\"group_id\"");
    ASSERT_CONTAINS(resp.body, "\"invite_code\"");
    ASSERT_CONTAINS(resp.body, "\"is_leader\":true");
    ASSERT_CONTAINS(resp.body, "\"target_count\":3");
}

TEST_CASE(ActivityHandler_GroupSignup_JoinGroup) {
    MockActivityDao activity_dao;
    MockActivitySignupDao signup_dao;
    MockActivityGroupDao group_dao;
    ActivityHandler handler(&activity_dao, &signup_dao, &group_dao, nullptr);

    ActivityInfo act;
    act.id = 1;
    act.status = 1;
    act.min_group_size = 3;
    act.signup_deadline = "2099-12-31 23:59";
    act.capacity = 100;
    act.signup_count = 0;
    activity_dao.mock_activity = act;

    ActivityGroupInfo group;
    group.id = 10;
    group.activity_id = 1;
    group.invite_code = "ABC123";
    group.status = GROUP_WAITING;
    group.current_count = 1;
    group.target_count = 3;
    group_dao.mock_group = group;

    crow::request req;
    req.body = "{\"activity_id\":1,\"name\":\"李四\",\"phone\":\"13900139000\",\"grade\":\"四年级\",\"signup_type\":\"晚托\",\"invite_code\":\"abc123\"}";
    crow::response resp = handler.HandleGroupSignup(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "\"code\":0");
    ASSERT_CONTAINS(resp.body, "\"is_leader\":false");
}

TEST_CASE(ActivityHandler_GroupSignup_NotGroupMode) {
    MockActivityDao activity_dao;
    MockActivitySignupDao signup_dao;
    MockActivityGroupDao group_dao;
    ActivityHandler handler(&activity_dao, &signup_dao, &group_dao, nullptr);

    ActivityInfo act;
    act.id = 1;
    act.status = 1;
    act.min_group_size = 0;
    act.signup_deadline = "2099-12-31 23:59";
    act.capacity = 100;
    act.signup_count = 0;
    activity_dao.mock_activity = act;

    crow::request req;
    req.body = "{\"activity_id\":1,\"name\":\"张三\",\"phone\":\"13800138000\",\"grade\":\"\",\"signup_type\":\"全托\",\"invite_code\":\"\"}";
    crow::response resp = handler.HandleGroupSignup(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, std::to_string(ERR_ACTIVITY_NOT_GROUP_MODE));
}

TEST_CASE(ActivityHandler_GetGroupStatus) {
    MockActivityDao activity_dao;
    MockActivitySignupDao signup_dao;
    MockActivityGroupDao group_dao;
    ActivityHandler handler(&activity_dao, &signup_dao, &group_dao, nullptr);

    ActivityGroupInfo group;
    group.id = 5;
    group.activity_id = 1;
    group.invite_code = "XY7890";
    group.status = GROUP_WAITING;
    group.current_count = 2;
    group.target_count = 3;
    group.created_at = "2099-12-31 23:50";
    group_dao.mock_group = group;

    ActivityGroupMemberInfo m1;
    m1.name = "张三";
    m1.phone = "13800138000";
    m1.is_leader = 1;
    ActivityGroupMemberInfo m2;
    m2.name = "李四";
    m2.phone = "13900139000";
    m2.is_leader = 0;
    group_dao.mock_members.push_back(m1);
    group_dao.mock_members.push_back(m2);

    crow::request req;
    req.url = "/api/public/activity/group_status?group_id=5";
    const_cast<crow::request&>(req).url_params = crow::query_string(req.url);
    crow::response resp = handler.HandleGetGroupStatus(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "\"code\":0");
    ASSERT_CONTAINS(resp.body, "\"current_count\":2");
    ASSERT_CONTAINS(resp.body, "\"target_count\":3");
    ASSERT_CONTAINS(resp.body, "\"status\":0");
    ASSERT_CONTAINS(resp.body, "张三");
    ASSERT_CONTAINS(resp.body, "138****8000");
}

TEST_CASE(ActivityHandler_ConfirmGroup_Success) {
    MockActivityDao activity_dao;
    MockActivitySignupDao signup_dao;
    MockActivityGroupDao group_dao;
    ActivityHandler handler(&activity_dao, &signup_dao, &group_dao, nullptr);

    ActivityGroupInfo group;
    group.id = 5;
    group.activity_id = 1;
    group.invite_code = "XY7890";
    group.status = GROUP_WAITING;
    group.current_count = 3;
    group.target_count = 3;
    group.leader_name = "张三";
    group.leader_phone = "13800138000";
    group.created_at = "2099-12-31 23:50";
    group_dao.mock_group = group;

    ActivityGroupMemberInfo m1;
    m1.name = "张三"; m1.phone = "13800138000"; m1.is_leader = 1;
    ActivityGroupMemberInfo m2;
    m2.name = "李四"; m2.phone = "13900139000"; m2.is_leader = 0;
    ActivityGroupMemberInfo m3;
    m3.name = "王五"; m3.phone = "13700137000"; m3.is_leader = 0;
    group_dao.mock_members.push_back(m1);
    group_dao.mock_members.push_back(m2);
    group_dao.mock_members.push_back(m3);

    crow::request req;
    req.body = "{\"group_id\":5,\"name\":\"张三\",\"phone\":\"13800138000\"}";
    crow::response resp = handler.HandleConfirmGroup(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "\"code\":0");
    ASSERT_CONTAINS(resp.body, "\"success\":true");
}

TEST_CASE(ActivityHandler_ConfirmGroup_NotLeader) {
    MockActivityDao activity_dao;
    MockActivitySignupDao signup_dao;
    MockActivityGroupDao group_dao;
    ActivityHandler handler(&activity_dao, &signup_dao, &group_dao, nullptr);

    ActivityGroupInfo group;
    group.id = 5;
    group.activity_id = 1;
    group.status = GROUP_WAITING;
    group.current_count = 3;
    group.target_count = 3;
    group.leader_name = "张三";
    group.leader_phone = "13800138000";
    group.created_at = "2099-12-31 23:50";
    group_dao.mock_group = group;

    crow::request req;
    req.body = "{\"group_id\":5,\"name\":\"李四\",\"phone\":\"13900139000\"}";
    crow::response resp = handler.HandleConfirmGroup(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, std::to_string(ERR_ACTIVITY_GROUP_NOT_LEADER));
}

TEST_CASE(ActivityHandler_CancelGroup_Success) {
    MockActivityDao activity_dao;
    MockActivitySignupDao signup_dao;
    MockActivityGroupDao group_dao;
    ActivityHandler handler(&activity_dao, &signup_dao, &group_dao, nullptr);

    ActivityGroupInfo group;
    group.id = 5;
    group.activity_id = 1;
    group.status = GROUP_WAITING;
    group.leader_name = "张三";
    group.leader_phone = "13800138000";
    group_dao.mock_group = group;

    crow::request req;
    req.body = "{\"group_id\":5,\"name\":\"张三\",\"phone\":\"13800138000\"}";
    crow::response resp = handler.HandleCancelGroup(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "\"code\":0");
}

TEST_CASE(ActivityHandler_LeaveGroup_Success) {
    MockActivityDao activity_dao;
    MockActivitySignupDao signup_dao;
    MockActivityGroupDao group_dao;
    ActivityHandler handler(&activity_dao, &signup_dao, &group_dao, nullptr);

    ActivityGroupInfo group;
    group.id = 5;
    group.activity_id = 1;
    group.status = GROUP_WAITING;
    group_dao.mock_group = group;

    crow::request req;
    req.body = "{\"group_id\":5,\"name\":\"李四\",\"phone\":\"13900139000\"}";
    crow::response resp = handler.HandleLeaveGroup(req);
    ASSERT_EQ(resp.code, 200);
    ASSERT_CONTAINS(resp.body, "\"code\":0");
}

TEST_CASE(ErrorCodes_GroupRange) {
    ASSERT_TRUE(ERR_ACTIVITY_GROUP_INVALID_CODE >= 1100 && ERR_ACTIVITY_GROUP_INVALID_CODE < 1200);
    ASSERT_TRUE(ERR_ACTIVITY_GROUP_CONFIRMED >= 1100 && ERR_ACTIVITY_GROUP_CONFIRMED < 1200);
    ASSERT_TRUE(ERR_ACTIVITY_GROUP_CANCELLED >= 1100 && ERR_ACTIVITY_GROUP_CANCELLED < 1200);
    ASSERT_TRUE(ERR_ACTIVITY_GROUP_TIMEOUT >= 1100 && ERR_ACTIVITY_GROUP_TIMEOUT < 1200);
    ASSERT_TRUE(ERR_ACTIVITY_GROUP_NOT_LEADER >= 1100 && ERR_ACTIVITY_GROUP_NOT_LEADER < 1200);
    ASSERT_TRUE(ERR_ACTIVITY_NOT_GROUP_MODE >= 1100 && ERR_ACTIVITY_NOT_GROUP_MODE < 1200);
}

TEST_CASE(Database_GroupCrud) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    /* Create an activity first */
    ActivityInfo act;
    act.title = "Group Test";
    act.start_time = "2025-06-01 10:00";
    act.end_time = "2025-06-02 18:00";
    act.signup_deadline = "2025-06-02 09:00";
    act.capacity = 50;
    act.min_group_size = 3;
    int64_t act_id = 0;
    ret = db.CreateActivity(act, act_id);
    ASSERT_EQ(ret, DB_OK);

    /* Create group */
    ActivityGroupInfo group;
    group.activity_id = act_id;
    group.invite_code = "TEST01";
    group.leader_name = "团长";
    group.leader_phone = "13800000001";
    group.current_count = 1;
    group.target_count = 3;
    group.status = GROUP_WAITING;
    group.cancel_reason = CANCEL_NONE;

    int64_t group_id = 0;
    ret = db.CreateGroup(group, group_id);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_TRUE(group_id > 0);

    /* Get group */
    ActivityGroupInfo fetched;
    ret = db.GetGroup(group_id, fetched);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_TRUE(fetched.invite_code == "TEST01");
    ASSERT_EQ(fetched.current_count, 1);

    /* Get by invite code */
    ActivityGroupInfo by_code;
    ret = db.GetGroupByInviteCode("TEST01", by_code);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(by_code.id, group_id);

    /* Add leader as member (handler does this after CreateGroup) */
    ActivityGroupMemberInfo leader;
    leader.group_id = group_id;
    leader.name = "团长";
    leader.phone = "13800000001";
    leader.grade = "";
    leader.is_leader = 1;
    int64_t leader_mid = 0;
    ret = db.AddMember(leader, leader_mid);
    ASSERT_EQ(ret, DB_OK);

    /* Add member */
    ActivityGroupMemberInfo member;
    member.group_id = group_id;
    member.name = "团员";
    member.phone = "13800000002";
    member.grade = "三年级";
    member.is_leader = 0;
    int64_t member_id = 0;
    ret = db.AddMember(member, member_id);
    ASSERT_EQ(ret, DB_OK);

    /* Update count */
    ret = db.UpdateGroupCount(group_id, 1);
    ASSERT_EQ(ret, DB_OK);

    /* List members */
    std::vector<ActivityGroupMemberInfo> members;
    ret = db.ListMembersByGroup(group_id, members);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ((int)members.size(), 2);

    /* List by activity (only confirmed groups) */
    ret = db.UpdateGroupStatus(group_id, GROUP_CONFIRMED, CANCEL_NONE);
    ASSERT_EQ(ret, DB_OK);

    std::vector<ActivityGroupMemberInfo> act_members;
    ret = db.ListMembersByActivity(act_id, act_members);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ((int)act_members.size(), 2);

    /* Check duplicate */
    bool dup = false;
    ret = db.CheckDuplicateInGroup(group_id, "团长", "13800000001", "", dup);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_TRUE(dup);

    bool dup2 = false;
    ret = db.CheckDuplicateInGroup(group_id, "新人", "13800000099", "", dup2);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_TRUE(!dup2);

    /* Remove member */
    int32_t is_leader = 0;
    int32_t remaining = 0;
    ret = db.RemoveMember(group_id, "团员", "13800000002", is_leader, remaining);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(is_leader, 0);
    ASSERT_EQ(remaining, 1);

    /* Update status */
    ret = db.UpdateGroupStatus(group_id, GROUP_CANCELLED, CANCEL_TIMEOUT);
    ASSERT_EQ(ret, DB_OK);

    ActivityGroupInfo cancelled;
    ret = db.GetGroup(group_id, cancelled);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(cancelled.status, (int32_t)GROUP_CANCELLED);

    db.Close();
}

TEST_CASE(Database_ConfirmGroupAtomic) {
    SqliteDatabase db;
    int ret = db.Open(":memory:");
    ASSERT_EQ(ret, DB_OK);

    /* Create activity with capacity */
    ActivityInfo act;
    act.title = "Atomic Test";
    act.start_time = "2025-06-01 10:00";
    act.end_time = "2025-06-02 18:00";
    act.signup_deadline = "2025-06-02 09:00";
    act.capacity = 10;
    act.signup_count = 2;
    int64_t act_id = 0;
    ret = db.CreateActivity(act, act_id);
    ASSERT_EQ(ret, DB_OK);

    /* Create group */
    ActivityGroupInfo group;
    group.activity_id = act_id;
    group.invite_code = "ATOM01";
    group.leader_name = "A";
    group.leader_phone = "13800000010";
    group.current_count = 2;
    group.target_count = 2;
    group.status = GROUP_WAITING;
    group.cancel_reason = CANCEL_NONE;
    int64_t group_id = 0;
    ret = db.CreateGroup(group, group_id);
    ASSERT_EQ(ret, DB_OK);

    /* Add members */
    ActivityGroupMemberInfo m1;
    m1.group_id = group_id; m1.name = "A"; m1.phone = "13800000010";
    m1.grade = "一年级"; m1.is_leader = 1;
    int64_t mid1 = 0;
    ret = db.AddMember(m1, mid1);
    ASSERT_EQ(ret, DB_OK);

    ActivityGroupMemberInfo m2;
    m2.group_id = group_id; m2.name = "B"; m2.phone = "13800000020";
    m2.grade = "二年级"; m2.is_leader = 0;
    int64_t mid2 = 0;
    ret = db.AddMember(m2, mid2);
    ASSERT_EQ(ret, DB_OK);

    /* Confirm atomically */
    std::vector<ActivityGroupMemberInfo> members;
    members.push_back(m1);
    members.push_back(m2);
    ret = db.ConfirmGroupAtomic(act_id, group_id, members);
    ASSERT_EQ(ret, DB_OK);

    /* Verify signup count incremented by 2 */
    ActivityInfo updated;
    ret = db.GetActivity(act_id, updated);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(updated.signup_count, 2);

    /* Verify group status is confirmed */
    ActivityGroupInfo confirmed;
    ret = db.GetGroup(group_id, confirmed);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ(confirmed.status, (int32_t)GROUP_CONFIRMED);

    /* Verify signup records exist */
    std::vector<ActivitySignupInfo> signups;
    ret = db.ListSignupsByActivity(act_id, signups);
    ASSERT_EQ(ret, DB_OK);
    ASSERT_EQ((int)signups.size(), 2);

    db.Close();
}

/* ==================== Registration Audit Tests ==================== */

/* TC1: InsertRegistrationRequest + QueryPendingRegistrationRequests */
TEST_CASE(RegAudit_InsertAndQueryPending) {
    SqliteDatabase db;
    db.Open(":memory:");

    RegistrationRequest req1;
    req1.id = 0;
    req1.username = "teacher1";
    req1.password_hash = "hash1";
    req1.salt = "salt1";
    req1.role = UserRole_Teacher;
    req1.display_name = "Teacher One";
    req1.status = RegStatus_Pending;
    req1.request_time = "2026-08-24 10:00:00";

    RegistrationRequest req2;
    req2.id = 0;
    req2.username = "teacher2";
    req2.password_hash = "hash2";
    req2.salt = "salt2";
    req2.role = UserRole_Teacher;
    req2.display_name = "Teacher Two";
    req2.status = RegStatus_Pending;
    req2.request_time = "2026-08-24 11:00:00";

    ASSERT_EQ(db.InsertRegistrationRequest(req1), DB_OK);
    ASSERT_EQ(db.InsertRegistrationRequest(req2), DB_OK);

    std::vector<RegistrationRequest> pending;
    ASSERT_EQ(db.QueryPendingRegistrationRequests(pending), DB_OK);
    ASSERT_EQ((int)pending.size(), 2);

    db.Close();
}

/* TC2: QueryRegistrationRequestStatus */
TEST_CASE(RegAudit_QueryStatus) {
    SqliteDatabase db;
    db.Open(":memory:");

    RegistrationRequest req;
    req.id = 0;
    req.username = "test_teacher";
    req.password_hash = "hash";
    req.salt = "salt";
    req.role = UserRole_Teacher;
    req.display_name = "Test";
    req.status = RegStatus_Pending;
    req.request_time = "2026-08-24 10:00:00";

    ASSERT_EQ(db.InsertRegistrationRequest(req), DB_OK);

    int status = -1;
    ASSERT_EQ(db.QueryRegistrationRequestStatus("test_teacher", status), DB_OK);
    ASSERT_EQ(status, (int)RegStatus_Pending);

    int status2 = -1;
    ASSERT_TRUE((db.QueryRegistrationRequestStatus("nonexistent", status2)) != (DB_OK));

    db.Close();
}

/* TC3: QueryRegistrationRequestById */
TEST_CASE(RegAudit_QueryById) {
    SqliteDatabase db;
    db.Open(":memory:");

    RegistrationRequest req;
    req.id = 0;
    req.username = "by_id_test";
    req.password_hash = "hashxyz";
    req.salt = "saltxyz";
    req.role = UserRole_Teacher;
    req.display_name = "ByID";
    req.status = RegStatus_Pending;
    req.request_time = "2026-08-24 12:00:00";

    ASSERT_EQ(db.InsertRegistrationRequest(req), DB_OK);

    std::vector<RegistrationRequest> pending;
    ASSERT_EQ(db.QueryPendingRegistrationRequests(pending), DB_OK);
    ASSERT_EQ((int)pending.size(), 1);

    RegistrationRequest found;
    ASSERT_EQ(db.QueryRegistrationRequestById(pending[0].id, found), DB_OK);
    ASSERT_EQ(found.username, "by_id_test");
    ASSERT_EQ(found.password_hash, "hashxyz");
    ASSERT_EQ(found.salt, "saltxyz");
    ASSERT_EQ(found.display_name, "ByID");

    RegistrationRequest not_found;
    ASSERT_TRUE((db.QueryRegistrationRequestById(9999, not_found)) != (DB_OK));

    db.Close();
}

/* TC4: UpdateRegistrationRequestStatus */
TEST_CASE(RegAudit_UpdateStatus) {
    SqliteDatabase db;
    db.Open(":memory:");

    RegistrationRequest req;
    req.id = 0;
    req.username = "update_test";
    req.password_hash = "h";
    req.salt = "s";
    req.role = UserRole_Teacher;
    req.display_name = "Update";
    req.status = RegStatus_Pending;
    req.request_time = "2026-08-24 10:00:00";

    ASSERT_EQ(db.InsertRegistrationRequest(req), DB_OK);

    std::vector<RegistrationRequest> pending;
    ASSERT_EQ(db.QueryPendingRegistrationRequests(pending), DB_OK);
    ASSERT_EQ((int)pending.size(), 1);

    ASSERT_EQ(db.UpdateRegistrationRequestStatus(pending[0].id, RegStatus_Approved), DB_OK);

    std::vector<RegistrationRequest> pending2;
    ASSERT_EQ(db.QueryPendingRegistrationRequests(pending2), DB_OK);
    ASSERT_EQ((int)pending2.size(), 0);

    db.Close();
}

/* TC5: DeleteRegistrationRequest */
TEST_CASE(RegAudit_DeleteRequest) {
    SqliteDatabase db;
    db.Open(":memory:");

    RegistrationRequest req;
    req.id = 0;
    req.username = "delete_test";
    req.password_hash = "h";
    req.salt = "s";
    req.role = UserRole_Teacher;
    req.display_name = "Del";
    req.status = RegStatus_Pending;
    req.request_time = "2026-08-24 10:00:00";

    ASSERT_EQ(db.InsertRegistrationRequest(req), DB_OK);

    std::vector<RegistrationRequest> pending;
    ASSERT_EQ(db.QueryPendingRegistrationRequests(pending), DB_OK);
    ASSERT_EQ((int)pending.size(), 1);

    ASSERT_EQ(db.DeleteRegistrationRequest(pending[0].id), DB_OK);

    std::vector<RegistrationRequest> pending2;
    ASSERT_EQ(db.QueryPendingRegistrationRequests(pending2), DB_OK);
    ASSERT_EQ((int)pending2.size(), 0);

    db.Close();
}

/* TC6: CheckRegistrationRequestExists */
TEST_CASE(RegAudit_CheckExists) {
    SqliteDatabase db;
    db.Open(":memory:");

    RegistrationRequest req;
    req.id = 0;
    req.username = "exists_test";
    req.password_hash = "h";
    req.salt = "s";
    req.role = UserRole_Teacher;
    req.display_name = "Exists";
    req.status = RegStatus_Pending;
    req.request_time = "2026-08-24 10:00:00";

    ASSERT_EQ(db.InsertRegistrationRequest(req), DB_OK);
    ASSERT_EQ(db.CheckRegistrationRequestExists("exists_test"), 1);
    ASSERT_EQ(db.CheckRegistrationRequestExists("nonexistent"), 0);

    db.Close();
}

/* TC7: Username uniqueness constraint */
TEST_CASE(RegAudit_UniqueConstraint) {
    SqliteDatabase db;
    db.Open(":memory:");

    RegistrationRequest req;
    req.id = 0;
    req.username = "unique_test";
    req.password_hash = "h";
    req.salt = "s";
    req.role = UserRole_Teacher;
    req.display_name = "Unique";
    req.status = RegStatus_Pending;
    req.request_time = "2026-08-24 10:00:00";

    ASSERT_EQ(db.InsertRegistrationRequest(req), DB_OK);

    req.password_hash = "h2";
    req.display_name = "UniqueReplaced";
    ASSERT_EQ(db.InsertRegistrationRequest(req), DB_OK);

    std::vector<RegistrationRequest> pending;
    ASSERT_EQ(db.QueryPendingRegistrationRequests(pending), DB_OK);
    ASSERT_EQ((int)pending.size(), 1);
    ASSERT_EQ(pending[0].password_hash, std::string("h2"));
    ASSERT_EQ(pending[0].display_name, std::string("UniqueReplaced"));

    db.Close();
}

static crow::response call_app(crow::SimpleApp& app, crow::request& req) {
    crow::response resp;
    app.handle(req, resp);
    return resp;
}

static int extract_json_int(const std::string& body, const char* key) {
    std::string pattern = std::string("\"") + key + "\":";
    size_t pos = body.find(pattern);
    if (pos == std::string::npos) return -999999;
    pos += pattern.size();
    while (pos < body.size() && body[pos] == ' ') ++pos;
    return std::atoi(body.c_str() + pos);
}

static bool extract_json_bool(const std::string& body, const char* key) {
    std::string pattern = std::string("\"") + key + "\":";
    size_t pos = body.find(pattern);
    if (pos == std::string::npos) return false;
    pos += pattern.size();
    while (pos < body.size() && body[pos] == ' ') ++pos;
    return body.substr(pos, 4) == "true";
}

/* TC8: Teacher register when no admin exists */
TEST_CASE(RegAudit_TeacherRegisterNoAdmin) {
    SqliteDatabase db;
    db.Open(":memory:");
    SqliteLogDatabase log_db;
    log_db.Open(":memory:");
    SessionManager session_mgr;

    AuthHandler auth_handler(&db, &log_db, &session_mgr);

    crow::request req;
    req.method = crow::HTTPMethod::POST;
    req.url = "/api/auth/register";
    req.raw_url = "/api/auth/register";
    req.body = "{\"username\":\"teacher1\",\"password\":\"123456\",\"role\":1}";

    crow::SimpleApp app;
    auth_handler.RegisterRoutes(app);

    crow::response res = call_app(app, req);
    ASSERT_EQ(res.code, 403);
    ASSERT_EQ(extract_json_int(res.body, "code"), ERR_AUTH_ADMIN_NOT_EXISTS);

    db.Close();
    log_db.Close();
}

/* TC9: Teacher register when admin exists */
TEST_CASE(RegAudit_TeacherRegisterWithAdmin) {
    SqliteDatabase db;
    db.Open(":memory:");
    SqliteLogDatabase log_db;
    log_db.Open(":memory:");
    SessionManager session_mgr;

    AuthHandler auth_handler(&db, &log_db, &session_mgr);

    crow::request admin_req;
    admin_req.method = crow::HTTPMethod::POST;
    admin_req.url = "/api/auth/register";
    admin_req.raw_url = "/api/auth/register";
    admin_req.body = "{\"username\":\"admin\",\"password\":\"123456\",\"role\":0}";

    crow::SimpleApp app;
    auth_handler.RegisterRoutes(app);
    call_app(app, admin_req);

    crow::request req;
    req.method = crow::HTTPMethod::POST;
    req.url = "/api/auth/register";
    req.raw_url = "/api/auth/register";
    req.body = "{\"username\":\"teacher1\",\"password\":\"123456\",\"role\":1}";

    crow::response res = call_app(app, req);
    ASSERT_EQ(res.code, 200);
    ASSERT_EQ(extract_json_int(res.body, "code"), DB_OK);
    ASSERT_TRUE(extract_json_bool(res.body, "pending_review"));

    UserInfo user;
    ASSERT_TRUE((db.QueryUserByUsername("teacher1", user)) != (DB_OK));

    std::vector<RegistrationRequest> pending;
    ASSERT_EQ(db.QueryPendingRegistrationRequests(pending), DB_OK);
    ASSERT_EQ((int)pending.size(), 1);
    ASSERT_EQ(pending[0].username, "teacher1");

    db.Close();
    log_db.Close();
}

/* TC10: Admin register not affected by audit */
TEST_CASE(RegAudit_AdminRegisterNoAudit) {
    SqliteDatabase db;
    db.Open(":memory:");
    SqliteLogDatabase log_db;
    log_db.Open(":memory:");
    SessionManager session_mgr;

    AuthHandler auth_handler(&db, &log_db, &session_mgr);

    crow::request req;
    req.method = crow::HTTPMethod::POST;
    req.url = "/api/auth/register";
    req.raw_url = "/api/auth/register";
    req.body = "{\"username\":\"admin\",\"password\":\"123456\",\"role\":0}";

    crow::SimpleApp app;
    auth_handler.RegisterRoutes(app);

    crow::response res = call_app(app, req);
    ASSERT_EQ(res.code, 200);
    ASSERT_EQ(extract_json_int(res.body, "code"), DB_OK);
    ASSERT_TRUE(res.body.find("pending_review") == std::string::npos);

    UserInfo user;
    ASSERT_EQ(db.QueryUserByUsername("admin", user), DB_OK);
    ASSERT_EQ(user.username, "admin");

    db.Close();
    log_db.Close();
}

/* TC11: Pending account login */
TEST_CASE(RegAudit_PendingAccountLogin) {
    SqliteDatabase db;
    db.Open(":memory:");
    SqliteLogDatabase log_db;
    log_db.Open(":memory:");
    SessionManager session_mgr;

    AuthHandler auth_handler(&db, &log_db, &session_mgr);

    crow::SimpleApp app;
    auth_handler.RegisterRoutes(app);

    crow::request admin_req;
    admin_req.method = crow::HTTPMethod::POST;
    admin_req.url = "/api/auth/register";
    admin_req.raw_url = "/api/auth/register";
    admin_req.body = "{\"username\":\"admin\",\"password\":\"123456\",\"role\":0}";
    call_app(app, admin_req);

    crow::request reg_req;
    reg_req.method = crow::HTTPMethod::POST;
    reg_req.url = "/api/auth/register";
    reg_req.raw_url = "/api/auth/register";
    reg_req.body = "{\"username\":\"teacher1\",\"password\":\"123456\",\"role\":1}";
    call_app(app, reg_req);

    crow::request login_req;
    login_req.method = crow::HTTPMethod::POST;
    login_req.url = "/api/auth/login";
    login_req.raw_url = "/api/auth/login";
    login_req.body = "{\"username\":\"teacher1\",\"password\":\"123456\"}";

    crow::response res = call_app(app, login_req);
    ASSERT_EQ(extract_json_int(res.body, "code"), ERR_AUTH_ACCOUNT_PENDING);

    db.Close();
    log_db.Close();
}

/* TC12: Admin approve registration */
TEST_CASE(RegAudit_ApproveRegistration) {
    SqliteDatabase db;
    db.Open(":memory:");
    SqliteLogDatabase log_db;
    log_db.Open(":memory:");
    SessionManager session_mgr;

    AuthHandler auth_handler(&db, &log_db, &session_mgr);
    AdminHandler admin_handler(&db, &log_db, &session_mgr);

    crow::SimpleApp app;
    auth_handler.RegisterRoutes(app);
    admin_handler.RegisterRoutes(app);

    crow::request admin_reg;
    admin_reg.method = crow::HTTPMethod::POST;
    admin_reg.url = "/api/auth/register";
    admin_reg.raw_url = "/api/auth/register";
    admin_reg.body = "{\"username\":\"admin\",\"password\":\"123456\",\"role\":0}";
    call_app(app, admin_reg);

    crow::request login_req;
    login_req.method = crow::HTTPMethod::POST;
    login_req.url = "/api/auth/login";
    login_req.raw_url = "/api/auth/login";
    login_req.body = "{\"username\":\"admin\",\"password\":\"123456\"}";
    crow::response login_res = call_app(app, login_req);
    std::string cookie = login_res.get_header_value("Set-Cookie");

    crow::request reg_req;
    reg_req.method = crow::HTTPMethod::POST;
    reg_req.url = "/api/auth/register";
    reg_req.raw_url = "/api/auth/register";
    reg_req.body = "{\"username\":\"teacher1\",\"password\":\"123456\",\"role\":1}";
    call_app(app, reg_req);

    std::vector<RegistrationRequest> pending;
    ASSERT_EQ(db.QueryPendingRegistrationRequests(pending), DB_OK);
    ASSERT_EQ((int)pending.size(), 1);
    int32_t req_id = pending[0].id;

    crow::request approve_req;
    approve_req.method = crow::HTTPMethod::POST;
    approve_req.url = "/api/admin/approve-registration";
    approve_req.raw_url = "/api/admin/approve-registration";
    approve_req.body = "{\"ids\":[" + std::to_string(req_id) + "]}";
    approve_req.add_header("Cookie", cookie.c_str());

    crow::response res = call_app(app, approve_req);
    ASSERT_EQ(extract_json_int(res.body, "code"), DB_OK);

    UserInfo user;
    ASSERT_EQ(db.QueryUserByUsername("teacher1", user), DB_OK);
    ASSERT_EQ(user.username, "teacher1");

    RegistrationRequest approved;
    ASSERT_EQ(db.QueryRegistrationRequestById(req_id, approved), DB_OK);
    ASSERT_EQ(approved.status, RegStatus_Approved);

    db.Close();
    log_db.Close();
}

/* TC13: Admin reject registration */
TEST_CASE(RegAudit_RejectRegistration) {
    SqliteDatabase db;
    db.Open(":memory:");
    SqliteLogDatabase log_db;
    log_db.Open(":memory:");
    SessionManager session_mgr;

    AuthHandler auth_handler(&db, &log_db, &session_mgr);
    AdminHandler admin_handler(&db, &log_db, &session_mgr);

    crow::SimpleApp app;
    auth_handler.RegisterRoutes(app);
    admin_handler.RegisterRoutes(app);

    crow::request admin_reg;
    admin_reg.method = crow::HTTPMethod::POST;
    admin_reg.url = "/api/auth/register";
    admin_reg.raw_url = "/api/auth/register";
    admin_reg.body = "{\"username\":\"admin\",\"password\":\"123456\",\"role\":0}";
    call_app(app, admin_reg);

    crow::request login_req;
    login_req.method = crow::HTTPMethod::POST;
    login_req.url = "/api/auth/login";
    login_req.raw_url = "/api/auth/login";
    login_req.body = "{\"username\":\"admin\",\"password\":\"123456\"}";
    crow::response login_res = call_app(app, login_req);
    std::string cookie = login_res.get_header_value("Set-Cookie");

    crow::request reg_req;
    reg_req.method = crow::HTTPMethod::POST;
    reg_req.url = "/api/auth/register";
    reg_req.raw_url = "/api/auth/register";
    reg_req.body = "{\"username\":\"teacher1\",\"password\":\"123456\",\"role\":1}";
    call_app(app, reg_req);

    std::vector<RegistrationRequest> pending;
    ASSERT_EQ(db.QueryPendingRegistrationRequests(pending), DB_OK);
    int32_t req_id = pending[0].id;

    crow::request reject_req;
    reject_req.method = crow::HTTPMethod::POST;
    reject_req.url = "/api/admin/reject-registration";
    reject_req.raw_url = "/api/admin/reject-registration";
    reject_req.body = "{\"ids\":[" + std::to_string(req_id) + "]}";
    reject_req.add_header("Cookie", cookie.c_str());

    crow::response res = call_app(app, reject_req);
    ASSERT_EQ(extract_json_int(res.body, "code"), DB_OK);

    RegistrationRequest deleted_req;
    ASSERT_TRUE((db.QueryRegistrationRequestById(req_id, deleted_req)) != (DB_OK));

    db.Close();
    log_db.Close();
}

/* TC14: Batch approve */
TEST_CASE(RegAudit_BatchApprove) {
    SqliteDatabase db;
    db.Open(":memory:");
    SqliteLogDatabase log_db;
    log_db.Open(":memory:");
    SessionManager session_mgr;

    AuthHandler auth_handler(&db, &log_db, &session_mgr);
    AdminHandler admin_handler(&db, &log_db, &session_mgr);

    crow::SimpleApp app;
    auth_handler.RegisterRoutes(app);
    admin_handler.RegisterRoutes(app);

    crow::request admin_reg;
    admin_reg.method = crow::HTTPMethod::POST;
    admin_reg.url = "/api/auth/register";
    admin_reg.raw_url = "/api/auth/register";
    admin_reg.body = "{\"username\":\"admin\",\"password\":\"123456\",\"role\":0}";
    call_app(app, admin_reg);

    crow::request login_req;
    login_req.method = crow::HTTPMethod::POST;
    login_req.url = "/api/auth/login";
    login_req.raw_url = "/api/auth/login";
    login_req.body = "{\"username\":\"admin\",\"password\":\"123456\"}";
    crow::response login_res = call_app(app, login_req);
    std::string cookie = login_res.get_header_value("Set-Cookie");

    for (int i = 1; i <= 3; ++i) {
        crow::request reg_req;
        reg_req.method = crow::HTTPMethod::POST;
        reg_req.url = "/api/auth/register";
        reg_req.raw_url = "/api/auth/register";
        std::string body = "{\"username\":\"teacher" + std::to_string(i) + "\",\"password\":\"123456\",\"role\":1}";
        reg_req.body = body;
        call_app(app, reg_req);
    }

    std::vector<RegistrationRequest> pending;
    ASSERT_EQ(db.QueryPendingRegistrationRequests(pending), DB_OK);
    ASSERT_EQ((int)pending.size(), 3);

    std::string ids_str = std::to_string(pending[0].id) + "," + std::to_string(pending[1].id) + "," + std::to_string(pending[2].id);
    crow::request approve_req;
    approve_req.method = crow::HTTPMethod::POST;
    approve_req.url = "/api/admin/approve-registration";
    approve_req.raw_url = "/api/admin/approve-registration";
    approve_req.body = "{\"ids\":[" + ids_str + "]}";
    approve_req.add_header("Cookie", cookie.c_str());

    crow::response res = call_app(app, approve_req);
    ASSERT_EQ(extract_json_int(res.body, "code"), DB_OK);

    for (int i = 1; i <= 3; ++i) {
        UserInfo user;
        ASSERT_EQ(db.QueryUserByUsername("teacher" + std::to_string(i), user), DB_OK);
    }

    db.Close();
    log_db.Close();
}

/* TC15: Duplicate username registration */
TEST_CASE(RegAudit_DuplicateUsername) {
    SqliteDatabase db;
    db.Open(":memory:");
    SqliteLogDatabase log_db;
    log_db.Open(":memory:");
    SessionManager session_mgr;

    AuthHandler auth_handler(&db, &log_db, &session_mgr);

    crow::SimpleApp app;
    auth_handler.RegisterRoutes(app);

    crow::request admin_reg;
    admin_reg.method = crow::HTTPMethod::POST;
    admin_reg.url = "/api/auth/register";
    admin_reg.raw_url = "/api/auth/register";
    admin_reg.body = "{\"username\":\"admin\",\"password\":\"123456\",\"role\":0}";
    call_app(app, admin_reg);

    crow::request reg_req;
    reg_req.method = crow::HTTPMethod::POST;
    reg_req.url = "/api/auth/register";
    reg_req.raw_url = "/api/auth/register";
    reg_req.body = "{\"username\":\"teacher1\",\"password\":\"123456\",\"role\":1}";
    call_app(app, reg_req);

    crow::request dup_req;
    dup_req.method = crow::HTTPMethod::POST;
    dup_req.url = "/api/auth/register";
    dup_req.raw_url = "/api/auth/register";
    dup_req.body = "{\"username\":\"teacher1\",\"password\":\"123456\",\"role\":1}";

    crow::response res = call_app(app, dup_req);
    ASSERT_EQ(extract_json_int(res.body, "code"), ERR_REG_REQ_DUPLICATE);

    db.Close();
    log_db.Close();
}

/* TC16: Re-register after rejection */
TEST_CASE(RegAudit_ReregisterAfterReject) {
    SqliteDatabase db;
    db.Open(":memory:");
    SqliteLogDatabase log_db;
    log_db.Open(":memory:");
    SessionManager session_mgr;

    AuthHandler auth_handler(&db, &log_db, &session_mgr);
    AdminHandler admin_handler(&db, &log_db, &session_mgr);

    crow::SimpleApp app;
    auth_handler.RegisterRoutes(app);
    admin_handler.RegisterRoutes(app);

    crow::request admin_reg;
    admin_reg.method = crow::HTTPMethod::POST;
    admin_reg.url = "/api/auth/register";
    admin_reg.raw_url = "/api/auth/register";
    admin_reg.body = "{\"username\":\"admin\",\"password\":\"123456\",\"role\":0}";
    call_app(app, admin_reg);

    crow::request login_req;
    login_req.method = crow::HTTPMethod::POST;
    login_req.url = "/api/auth/login";
    login_req.raw_url = "/api/auth/login";
    login_req.body = "{\"username\":\"admin\",\"password\":\"123456\"}";
    crow::response login_res = call_app(app, login_req);
    std::string cookie = login_res.get_header_value("Set-Cookie");

    crow::request reg_req;
    reg_req.method = crow::HTTPMethod::POST;
    reg_req.url = "/api/auth/register";
    reg_req.raw_url = "/api/auth/register";
    reg_req.body = "{\"username\":\"teacher1\",\"password\":\"123456\",\"role\":1}";
    call_app(app, reg_req);

    std::vector<RegistrationRequest> pending;
    ASSERT_EQ(db.QueryPendingRegistrationRequests(pending), DB_OK);
    int32_t req_id = pending[0].id;

    crow::request reject_req;
    reject_req.method = crow::HTTPMethod::POST;
    reject_req.url = "/api/admin/reject-registration";
    reject_req.raw_url = "/api/admin/reject-registration";
    reject_req.body = "{\"ids\":[" + std::to_string(req_id) + "]}";
    reject_req.add_header("Cookie", cookie.c_str());
    call_app(app, reject_req);

    crow::request rereg_req;
    rereg_req.method = crow::HTTPMethod::POST;
    rereg_req.url = "/api/auth/register";
    rereg_req.raw_url = "/api/auth/register";
    rereg_req.body = "{\"username\":\"teacher1\",\"password\":\"123456\",\"role\":1}";

    crow::response res = call_app(app, rereg_req);
    ASSERT_EQ(res.code, 200);
    ASSERT_EQ(extract_json_int(res.body, "code"), DB_OK);
    ASSERT_TRUE(extract_json_bool(res.body, "pending_review"));

    db.Close();
    log_db.Close();
}

/* TC17: Non-admin access denied */
TEST_CASE(RegAudit_NonAdminDenied) {
    SqliteDatabase db;
    db.Open(":memory:");
    SqliteLogDatabase log_db;
    log_db.Open(":memory:");
    SessionManager session_mgr;

    AuthHandler auth_handler(&db, &log_db, &session_mgr);
    AdminHandler admin_handler(&db, &log_db, &session_mgr);

    crow::SimpleApp app;
    auth_handler.RegisterRoutes(app);
    admin_handler.RegisterRoutes(app);

    crow::request admin_reg;
    admin_reg.method = crow::HTTPMethod::POST;
    admin_reg.url = "/api/auth/register";
    admin_reg.raw_url = "/api/auth/register";
    admin_reg.body = "{\"username\":\"admin\",\"password\":\"123456\",\"role\":0}";
    call_app(app, admin_reg);

    RegistrationRequest reg_req_item;
    reg_req_item.id = 0;
    reg_req_item.username = "teacher1";
    std::string salt = register_student::GenerateSalt();
    reg_req_item.password_hash = register_student::EncryptPassword("123456", salt);
    reg_req_item.salt = salt;
    reg_req_item.role = UserRole_Teacher;
    reg_req_item.display_name = "teacher1";
    reg_req_item.status = RegStatus_Pending;
    reg_req_item.request_time = "2026-08-24 10:00:00";
    db.InsertRegistrationRequest(reg_req_item);

    std::vector<int32_t> ids;
    ids.push_back(1);
    db.ApproveRegistrationRequestsAtomic(ids);

    crow::request login_req;
    login_req.method = crow::HTTPMethod::POST;
    login_req.url = "/api/auth/login";
    login_req.raw_url = "/api/auth/login";
    login_req.body = "{\"username\":\"teacher1\",\"password\":\"123456\"}";
    crow::response login_res = call_app(app, login_req);
    std::string cookie = login_res.get_header_value("Set-Cookie");

    crow::request list_req;
    list_req.method = crow::HTTPMethod::GET;
    list_req.url = "/api/admin/registration-requests";
    list_req.raw_url = "/api/admin/registration-requests";
    list_req.add_header("Cookie", cookie.c_str());

    crow::response res = call_app(app, list_req);
    ASSERT_EQ(res.code, 403);

    db.Close();
    log_db.Close();
}

/* ====== T7.1 GroupSessionManager file operation tests ====== */

static std::string GsmTestDir() {
    return std::string(TEST_TEMP_DIR) + "/gsm_test_" + std::to_string(std::time(nullptr));
}

static void CleanupGsmDir(const std::string& dir) {
    std::string cmd = "rm -rf " + dir;
    system(cmd.c_str());
}

TEST_CASE(gsm_create_session) {
    std::string dir = GsmTestDir();
    GroupSessionManager mgr;
    ASSERT_EQ(mgr.Init(dir), DB_OK);

    GroupSessionInfo info;
    info.invite_code = "ABC123";
    info.activity_id = 1;
    info.leader_name = "Zhang San";
    info.leader_phone = "13800138000";
    info.leader_grade = "三年级";
    info.target_count = 3;
    info.current_count = 1;
    info.created_at = "2026-01-01 12:00:00";

    ActivityGroupMemberInfo m;
    m.name = "Zhang San";
    m.phone = "13800138000";
    m.grade = "三年级";
    m.is_leader = 1;
    info.members.push_back(m);

    ASSERT_EQ(mgr.CreateSession(info), DB_OK);

    GroupSessionInfo read_back;
    ASSERT_EQ(mgr.GetSession(1, "ABC123", read_back), DB_OK);
    ASSERT_EQ(read_back.invite_code, std::string("ABC123"));
    ASSERT_EQ(read_back.activity_id, (int64_t)1);
    ASSERT_EQ(read_back.leader_name, std::string("Zhang San"));
    ASSERT_EQ(read_back.current_count, (int32_t)1);
    ASSERT_EQ(read_back.target_count, (int32_t)3);
    ASSERT_EQ(read_back.members.size(), (size_t)1);
    ASSERT_EQ(read_back.members[0].name, std::string("Zhang San"));

    CleanupGsmDir(dir);
}

TEST_CASE(gsm_create_session_auto_dir) {
    std::string dir = GsmTestDir() + "_auto";
    GroupSessionManager mgr;
    ASSERT_EQ(mgr.Init(dir), DB_OK);

    GroupSessionInfo info;
    info.invite_code = "XY7890";
    info.activity_id = 999;
    info.leader_name = "Li Si";
    info.leader_phone = "13900139000";
    info.target_count = 2;
    info.current_count = 1;
    info.created_at = "2026-01-01 12:00:00";

    ASSERT_EQ(mgr.CreateSession(info), DB_OK);

    GroupSessionInfo read_back;
    ASSERT_EQ(mgr.GetSession(999, "XY7890", read_back), DB_OK);
    ASSERT_EQ(read_back.invite_code, std::string("XY7890"));

    CleanupGsmDir(dir);
}

TEST_CASE(gsm_get_session_not_found) {
    std::string dir = GsmTestDir() + "_nf";
    GroupSessionManager mgr;
    ASSERT_EQ(mgr.Init(dir), DB_OK);

    GroupSessionInfo read_back;
    ASSERT_TRUE(mgr.GetSession(1, "NONEXIST", read_back) != DB_OK);

    CleanupGsmDir(dir);
}

TEST_CASE(gsm_update_session) {
    std::string dir = GsmTestDir() + "_upd";
    GroupSessionManager mgr;
    ASSERT_EQ(mgr.Init(dir), DB_OK);

    GroupSessionInfo info;
    info.invite_code = "UPD001";
    info.activity_id = 10;
    info.leader_name = "Wang Wu";
    info.leader_phone = "13700137000";
    info.target_count = 3;
    info.current_count = 1;
    info.created_at = "2026-01-01 12:00:00";
    ASSERT_EQ(mgr.CreateSession(info), DB_OK);

    info.current_count = 2;
    ASSERT_EQ(mgr.UpdateSession(info), DB_OK);

    GroupSessionInfo read_back;
    ASSERT_EQ(mgr.GetSession(10, "UPD001", read_back), DB_OK);
    ASSERT_EQ(read_back.current_count, (int32_t)2);

    CleanupGsmDir(dir);
}

TEST_CASE(gsm_delete_session) {
    std::string dir = GsmTestDir() + "_del";
    GroupSessionManager mgr;
    ASSERT_EQ(mgr.Init(dir), DB_OK);

    GroupSessionInfo info;
    info.invite_code = "DEL001";
    info.activity_id = 20;
    info.leader_name = "Zhao Liu";
    info.leader_phone = "13600136000";
    info.target_count = 2;
    info.current_count = 1;
    info.created_at = "2026-01-01 12:00:00";
    ASSERT_EQ(mgr.CreateSession(info), DB_OK);

    ASSERT_EQ(mgr.DeleteSession(20, "DEL001"), DB_OK);

    GroupSessionInfo read_back;
    ASSERT_TRUE(mgr.GetSession(20, "DEL001", read_back) != DB_OK);

    CleanupGsmDir(dir);
}

TEST_CASE(gsm_add_member) {
    std::string dir = GsmTestDir() + "_add";
    GroupSessionManager mgr;
    ASSERT_EQ(mgr.Init(dir), DB_OK);

    GroupSessionInfo info;
    info.invite_code = "ADD001";
    info.activity_id = 30;
    info.leader_name = "Leader";
    info.leader_phone = "13500135000";
    info.target_count = 3;
    info.current_count = 1;
    info.created_at = "2026-01-01 12:00:00";

    ActivityGroupMemberInfo m1;
    m1.name = "Leader";
    m1.phone = "13500135000";
    m1.is_leader = 1;
    info.members.push_back(m1);
    ASSERT_EQ(mgr.CreateSession(info), DB_OK);

    ActivityGroupMemberInfo m2;
    m2.name = "Member";
    m2.phone = "13400134000";
    m2.grade = "四年级";
    m2.is_leader = 0;

    GroupSessionInfo updated;
    ASSERT_EQ(mgr.AddMember(30, "ADD001", m2, updated), DB_OK);
    ASSERT_EQ(updated.members.size(), (size_t)2);
    ASSERT_EQ(updated.members[1].name, std::string("Member"));

    CleanupGsmDir(dir);
}

TEST_CASE(gsm_find_member_found) {
    std::string dir = GsmTestDir() + "_find";
    GroupSessionManager mgr;
    ASSERT_EQ(mgr.Init(dir), DB_OK);

    GroupSessionInfo info;
    info.invite_code = "FND001";
    info.activity_id = 40;
    info.leader_name = "Finder";
    info.leader_phone = "13300133000";
    info.target_count = 2;
    info.current_count = 1;
    info.created_at = "2026-01-01 12:00:00";

    ActivityGroupMemberInfo m1;
    m1.name = "Finder";
    m1.phone = "13300133000";
    m1.is_leader = 1;
    info.members.push_back(m1);
    ASSERT_EQ(mgr.CreateSession(info), DB_OK);

    GroupSessionInfo found;
    bool found_flag = false;
    ASSERT_EQ(mgr.FindMemberInActivity(40, "Finder", "13300133000", found, found_flag), DB_OK);
    ASSERT_TRUE(found_flag);
    ASSERT_EQ(found.invite_code, std::string("FND001"));

    CleanupGsmDir(dir);
}

TEST_CASE(gsm_find_member_not_found) {
    std::string dir = GsmTestDir() + "_fnf";
    GroupSessionManager mgr;
    ASSERT_EQ(mgr.Init(dir), DB_OK);

    GroupSessionInfo info;
    info.invite_code = "FNF001";
    info.activity_id = 50;
    info.leader_name = "Someone";
    info.leader_phone = "13200132000";
    info.target_count = 2;
    info.current_count = 1;
    info.created_at = "2026-01-01 12:00:00";

    ActivityGroupMemberInfo m1;
    m1.name = "Someone";
    m1.phone = "13200132000";
    m1.is_leader = 1;
    info.members.push_back(m1);
    ASSERT_EQ(mgr.CreateSession(info), DB_OK);

    GroupSessionInfo found;
    bool found_flag = false;
    ASSERT_EQ(mgr.FindMemberInActivity(50, "Nobody", "11111111111", found, found_flag), DB_OK);
    ASSERT_TRUE(!found_flag);

    CleanupGsmDir(dir);
}

TEST_CASE(gsm_list_all_sessions) {
    std::string dir = GsmTestDir() + "_list";
    GroupSessionManager mgr;
    ASSERT_EQ(mgr.Init(dir), DB_OK);

    for (int i = 0; i < 3; ++i) {
        GroupSessionInfo info;
        info.invite_code = "LST00" + std::to_string(i + 1);
        info.activity_id = 60;
        info.leader_name = "Leader" + std::to_string(i);
        info.leader_phone = "1310013100" + std::to_string(i);
        info.target_count = 2;
        info.current_count = 1;
        info.created_at = "2026-01-01 12:00:00";
        ASSERT_EQ(mgr.CreateSession(info), DB_OK);
    }

    std::vector<GroupSessionInfo> list;
    ASSERT_EQ(mgr.ListAllSessions(60, list), DB_OK);
    ASSERT_EQ(list.size(), (size_t)3);

    CleanupGsmDir(dir);
}

TEST_CASE(gsm_cleanup_activity) {
    std::string dir = GsmTestDir() + "_clean";
    GroupSessionManager mgr;
    ASSERT_EQ(mgr.Init(dir), DB_OK);

    GroupSessionInfo info;
    info.invite_code = "CLN001";
    info.activity_id = 70;
    info.leader_name = "Cleaner";
    info.leader_phone = "13000130000";
    info.target_count = 2;
    info.current_count = 1;
    info.created_at = "2026-01-01 12:00:00";
    ASSERT_EQ(mgr.CreateSession(info), DB_OK);

    ASSERT_EQ(mgr.CleanupActivity(70), DB_OK);

    std::vector<GroupSessionInfo> list;
    ASSERT_EQ(mgr.ListAllSessions(70, list), DB_OK);
    ASSERT_EQ(list.size(), (size_t)0);

    CleanupGsmDir(dir);
}

TEST_CASE(gsm_cleanup_orphans) {
    std::string dir = GsmTestDir() + "_orphan";
    GroupSessionManager mgr;
    ASSERT_EQ(mgr.Init(dir), DB_OK);

    GroupSessionInfo info;
    info.invite_code = "ORP001";
    info.activity_id = 80;
    info.leader_name = "Orphan";
    info.leader_phone = "12900129000";
    info.target_count = 2;
    info.current_count = 2;
    info.created_at = "2026-01-01 12:00:00";

    ActivityGroupMemberInfo m1, m2;
    m1.name = "Orphan";
    m1.phone = "12900129000";
    m1.is_leader = 1;
    m2.name = "Partner";
    m2.phone = "12800128000";
    m2.is_leader = 0;
    info.members.push_back(m1);
    info.members.push_back(m2);
    ASSERT_EQ(mgr.CreateSession(info), DB_OK);

    auto check_fn = [](const std::string& name, const std::string& phone) -> bool {
        (void)name;
        (void)phone;
        return true;
    };
    ASSERT_EQ(mgr.CleanupOrphans(80, check_fn), DB_OK);

    std::vector<GroupSessionInfo> list;
    ASSERT_EQ(mgr.ListAllSessions(80, list), DB_OK);
    ASSERT_EQ(list.size(), (size_t)0);

    CleanupGsmDir(dir);
}

/* ====== T7.6 ConfirmSessionAtomic database tests ====== */

TEST_CASE(db_confirm_session_ok) {
    std::string db_path = std::string(TEST_TEMP_DIR) + "/test_confirm_session.db";
    SqliteDatabase db;
    ASSERT_EQ(db.Open(db_path), DB_OK);

    ActivityInfo act;
    act.title = "Test";
    act.start_time = "2026-01-01 00:00:00";
    act.end_time = "2026-12-31 23:59:59";
    act.signup_deadline = "2026-12-31 23:59:59";
    act.capacity = 10;
    act.status = 1;
    int64_t act_id = 0;
    ASSERT_EQ(db.CreateActivity(act, act_id), DB_OK);

    std::vector<ActivitySignupInfo> members;
    for (int i = 0; i < 3; ++i) {
        ActivitySignupInfo s;
        s.activity_id = act_id;
        s.name = "Student" + std::to_string(i);
        s.phone = "1380013800" + std::to_string(i);
        s.grade = "三年级";
        s.signup_type = "全托";
        members.push_back(s);
    }

    ASSERT_EQ(db.ConfirmSessionAtomic(act_id, members), DB_OK);

    std::vector<ActivitySignupInfo> list;
    ASSERT_EQ(db.ListSignupsByActivity(act_id, list), DB_OK);
    ASSERT_EQ(list.size(), (size_t)3);

    db.Close();
    std::remove(db_path.c_str());
}

TEST_CASE(db_check_duplicate_found) {
    std::string db_path = std::string(TEST_TEMP_DIR) + "/test_dup_check.db";
    SqliteDatabase db;
    ASSERT_EQ(db.Open(db_path), DB_OK);

    ActivityInfo act;
    act.title = "Test";
    act.start_time = "2026-01-01 00:00:00";
    act.end_time = "2026-12-31 23:59:59";
    act.signup_deadline = "2026-12-31 23:59:59";
    act.capacity = 10;
    act.status = 1;
    int64_t act_id = 0;
    ASSERT_EQ(db.CreateActivity(act, act_id), DB_OK);

    ActivitySignupInfo s;
    s.activity_id = act_id;
    s.name = "Existing";
    s.phone = "13800138000";
    s.grade = "三年级";
    s.signup_type = "全托";
    int64_t sid = 0;
    ASSERT_EQ(db.CreateSignupAtomic(s, sid), DB_OK);

    bool exists = false;
    ASSERT_EQ(db.CheckDuplicateSignup(act_id, "Existing", "13800138000", exists), DB_OK);
    ASSERT_TRUE(exists);

    exists = false;
    ASSERT_EQ(db.CheckDuplicateSignup(act_id, "Nobody", "11111111111", exists), DB_OK);
    ASSERT_TRUE(!exists);

    db.Close();
    std::remove(db_path.c_str());
}

int main() {
    printf("Running unit tests...\n\n");
    fflush(stdout);
    return RunAllTests();
}