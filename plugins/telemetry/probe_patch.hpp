#pragma once

#include <string>
#include <vector>
#include <Windows.h>
#include <TlHelp32.h>

namespace telemetry_plugin
{
    class ScopedThreadQuiescence
    {
    public:
        explicit ScopedThreadQuiescence(std::string *error)
        {
            HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            if (snapshot == INVALID_HANDLE_VALUE) {
                *error = "could not enumerate threads before patching telemetry probes";
                return;
            }
            THREADENTRY32 entry{sizeof(entry)};
            const DWORD process_id = GetCurrentProcessId();
            const DWORD current_thread_id = GetCurrentThreadId();
            BOOL found = Thread32First(snapshot, &entry);
            size_t thread_count = 0;
            while (found) {
                if (entry.th32OwnerProcessID == process_id && entry.th32ThreadID != current_thread_id) ++thread_count;
                found = Thread32Next(snapshot, &entry);
            }
            CloseHandle(snapshot);
            try {
                threads_.reserve(thread_count);
            } catch (...) {
                *error = "could not reserve thread handles before patching telemetry probes";
                return;
            }
            snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            if (snapshot == INVALID_HANDLE_VALUE) {
                *error = "could not enumerate threads before patching telemetry probes";
                return;
            }
            entry = {sizeof(entry)};
            found = Thread32First(snapshot, &entry);
            bool thread_started_during_enumeration = false;
            while (found) {
                if (entry.th32OwnerProcessID == process_id && entry.th32ThreadID != current_thread_id) {
                    if (threads_.size() >= thread_count) {
                        thread_started_during_enumeration = true;
                        found = Thread32Next(snapshot, &entry);
                        continue;
                    }
                    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_QUERY_LIMITED_INFORMATION,
                                               FALSE, entry.th32ThreadID);
                    if (thread == nullptr) {
                        if (GetLastError() != ERROR_INVALID_PARAMETER) {
                            CloseHandle(snapshot);
                            CloseThreads();
                            *error = "could not open a game thread before patching telemetry probes";
                            return;
                        }
                    } else {
                        threads_.push_back(thread);
                    }
                }
                found = Thread32Next(snapshot, &entry);
            }
            CloseHandle(snapshot);
            if (thread_started_during_enumeration) {
                CloseThreads();
                *error = "a game thread started while preparing telemetry probe patches";
                return;
            }
            for (HANDLE thread : threads_) {
                if (SuspendThread(thread) == static_cast<DWORD>(-1)) {
                    std::string ignored;
                    (void)Release(&ignored);
                    *error = "could not suspend a game thread before patching telemetry probes";
                    return;
                }
                ++suspended_count_;
            }
            ready_ = true;
        }

        ~ScopedThreadQuiescence()
        {
            std::string ignored;
            if (!Release(&ignored)) {
                TerminateProcess(GetCurrentProcess(), ERROR_OPERATION_ABORTED);
            }
        }

        ScopedThreadQuiescence(const ScopedThreadQuiescence &) = delete;
        ScopedThreadQuiescence &operator=(const ScopedThreadQuiescence &) = delete;

        explicit operator bool() const noexcept { return ready_; }

        bool Release(std::string *error) noexcept
        {
            const size_t old_suspended_count = suspended_count_;
            const size_t old_thread_count = threads_.size();
            size_t failed_count = 0;
            for (size_t index = 0; index < old_suspended_count; ++index) {
                const HANDLE thread = threads_[index];
                if (ResumeThread(thread) != static_cast<DWORD>(-1)
                    || WaitForSingleObject(thread, 0) == WAIT_OBJECT_0) {
                    CloseHandle(thread);
                } else {
                    threads_[failed_count++] = thread;
                }
            }
            for (size_t index = old_suspended_count; index < old_thread_count; ++index) {
                CloseHandle(threads_[index]);
            }
            threads_.resize(failed_count);
            suspended_count_ = failed_count;
            ready_ = failed_count != 0;
            if (failed_count != 0 && error != nullptr) {
                *error = "could not resume a game thread after patching telemetry probes";
            }
            return failed_count == 0;
        }

    private:
        void CloseThreads() noexcept
        {
            for (HANDLE thread : threads_) CloseHandle(thread);
            threads_.clear();
        }

        std::vector<HANDLE> threads_;
        size_t suspended_count_ = 0;
        bool ready_ = false;
    };
}
