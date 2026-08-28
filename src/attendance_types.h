#ifndef __ATTENDANCE_TYPES_H__
#define __ATTENDANCE_TYPES_H__

#include <string>
#include <cstdint>

enum AttendanceStatusType {
    AttendanceStatus_Present    = 0,
    AttendanceStatus_Absent     = 1,
    AttendanceStatus_EarlyLeave = 2,  /* 早退第三状态，leave_time 必填 HH:MM */
    AttendanceStatus_Late       = 3  /* 迟到第四状态，leave_time 必填 HH:MM（迟到时间，打印时前缀 "+"） */
};

struct AttendanceRecord {
    int32_t id;
    int32_t class_id;
    int32_t registration_id;
    std::string student_name;
    std::string student_gender;
    std::string attendance_date;  /* YYYY-MM-DD */
    AttendanceStatusType status;
    std::string leave_time;  /* HH:MM 格式，status 为 EarlyLeave 或 Late 时必填，其他状态为空 */
    std::string teacher_name;
    std::string record_time;
};

#endif /* __ATTENDANCE_TYPES_H__ */
