#ifndef __I_ACTIVITY_SIGNUP_DAO_H__
#define __I_ACTIVITY_SIGNUP_DAO_H__

#include "activity_types.h"
#include <vector>

class IActivitySignupDao {
public:
    virtual ~IActivitySignupDao() {}

    /**
     * @brief Atomic signup: check duplicate + INSERT + increment signup_count
     * @param info Signup info (id ignored, activity_id/name/phone required)
     * @param out_id Output: new signup record ID
     * @return DB_OK on success, ERR_ACTIVITY_DUPLICATE_SIGNUP / ERR_ACTIVITY_CAPACITY_FULL / etc.
     */
    virtual int CreateSignupAtomic(const ActivitySignupInfo& info,
                                   int64_t& out_id) = 0;

    /**
     * @brief List all signups for an activity, ordered by created_at DESC
     */
    virtual int ListSignupsByActivity(int64_t activity_id,
                                      std::vector<ActivitySignupInfo>& list) = 0;

    /**
     * @brief Atomic batch write of group session members to activity_signup + update signup_count
     * @param activity_id Activity ID
     * @param members Member list (name/phone/grade/signup_type required)
     * @return DB_OK on success, ERR_ACTIVITY_CAPACITY_FULL if capacity insufficient
     */
    virtual int ConfirmSessionAtomic(int64_t activity_id,
                                     const std::vector<ActivitySignupInfo>& members) = 0;

    /**
     * @brief Check if name+phone already exists in activity_signup
     * @param activity_id Activity ID
     * @param name Student name
     * @param phone Phone number
     * @param out_exists Output: true=already signed up, false=not found
     * @return DB_OK on success
     */
    virtual int CheckDuplicateSignup(int64_t activity_id,
                                     const std::string& name,
                                     const std::string& phone,
                                     bool& out_exists) = 0;
};

#endif /* __I_ACTIVITY_SIGNUP_DAO_H__ */
