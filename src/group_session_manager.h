#ifndef __GROUP_SESSION_MANAGER_H__
#define __GROUP_SESSION_MANAGER_H__

#include "activity_types.h"
#include "error_codes.h"
#include <string>
#include <vector>
#include <functional>

class GroupSessionManager {
public:
    GroupSessionManager();
    ~GroupSessionManager();

    /* @brief Initialize root directory, create group_sessions/ if not exists */
    int Init(const std::string& base_path);

    /* @brief Create a session file (auto-creates activity subdirectory) */
    int CreateSession(const GroupSessionInfo& info);

    /* @brief Read a session file by invite_code */
    int GetSession(int64_t activity_id, const std::string& invite_code,
                   GroupSessionInfo& out_info);

    /* @brief Update a session file (atomic write: tmp + rename) */
    int UpdateSession(const GroupSessionInfo& info);

    /* @brief Delete a session file */
    int DeleteSession(int64_t activity_id, const std::string& invite_code);

    /* @brief Add a member to a session (lock + read + append + atomic write) */
    int AddMember(int64_t activity_id, const std::string& invite_code,
                  const ActivityGroupMemberInfo& member,
                  GroupSessionInfo& out_updated);

    /* @brief Scan activity directory for a member by name+phone */
    int FindMemberInActivity(int64_t activity_id, const std::string& name,
                             const std::string& phone,
                             GroupSessionInfo& out_info, bool& out_found);

    /* @brief List all sessions under an activity */
    int ListAllSessions(int64_t activity_id,
                        std::vector<GroupSessionInfo>& out_list);

    /* @brief Delete entire activity subdirectory */
    int CleanupActivity(int64_t activity_id);

    /* @brief Cleanup orphan session files where all members already signed up */
    int CleanupOrphans(int64_t activity_id,
                       std::function<bool(const std::string&, const std::string&)> check_fn);

private:
    std::string base_path_;
    std::string root_dir_;

    std::string GetActivityDir(int64_t activity_id) const;
    int FindSessionFile(int64_t activity_id, const std::string& invite_code,
                        std::string& out_path) const;
    int LockFile(int fd);
    int UnlockFile(int fd);
    int AtomicWriteFile(const std::string& path, const GroupSessionInfo& info);
    std::string SerializeSession(const GroupSessionInfo& info) const;
    int DeserializeSession(const std::string& json, GroupSessionInfo& out_info) const;
    int EnsureDirectory(const std::string& path) const;
    int RemoveDirectoryRecursive(const std::string& path) const;
    int ListJsonFiles(const std::string& dir,
                      std::vector<std::string>& out_files) const;

    /* @brief Simple JSON string escape */
    std::string JsonEscape(const std::string& s) const;
    /* @brief Simple JSON string unescape */
    std::string JsonUnescape(const std::string& s) const;
};

#endif /* __GROUP_SESSION_MANAGER_H__ */
