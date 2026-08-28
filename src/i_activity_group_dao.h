#ifndef __I_ACTIVITY_GROUP_DAO_H__
#define __I_ACTIVITY_GROUP_DAO_H__

#include "activity_types.h"
#include <vector>

class IActivityGroupDao {
public:
    virtual ~IActivityGroupDao() {}

    /* @brief Create a group signup record */
    virtual int CreateGroup(const ActivityGroupInfo& info, int64_t& out_id) = 0;

    /* @brief Get group by id */
    virtual int GetGroup(int64_t group_id, ActivityGroupInfo& info) = 0;

    /* @brief Find a waiting group by invite code (case-insensitive) */
    virtual int GetGroupByInviteCode(const std::string& invite_code,
                                     ActivityGroupInfo& info) = 0;

    /* @brief Add a member to a group */
    virtual int AddMember(const ActivityGroupMemberInfo& member,
                          int64_t& out_id) = 0;

    /* @brief Remove a member from a group
     * @param out_is_leader whether the removed member was the leader
     * @param out_remaining_count remaining member count after removal */
    virtual int RemoveMember(int64_t group_id, const std::string& name,
                             const std::string& phone,
                             int32_t& out_is_leader,
                             int32_t& out_remaining_count) = 0;

    /* @brief Update group current_count by delta (+ or -) */
    virtual int UpdateGroupCount(int64_t group_id, int32_t delta) = 0;

    /* @brief Update group status and cancel reason */
    virtual int UpdateGroupStatus(int64_t group_id, int32_t status,
                                  int32_t cancel_reason) = 0;

    /* @brief List all members of a group, leader first */
    virtual int ListMembersByGroup(int64_t group_id,
                                   std::vector<ActivityGroupMemberInfo>& list) = 0;

    /* @brief List members of all confirmed groups for an activity (admin use) */
    virtual int ListMembersByActivity(int64_t activity_id,
                                      std::vector<ActivityGroupMemberInfo>& list) = 0;

    /* @brief Check if a duplicate member exists in a group (name+phone+grade) */
    virtual int CheckDuplicateInGroup(int64_t group_id, const std::string& name,
                                      const std::string& phone,
                                      const std::string& grade,
                                      bool& out_duplicate) = 0;

    /* @brief Atomically confirm a group: insert all members as signups,
     *        increment activity signup_count, update group status.
     * @param activity_id the activity to sign up for
     * @param group_id the group to confirm
     * @param members all group members to insert as signups
     * @return DB_OK on success, ERR_ACTIVITY_CAPACITY_FULL / ERR_ACTIVITY_DUPLICATE_SIGNUP on failure */
    virtual int ConfirmGroupAtomic(int64_t activity_id, int64_t group_id,
                                   const std::vector<ActivityGroupMemberInfo>& members) = 0;
};

#endif /* __I_ACTIVITY_GROUP_DAO_H__ */
