#ifndef __SQLITE_DATABASE_H__
#define __SQLITE_DATABASE_H__

#include "i_user_dao.h"
#include "i_class_dao.h"
#include "i_registration_dao.h"
#include "i_resource_dao.h"
#include "i_attendance_dao.h"
#include "i_refund_dao.h"
#include "i_activity_dao.h"
#include "i_activity_signup_dao.h"
#include "i_activity_group_dao.h"
#include "i_data_transfer_dao.h"
#include <sqlite3.h>
#include <mutex>

class SqliteDatabase : public IUserDao, public IClassDao, public IRegistrationDao, public IResourceDao, public IAttendanceDao, public IRefundDao, public IActivityDao, public IActivitySignupDao, public IActivityGroupDao, public IDataTransferDao {
public:
    SqliteDatabase();
    ~SqliteDatabase();

    int Open(const std::string& db_path);
    void Close();

    /* IUserDao */
    int InsertUser(const UserInfo& info) override;
    int QueryUserByUsername(const std::string& username, UserInfo& info) override;
    int QueryUserById(int32_t id, UserInfo& info) override;
    int UpdatePassword(int32_t user_id, const std::string& password_hash, const std::string& salt) override;
    int UpdateUserInfo(int32_t user_id, const std::string& display_name) override;
    int DeleteUser(int32_t user_id) override;
    int CheckAdminExists() override;
    int QueryAllTeachers(std::vector<UserInfo>& teachers) override;
    int InsertResetRequest(const PasswordResetRequest& req) override;
    int QueryPendingResetRequests(std::vector<PasswordResetRequest>& requests) override;
    int ApproveResetRequest(int32_t request_id, int32_t approver_id, const std::string& new_hash, const std::string& new_salt) override;
    int CheckResetPending(int32_t user_id) override;

    /* Registration request */
    int InsertRegistrationRequest(const RegistrationRequest& request) override;
    int QueryRegistrationRequestStatus(const std::string& username, int& status) override;
    int QueryPendingRegistrationRequests(std::vector<RegistrationRequest>& requests) override;
    int QueryRegistrationRequestById(int32_t id, RegistrationRequest& request) override;
    int UpdateRegistrationRequestStatus(int32_t id, RegistrationRequestStatusType status) override;
    int DeleteRegistrationRequest(int32_t id) override;
    int CheckRegistrationRequestExists(const std::string& username) override;
    int ApproveRegistrationRequestsAtomic(const std::vector<int32_t>& ids) override;

    /* IClassDao */
    int InsertClass(const ClassInfo& info) override;
    int QueryClassById(int32_t id, ClassInfo& info) override;
    int QueryClassByName(const std::string& name, ClassInfo& info) override;
    int QueryAllClasses(std::vector<ClassInfo>& classes) override;
    int QueryActiveClasses(std::vector<ClassInfo>& classes) override;
    int SearchClassesByName(const std::string& keyword, std::vector<ClassInfo>& classes) override;
    int SearchActiveClassesByName(const std::string& keyword, std::vector<ClassInfo>& classes) override;
    int UpdateEnrollment(int32_t class_id, int32_t capacity) override;
    int IncrementEnrollmentUsed(int32_t class_id, double delta) override;
    int InsertPrice(const PriceInfo& info) override;
    int QueryPricesByClassId(int32_t class_id, std::vector<PriceInfo>& prices) override;
    int QueryPriceById(int32_t price_id, PriceInfo& price) override;
    int InsertQrcode(int32_t price_id, const std::string& qrcode_path) override;
    int QueryQrcodesByPriceId(int32_t price_id, std::vector<std::string>& paths) override;
    int InsertClassType(const ClassType& type) override;
    int QueryAllClassTypes(std::vector<ClassType>& types) override;
    int DeleteClassType(int32_t id) override;
    int DeleteClass(int32_t id) override;
    int QueryClassTypeById(int32_t id, ClassType& type) override;

    /* IClassDao - 价位预设管理 */
    int InsertPricePreset(const PricePresetInfo& info) override;
    int QueryAllPricePresets(std::vector<PricePresetInfo>& presets) override;
    int QueryPricePresetById(int32_t id, PricePresetInfo& info) override;
    int DeletePricePresetAtomic(int32_t preset_id,
                                std::vector<std::string>& deleted_files) override;
    int AddPresetQrcode(int32_t preset_id, const std::string& qrcode_path) override;
    int DeletePresetQrcode(int32_t preset_id, const std::string& qrcode_path,
                           std::string& deleted_file) override;
    std::string QueryClassNameByPresetId(int32_t preset_id) override;
    int CreateClassWithPricesAtomic(
        const ClassInfo& class_info,
        const std::vector<std::pair<std::string, int32_t> >& prices,
        int32_t& generated_class_id) override;
    int UpdateClassPricesAtomic(
        int32_t class_id,
        const std::vector<PriceUpdateItem>& prices) override;

    /* IRegistrationDao */
    int InsertRegistration(const RegistrationInfo& info) override;
    int QueryRegistrationById(int32_t id, RegistrationInfo& info) override;
    int QueryRegistrationsByClassId(int32_t class_id, std::vector<RegistrationInfo>& regs) override;
    int QueryRegistrationsByTimeRange(const std::string& start_time, const std::string& end_time, std::vector<RegistrationInfo>& regs) override;
    int CountEnrolledByClassId(int32_t class_id) override;
    int CountActiveStudentsByClassId(int32_t class_id) override;
    int CheckEnrollmentAvailable(int32_t class_id, int32_t capacity) override;
    int RegisterStudentAtomic(const RegistrationInfo& info, int32_t class_id,
                              int32_t capacity, int32_t need_bed,
                              int32_t bed_resource_id) override;
    int RegisterStudentsBatchAtomic(const std::vector<RegistrationInfo>& infos,
                                    int32_t class_id, int32_t capacity,
                                    int32_t bed_resource_id) override;
    int UpdateStudentBasicInfo(const RegistrationInfo& info) override;
    int TransferClassAtomic(int32_t registration_id, int32_t old_class_id,
                            int32_t new_class_id, int32_t new_class_capacity) override;
    int RegisterDepositAtomic(const std::vector<RegistrationInfo>& infos,
                              int32_t class_id, int32_t capacity,
                              int32_t bed_resource_id) override;
    int SupplementDepositAtomic(int32_t registration_id,
                                int32_t target_class_price_id,
                                int32_t target_preset_id,
                                double target_amount,
                                const std::string& operator_name,
                                const std::string& operate_time,
                                double& out_supplement_amount) override;
    int DeleteRegistrationAtomic(int32_t registration_id, int32_t bed_resource_id) override;
    int RenewRegistrationAtomic(int32_t registration_id,
                                const std::string& new_end_date,
                                double renew_amount,
                                double enrollment_delta,
                                const std::string& operator_name,
                                const std::string& operate_time) override;
    double QueryEnrollmentUsedByClassId(int32_t class_id) override;
    int QueryRenewalsByRegId(int32_t registration_id,
                             std::vector<RenewalRecordInfo>& records) override;

    /* IResourceDao */
    int InsertResource(const ResourceInfo& info) override;
    int QueryResourceById(int32_t id, ResourceInfo& info) override;
    int QueryAllResources(std::vector<ResourceInfo>& resources) override;
    int UpdateResourceTotal(int32_t id, int32_t total_count) override;
    int DeleteResource(int32_t id) override;
    int CheckResourceInUse(int32_t resource_id, std::vector<std::string>& using_classes) override;
    int InsertAllocation(const ResourceAllocation& alloc) override;
    int QueryAllocationsByResourceId(int32_t resource_id, std::vector<ResourceAllocation>& allocs) override;
    int QueryAllocationsByClassId(int32_t class_id, std::vector<ResourceAllocation>& allocs) override;
    int QueryAllocationsByTimeRange(const std::string& start_time, const std::string& end_time, std::vector<ResourceAllocation>& allocs) override;
    int CheckResourceCodeOccupied(int32_t resource_id, int32_t resource_code) override;
    int CheckStudentResourceAllocated(int32_t resource_id, int32_t registration_id) override;
    int IncrementResourceUsed(int32_t resource_id) override;
    int DecrementResourceUsed(int32_t resource_id) override;
    int QueryResourceByName(const std::string& name, ResourceInfo& info) override;
    int QueryBedResourceRemain(int32_t resource_id) override;
    int QueryResourceByType(int32_t resource_type, ResourceInfo& info) override;
    int AllocateResourceAtomic(const ResourceAllocation& alloc) override;

    /* IAttendanceDao */
    int InsertAttendance(const AttendanceRecord& record) override;
    int QueryAttendanceByClassAndDate(int32_t class_id, const std::string& date, std::vector<AttendanceRecord>& records) override;
    int CheckAttendanceExists(int32_t class_id, const std::string& date) override;
    int QueryAttendanceByClassAndDateRange(int32_t class_id, const std::string& start_date, const std::string& end_date, std::vector<AttendanceRecord>& records) override;
    int QueryAttendanceByRegId(int32_t registration_id, std::vector<AttendanceRecord>& records) override;

    /* IRefundDao */
    int InsertRefundAtomic(RefundRecordInfo& info, double original_amount, double tolerance, bool skip_attendance_check = false) override;
    int CancelRefundAtomic(int32_t registration_id, const std::string& cancel_operator_name,
                           const std::string& cancel_time, double& restored_paid_amount) override;
    int QueryRefundsByRegId(int32_t registration_id, std::vector<RefundRecordInfo>& records) override;
    int QueryActiveRefundSumByRegId(int32_t registration_id, double& sum) override;

    /* IActivityDao */
    int CreateActivity(const ActivityInfo& info, int64_t& out_id) override;
    int UpdateActivity(const ActivityInfo& info) override;
    int DeleteActivity(int64_t id) override;
    int GetActivity(int64_t id, ActivityInfo& info) override;
    int ListActivities(std::vector<ActivityInfo>& list) override;
    int ListPublishedActivities(std::vector<ActivityInfo>& list) override;
    int ListPublishedActivitiesPaged(std::vector<ActivityInfo>& list, int limit, int offset, int& total_count) override;
    int UpdateActivityStatus(int64_t id, int32_t status) override;
    int UpdateActivityImage(int64_t id, const std::string& field,
                            const std::string& path) override;
    int IncrementSignupCount(int64_t id) override;
    int BatchIncrementSignupCount(int64_t id, int32_t delta) override;
    int BatchUpdateSortOrder(
        const std::vector<std::pair<int64_t, int32_t>>& orders) override;
    int AddCoverImage(int64_t activity_id, const std::string& path,
                      int sort_order, int64_t& out_id) override;
    int GetCoverImages(int64_t activity_id,
                       std::vector<ActivityCoverImage>& images) override;
    int DeleteCoverImage(int64_t image_id) override;
    int DeleteCoverImagesByActivityId(int64_t activity_id) override;

    /* Promotion */
    int AddPromotionImage(const std::string& path, int sort_order, int64_t& out_id) override;
    int GetPromotionImages(std::vector<ActivityCoverImage>& images) override;
    int DeletePromotionImage(int64_t image_id) override;
    int BatchUpdatePromotionImageSortOrder(
        const std::vector<std::pair<int64_t, int32_t>>& orders) override;
    int GetPromotionText(std::string& content) override;
    int UpdatePromotionText(const std::string& content) override;

    int GetActivityNotice(std::string& content) override;
    int UpdateActivityNotice(const std::string& content) override;

    int AddAboutUsCard(const std::string& image_path,
                       const std::string& text, int32_t layout_type,
                       int32_t sort_order, int64_t& out_id) override;
    int GetAboutUsCards(std::vector<AboutUsCard>& cards) override;
    int UpdateAboutUsCard(int64_t id, const std::string& image_path,
                          const std::string& text,
                          int32_t layout_type) override;
    int DeleteAboutUsCard(int64_t card_id) override;
    int BatchUpdateAboutUsCardSortOrder(
        const std::vector<std::pair<int64_t, int32_t>>& orders) override;

    /* IActivitySignupDao */
    int CreateSignupAtomic(const ActivitySignupInfo& info, int64_t& out_id) override;
    int ListSignupsByActivity(int64_t activity_id,
                              std::vector<ActivitySignupInfo>& list) override;
    int ConfirmSessionAtomic(int64_t activity_id,
                             const std::vector<ActivitySignupInfo>& members) override;
    int CheckDuplicateSignup(int64_t activity_id,
                             const std::string& name,
                             const std::string& phone,
                             bool& out_exists) override;

    /* IActivityGroupDao */
    int CreateGroup(const ActivityGroupInfo& info, int64_t& out_id) override;
    int GetGroup(int64_t group_id, ActivityGroupInfo& info) override;
    int GetGroupByInviteCode(const std::string& invite_code,
                             ActivityGroupInfo& info) override;
    int AddMember(const ActivityGroupMemberInfo& member,
                  int64_t& out_id) override;
    int RemoveMember(int64_t group_id, const std::string& name,
                     const std::string& phone,
                     int32_t& out_is_leader,
                     int32_t& out_remaining_count) override;
    int UpdateGroupCount(int64_t group_id, int32_t delta) override;
    int UpdateGroupStatus(int64_t group_id, int32_t status,
                          int32_t cancel_reason) override;
    int ListMembersByGroup(int64_t group_id,
                           std::vector<ActivityGroupMemberInfo>& list) override;
    int ListMembersByActivity(int64_t activity_id,
                              std::vector<ActivityGroupMemberInfo>& list) override;
    int CheckDuplicateInGroup(int64_t group_id, const std::string& name,
                              const std::string& phone,
                              const std::string& grade,
                              bool& out_duplicate) override;
    int ConfirmGroupAtomic(int64_t activity_id, int64_t group_id,
                           const std::vector<ActivityGroupMemberInfo>& members) override;

    /* IDataTransferDao */
    int ExportTableRows(const std::string& table_name,
                        std::vector<DataRow>& out_rows) override;
    int ImportTableRows(const std::string& table_name,
                        const std::vector<DataRow>& rows,
                        const std::vector<std::string>& unique_keys,
                        ImportModeType mode,
                        TableImportStats& out_stats) override;
    int ClearBusinessTables() override;
    int GetTableColumnNames(const std::string& table_name,
                            std::vector<std::string>& out_columns) override;

private:
    int CreateTables();
    void MigrateSchema();
    int InitClassTypes();

    /* Internal helpers (caller holds db_mutex_) — used by atomic operations */
    int CommitTransactionInternal();
    int RollbackTransactionInternal();

    /* Internal helpers (caller holds db_mutex_) — used by atomic operations */
    int QueryClassByIdInternal(int32_t id, ClassInfo& info);
    double QueryEnrollmentUsedInternal(int32_t class_id);
    int InsertRegistrationInternal(const RegistrationInfo& info);
    int IncrementEnrollmentUsedInternal(int32_t class_id, double delta);
    int DecrementEnrollmentUsedInternal(int32_t class_id, double delta);
    int UpdateRegistrationClassIdInternal(int32_t registration_id, int32_t new_class_id);
    int UpdateStudentBasicInfoInternal(const RegistrationInfo& info);
    int IncrementResourceUsedInternal(int32_t resource_id);
    int DecrementResourceUsedInternal(int32_t resource_id);
    int IncrementBedReservedInternal(int32_t resource_id);
    int DecrementBedReservedInternal(int32_t resource_id);
    int QueryBedResourceRemainInternal(int32_t resource_id);
    int InsertAllocationInternal(const ResourceAllocation& alloc);
    int DeleteAllocationsByRegIdInternal(int32_t registration_id, std::vector<int32_t>& resource_ids);
    int CheckResourceCodeOccupiedInternal(int32_t resource_id, int32_t resource_code);

    /* Registration delete internal helpers (caller holds db_mutex_) */
    int DeleteAttendanceByRegIdInternal(int32_t registration_id);
    int DeleteRefundsByRegIdInternal(int32_t registration_id);
    int DeleteRegistrationInternal(int32_t registration_id);
    int QueryRegistrationForDeleteInternal(int32_t registration_id, int32_t& class_id, int32_t& need_bed, std::string& student_start_date, std::string& student_end_date);

    /* Price preset internal helpers (caller holds db_mutex_) */
    int InsertPricePresetInternal(const PricePresetInfo& info);
    int QueryPricePresetByIdInternal(int32_t id, PricePresetInfo& info);
    int CountClassPriceByPresetIdInternal(int32_t preset_id);
    int CountRegistrationByPresetIdInternal(int32_t preset_id);
    int CountPresetQrcodeInternal(int32_t preset_id);

    /* Refund internal helpers (caller holds db_mutex_) */
    int CreateRefundTableInternal();
    int InsertRefundInternal(const RefundRecordInfo& info);
    int QueryActiveRefundSumByRegIdInternal(int32_t registration_id, double& sum);
    int QueryLatestActiveRefundInternal(int32_t registration_id, RefundRecordInfo& info);

    /* Supplement (deposit -> full) internal helpers (caller holds db_mutex_) */
    int QueryRegistrationForUpdateInternal(int32_t registration_id, RegistrationInfo& info);
    int UpdateRegistrationSupplementInternal(int32_t registration_id, int32_t target_class_price_id,
                                             double target_amount, double supplement_amount,
                                             int32_t target_preset_id,
                                             const std::string& operator_name,
                                             const std::string& operate_time);

    /* Renewal internal helpers (caller holds db_mutex_) */
    int InsertRenewalInternal(const RenewalRecordInfo& info);
    int QueryRegistrationPeriodForRenewInternal(int32_t registration_id, int32_t& class_id,
                                                std::string& student_end_date);

    sqlite3* db_;
    std::mutex db_mutex_;
};

#endif /* __SQLITE_DATABASE_H__ */
