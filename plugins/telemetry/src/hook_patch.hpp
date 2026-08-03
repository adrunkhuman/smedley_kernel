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
                *error = "could not enumerate threads before patching telemetry hooks";
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
                *error = "could not reserve thread handles before patching telemetry hooks";
                return;
            }
            snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            if (snapshot == INVALID_HANDLE_VALUE) {
                *error = "could not enumerate threads before patching telemetry hooks";
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
                    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_QUERY_LIMITED_INFORMATION
                                                   | THREAD_GET_CONTEXT,
                                               FALSE, entry.th32ThreadID);
                    if (thread == nullptr) {
                        if (GetLastError() != ERROR_INVALID_PARAMETER) {
                            CloseHandle(snapshot);
                            CloseThreads();
                            *error = "could not open a game thread before patching telemetry hooks";
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
                *error = "a game thread started while preparing telemetry hook patches";
                return;
            }
            for (HANDLE thread : threads_) {
                if (SuspendThread(thread) == static_cast<DWORD>(-1)) {
                    std::string ignored;
                    (void)Release(&ignored);
                    *error = "could not suspend a game thread before patching telemetry hooks";
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

        bool AnyInstructionPointerIn(uintptr_t address, size_t size, bool *found,
                                     std::string *error) const noexcept
        {
            size_t count = 0;
            if (!InstructionPointerCountIn(address, size, &count, error)) return false;
            if (found == nullptr) {
                if (error != nullptr) *error = "invalid suspended-thread instruction result";
                return false;
            }
            *found = count != 0;
            return true;
        }

        bool InstructionPointerCountIn(uintptr_t address, size_t size, size_t *count,
                                       std::string *error) const noexcept
        {
            if (!ready_ || count == nullptr || size == 0 || address > UINTPTR_MAX - size) {
                if (error != nullptr) *error = "invalid suspended-thread instruction range";
                return false;
            }
            *count = 0;
            for (size_t index = 0; index < suspended_count_; ++index) {
                CONTEXT context{};
                context.ContextFlags = CONTEXT_CONTROL;
                if (!GetThreadContext(threads_[index], &context)) {
                    if (WaitForSingleObject(threads_[index], 0) == WAIT_OBJECT_0) continue;
                    if (error != nullptr) *error = "could not inspect a suspended game thread before patching telemetry hooks";
                    return false;
                }
#if defined(_M_IX86)
                const uintptr_t instruction_pointer = context.Eip;
#else
#error Telemetry hooks require the Win32 x86 build.
#endif
                if (instruction_pointer >= address && instruction_pointer < address + size) {
                    ++*count;
                }
            }
            return true;
        }

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
                *error = "could not resume a game thread after patching telemetry hooks";
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
