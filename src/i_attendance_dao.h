#ifndef __I_ATTENDANCE_DAO_H__
#define __I_ATTENDANCE_DAO_H__

#include "attendance_types.h"
#include <vector>

class IAttendanceDao {
public:
    virtual ~IAttendanceDao() {}

    virtual int InsertAttendance(const AttendanceRecord& record) = 0;
    virtual int QueryAttendanceByClassAndDate(int32_t class_id, const std::string& date, std::vector<AttendanceRecord>& records) = 0;
    virtual int CheckAttendanceExists(int32_t class_id, const std::string& date) = 0;
    virtual int QueryAttendanceByClassAndDateRange(int32_t class_id, const std::string& start_date, const std::string& end_date, std::vector<AttendanceRecord>& records) = 0;

    /**
     * @brief 按 registration_id 查询该学生全部考勤记录
     * @param registration_id 报名记录 ID
     * @param records 输出：考勤记录列表（按日期升序）
     * @return DB_OK=成功, ERR_DB_*=失败
     * @note   用于退费考勤折损计算（避免拿全班级再过滤）
     */
    virtual int QueryAttendanceByRegId(int32_t registration_id,
                                       std::vector<AttendanceRecord>& records) = 0;
};

#endif /* __I_ATTENDANCE_DAO_H__ */
