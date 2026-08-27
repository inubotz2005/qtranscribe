#include "launcher_auth.h"

#include "logging.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstring>
#include <filesystem>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>

namespace keyinjectord {

const char* authResultToString(AuthResult result) {
    switch (result) {
        case AuthResult::Success:
            return "Success";
        case AuthResult::InvalidFd:
            return "Invalid file descriptor";
        case AuthResult::NotASocket:
            return "File descriptor is not a SOCK_STREAM socket";
        case AuthResult::NotConnectedUnixSocket:
            return "File descriptor is not a connected AF_UNIX socket";
        case AuthResult::PeerCredsFailed:
            return "Failed to retrieve SO_PEERCRED from socket";
        case AuthResult::UidMismatch:
            return "Peer UID does not match current process UID";
        case AuthResult::PidMismatch:
            return "Peer PID does not match parent process PID";
        case AuthResult::ParentExeReadFailed:
            return "Failed to resolve parent executable path from /proc";
        case AuthResult::SelfExeReadFailed:
            return "Failed to resolve self executable path from /proc";
        case AuthResult::UnauthorizedExecutable:
            return "Parent executable name is not authorized";
        case AuthResult::StatFailed:
            return "Failed to stat parent executable binary or directory";
        case AuthResult::NotRegularFile:
            return "Parent executable is not a regular file";
        case AuthResult::WorldWritable:
            return "Parent executable or ancestor directory is world-writable";
        case AuthResult::GroupWritable:
            return "Parent executable or ancestor directory is group-writable";
        case AuthResult::NonRootOwner:
            return "Production helper executable, parent binary, or directory is not owned by root (UID 0)";
        case AuthResult::UntrustedLocation:
            return "Parent executable is located in an untrusted or non-colocated directory";
        case AuthResult::DeletedExecutable:
            return "Executable binary was unlinked or deleted from disk";
        case AuthResult::InodeMismatch:
            return "Process executable inode does not match filesystem path inode";
    }
    return "Unknown error";
}

bool validateDirectoryAncestry(const std::filesystem::path& rawPath, AuthResult* outResult, bool allowDevMode,
                               struct stat* outLeafStat, bool rejectGroupWritable) {
    auto setResult = [outResult](AuthResult res) {
        if (outResult) {
            *outResult = res;
        }
        return res == AuthResult::Success;
    };

    std::error_code ec;
    std::filesystem::path canon = std::filesystem::canonical(rawPath, ec);
    if (ec || canon.empty() || !canon.is_absolute()) {
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: canonicalize path '%s' error: %s", rawPath.c_str(),
                               ec.message().c_str());
        return setResult(AuthResult::StatFailed);
    }

    std::string canonStr = canon.string();
    static constexpr auto kUntrustedPrefixes =
        std::to_array<std::string_view>({"/tmp", "/var/tmp", "/dev/shm", "/run/user"});
    const std::string_view norm = canonStr;
    const bool isUntrusted = std::ranges::any_of(kUntrustedPrefixes, [&](std::string_view prefix) {
        return norm == prefix ||
               (norm.starts_with(prefix) && norm.size() > prefix.size() && norm[prefix.size()] == '/');
    });
    if (isUntrusted) {
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: path in untrusted directory '%s'", canonStr.c_str());
        return setResult(AuthResult::UntrustedLocation);
    }

    std::vector<std::string> parts;
    for (const auto& part : canon) {
        std::string s = part.string();
        if (s.empty() || s == "/") {
            continue;
        }
        parts.push_back(s);
    }

    int currentFd = open("/", O_PATH | O_DIRECTORY | O_CLOEXEC);
    if (currentFd < 0) {
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: open('/') error: %s", std::strerror(errno));
        return setResult(AuthResult::StatFailed);
    }

    struct stat st {};
    if (fstat(currentFd, &st) != 0) {
        close(currentFd);
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: fstat('/') error: %s", std::strerror(errno));
        return setResult(AuthResult::StatFailed);
    }

    if (!S_ISDIR(st.st_mode)) {
        close(currentFd);
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: '/' is not a directory");
        return setResult(AuthResult::StatFailed);
    }

    if (st.st_mode & S_IWOTH) {
        close(currentFd);
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: root directory '/' is world-writable");
        return setResult(AuthResult::WorldWritable);
    }

    if (rejectGroupWritable && (st.st_mode & S_IWGRP)) {
        close(currentFd);
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: root directory '/' is group-writable");
        return setResult(AuthResult::GroupWritable);
    }

    if (!allowDevMode && st.st_uid != 0) {
        close(currentFd);
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: root directory '/' is not root-owned (UID: %d)",
                               st.st_uid);
        return setResult(AuthResult::NonRootOwner);
    }

    if (allowDevMode && st.st_uid != 0 && st.st_uid != getuid()) {
        close(currentFd);
        KEYINJECTORD_LOG_ERROR("Dev launcher authorization failed: '/' owned by UID %d (expected 0 or %d)", st.st_uid,
                               getuid());
        return setResult(AuthResult::UntrustedLocation);
    }

    if (parts.empty()) {
        if (outLeafStat) {
            *outLeafStat = st;
        }
        close(currentFd);
        return setResult(AuthResult::Success);
    }

    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        int nextFd = openat(currentFd, parts[i].c_str(), O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (nextFd < 0) {
            close(currentFd);
            KEYINJECTORD_LOG_ERROR("Launcher authorization failed: openat directory component '%s' error: %s",
                                   parts[i].c_str(), std::strerror(errno));
            return setResult(AuthResult::StatFailed);
        }
        close(currentFd);
        currentFd = nextFd;

        if (fstat(currentFd, &st) != 0) {
            close(currentFd);
            KEYINJECTORD_LOG_ERROR("Launcher authorization failed: fstat component '%s' error: %s", parts[i].c_str(),
                                   std::strerror(errno));
            return setResult(AuthResult::StatFailed);
        }

        if (!S_ISDIR(st.st_mode)) {
            close(currentFd);
            KEYINJECTORD_LOG_ERROR("Launcher authorization failed: ancestor '%s' is not a directory", parts[i].c_str());
            return setResult(AuthResult::StatFailed);
        }

        if (st.st_mode & S_IWOTH) {
            close(currentFd);
            KEYINJECTORD_LOG_ERROR("Launcher authorization failed: ancestor directory '%s' is world-writable",
                                   parts[i].c_str());
            return setResult(AuthResult::WorldWritable);
        }

        if (rejectGroupWritable && (st.st_mode & S_IWGRP)) {
            close(currentFd);
            KEYINJECTORD_LOG_ERROR("Launcher authorization failed: ancestor directory '%s' is group-writable",
                                   parts[i].c_str());
            return setResult(AuthResult::GroupWritable);
        }

        if (!allowDevMode && st.st_uid != 0) {
            close(currentFd);
            KEYINJECTORD_LOG_ERROR("Launcher authorization failed: ancestor directory '%s' is not root-owned (UID: %d)",
                                   parts[i].c_str(), st.st_uid);
            return setResult(AuthResult::NonRootOwner);
        }

        if (allowDevMode && st.st_uid != 0 && st.st_uid != getuid()) {
            close(currentFd);
            KEYINJECTORD_LOG_ERROR(
                "Dev launcher authorization failed: ancestor directory '%s' owned by UID %d (expected 0 or %d)",
                parts[i].c_str(), st.st_uid, getuid());
            return setResult(AuthResult::UntrustedLocation);
        }
    }

    const std::string& leafName = parts.back();
    int leafFd = openat(currentFd, leafName.c_str(), O_PATH | O_NOFOLLOW | O_CLOEXEC);
    if (leafFd < 0) {
        close(currentFd);
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: openat leaf '%s' error: %s", leafName.c_str(),
                               std::strerror(errno));
        return setResult(AuthResult::StatFailed);
    }
    close(currentFd);

    if (fstat(leafFd, &st) != 0) {
        close(leafFd);
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: fstat leaf '%s' error: %s", leafName.c_str(),
                               std::strerror(errno));
        return setResult(AuthResult::StatFailed);
    }
    close(leafFd);

    if (outLeafStat) {
        *outLeafStat = st;
    }

    if (st.st_nlink == 0) {
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: leaf '%s' has 0 hard links (deleted)", leafName.c_str());
        return setResult(AuthResult::DeletedExecutable);
    }

    if (st.st_mode & S_IWOTH) {
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: leaf '%s' is world-writable", leafName.c_str());
        return setResult(AuthResult::WorldWritable);
    }

    if (rejectGroupWritable && (st.st_mode & S_IWGRP)) {
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: leaf '%s' is group-writable", leafName.c_str());
        return setResult(AuthResult::GroupWritable);
    }

    if (!allowDevMode && st.st_uid != 0) {
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: leaf '%s' is not root-owned (UID: %d)", leafName.c_str(),
                               st.st_uid);
        return setResult(AuthResult::NonRootOwner);
    }

    if (allowDevMode && st.st_uid != 0 && st.st_uid != getuid()) {
        KEYINJECTORD_LOG_ERROR("Dev launcher authorization failed: leaf '%s' owned by UID %d (expected 0 or %d)",
                               leafName.c_str(), st.st_uid, getuid());
        return setResult(AuthResult::UntrustedLocation);
    }

    return setResult(AuthResult::Success);
}

bool validateExecutableTopology(const std::filesystem::path& parentExePath, const std::filesystem::path& selfExePath,
                                AuthResult* outResult, bool allowDevMode, struct stat* outParentStat,
                                bool rejectGroupWritable) {
    auto setResult = [outResult](AuthResult res) {
        if (outResult) {
            *outResult = res;
        }
        return res == AuthResult::Success;
    };

    std::string parentStr = parentExePath.string();
    std::string selfStr = selfExePath.string();

    if (parentStr.empty() || selfStr.empty()) {
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: empty executable path");
        return setResult(AuthResult::UntrustedLocation);
    }

    if (parentStr.ends_with(" (deleted)") || selfStr.ends_with(" (deleted)")) {
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: executable is deleted (parent: '%s', self: '%s')",
                               parentStr.c_str(), selfStr.c_str());
        return setResult(AuthResult::DeletedExecutable);
    }

    std::string parentNorm = parentExePath.lexically_normal().string();
    static constexpr auto kUntrustedPrefixes =
        std::to_array<std::string_view>({"/tmp", "/var/tmp", "/dev/shm", "/run/user"});
    const std::string_view norm = parentNorm;
    const bool isUntrusted = std::ranges::any_of(kUntrustedPrefixes, [&](std::string_view prefix) {
        return norm == prefix ||
               (norm.starts_with(prefix) && norm.size() > prefix.size() && norm[prefix.size()] == '/');
    });
    if (isUntrusted) {
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: parent binary in untrusted directory '%s'",
                               parentNorm.c_str());
        return setResult(AuthResult::UntrustedLocation);
    }

    std::string parentExeName = parentExePath.filename().string();
    if (allowDevMode) {
        if (parentExeName != "qtranscribe" && parentExeName != "test_ipc_server" &&
            parentExeName != "test_transcription_pipeline") {
            KEYINJECTORD_LOG_ERROR("Launcher authorization failed: unauthorized parent executable name '%s' at '%s'",
                                   parentExeName.c_str(), parentExePath.c_str());
            return setResult(AuthResult::UnauthorizedExecutable);
        }
    } else {
        if (parentExeName != "qtranscribe") {
            KEYINJECTORD_LOG_ERROR("Launcher authorization failed: unauthorized parent executable name '%s' at '%s'",
                                   parentExeName.c_str(), parentExePath.c_str());
            return setResult(AuthResult::UnauthorizedExecutable);
        }
    }

    std::filesystem::path parentDir = parentExePath.parent_path().lexically_normal();
    std::filesystem::path selfDir = selfExePath.parent_path().lexically_normal();

    if (parentExePath != selfExePath && parentDir != selfDir) {
        KEYINJECTORD_LOG_ERROR(
            "Launcher authorization failed: parent path '%s' is not colocated in helper directory '%s'",
            parentExePath.c_str(), selfExePath.c_str());
        return setResult(AuthResult::UntrustedLocation);
    }

    struct stat selfLeafStat {};
    if (!validateDirectoryAncestry(selfExePath, outResult, allowDevMode, &selfLeafStat, rejectGroupWritable)) {
        return false;
    }

    if (!S_ISREG(selfLeafStat.st_mode)) {
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: self executable '%s' is not a regular file",
                               selfExePath.c_str());
        return setResult(AuthResult::NotRegularFile);
    }

    if (parentExePath == selfExePath) {
        if (outParentStat) {
            *outParentStat = selfLeafStat;
        }
        return setResult(AuthResult::Success);
    }

    struct stat parentLeafStat {};
    if (!validateDirectoryAncestry(parentExePath, outResult, allowDevMode, &parentLeafStat, rejectGroupWritable)) {
        return false;
    }

    if (!S_ISREG(parentLeafStat.st_mode)) {
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: parent executable '%s' is not a regular file",
                               parentExePath.c_str());
        return setResult(AuthResult::NotRegularFile);
    }

    if (outParentStat) {
        *outParentStat = parentLeafStat;
    }

    return setResult(AuthResult::Success);
}

bool authorizeLauncher(int socketFd, AuthResult* outResult, bool allowDevMode, bool rejectGroupWritable) {
    auto setResult = [outResult](AuthResult res) {
        if (outResult) {
            *outResult = res;
        }
        return res == AuthResult::Success;
    };

    if (socketFd < 0 || fcntl(socketFd, F_GETFD) == -1) {
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: invalid socket descriptor %d (%s)", socketFd,
                               std::strerror(errno));
        return setResult(AuthResult::InvalidFd);
    }

    int type = 0;
    socklen_t typeLen = sizeof(type);
    if (getsockopt(socketFd, SOL_SOCKET, SO_TYPE, &type, &typeLen) != 0 || type != SOCK_STREAM) {
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: descriptor %d is not a SOCK_STREAM socket", socketFd);
        return setResult(AuthResult::NotASocket);
    }

    struct sockaddr_storage addr {};
    socklen_t addrLen = sizeof(addr);
    if (getpeername(socketFd, reinterpret_cast<struct sockaddr*>(&addr), &addrLen) != 0 || addr.ss_family != AF_UNIX) {
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: descriptor %d is not an AF_UNIX connected socket",
                               socketFd);
        return setResult(AuthResult::NotConnectedUnixSocket);
    }

    struct ucred peerCreds {};
    socklen_t credLen = sizeof(peerCreds);
    if (getsockopt(socketFd, SOL_SOCKET, SO_PEERCRED, &peerCreds, &credLen) != 0) {
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: getsockopt(SO_PEERCRED) error: %s",
                               std::strerror(errno));
        return setResult(AuthResult::PeerCredsFailed);
    }

    if (peerCreds.uid != getuid()) {
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: peer UID %d does not match process UID %d",
                               peerCreds.uid, getuid());
        return setResult(AuthResult::UidMismatch);
    }

    pid_t parentPid = getppid();
    if (peerCreds.pid != parentPid && peerCreds.pid != getpid()) {
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: peer PID %d does not match parent PID %d", peerCreds.pid,
                               parentPid);
        return setResult(AuthResult::PidMismatch);
    }

    if (peerCreds.pid <= 1) {
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: invalid peer PID %d", peerCreds.pid);
        return setResult(AuthResult::PidMismatch);
    }

    std::string procParentExe = "/proc/" + std::to_string(peerCreds.pid) + "/exe";
    int peerExeFd = open(procParentExe.c_str(), O_PATH | O_CLOEXEC);
    if (peerExeFd < 0) {
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: open(%s) error: %s", procParentExe.c_str(),
                               std::strerror(errno));
        return setResult(AuthResult::ParentExeReadFailed);
    }

    struct stat peerFdStat {};
    if (fstat(peerExeFd, &peerFdStat) != 0) {
        close(peerExeFd);
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: fstat(%s) error: %s", procParentExe.c_str(),
                               std::strerror(errno));
        return setResult(AuthResult::StatFailed);
    }

    if (peerFdStat.st_nlink == 0) {
        close(peerExeFd);
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: peer executable is deleted (st_nlink == 0)");
        return setResult(AuthResult::DeletedExecutable);
    }

    if (!S_ISREG(peerFdStat.st_mode)) {
        close(peerExeFd);
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: peer executable is not a regular file");
        return setResult(AuthResult::NotRegularFile);
    }

    if (peerFdStat.st_mode & S_IWOTH) {
        close(peerExeFd);
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: peer executable is world-writable");
        return setResult(AuthResult::WorldWritable);
    }

    if (rejectGroupWritable && (peerFdStat.st_mode & S_IWGRP)) {
        close(peerExeFd);
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: peer executable is group-writable");
        return setResult(AuthResult::GroupWritable);
    }

    if (!allowDevMode && peerFdStat.st_uid != 0) {
        close(peerExeFd);
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: peer executable not root-owned (UID: %d)",
                               peerFdStat.st_uid);
        return setResult(AuthResult::NonRootOwner);
    }

    if (allowDevMode && peerFdStat.st_uid != 0 && peerFdStat.st_uid != getuid()) {
        close(peerExeFd);
        KEYINJECTORD_LOG_ERROR("Dev launcher authorization failed: peer executable owned by UID %d (expected 0 or %d)",
                               peerFdStat.st_uid, getuid());
        return setResult(AuthResult::UntrustedLocation);
    }

    int selfExeFd = open("/proc/self/exe", O_PATH | O_CLOEXEC);
    if (selfExeFd < 0) {
        close(peerExeFd);
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: open('/proc/self/exe') error: %s", std::strerror(errno));
        return setResult(AuthResult::SelfExeReadFailed);
    }

    struct stat selfFdStat {};
    if (fstat(selfExeFd, &selfFdStat) != 0) {
        close(selfExeFd);
        close(peerExeFd);
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: fstat('/proc/self/exe') error: %s",
                               std::strerror(errno));
        return setResult(AuthResult::StatFailed);
    }
    close(selfExeFd);

    if (selfFdStat.st_nlink == 0) {
        close(peerExeFd);
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: self executable is deleted (st_nlink == 0)");
        return setResult(AuthResult::DeletedExecutable);
    }

    char parentExeBuf[PATH_MAX];
    ssize_t parentLen = readlink(procParentExe.c_str(), parentExeBuf, sizeof(parentExeBuf) - 1);
    if (parentLen <= 0) {
        close(peerExeFd);
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: readlink(%s) error: %s", procParentExe.c_str(),
                               std::strerror(errno));
        return setResult(AuthResult::ParentExeReadFailed);
    }
    parentExeBuf[parentLen] = '\0';
    std::filesystem::path parentExePath(parentExeBuf);

    char selfExeBuf[PATH_MAX];
    ssize_t selfLen = readlink("/proc/self/exe", selfExeBuf, sizeof(selfExeBuf) - 1);
    if (selfLen <= 0) {
        close(peerExeFd);
        KEYINJECTORD_LOG_ERROR("Launcher authorization failed: readlink(/proc/self/exe) error: %s",
                               std::strerror(errno));
        return setResult(AuthResult::SelfExeReadFailed);
    }
    selfExeBuf[selfLen] = '\0';
    std::filesystem::path selfExePath(selfExeBuf);

    struct stat diskParentStat {};
    if (!validateExecutableTopology(parentExePath, selfExePath, outResult, allowDevMode, &diskParentStat,
                                    rejectGroupWritable)) {
        close(peerExeFd);
        return false;
    }

    if (peerFdStat.st_dev != diskParentStat.st_dev || peerFdStat.st_ino != diskParentStat.st_ino) {
        close(peerExeFd);
        KEYINJECTORD_LOG_ERROR(
            "Launcher authorization failed: inode mismatch between /proc/%d/exe (dev=%llu, ino=%llu) and disk '%s' "
            "(dev=%llu, ino=%llu)",
            peerCreds.pid, static_cast<unsigned long long>(peerFdStat.st_dev),
            static_cast<unsigned long long>(peerFdStat.st_ino), parentExePath.c_str(),
            static_cast<unsigned long long>(diskParentStat.st_dev),
            static_cast<unsigned long long>(diskParentStat.st_ino));
        return setResult(AuthResult::InodeMismatch);
    }

    close(peerExeFd);
    KEYINJECTORD_LOG_INFO("Launcher authorization verified for peer PID %d (%s)", peerCreds.pid, parentExePath.c_str());
    return setResult(AuthResult::Success);
}

} // namespace keyinjectord
