#include "group_session_manager.h"
#include "plog/Log.h"

#ifdef _WIN32
#include "utils.h"
#endif

#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <direct.h>
#include <fcntl.h>
#else
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#endif

GroupSessionManager::GroupSessionManager() {}

GroupSessionManager::~GroupSessionManager() {}

int GroupSessionManager::Init(const std::string& base_path) {
    base_path_ = base_path;
    root_dir_ = base_path + "/group_sessions";
    int ret = EnsureDirectory(root_dir_);
    if (ret != DB_OK) {
        LOG_ERROR << "GroupSessionManager: failed to create root dir: " << root_dir_;
        return ret;
    }
    LOG_INFO << "GroupSessionManager: initialized, root=" << root_dir_;
    return DB_OK;
}

std::string GroupSessionManager::GetActivityDir(int64_t activity_id) const {
    return root_dir_ + "/" + std::to_string(activity_id);
}

int GroupSessionManager::FindSessionFile(int64_t activity_id,
                                         const std::string& invite_code,
                                         std::string& out_path) const {
    std::string dir = GetActivityDir(activity_id);
    std::vector<std::string> files;
    int ret = ListJsonFiles(dir, files);
    if (ret != DB_OK) {
        return ret;
    }
    std::string prefix = invite_code + "_";
    for (size_t i = 0; i < files.size(); ++i) {
        std::string filename = files[i];
        if (filename.compare(0, prefix.size(), prefix) == 0) {
            out_path = dir + "/" + filename;
            return DB_OK;
        }
    }
    return ERR_ACTIVITY_GROUP_SESSION_EXPIRED;
}

int GroupSessionManager::CreateSession(const GroupSessionInfo& info) {
    std::string dir = GetActivityDir(info.activity_id);
    int ret = EnsureDirectory(dir);
    if (ret != DB_OK) {
        LOG_ERROR << "GroupSessionManager: failed to create activity dir: " << dir;
        return ret;
    }

    time_t now = std::time(nullptr);
    std::string filename = info.invite_code + "_" + std::to_string(now) + ".json";
    std::string path = dir + "/" + filename;

    ret = AtomicWriteFile(path, info);
    if (ret != DB_OK) {
        LOG_ERROR << "GroupSessionManager: failed to create session file: " << path;
        return ret;
    }

    LOG_INFO << "GroupSessionManager: session created, activity_id=" << info.activity_id;
    return DB_OK;
}

int GroupSessionManager::GetSession(int64_t activity_id,
                                    const std::string& invite_code,
                                    GroupSessionInfo& out_info) {
    std::string path;
    int ret = FindSessionFile(activity_id, invite_code, path);
    if (ret != DB_OK) {
        return ret;
    }

    std::ifstream ifs(path.c_str());
    if (!ifs.is_open()) {
        return ERR_ACTIVITY_GROUP_SESSION_EXPIRED;
    }
    std::stringstream ss;
    ss << ifs.rdbuf();
    ifs.close();

    return DeserializeSession(ss.str(), out_info);
}

int GroupSessionManager::UpdateSession(const GroupSessionInfo& info) {
    std::string path;
    int ret = FindSessionFile(info.activity_id, info.invite_code, path);
    if (ret != DB_OK) {
        return ret;
    }
    return AtomicWriteFile(path, info);
}

int GroupSessionManager::DeleteSession(int64_t activity_id,
                                       const std::string& invite_code) {
    std::string path;
    int ret = FindSessionFile(activity_id, invite_code, path);
    if (ret != DB_OK) {
        return ret;
    }
    if (std::remove(path.c_str()) != 0) {
        LOG_ERROR << "GroupSessionManager: failed to delete session file: " << path;
        return ERR_DB_EXEC_FAILED;
    }
    LOG_INFO << "GroupSessionManager: session deleted, activity_id=" << activity_id;
    return DB_OK;
}

int GroupSessionManager::AddMember(int64_t activity_id,
                                   const std::string& invite_code,
                                   const ActivityGroupMemberInfo& member,
                                   GroupSessionInfo& out_updated) {
    std::string path;
    int ret = FindSessionFile(activity_id, invite_code, path);
    if (ret != DB_OK) {
        return ret;
    }

#ifdef _WIN32
    HANDLE hFile = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                               0, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return ERR_DB_EXEC_FAILED;
    }
    int fd = _open_osfhandle(reinterpret_cast<intptr_t>(hFile), 0);
    if (fd == -1) {
        CloseHandle(hFile);
        return ERR_DB_EXEC_FAILED;
    }
#else
    int fd = ::open(path.c_str(), O_RDWR);
    if (fd < 0) {
        return ERR_DB_EXEC_FAILED;
    }
#endif

    ret = LockFile(fd);
    if (ret != DB_OK) {
#ifdef _WIN32
        _close(fd);
#else
        ::close(fd);
#endif
        return ret;
    }

    /* read current content */
    std::ifstream ifs(path.c_str());
    std::stringstream ss;
    ss << ifs.rdbuf();
    ifs.close();

    GroupSessionInfo current;
    ret = DeserializeSession(ss.str(), current);
    if (ret != DB_OK) {
        UnlockFile(fd);
#ifdef _WIN32
        _close(fd);
#else
        ::close(fd);
#endif
        return ret;
    }

    current.members.push_back(member);
    current.current_count = static_cast<int32_t>(current.members.size());

    ret = AtomicWriteFile(path, current);
    UnlockFile(fd);
#ifdef _WIN32
    _close(fd);
#else
    ::close(fd);
#endif

    if (ret != DB_OK) {
        return ret;
    }

    out_updated = current;
    return DB_OK;
}

int GroupSessionManager::FindMemberInActivity(int64_t activity_id,
                                              const std::string& name,
                                              const std::string& phone,
                                              GroupSessionInfo& out_info,
                                              bool& out_found) {
    out_found = false;
    std::string dir = GetActivityDir(activity_id);
    std::vector<std::string> files;
    int ret = ListJsonFiles(dir, files);
    if (ret != DB_OK) {
        return DB_OK; /* directory may not exist yet */
    }

    for (size_t i = 0; i < files.size(); ++i) {
        std::string path = dir + "/" + files[i];
        std::ifstream ifs(path.c_str());
        if (!ifs.is_open()) {
            continue;
        }
        std::stringstream ss;
        ss << ifs.rdbuf();
        ifs.close();

        GroupSessionInfo session;
        if (DeserializeSession(ss.str(), session) != DB_OK) {
            continue;
        }

        for (size_t j = 0; j < session.members.size(); ++j) {
            if (session.members[j].name == name &&
                session.members[j].phone == phone) {
                out_info = session;
                out_found = true;
                return DB_OK;
            }
        }
    }

    return DB_OK;
}

int GroupSessionManager::ListAllSessions(int64_t activity_id,
                                         std::vector<GroupSessionInfo>& out_list) {
    std::string dir = GetActivityDir(activity_id);
    std::vector<std::string> files;
    int ret = ListJsonFiles(dir, files);
    if (ret != DB_OK) {
        return DB_OK;
    }

    for (size_t i = 0; i < files.size(); ++i) {
        std::string path = dir + "/" + files[i];
        std::ifstream ifs(path.c_str());
        if (!ifs.is_open()) {
            continue;
        }
        std::stringstream ss;
        ss << ifs.rdbuf();
        ifs.close();

        GroupSessionInfo session;
        if (DeserializeSession(ss.str(), session) == DB_OK) {
            out_list.push_back(session);
        }
    }

    return DB_OK;
}

int GroupSessionManager::CleanupActivity(int64_t activity_id) {
    std::string dir = GetActivityDir(activity_id);
    int ret = RemoveDirectoryRecursive(dir);
    if (ret != DB_OK) {
        LOG_WARNING << "GroupSessionManager: cleanup activity dir failed, dir=" << dir;
        return ret;
    }
    LOG_INFO << "GroupSessionManager: activity dir cleaned, activity_id=" << activity_id;
    return DB_OK;
}

int GroupSessionManager::CleanupOrphans(int64_t activity_id,
                                        std::function<bool(const std::string&, const std::string&)> check_fn) {
    std::vector<GroupSessionInfo> sessions;
    int ret = ListAllSessions(activity_id, sessions);
    if (ret != DB_OK) {
        return ret;
    }

    int cleaned = 0;
    for (size_t i = 0; i < sessions.size(); ++i) {
        bool all_signed_up = true;
        for (size_t j = 0; j < sessions[i].members.size(); ++j) {
            if (!check_fn(sessions[i].members[j].name, sessions[i].members[j].phone)) {
                all_signed_up = false;
                break;
            }
        }
        if (all_signed_up && !sessions[i].members.empty()) {
            ret = DeleteSession(activity_id, sessions[i].invite_code);
            if (ret == DB_OK) {
                ++cleaned;
            }
        }
    }

    if (cleaned > 0) {
        LOG_INFO << "GroupSessionManager: cleaned " << cleaned
                 << " orphan sessions for activity_id=" << activity_id;
    }
    return DB_OK;
}

/* ====== Private helpers ====== */

int GroupSessionManager::LockFile(int fd) {
#ifdef _WIN32
    HANDLE hFile = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    OVERLAPPED ov;
    std::memset(&ov, 0, sizeof(ov));
    if (!LockFileEx(hFile, TRUE, FALSE, MAXDWORD, MAXDWORD, &ov)) {
        LOG_ERROR << "GroupSessionManager: LockFileEx failed";
        return ERR_DB_EXEC_FAILED;
    }
#else
    if (flock(fd, LOCK_EX) != 0) {
        LOG_ERROR << "GroupSessionManager: flock LOCK_EX failed";
        return ERR_DB_EXEC_FAILED;
    }
#endif
    return DB_OK;
}

int GroupSessionManager::UnlockFile(int fd) {
#ifdef _WIN32
    HANDLE hFile = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    OVERLAPPED ov;
    std::memset(&ov, 0, sizeof(ov));
    UnlockFileEx(hFile, 0, MAXDWORD, MAXDWORD, &ov);
#else
    flock(fd, LOCK_UN);
#endif
    return DB_OK;
}

int GroupSessionManager::AtomicWriteFile(const std::string& path,
                                         const GroupSessionInfo& info) {
    std::string tmp_path = path + ".tmp";
    std::string content = SerializeSession(info);

    std::ofstream ofs(tmp_path.c_str(), std::ios::trunc);
    if (!ofs.is_open()) {
        LOG_ERROR << "GroupSessionManager: failed to open tmp file: " << tmp_path;
        return ERR_DB_EXEC_FAILED;
    }
    ofs << content;
    ofs.close();

    if (std::rename(tmp_path.c_str(), path.c_str()) != 0) {
        LOG_ERROR << "GroupSessionManager: rename failed: " << tmp_path << " -> " << path;
        std::remove(tmp_path.c_str());
        return ERR_DB_EXEC_FAILED;
    }

    return DB_OK;
}

int GroupSessionManager::EnsureDirectory(const std::string& path) const {
#ifdef _WIN32
    std::wstring wpath = register_student::Utf8ToWide(path);
    if (_wmkdir(wpath.c_str()) == 0 || errno == EEXIST) {
        return DB_OK;
    }
    /* try parent */
    size_t pos = path.find_last_of("/\\");
    if (pos != std::string::npos && pos > 0) {
        int ret = EnsureDirectory(path.substr(0, pos));
        if (ret != DB_OK) { return ret; }
        if (_wmkdir(wpath.c_str()) == 0 || errno == EEXIST) {
            return DB_OK;
        }
    }
    return ERR_DB_EXEC_FAILED;
#else
    if (mkdir(path.c_str(), 0700) == 0 || errno == EEXIST) {
        return DB_OK;
    }
    /* try parent */
    size_t pos = path.find_last_of('/');
    if (pos != std::string::npos && pos > 0) {
        int ret = EnsureDirectory(path.substr(0, pos));
        if (ret != DB_OK) { return ret; }
        if (mkdir(path.c_str(), 0700) == 0 || errno == EEXIST) {
            return DB_OK;
        }
    }
    return ERR_DB_EXEC_FAILED;
#endif
}

int GroupSessionManager::RemoveDirectoryRecursive(const std::string& path) const {
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    std::string search = path + "\\*";
    HANDLE hFind = FindFirstFileA(search.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        return DB_OK; /* directory does not exist */
    }
    do {
        std::string name = fd.cFileName;
        if (name == "." || name == "..") { continue; }
        std::string full = path + "\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            RemoveDirectoryRecursive(full);
        } else {
            std::remove(full.c_str());
        }
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
    _rmdir(path.c_str());
#else
    DIR* dir = opendir(path.c_str());
    if (!dir) {
        return DB_OK; /* directory does not exist */
    }
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") { continue; }
        std::string full = path + "/" + name;
        struct stat st;
        if (stat(full.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                RemoveDirectoryRecursive(full);
            } else {
                std::remove(full.c_str());
            }
        }
    }
    closedir(dir);
    rmdir(path.c_str());
#endif
    return DB_OK;
}

int GroupSessionManager::ListJsonFiles(const std::string& dir,
                                       std::vector<std::string>& out_files) const {
    out_files.clear();
#ifdef _WIN32
    std::string search = dir + "\\*.json";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        return DB_OK;
    }
    do {
        std::string name = fd.cFileName;
        if (name != "." && name != "..") {
            out_files.push_back(name);
        }
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
#else
    DIR* d = opendir(dir.c_str());
    if (!d) {
        return DB_OK; /* directory does not exist */
    }
    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
        std::string name = entry->d_name;
        if (name.size() > 5 && name.compare(name.size() - 5, 5, ".json") == 0) {
            out_files.push_back(name);
        }
    }
    closedir(d);
#endif
    std::sort(out_files.begin(), out_files.end());
    return DB_OK;
}

/* ====== JSON serialization ====== */

std::string GroupSessionManager::JsonEscape(const std::string& s) const {
    std::string out;
    out.reserve(s.size() + 8);
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c; break;
        }
    }
    return out;
}

std::string GroupSessionManager::JsonUnescape(const std::string& s) const {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char next = s[i + 1];
            switch (next) {
                case '"':  out += '"'; ++i; break;
                case '\\': out += '\\'; ++i; break;
                case 'n':  out += '\n'; ++i; break;
                case 'r':  out += '\r'; ++i; break;
                case 't':  out += '\t'; ++i; break;
                default:   out += s[i]; break;
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

std::string GroupSessionManager::SerializeSession(const GroupSessionInfo& info) const {
    std::ostringstream ss;
    ss << "{\n";
    ss << "\"invite_code\":\"" << JsonEscape(info.invite_code) << "\",\n";
    ss << "\"activity_id\":" << info.activity_id << ",\n";
    ss << "\"leader_name\":\"" << JsonEscape(info.leader_name) << "\",\n";
    ss << "\"leader_phone\":\"" << JsonEscape(info.leader_phone) << "\",\n";
    ss << "\"leader_grade\":\"" << JsonEscape(info.leader_grade) << "\",\n";
    ss << "\"target_count\":" << info.target_count << ",\n";
    ss << "\"current_count\":" << info.current_count << ",\n";
    ss << "\"created_at\":\"" << JsonEscape(info.created_at) << "\",\n";
    ss << "\"members\":[\n";
    for (size_t i = 0; i < info.members.size(); ++i) {
        const ActivityGroupMemberInfo& m = info.members[i];
        ss << "{\"name\":\"" << JsonEscape(m.name) << "\""
           << ",\"phone\":\"" << JsonEscape(m.phone) << "\""
           << ",\"grade\":\"" << JsonEscape(m.grade) << "\""
           << ",\"signup_type\":\"" << JsonEscape(m.signup_type) << "\""
           << ",\"is_leader\":" << m.is_leader
           << ",\"created_at\":\"" << JsonEscape(m.created_at) << "\"}";
        if (i + 1 < info.members.size()) { ss << ","; }
        ss << "\n";
    }
    ss << "]\n}\n";
    return ss.str();
}

/* ====== Simple JSON parser helpers ====== */

namespace {

/* skip whitespace */
size_t SkipWs(const std::string& s, size_t pos) {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' ||
                               s[pos] == '\n' || s[pos] == '\r')) {
        ++pos;
    }
    return pos;
}

/* parse a JSON string value starting at pos (which should point to opening ") */
std::string ParseJsonString(const std::string& s, size_t& pos) {
    if (pos >= s.size() || s[pos] != '"') { return ""; }
    ++pos; /* skip opening " */
    std::string result;
    while (pos < s.size() && s[pos] != '"') {
        if (s[pos] == '\\' && pos + 1 < s.size()) {
            char next = s[pos + 1];
            switch (next) {
                case '"':  result += '"'; break;
                case '\\': result += '\\'; break;
                case 'n':  result += '\n'; break;
                case 'r':  result += '\r'; break;
                case 't':  result += '\t'; break;
                default:   result += s[pos]; result += next; break;
            }
            pos += 2;
        } else {
            result += s[pos];
            ++pos;
        }
    }
    if (pos < s.size()) { ++pos; } /* skip closing " */
    return result;
}

/* parse a JSON integer value starting at pos */
int64_t ParseJsonInt(const std::string& s, size_t& pos) {
    size_t start = pos;
    if (pos < s.size() && s[pos] == '-') { ++pos; }
    while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') { ++pos; }
    return std::atoll(s.substr(start, pos - start).c_str());
}

/* skip a JSON value (string, number, object, array) */
void SkipJsonValue(const std::string& s, size_t& pos);

void SkipJsonObject(const std::string& s, size_t& pos) {
    if (pos >= s.size() || s[pos] != '{') { return; }
    ++pos;
    pos = SkipWs(s, pos);
    if (pos < s.size() && s[pos] == '}') { ++pos; return; }
    while (pos < s.size()) {
        /* skip key */
        ParseJsonString(s, pos);
        pos = SkipWs(s, pos);
        if (pos < s.size() && s[pos] == ':') { ++pos; }
        pos = SkipWs(s, pos);
        SkipJsonValue(s, pos);
        pos = SkipWs(s, pos);
        if (pos < s.size() && s[pos] == ',') { ++pos; pos = SkipWs(s, pos); }
        else { break; }
    }
    if (pos < s.size() && s[pos] == '}') { ++pos; }
}

void SkipJsonArray(const std::string& s, size_t& pos) {
    if (pos >= s.size() || s[pos] != '[') { return; }
    ++pos;
    pos = SkipWs(s, pos);
    if (pos < s.size() && s[pos] == ']') { ++pos; return; }
    while (pos < s.size()) {
        SkipJsonValue(s, pos);
        pos = SkipWs(s, pos);
        if (pos < s.size() && s[pos] == ',') { ++pos; pos = SkipWs(s, pos); }
        else { break; }
    }
    if (pos < s.size() && s[pos] == ']') { ++pos; }
}

void SkipJsonValue(const std::string& s, size_t& pos) {
    pos = SkipWs(s, pos);
    if (pos >= s.size()) { return; }
    if (s[pos] == '"') { ParseJsonString(s, pos); }
    else if (s[pos] == '{') { SkipJsonObject(s, pos); }
    else if (s[pos] == '[') { SkipJsonArray(s, pos); }
    else {
        /* number, bool, null */
        while (pos < s.size() && s[pos] != ',' && s[pos] != '}' &&
               s[pos] != ']' && s[pos] != ' ' && s[pos] != '\n' &&
               s[pos] != '\r' && s[pos] != '\t') {
            ++pos;
        }
    }
}

} /* anonymous namespace */

int GroupSessionManager::DeserializeSession(const std::string& json,
                                            GroupSessionInfo& out_info) const {
    GroupSessionInfo info;
    size_t pos = SkipWs(json, 0);
    if (pos >= json.size() || json[pos] != '{') {
        return ERR_DB_EXEC_FAILED;
    }
    ++pos;
    pos = SkipWs(json, pos);

    while (pos < json.size() && json[pos] != '}') {
        std::string key = ParseJsonString(json, pos);
        pos = SkipWs(json, pos);
        if (pos < json.size() && json[pos] == ':') { ++pos; }
        pos = SkipWs(json, pos);

        if (key == "invite_code") {
            info.invite_code = ParseJsonString(json, pos);
        } else if (key == "activity_id") {
            info.activity_id = ParseJsonInt(json, pos);
        } else if (key == "leader_name") {
            info.leader_name = ParseJsonString(json, pos);
        } else if (key == "leader_phone") {
            info.leader_phone = ParseJsonString(json, pos);
        } else if (key == "leader_grade") {
            info.leader_grade = ParseJsonString(json, pos);
        } else if (key == "target_count") {
            info.target_count = static_cast<int32_t>(ParseJsonInt(json, pos));
        } else if (key == "current_count") {
            info.current_count = static_cast<int32_t>(ParseJsonInt(json, pos));
        } else if (key == "created_at") {
            info.created_at = ParseJsonString(json, pos);
        } else if (key == "members") {
            /* parse array of member objects */
            if (pos < json.size() && json[pos] == '[') {
                ++pos;
                pos = SkipWs(json, pos);
                while (pos < json.size() && json[pos] != ']') {
                    if (json[pos] == '{') {
                        ActivityGroupMemberInfo m;
                        ++pos;
                        pos = SkipWs(json, pos);
                        while (pos < json.size() && json[pos] != '}') {
                            std::string mk = ParseJsonString(json, pos);
                            pos = SkipWs(json, pos);
                            if (pos < json.size() && json[pos] == ':') { ++pos; }
                            pos = SkipWs(json, pos);
                            if (mk == "name") { m.name = ParseJsonString(json, pos); }
                            else if (mk == "phone") { m.phone = ParseJsonString(json, pos); }
                            else if (mk == "grade") { m.grade = ParseJsonString(json, pos); }
                            else if (mk == "signup_type") { m.signup_type = ParseJsonString(json, pos); }
                            else if (mk == "is_leader") { m.is_leader = static_cast<int32_t>(ParseJsonInt(json, pos)); }
                            else if (mk == "created_at") { m.created_at = ParseJsonString(json, pos); }
                            else { SkipJsonValue(json, pos); }
                            pos = SkipWs(json, pos);
                            if (pos < json.size() && json[pos] == ',') { ++pos; pos = SkipWs(json, pos); }
                            else { break; }
                        }
                        if (pos < json.size() && json[pos] == '}') { ++pos; }
                        info.members.push_back(m);
                    } else {
                        SkipJsonValue(json, pos);
                    }
                    pos = SkipWs(json, pos);
                    if (pos < json.size() && json[pos] == ',') { ++pos; pos = SkipWs(json, pos); }
                    else { break; }
                }
                if (pos < json.size() && json[pos] == ']') { ++pos; }
            }
        } else {
            SkipJsonValue(json, pos);
        }

        pos = SkipWs(json, pos);
        if (pos < json.size() && json[pos] == ',') { ++pos; pos = SkipWs(json, pos); }
        else { break; }
    }

    out_info = info;
    return DB_OK;
}
