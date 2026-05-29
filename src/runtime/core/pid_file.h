#pragma once

#include <cstdio>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace engine {

// PID file with exclusive lock for multi-instance prevention.
// WritePidFile() opens/creates the file, locks it, and writes the PID.
// RemovePidFile() removes the file on clean shutdown.
// TryLockPidFile() checks whether another instance holds the lock.

class PidFile {
public:
    // Try to create and lock the PID file. Returns true on success.
    // If the file is already locked by another process, returns false
    // and sets *error_out (if non-null).
    static bool WritePidFile(const std::string& path, std::string* error_out = nullptr) {
#ifdef _WIN32
        HANDLE h = CreateFileA(path.c_str(),
                               GENERIC_WRITE,
                               0,  // exclusive access
                               nullptr,
                               CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL,
                               nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            DWORD err = GetLastError();
            if (err == ERROR_SHARING_VIOLATION || err == ERROR_ACCESS_DENIED) {
                if (error_out) *error_out = "PID file locked by another instance";
            } else {
                if (error_out) *error_out = "Failed to create PID file";
            }
            return false;
        }
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "%lu\n", GetCurrentProcessId());
        DWORD written = 0;
        WriteFile(h, buf, static_cast<DWORD>(len), &written, nullptr);
        // Keep the handle open to hold the lock. Closed in RemovePidFile().
        // We store it as a static to survive.
        GetHandle() = h;
        return true;
#else
        int fd = open(path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd < 0) {
            if (error_out) *error_out = "Failed to create PID file: " + std::string(strerror(errno));
            return false;
        }
        if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
            if (error_out) *error_out = "PID file locked by another instance";
            close(fd);
            return false;
        }
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "%d\n", getpid());
        ssize_t written = write(fd, buf, static_cast<size_t>(len));
        (void)written;
        GetFd() = fd;
        return true;
#endif
    }

    static void RemovePidFile(const std::string& path) {
#ifdef _WIN32
        HANDLE& h = GetHandle();
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
            h = INVALID_HANDLE_VALUE;
        }
        DeleteFileA(path.c_str());
#else
        int& fd = GetFd();
        if (fd >= 0) {
            flock(fd, LOCK_UN);
            close(fd);
            fd = -1;
        }
        unlink(path.c_str());
#endif
    }

private:
#ifdef _WIN32
    static HANDLE& GetHandle() {
        static HANDLE h = INVALID_HANDLE_VALUE;
        return h;
    }
#else
    static int& GetFd() {
        static int fd = -1;
        return fd;
    }
#endif
};

}  // namespace engine
