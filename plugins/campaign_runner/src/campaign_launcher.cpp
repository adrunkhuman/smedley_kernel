#include "campaign_launcher.hpp"
#include "campaign_save_selection.hpp"

#include <smedley/log.hpp>
#include <smedley/memory.hpp>
#include <smedley/std/string.hpp>
#include <smedley/std/vector.hpp>
#include <smedley/v2/console.hpp>
#include <smedley/v2/gamestate.hpp>

#include <psapi.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <mutex>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace campaign_runner
{
    namespace
    {
        namespace fs = std::filesystem;

        CampaignLauncher *launcher_instance = nullptr;
        std::recursive_mutex launcher_callback_mutex;
        uintptr_t frontend_constructor_return_address = 0;
        uintptr_t main_menu_return_address = 0;
        uintptr_t frontend_destructor_return_address = 0;
        uintptr_t main_menu_destructor_return_address = 0;
        uintptr_t country_annex_return_address = 0;
        uintptr_t message_dispatch_return_address = 0;
        uintptr_t message_dispatch_popup_address = 0;
        uintptr_t message_dispatch_suppressed_address = 0;
        uintptr_t message_dispatch_2_return_address = 0;
        uintptr_t message_dispatch_2_popup_address = 0;
        uintptr_t message_dispatch_2_suppressed_address = 0;
        uintptr_t message_dispatch_3_return_address = 0;
        uintptr_t message_dispatch_3_popup_address = 0;
        uintptr_t message_dispatch_3_suppressed_address = 0;
        uintptr_t message_dispatch_4_return_address = 0;
        uintptr_t message_dispatch_4_popup_address = 0;
        uintptr_t message_dispatch_4_suppressed_address = 0;
        uintptr_t message_dispatch_5_return_address = 0;
        uintptr_t message_dispatch_5_popup_address = 0;
        uintptr_t message_dispatch_5_suppressed_address = 0;
        uintptr_t message_dispatch_6_return_address = 0;
        uintptr_t message_dispatch_6_popup_address = 0;
        uintptr_t message_dispatch_6_suppressed_address = 0;
        uintptr_t message_dispatch_7_return_address = 0;
        uintptr_t message_dispatch_7_popup_address = 0;
        uintptr_t message_dispatch_7_suppressed_address = 0;
        uintptr_t message_dispatch_8_return_address = 0;
        uintptr_t message_dispatch_8_popup_address = 0;
        uintptr_t message_dispatch_8_suppressed_address = 0;
        uintptr_t message_dispatch_9_return_address = 0;
        uintptr_t message_dispatch_9_popup_address = 0;
        uintptr_t message_dispatch_9_suppressed_address = 0;
        volatile bool suppress_message_popups = false;
        volatile long suppressed_message_count = 0;

        constexpr size_t selected_save_offset = 0x590;
        constexpr size_t save_request_offset = 0x5bc;
        constexpr size_t save_complete_offset = 0x5bd;
        constexpr size_t frontend_gui_offset = 0x278;
        constexpr size_t main_menu_gui_offset = 0x704;
        constexpr size_t control_signal_offset = 0x54;
        constexpr size_t maximum_save_basename = MAX_PATH - 1;
        constexpr uintptr_t frontend_vtable_rva = 0xa14ed0;
        constexpr uintptr_t main_menu_vtable_rva = 0xa13dbc;
        static_assert(save_complete_offset == save_request_offset + 1);

        bool IsAccessible(const void *pointer, size_t size, bool require_writable)
        {
            if (pointer == nullptr || size == 0) return false;
            const uintptr_t begin = reinterpret_cast<uintptr_t>(pointer);
            if (begin > (std::numeric_limits<uintptr_t>::max)() - size) return false;
            const uintptr_t end = begin + size;
            for (uintptr_t cursor = begin; cursor < end;) {
                MEMORY_BASIC_INFORMATION region{};
                if (VirtualQuery(reinterpret_cast<const void *>(cursor), &region, sizeof(region)) != sizeof(region)
                    || region.State != MEM_COMMIT
                    || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
                const DWORD allowed = require_writable
                    ? PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY
                    : PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY
                        | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
                if ((region.Protect & allowed) == 0) return false;
                const uintptr_t region_begin = reinterpret_cast<uintptr_t>(region.BaseAddress);
                if (region_begin > (std::numeric_limits<uintptr_t>::max)() - region.RegionSize) return false;
                const uintptr_t region_end = region_begin + region.RegionSize;
                if (region_end <= cursor) return false;
                cursor = (std::min)(end, region_end);
            }
            return true;
        }

        bool CopyReadable(void *destination, const void *source, size_t size)
        {
            if (!IsAccessible(source, size, false)) return false;
            __try {
                std::memcpy(destination, source, size);
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        bool CopyWritable(void *destination, const void *source, size_t size)
        {
            if (!IsAccessible(destination, size, true)) return false;
            __try {
                std::memcpy(destination, source, size);
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        template <typename T>
        bool ReadValue(const void *source, T *value)
        {
            return value != nullptr && CopyReadable(value, source, sizeof(T));
        }

        const void *OffsetAddress(const void *base, size_t offset)
        {
            const uintptr_t address = reinterpret_cast<uintptr_t>(base);
            if (address > (std::numeric_limits<uintptr_t>::max)() - offset) return nullptr;
            return reinterpret_cast<const void *>(address + offset);
        }

        void *OffsetAddress(void *base, size_t offset)
        {
            return const_cast<void *>(OffsetAddress(static_cast<const void *>(base), offset));
        }

        bool IsGameCodeAddress(uintptr_t address)
        {
            MODULEINFO module{};
            if (address == 0
                || !GetModuleInformation(GetCurrentProcess(), GetModuleHandle(nullptr), &module, sizeof(module))) return false;
            const uintptr_t begin = reinterpret_cast<uintptr_t>(module.lpBaseOfDll);
            if (begin > (std::numeric_limits<uintptr_t>::max)() - module.SizeOfImage) return false;
            if (address < begin || address >= begin + module.SizeOfImage) return false;
            MEMORY_BASIC_INFORMATION region{};
            if (VirtualQuery(reinterpret_cast<const void *>(address), &region, sizeof(region)) != sizeof(region)) return false;
            return region.State == MEM_COMMIT
                && (region.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ
                    | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0
                && (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0;
        }

        bool ReadVirtualTarget(const void *object, size_t slot_offset, uintptr_t *target)
        {
            uintptr_t vtable = 0;
            if (!ReadValue(object, &vtable) || vtable == 0) return false;
            const void *slot = OffsetAddress(reinterpret_cast<const void *>(vtable), slot_offset);
            return slot != nullptr && ReadValue(slot, target) && IsGameCodeAddress(*target);
        }

        struct RawEngineString
        {
            union {
                char buffer[16];
                const char *pointer;
            } storage;
            uint32_t size;
            uint32_t capacity;
            uint32_t allocator;
        };

        static_assert(sizeof(RawEngineString) == sizeof(smedley::sstd::string));

        bool ReadMappedString(const void *address, size_t maximum_size, std::string *value,
                              RawEngineString *metadata = nullptr)
        {
            if (value == nullptr) return false;
            RawEngineString snapshot{};
            if (!ReadValue(address, &snapshot) || snapshot.size > maximum_size
                || snapshot.capacity < snapshot.size
                || snapshot.capacity < 0xf) return false;
            const char *source = snapshot.capacity > 0xf ? snapshot.storage.pointer : snapshot.storage.buffer;
            std::string copy(snapshot.size, '\0');
            if (snapshot.size != 0 && !CopyReadable(copy.data(), source, snapshot.size)) return false;
            char terminator = 0;
            if (!ReadValue(source + snapshot.size, &terminator) || terminator != '\0') return false;
            *value = std::move(copy);
            if (metadata != nullptr) *metadata = snapshot;
            return true;
        }

        class InlineEngineString final : public smedley::sstd::string
        {
        public:
            bool Assign(std::string_view value)
            {
                if (value.size() > default_capacity) return false;
                std::fill(std::begin(_impl.buf), std::end(_impl.buf), '\0');
                std::memcpy(_impl.buf, value.data(), value.size());
                _size = value.size();
                _capacity = default_capacity;
                return true;
            }
        };

        class SingleConsoleArgument final : public smedley::sstd::vector<smedley::sstd::string>
        {
        public:
            explicit SingleConsoleArgument(std::string_view value)
            {
                if (!argument_.Assign(value)) return;
                _first = &argument_;
                _last = _first + 1;
                _end = _last;
                valid_ = true;
            }

            bool valid() const noexcept { return valid_; }

        private:
            InlineEngineString argument_;
            bool valid_ = false;
        };

        class EngineStringArgument final : public smedley::sstd::string
        {
        public:
            explicit EngineStringArgument(std::string_view value)
            {
                _size = value.size();
                if (value.size() <= default_capacity) {
                    std::fill(std::begin(_impl.buf), std::end(_impl.buf), '\0');
                    std::memcpy(_impl.buf, value.data(), value.size());
                    _capacity = default_capacity;
                } else {
                    _impl.ptr = const_cast<char *>(value.data());
                    _capacity = value.size();
                }
            }
        };

        void *InvokeFindControl(void *object, uintptr_t target, const smedley::sstd::string *name)
        {
            using FindControl = void *(__thiscall *)(void *, const smedley::sstd::string *);
            __try {
                return reinterpret_cast<FindControl>(target)(object, name);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return nullptr;
            }
        }

        bool DispatchNativeSignal(void *signal)
        {
            if (!IsAccessible(signal, sizeof(uintptr_t), true)) return false;
            const auto press = smedley::memory::Map::base_addr + 0x5ee510;
            const auto release = smedley::memory::Map::base_addr + 0x5ee550;
            __try {
                __asm mov eax, signal
                __asm call press
                __asm mov eax, signal
                __asm call release
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        bool ValidateGuiRegistry(void *controller, size_t registry_offset, size_t lookup_slot)
        {
            void *registry = nullptr;
            uintptr_t lookup = 0;
            return ReadValue(OffsetAddress(controller, registry_offset), &registry)
                && registry != nullptr
                && ReadVirtualTarget(registry, lookup_slot, &lookup);
        }

        bool ControllerVtableMatches(const void *controller, uintptr_t expected_rva)
        {
            uintptr_t vtable = 0;
            return ReadValue(controller, &vtable)
                && vtable == smedley::memory::Map::base_addr + expected_rva;
        }

        uint64_t MonotonicMicroseconds()
        {
            LARGE_INTEGER frequency{}, counter{};
            if (!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&counter) || frequency.QuadPart <= 0) return 0;
            const uint64_t ticks = static_cast<uint64_t>(counter.QuadPart);
            const uint64_t rate = static_cast<uint64_t>(frequency.QuadPart);
            return ticks / rate * 1000000ull + ticks % rate * 1000000ull / rate;
        }

        struct ProcessMetricsSnapshot
        {
            std::optional<int64_t> process_cpu_us;
            std::optional<int64_t> working_set_bytes;
            std::optional<int64_t> private_bytes;
            std::optional<int64_t> process_peak_working_set_bytes;
        };

        ProcessMetricsSnapshot SampleProcessMetrics()
        {
            ProcessMetricsSnapshot snapshot;
            FILETIME created{}, exited{}, kernel{}, user{};
            if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
                ULARGE_INTEGER kernel_ticks{}, user_ticks{};
                kernel_ticks.LowPart = kernel.dwLowDateTime;
                kernel_ticks.HighPart = kernel.dwHighDateTime;
                user_ticks.LowPart = user.dwLowDateTime;
                user_ticks.HighPart = user.dwHighDateTime;
                if (user_ticks.QuadPart <= (std::numeric_limits<uint64_t>::max)() - kernel_ticks.QuadPart) {
                    const uint64_t total_us = (kernel_ticks.QuadPart + user_ticks.QuadPart) / 10;
                    if (total_us <= static_cast<uint64_t>((std::numeric_limits<int64_t>::max)())) {
                        snapshot.process_cpu_us = static_cast<int64_t>(total_us);
                    }
                }
            }
            PROCESS_MEMORY_COUNTERS_EX counters{};
            counters.cb = sizeof(counters);
            if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                                     sizeof(counters))) {
                snapshot.working_set_bytes = static_cast<int64_t>(counters.WorkingSetSize);
                snapshot.private_bytes = static_cast<int64_t>(counters.PrivateUsage);
                snapshot.process_peak_working_set_bytes = static_cast<int64_t>(counters.PeakWorkingSetSize);
            }
            return snapshot;
        }

        bool IsInGameIdler(const void *object)
        {
            constexpr char expected[] = ".?AVCInGameIdler@@";
            uintptr_t vtable = 0;
            uintptr_t locator = 0;
            uintptr_t type_descriptor = 0;
            std::array<char, sizeof(expected)> type_name{};
            const void *locator_slot = nullptr;
            const void *descriptor_slot = nullptr;
            const void *name = nullptr;
            if (!ReadValue(object, &vtable) || vtable < sizeof(uintptr_t)) return false;
            locator_slot = reinterpret_cast<const void *>(vtable - sizeof(uintptr_t));
            if (!ReadValue(locator_slot, &locator) || locator == 0) return false;
            descriptor_slot = OffsetAddress(reinterpret_cast<const void *>(locator), 0x0c);
            if (descriptor_slot == nullptr || !ReadValue(descriptor_slot, &type_descriptor)
                || type_descriptor == 0) return false;
            name = OffsetAddress(reinterpret_cast<const void *>(type_descriptor), 0x08);
            return name != nullptr && CopyReadable(type_name.data(), name, type_name.size())
                && std::memcmp(type_name.data(), expected, sizeof(expected)) == 0;
        }

        void __stdcall CaptureFrontendController(void *controller) noexcept
        {
            try {
                const std::lock_guard<std::recursive_mutex> lock(launcher_callback_mutex);
                if (launcher_instance != nullptr) launcher_instance->CaptureFrontendController(controller);
            } catch (...) {}
        }

        void __stdcall CaptureMainMenuController(void *controller) noexcept
        {
            try {
                const std::lock_guard<std::recursive_mutex> lock(launcher_callback_mutex);
                if (launcher_instance != nullptr) launcher_instance->CaptureMainMenuController(controller);
            } catch (...) {}
        }

        void __stdcall ReleaseFrontendController(void *controller) noexcept
        {
            try {
                const std::lock_guard<std::recursive_mutex> lock(launcher_callback_mutex);
                if (launcher_instance != nullptr) launcher_instance->ReleaseFrontendController(controller);
            } catch (...) {}
        }

        void __stdcall ReleaseMainMenuController(void *controller) noexcept
        {
            try {
                const std::lock_guard<std::recursive_mutex> lock(launcher_callback_mutex);
                if (launcher_instance != nullptr) launcher_instance->ReleaseMainMenuController(controller);
            } catch (...) {}
        }

        void __stdcall PrepareObserverForAnnexation(int annexed_ordinal) noexcept
        {
            try {
                const std::lock_guard<std::recursive_mutex> lock(launcher_callback_mutex);
                if (launcher_instance != nullptr) launcher_instance->PrepareObserverForAnnexation(annexed_ordinal);
            } catch (...) {}
        }

        __declspec(naked) void FrontendConstructorTrampoline()
        {
            __asm {
                pushfd
                pushad
                mov eax, dword ptr [esp + 0x28]
                push eax
                call CaptureFrontendController
                popad
                popfd

                push ebp
                mov ebp, esp
                push 0xffffffff
                jmp frontend_constructor_return_address
            }
        }

        __declspec(naked) void MainMenuTrampoline()
        {
            __asm {
                pushfd
                pushad
                mov eax, dword ptr [esp + 0x28]
                push eax
                call CaptureMainMenuController
                popad
                popfd

                push ebp
                mov ebp, esp
                push 0xffffffff
                jmp main_menu_return_address
            }
        }

        __declspec(naked) void FrontendDestructorTrampoline()
        {
            __asm {
                pushfd
                pushad
                push ecx
                call ReleaseFrontendController
                popad
                popfd

                push ebp
                mov ebp, esp
                push esi
                mov esi, ecx
                jmp frontend_destructor_return_address
            }
        }

        __declspec(naked) void MainMenuDestructorTrampoline()
        {
            __asm {
                pushfd
                pushad
                push ecx
                call ReleaseMainMenuController
                popad
                popfd

                push ebp
                mov ebp, esp
                push esi
                mov esi, ecx
                jmp main_menu_destructor_return_address
            }
        }

        __declspec(naked) void CountryAnnexTrampoline()
        {
            __asm {
                pushfd
                pushad
                mov eax, dword ptr [esp + 0x34]
                push eax
                call PrepareObserverForAnnexation
                popad
                popfd

                push ebp
                mov ebp, esp
                and esp, 0xfffffff8
                jmp country_annex_return_address
            }
        }

        __declspec(naked) void MessageDispatchTrampoline()
        {
            __asm {
                cmp byte ptr [suppress_message_popups], 0
                jne suppressed
                cmp byte ptr [edi + 0x0e], 0
                jne popup
                jmp message_dispatch_return_address
            suppressed:
                cmp byte ptr [edi + 0x0e], 0
                jne count_suppressed
                cmp byte ptr [edi + 0x10], 0
                je bypass_popup
            count_suppressed:
                lock inc dword ptr [suppressed_message_count]
            bypass_popup:
                jmp message_dispatch_suppressed_address
            popup:
                jmp message_dispatch_popup_address
            }
        }

        __declspec(naked) void MessageDispatch2Trampoline()
        {
            __asm {
                cmp byte ptr [suppress_message_popups], 0
                jne dispatch2_suppressed
                cmp byte ptr [ebx + 0x0e], 0
                jne dispatch2_popup
                jmp message_dispatch_2_return_address
            dispatch2_suppressed:
                cmp byte ptr [ebx + 0x0e], 0
                jne dispatch2_count
                cmp byte ptr [ebx + 0x10], 0
                je dispatch2_bypass
            dispatch2_count:
                lock inc dword ptr [suppressed_message_count]
            dispatch2_bypass:
                jmp message_dispatch_2_suppressed_address
            dispatch2_popup:
                jmp message_dispatch_2_popup_address
            }
        }

        __declspec(naked) void MessageDispatch3Trampoline()
        {
            __asm {
                cmp byte ptr [suppress_message_popups], 0
                jne dispatch3_suppressed
                cmp byte ptr [edi + 0x0e], 0
                jne dispatch3_popup
                jmp message_dispatch_3_return_address
            dispatch3_suppressed:
                cmp byte ptr [edi + 0x0e], 0
                jne dispatch3_count
                cmp byte ptr [edi + 0x10], 0
                je dispatch3_bypass
            dispatch3_count:
                lock inc dword ptr [suppressed_message_count]
            dispatch3_bypass:
                jmp message_dispatch_3_suppressed_address
            dispatch3_popup:
                jmp message_dispatch_3_popup_address
            }
        }

        __declspec(naked) void MessageDispatch4Trampoline()
        {
            __asm {
                cmp byte ptr [suppress_message_popups], 0
                jne dispatch4_suppressed
                cmp byte ptr [edi + 0x0e], 0
                jne dispatch4_popup
                jmp message_dispatch_4_return_address
            dispatch4_suppressed:
                cmp byte ptr [edi + 0x0e], 0
                jne dispatch4_count
                cmp byte ptr [edi + 0x10], 0
                je dispatch4_bypass
            dispatch4_count:
                lock inc dword ptr [suppressed_message_count]
            dispatch4_bypass:
                jmp message_dispatch_4_suppressed_address
            dispatch4_popup:
                jmp message_dispatch_4_popup_address
            }
        }

        __declspec(naked) void MessageDispatch5Trampoline()
        {
            __asm {
                cmp byte ptr [suppress_message_popups], 0
                jne dispatch5_suppressed
                cmp byte ptr [ebx + 0x0e], 0
                jne dispatch5_popup
                jmp message_dispatch_5_return_address
            dispatch5_suppressed:
                cmp byte ptr [ebx + 0x0e], 0
                jne dispatch5_count
                cmp byte ptr [ebx + 0x10], 0
                je dispatch5_bypass
            dispatch5_count:
                lock inc dword ptr [suppressed_message_count]
            dispatch5_bypass:
                jmp message_dispatch_5_suppressed_address
            dispatch5_popup:
                jmp message_dispatch_5_popup_address
            }
        }

        __declspec(naked) void MessageDispatch6Trampoline()
        {
            __asm {
                cmp byte ptr [suppress_message_popups], 0
                jne dispatch6_suppressed
                cmp byte ptr [edi + 0x0e], 0
                jne dispatch6_popup
                jmp message_dispatch_6_return_address
            dispatch6_suppressed:
                cmp byte ptr [edi + 0x0e], 0
                jne dispatch6_count
                cmp byte ptr [edi + 0x10], 0
                je dispatch6_bypass
            dispatch6_count:
                lock inc dword ptr [suppressed_message_count]
            dispatch6_bypass:
                jmp message_dispatch_6_suppressed_address
            dispatch6_popup:
                jmp message_dispatch_6_popup_address
            }
        }

        __declspec(naked) void MessageDispatch7Trampoline()
        {
            __asm {
                cmp byte ptr [suppress_message_popups], 0
                jne dispatch7_suppressed
                cmp byte ptr [edi + 0x0e], 0
                jne dispatch7_popup
                jmp message_dispatch_7_return_address
            dispatch7_suppressed:
                cmp byte ptr [edi + 0x0e], 0
                jne dispatch7_count
                cmp byte ptr [edi + 0x10], 0
                je dispatch7_bypass
            dispatch7_count:
                lock inc dword ptr [suppressed_message_count]
            dispatch7_bypass:
                jmp message_dispatch_7_suppressed_address
            dispatch7_popup:
                jmp message_dispatch_7_popup_address
            }
        }

        __declspec(naked) void MessageDispatch8Trampoline()
        {
            __asm {
                cmp byte ptr [suppress_message_popups], 0
                jne dispatch8_suppressed
                cmp byte ptr [edi + 0x0e], 0
                jne dispatch8_popup
                jmp message_dispatch_8_return_address
            dispatch8_suppressed:
                cmp byte ptr [edi + 0x0e], 0
                jne dispatch8_count
                cmp byte ptr [edi + 0x10], 0
                je dispatch8_bypass
            dispatch8_count:
                lock inc dword ptr [suppressed_message_count]
            dispatch8_bypass:
                jmp message_dispatch_8_suppressed_address
            dispatch8_popup:
                jmp message_dispatch_8_popup_address
            }
        }

        __declspec(naked) void MessageDispatch9Trampoline()
        {
            __asm {
                cmp byte ptr [suppress_message_popups], 0
                jne dispatch9_suppressed
                cmp byte ptr [edi + 0x0e], 0
                jne dispatch9_popup
                jmp message_dispatch_9_return_address
            dispatch9_suppressed:
                cmp byte ptr [edi + 0x0e], 0
                jne dispatch9_count
                cmp byte ptr [edi + 0x10], 0
                je dispatch9_bypass
            dispatch9_count:
                lock inc dword ptr [suppressed_message_count]
            dispatch9_bypass:
                jmp message_dispatch_9_suppressed_address
            dispatch9_popup:
                jmp message_dispatch_9_popup_address
            }
        }
    }

    CampaignLauncher::CampaignLauncher(smedley::Logger &logger) noexcept
        : logger_(logger)
    {
    }

    bool CampaignLauncher::Start(
        std::wstring save_path,
        bool observe,
        std::wstring observer_view_tag,
        int speed,
        bool start_paused,
        bool quit_after_run,
        CampaignRunCondition condition)
    {
        const std::lock_guard<std::recursive_mutex> lock(launcher_callback_mutex);
        save_path_ = std::move(save_path);
        observe_ = observe;
        target_speed_ = speed;
        start_paused_ = start_paused;
        quit_after_run_ = quit_after_run;
        run_condition_ = condition;
        observer_enabled_ = false;
        speed_ready_ = false;
        suppress_message_popups = false;
        suppressed_message_count = 0;
        launcher_instance = this;
        if (target_speed_ < 1 || target_speed_ > 5) {
            logger_.Failure("campaign speed must be from 1 through 5");
            launcher_instance = nullptr;
            return false;
        }
        if (observe_ && start_paused_) {
            logger_.Failure("observer mode cannot start paused because its watchdog requires advancement");
            launcher_instance = nullptr;
            return false;
        }
        if (run_condition_.requested() && (start_paused_ || !observer_view_tag.empty())) {
            logger_.Failure("benchmark target runs require unpaused start and do not support an initial view switch");
            launcher_instance = nullptr;
            return false;
        }
        if (quit_after_run_ && !run_condition_.requested()) {
            logger_.Failure("quit-after-run requires a benchmark run target");
            launcher_instance = nullptr;
            return false;
        }
        if (!observer_view_tag.empty()) {
            if (!observe || observer_view_tag.size() != 3) {
                logger_.Failure("observer view tag requires --observe and three ASCII alphanumeric characters");
                launcher_instance = nullptr;
                return false;
            }
            for (const auto character : observer_view_tag) {
                if (!((character >= L'A' && character <= L'Z') || (character >= L'a' && character <= L'z')
                      || (character >= L'0' && character <= L'9'))) {
                    logger_.Failure("observer view tag requires --observe and three ASCII alphanumeric characters");
                    launcher_instance = nullptr;
                    return false;
                }
                initial_observer_view_tag_.push_back(static_cast<char>(
                    character >= L'a' && character <= L'z'
                        ? character - L'a' + L'A'
                        : character));
            }
        }
        if (save_path_.empty()) {
            logger_.Warn("no unattended save argument found");
            return false;
        }
        if (!CheckSignatures() || !InstallControllerHooks()) {
            observe_ = false;
            launcher_instance = nullptr;
            return false;
        }
        observer_enabled_ = observe;
        logger_.Info("waiting for the frontend before unattended save loading");
        return true;
    }

    void CampaignLauncher::Stop()
    {
        const std::lock_guard<std::recursive_mutex> lock(launcher_callback_mutex);
        auto *game_state = smedley::v2::CCurrentGameState::instance();
        if (observer_view_switch_pending_ && game_state != nullptr) {
            auto *target = game_state->country(observer_target_ordinal_);
            if (target != nullptr
                && target->exists()
                && game_state->player_control_state(observer_target_ordinal_) > 0
                && target->ai() == nullptr) {
                game_state->ReturnCountryToAI(target->tag());
            }
        }
        if (save_timer_ != 0) {
            KillTimer(nullptr, save_timer_);
            save_timer_ = 0;
        }
        frontend_controller_.store(nullptr, std::memory_order_release);
        main_menu_controller_.store(nullptr, std::memory_order_release);
        frontend_thread_id_.store(0, std::memory_order_release);
        main_menu_thread_id_.store(0, std::memory_order_release);
        suppress_message_popups = false;
        if (native_tag_command_ != nullptr
            && native_tag_command_->handler == &CampaignLauncher::RejectNativeTag) {
            native_tag_command_->handler = native_tag_handler_;
        }
        if (observer_command_manager_ != nullptr && observer_switch_command_ != nullptr) {
            if (observer_command_manager_->commands().erase_value(observer_switch_command_)) {
                delete observer_switch_command_;
            }
        }
        observer_switch_command_ = nullptr;
        observer_command_manager_ = nullptr;
        observer_console_ready_ = false;
        // Legacy plugin modules remain loaded. Leave callbacks inert rather than
        // rewriting executable memory without a process-wide quiescence protocol.
        launcher_instance = nullptr;
    }

    void CampaignLauncher::CaptureConsoleCommandManager(smedley::v2::CConsoleCmdManager *manager)
    {
        if (manager == nullptr) {
            return;
        }
        console_manager_.store(manager, std::memory_order_release);
        logger_.Info("captured native console command manager");
        if (!observer_enabled_) {
            return;
        }
        if (observer_console_ready_ && observer_command_manager_ == manager) {
            return;
        }
        if (observer_command_manager_ != nullptr && observer_command_manager_ != manager) {
            // The old manager is no longer active. Keep its command metadata in
            // process-lifetime storage so late callbacks cannot reference
            // destroyed data.
            native_tag_command_ = nullptr;
            native_tag_handler_ = nullptr;
            observer_switch_command_ = nullptr;
            observer_console_ready_ = false;
        }
        observer_command_manager_ = manager;
        native_tag_command_ = manager->FindCommand("tag");
        const auto expected_handler = smedley::memory::Map::base_addr + 0x1f720;
        if (native_tag_command_ == nullptr
            || reinterpret_cast<uintptr_t>(native_tag_command_->handler) != expected_handler) {
            logger_.Failure("native tag command is unavailable; safe observer switching disabled");
            return;
        }
        native_tag_handler_ = native_tag_command_->handler;
        native_tag_command_->handler = &CampaignLauncher::RejectNativeTag;
        if (manager->FindCommand("switch") != nullptr) {
            logger_.Failure("console command switch already exists; safe observer switching disabled");
            native_tag_command_->handler = native_tag_handler_;
            return;
        }
        observer_switch_command_ = new smedley::v2::CConsoleCmd::SCommandData{};
        observer_switch_command_->is_allowed = true;
        observer_switch_command_->name = "switch";
        observer_switch_command_->description = "Change observer view while preserving country AI.";
        observer_switch_command_->handler = &CampaignLauncher::HandleObserverSwitch;
        observer_switch_command_->num_args = 1;
        observer_switch_command_->args[0] = "TAG";
        manager->commands().push_back(observer_switch_command_);
        observer_console_ready_ = true;
        logger_.Info("registered observer-safe switch command and disabled native tag");
    }

    void CampaignLauncher::PrepareObserverForAnnexation(int annexed_ordinal)
    {
        auto *game_state = smedley::v2::CCurrentGameState::instance();
        if (!observer_enabled_ || !observer_monitoring_ || observer_view_switch_pending_
            || game_state == nullptr || annexed_ordinal != game_state->player_tag().ordinal()) return;

        smedley::v2::CCountry *target = nullptr;
        for (size_t ordinal = 1; ordinal < game_state->country_count(); ++ordinal) {
            auto *candidate = game_state->country(static_cast<int>(ordinal));
            if (static_cast<int>(ordinal) != annexed_ordinal && candidate != nullptr && candidate->exists()
                && game_state->player_control_state(static_cast<int>(ordinal)) == 0 && candidate->ai() != nullptr
                && game_state->is_scheduled_ai(candidate->ai())) {
                target = candidate;
                break;
            }
        }
        if (target == nullptr) {
            logger_.Failure("observer could not select a safe view before country annexation");
            return;
        }

        const int target_ordinal = target->tag().ordinal();
        const size_t ai_count_before = game_state->country_ai_count();
        game_state->set_observer_view_tag(target->tag());
        if (game_state->player_tag().ordinal() != target_ordinal || game_state->has_human_controlled_country()
            || game_state->player_control_state(target_ordinal) != 0 || target->ai() == nullptr
            || !game_state->is_scheduled_ai(target->ai()) || game_state->country_ai_count() != ai_count_before) {
            logger_.Failure("observer pre-annexation view handoff violated AI ownership state");
            return;
        }
        logger_.Info(std::string("observer view moved to ") + target->tag().str() + " before annexation");
    }

    smedley::v2::CConsoleCmd::SResult CampaignLauncher::RejectNativeTag(
        const smedley::sstd::vector<smedley::sstd::string> &)
    {
        const std::lock_guard<std::recursive_mutex> lock(launcher_callback_mutex);
        if (launcher_instance != nullptr) {
            launcher_instance->logger_.Warn("blocked native tag command in observer mode");
        }
        return smedley::v2::CConsoleCmd::SResult("tag disabled; use switch TAG", false);
    }

    smedley::v2::CConsoleCmd::SResult CampaignLauncher::HandleObserverSwitch(
        const smedley::sstd::vector<smedley::sstd::string> &arguments)
    {
        const std::lock_guard<std::recursive_mutex> lock(launcher_callback_mutex);
        if (launcher_instance == nullptr) {
            return smedley::v2::CConsoleCmd::SResult("observer unavailable", false);
        }
        return launcher_instance->RequestObserverSwitch(arguments);
    }

    smedley::v2::CConsoleCmd::SResult CampaignLauncher::RequestObserverSwitch(
        const smedley::sstd::vector<smedley::sstd::string> &arguments)
    {
        if (!observer_enabled_ || !observer_monitoring_) {
            return smedley::v2::CConsoleCmd::SResult("observer not ready", false);
        }
        if (observer_view_switch_pending_) {
            return smedley::v2::CConsoleCmd::SResult("switch already pending", false);
        }
        if (arguments.size() != 1) {
            return smedley::v2::CConsoleCmd::SResult("usage: switch TAG", false);
        }
        std::string requested_tag = arguments[0].c_str();
        if (requested_tag.size() != 3) {
            return smedley::v2::CConsoleCmd::SResult("TAG must be 3 ASCII alphanumeric characters", false);
        }
        for (auto &character : requested_tag) {
            if (!((character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z')
                  || (character >= '0' && character <= '9'))) {
                return smedley::v2::CConsoleCmd::SResult("TAG must be 3 ASCII alphanumeric characters", false);
            }
            if (character >= 'a' && character <= 'z') {
                character = character - 'a' + 'A';
            }
        }

        auto *game_state = smedley::v2::CCurrentGameState::instance();
        auto *idler = game_state == nullptr ? nullptr : game_state->idler();
        if (!IsInGameIdler(idler)) {
            return smedley::v2::CConsoleCmd::SResult("campaign unavailable", false);
        }
        smedley::v2::CCountry *target = nullptr;
        for (size_t ordinal = 1; ordinal < game_state->country_count(); ++ordinal) {
            auto *candidate = game_state->country(static_cast<int>(ordinal));
            if (candidate != nullptr
                && std::strcmp(candidate->tag().str(), requested_tag.c_str()) == 0) {
                target = candidate;
                break;
            }
        }
        if (target == nullptr || !target->exists()) {
            return smedley::v2::CConsoleCmd::SResult("country does not exist", false);
        }
        if (target->tag().ordinal() == game_state->player_tag().ordinal()) {
            return smedley::v2::CConsoleCmd::SResult("already viewing country");
        }
        if (game_state->player_control_state(target->tag().ordinal()) != 0
            || target->ai() == nullptr
            || !game_state->is_scheduled_ai(target->ai())) {
            return smedley::v2::CConsoleCmd::SResult("target AI is not healthy", false);
        }

        auto pause_state = idler->pause_state();
        const bool paused_by_command = pause_state == 0;
        if (pause_state == 0) {
            idler->TogglePause();
            pause_state = idler->pause_state();
        }
        if (pause_state != 1) {
            return smedley::v2::CConsoleCmd::SResult("failed to pause", false);
        }

        observer_target_ordinal_ = target->tag().ordinal();
        observer_target_tag_ = requested_tag;
        observer_ai_count_before_switch_ = game_state->country_ai_count();
        observer_view_switch_pending_ = true;
        observer_attempts_ = 0;
        SingleConsoleArgument native_arguments(requested_tag);
        if (!native_arguments.valid()) {
            observer_view_switch_pending_ = false;
            return smedley::v2::CConsoleCmd::SResult("TAG exceeds the native inline-string limit", false);
        }
        const auto result = native_tag_handler_(native_arguments);
        if (!result.success) {
            observer_view_switch_pending_ = false;
            observer_target_ordinal_ = 0;
            observer_target_tag_.clear();
            if (paused_by_command) {
                idler->TogglePause();
            }
            return result;
        }
        logger_.Info(std::string("requested observer-safe switch to ") + requested_tag);
        return smedley::v2::CConsoleCmd::SResult("observer switch queued");
    }

    bool CampaignLauncher::ScheduleTimer(UINT delay, const char *failure_message)
    {
        if (save_timer_ != 0) {
            KillTimer(nullptr, save_timer_);
            save_timer_ = 0;
        }
        save_timer_ = SetTimer(nullptr, 0, delay, SaveTimerCallback);
        if (save_timer_ != 0) {
            return true;
        }
        logger_.Failure(failure_message);
        return false;
    }

    void CampaignLauncher::CaptureFrontendController(void *controller)
    {
        if (controller == nullptr) {
            return;
        }
        const auto previous = frontend_controller_.exchange(controller, std::memory_order_acq_rel);
        frontend_thread_id_.store(GetCurrentThreadId(), std::memory_order_release);
        if (previous == controller) {
            return;
        }
        std::ostringstream message;
        message << "captured frontend controller=" << controller;
        logger_.Info(message.str());
        if (save_timer_ != 0 || save_attempts_ != 0) {
            return;
        }
        if (ScheduleTimer(10'000, "failed to schedule save loading on the frontend thread")) {
            logger_.Info("scheduled save loading on the frontend thread");
        }
    }

    void CampaignLauncher::CaptureMainMenuController(void *controller)
    {
        if (controller == nullptr) {
            return;
        }
        main_menu_controller_.store(controller, std::memory_order_release);
        main_menu_thread_id_.store(GetCurrentThreadId(), std::memory_order_release);
    }

    void CampaignLauncher::ReleaseFrontendController(void *controller)
    {
        void *expected = controller;
        frontend_controller_.compare_exchange_strong(
            expected,
            nullptr,
            std::memory_order_acq_rel,
            std::memory_order_relaxed);
    }

    void CampaignLauncher::ReleaseMainMenuController(void *controller)
    {
        void *expected = controller;
        main_menu_controller_.compare_exchange_strong(
            expected,
            nullptr,
            std::memory_order_acq_rel,
            std::memory_order_relaxed);
    }

    bool CampaignLauncher::CheckSignatures() const
    {
        const auto load_save = smedley::memory::Map::base_addr + 0x27f1d0;
        constexpr unsigned char load_save_expected[] = {0x55, 0x8b, 0xec, 0x6a, 0xff};
        const auto press_dispatch = smedley::memory::Map::base_addr + 0x5ee510;
        constexpr unsigned char press_expected[] = {
            0x56, 0x8b, 0x70, 0x04, 0x85, 0xf6, 0x74, 0x10, 0x8b, 0x0e};
        const auto release_dispatch = smedley::memory::Map::base_addr + 0x5ee550;
        constexpr unsigned char release_expected[] = {
            0x56, 0x8b, 0x70, 0x04, 0x85, 0xf6, 0x74, 0x10, 0x8b, 0x0e};
        const auto toggle_pause = smedley::memory::Map::base_addr + 0x26a2c0;
        constexpr unsigned char toggle_pause_expected[] = {
            0x55, 0x8b, 0xec, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00};
        const auto return_country_to_ai = smedley::memory::Map::base_addr + 0x287a70;
        constexpr unsigned char return_country_to_ai_expected[] = {
            0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00};
        const auto console_command_handler = smedley::memory::Map::base_addr + 0x20eb0;
        constexpr unsigned char console_command_handler_expected[] = {
            0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8, 0x6a, 0xff};
        // ASLR relocates the leading global pointer, so validate the invariant
        // handler body instead.
        const auto speed_up_handler_body = smedley::memory::Map::base_addr + 0x32ee96;
        constexpr unsigned char speed_up_handler_body_expected[] = {
            0x8b, 0x81, 0x28, 0x0b, 0x00, 0x00, 0x40, 0x83, 0xf8, 0x04};
        const auto speed_down_handler_body = smedley::memory::Map::base_addr + 0x32efe6;
        constexpr unsigned char speed_down_handler_body_expected[] = {
            0x8b, 0x81, 0x28, 0x0b, 0x00, 0x00, 0x48, 0x83, 0xf8, 0x04};
        const auto tag_handler = smedley::memory::Map::base_addr + 0x1f720;
        constexpr unsigned char tag_handler_expected[] = {0x55, 0x8b, 0xec, 0x6a, 0xff};
        const auto request_quit = smedley::memory::Map::base_addr + 0x24edb0;
        constexpr unsigned char request_quit_expected[] = {0xc6, 0x81, 0x20, 0x1d, 0x00, 0x00, 0x01, 0xc3};
        if (std::memcmp(reinterpret_cast<const void *>(load_save), load_save_expected, sizeof(load_save_expected)) != 0
            || std::memcmp(reinterpret_cast<const void *>(press_dispatch), press_expected, sizeof(press_expected)) != 0
            || std::memcmp(reinterpret_cast<const void *>(release_dispatch), release_expected, sizeof(release_expected)) != 0
            || std::memcmp(reinterpret_cast<const void *>(toggle_pause), toggle_pause_expected, sizeof(toggle_pause_expected)) != 0
            || std::memcmp(
                reinterpret_cast<const void *>(return_country_to_ai),
                return_country_to_ai_expected,
                sizeof(return_country_to_ai_expected)) != 0
            || std::memcmp(
                reinterpret_cast<const void *>(console_command_handler),
                console_command_handler_expected,
                sizeof(console_command_handler_expected)) != 0
            || std::memcmp(
                reinterpret_cast<const void *>(speed_up_handler_body),
                speed_up_handler_body_expected,
                sizeof(speed_up_handler_body_expected)) != 0
            || std::memcmp(
                reinterpret_cast<const void *>(speed_down_handler_body),
                speed_down_handler_body_expected,
                sizeof(speed_down_handler_body_expected)) != 0
            || std::memcmp(
                reinterpret_cast<const void *>(tag_handler),
                tag_handler_expected,
                sizeof(tag_handler_expected)) != 0
            || std::memcmp(
                reinterpret_cast<const void *>(request_quit),
                request_quit_expected,
                sizeof(request_quit_expected)) != 0) {
            logger_.Failure("campaign automation signature mismatch; save loading disabled");
            return false;
        }
        return true;
    }

    bool CampaignLauncher::SelectSpeed(smedley::v2::CCurrentGameState *game_state)
    {
        int speed = game_state->speed_index();
        const int previous_speed = speed + 1;
        const int target_index = target_speed_ - 1;
        if (speed < 0 || speed > 4) {
            logger_.Failure("native speed index is outside the supported range");
            return false;
        }
        using SpeedFn = void (__cdecl *)();
        const auto change_speed = reinterpret_cast<SpeedFn>(
            smedley::memory::Map::base_addr + (speed < target_index ? 0x32ee90 : 0x32efe0));
        while (speed != target_index) {
            const int expected = speed + (speed < target_index ? 1 : -1);
            change_speed();
            const int actual = game_state->speed_index();
            if (actual != expected) {
                logger_.Failure("native speed handler did not produce the requested speed index");
                return false;
            }
            speed = actual;
        }
        ReportTelemetryResult(telemetry_.SpeedConfigured(previous_speed, speed + 1, target_speed_));
        logger_.Info(std::string("selected native speed ") + std::to_string(target_speed_));
        return true;
    }

    void CampaignLauncher::ReportTelemetryResult(SmedleyTelemetryResult result)
    {
        if (result == SMEDLEY_TELEMETRY_INVALID && !telemetry_invalid_logged_) {
            telemetry_invalid_logged_ = true;
            logger_.Warn("campaign lifecycle telemetry rejected an invalid ABI record");
        } else if (result == SMEDLEY_TELEMETRY_DROPPED && !telemetry_dropped_logged_) {
            telemetry_dropped_logged_ = true;
            logger_.Warn("campaign lifecycle telemetry queue dropped a record");
        }
    }

    bool CampaignLauncher::ObserverInvariantsValid(smedley::v2::CCurrentGameState *game_state) const
    {
        if (!observer_ai_ready_ || observer_view_switch_pending_ || game_state == nullptr || game_state->has_human_controlled_country()
            || *reinterpret_cast<unsigned char *>(smedley::memory::Map::base_addr + 0xb092fb) != 0) return false;
        const auto ordinal = game_state->player_tag().ordinal();
        const auto *country = ordinal <= 0 ? nullptr : game_state->country(ordinal);
        if (country == nullptr || !country->exists() || game_state->player_control_state(ordinal) != 0
            || country->ai() == nullptr || !game_state->is_scheduled_ai(country->ai())) return false;
        const char *tag = country->tag().str();
        if (tag == nullptr || std::strlen(tag) != 3 || !std::all_of(tag, tag + 3, [](unsigned char character) {
            return (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9');
        })) return false;
        return true;
    }

    bool CampaignLauncher::EmitObserverConfiguredIfReady(smedley::v2::CCurrentGameState *game_state)
    {
        if (!ObserverInvariantsValid(game_state)) return false;
        const auto ordinal = game_state->player_tag().ordinal();
        const auto *country = game_state->country(ordinal);
        const char *tag = country->tag().str();
        ReportTelemetryResult(telemetry_.ObserverConfigured(tag));
        return true;
    }

    bool CampaignLauncher::InstallControllerHooks()
    {
        const auto frontend_constructor = smedley::memory::Map::base_addr + 0x36a2f0;
        constexpr unsigned char frontend_expected[] = {0x55, 0x8b, 0xec, 0x6a, 0xff};
        const auto main_menu_constructor = smedley::memory::Map::base_addr + 0x354a00;
        constexpr unsigned char main_menu_expected[] = {0x55, 0x8b, 0xec, 0x6a, 0xff};
        const auto frontend_destructor = smedley::memory::Map::base_addr + 0x36b030;
        constexpr unsigned char frontend_destructor_expected[] = {0x55, 0x8b, 0xec, 0x56, 0x8b, 0xf1};
        const auto main_menu_destructor = smedley::memory::Map::base_addr + 0x354df0;
        constexpr unsigned char main_menu_destructor_expected[] = {0x55, 0x8b, 0xec, 0x56, 0x8b, 0xf1};
        const auto country_annex = smedley::memory::Map::base_addr + 0x118620;
        constexpr unsigned char country_annex_expected[] = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8};
        const auto message_dispatch = smedley::memory::Map::base_addr + 0x2bc68;
        constexpr unsigned char message_dispatch_expected[] = {0x80, 0x7f, 0x0e, 0x00, 0x75, 0x23};
        constexpr unsigned char message_dispatch_ebx_expected[] = {0x80, 0x7b, 0x0e, 0x00, 0x75, 0x23};
        const auto message_dispatch_2 = smedley::memory::Map::base_addr + 0x934f8;
        const auto message_dispatch_3 = smedley::memory::Map::base_addr + 0xe9678;
        const auto message_dispatch_4 = smedley::memory::Map::base_addr + 0x149c68;
        const auto message_dispatch_5 = smedley::memory::Map::base_addr + 0x1abaa8;
        const auto message_dispatch_6 = smedley::memory::Map::base_addr + 0x32dfb8;
        const auto message_dispatch_7 = smedley::memory::Map::base_addr + 0x507038;
        const auto message_dispatch_8 = smedley::memory::Map::base_addr + 0x509168;
        const auto message_dispatch_9 = smedley::memory::Map::base_addr + 0x53d818;
        constexpr unsigned char message_suppressed_expected[] = {0x8b, 0x5d, 0x10, 0xeb, 0x89};
        constexpr unsigned char message_suppressed_ebx_expected[] = {0x80, 0x7b, 0x11, 0x00, 0x0f, 0x84};
        const auto message_suppressed_1 = smedley::memory::Map::base_addr + 0x2be42;
        const auto message_suppressed_2 = smedley::memory::Map::base_addr + 0x93663;
        const auto message_suppressed_3 = smedley::memory::Map::base_addr + 0xe9852;
        const auto message_suppressed_4 = smedley::memory::Map::base_addr + 0x149e42;
        const auto message_suppressed_5 = smedley::memory::Map::base_addr + 0x1abc13;
        const auto message_suppressed_6 = smedley::memory::Map::base_addr + 0x32e192;
        const auto message_suppressed_7 = smedley::memory::Map::base_addr + 0x507212;
        const auto message_suppressed_8 = smedley::memory::Map::base_addr + 0x509342;
        const auto message_suppressed_9 = smedley::memory::Map::base_addr + 0x53d9f2;
        const bool message_dispatches_match =
            std::memcmp(
                reinterpret_cast<const void *>(message_dispatch),
                message_dispatch_expected,
                sizeof(message_dispatch_expected)) == 0
            && std::memcmp(
                reinterpret_cast<const void *>(message_dispatch_2),
                message_dispatch_ebx_expected,
                sizeof(message_dispatch_ebx_expected)) == 0
            && std::memcmp(
                reinterpret_cast<const void *>(message_dispatch_3),
                message_dispatch_expected,
                sizeof(message_dispatch_expected)) == 0
            && std::memcmp(
                reinterpret_cast<const void *>(message_dispatch_4),
                message_dispatch_expected,
                sizeof(message_dispatch_expected)) == 0
            && std::memcmp(
                reinterpret_cast<const void *>(message_dispatch_5),
                message_dispatch_ebx_expected,
                sizeof(message_dispatch_ebx_expected)) == 0
            && std::memcmp(
                reinterpret_cast<const void *>(message_dispatch_6),
                message_dispatch_expected,
                sizeof(message_dispatch_expected)) == 0
            && std::memcmp(
                reinterpret_cast<const void *>(message_dispatch_7),
                message_dispatch_expected,
                sizeof(message_dispatch_expected)) == 0
            && std::memcmp(
                reinterpret_cast<const void *>(message_dispatch_8),
                message_dispatch_expected,
                sizeof(message_dispatch_expected)) == 0
            && std::memcmp(
                reinterpret_cast<const void *>(message_dispatch_9),
                message_dispatch_expected,
                sizeof(message_dispatch_expected)) == 0
            && std::memcmp(
                reinterpret_cast<const void *>(message_suppressed_1),
                message_suppressed_expected,
                sizeof(message_suppressed_expected)) == 0
            && std::memcmp(
                reinterpret_cast<const void *>(message_suppressed_2),
                message_suppressed_ebx_expected,
                sizeof(message_suppressed_ebx_expected)) == 0
            && std::memcmp(
                reinterpret_cast<const void *>(message_suppressed_3),
                message_suppressed_expected,
                sizeof(message_suppressed_expected)) == 0
            && std::memcmp(
                reinterpret_cast<const void *>(message_suppressed_4),
                message_suppressed_expected,
                sizeof(message_suppressed_expected)) == 0
            && std::memcmp(
                reinterpret_cast<const void *>(message_suppressed_5),
                message_suppressed_ebx_expected,
                sizeof(message_suppressed_ebx_expected)) == 0
            && std::memcmp(
                reinterpret_cast<const void *>(message_suppressed_6),
                message_suppressed_expected,
                sizeof(message_suppressed_expected)) == 0
            && std::memcmp(
                reinterpret_cast<const void *>(message_suppressed_7),
                message_suppressed_expected,
                sizeof(message_suppressed_expected)) == 0
            && std::memcmp(
                reinterpret_cast<const void *>(message_suppressed_8),
                message_suppressed_expected,
                sizeof(message_suppressed_expected)) == 0
            && std::memcmp(
                reinterpret_cast<const void *>(message_suppressed_9),
                message_suppressed_expected,
                sizeof(message_suppressed_expected)) == 0;
        if (std::memcmp(
                reinterpret_cast<const void *>(frontend_constructor),
                frontend_expected,
                sizeof(frontend_expected)) != 0
            || std::memcmp(
                reinterpret_cast<const void *>(main_menu_constructor),
                main_menu_expected,
                sizeof(main_menu_expected)) != 0
            || std::memcmp(
                reinterpret_cast<const void *>(frontend_destructor),
                frontend_destructor_expected,
                sizeof(frontend_destructor_expected)) != 0
            || std::memcmp(
                reinterpret_cast<const void *>(main_menu_destructor),
                main_menu_destructor_expected,
                sizeof(main_menu_destructor_expected)) != 0
            || std::memcmp(
                reinterpret_cast<const void *>(country_annex),
                country_annex_expected,
                sizeof(country_annex_expected)) != 0
            || !message_dispatches_match) {
            logger_.Failure("frontend constructor signature mismatch; save loading disabled");
            return false;
        }
        frontend_constructor_return_address = frontend_constructor + sizeof(frontend_expected);
        main_menu_return_address = main_menu_constructor + sizeof(main_menu_expected);
        frontend_destructor_return_address = frontend_destructor + sizeof(frontend_destructor_expected);
        main_menu_destructor_return_address = main_menu_destructor + sizeof(main_menu_destructor_expected);
        country_annex_return_address = country_annex + sizeof(country_annex_expected);
        message_dispatch_return_address = message_dispatch + sizeof(message_dispatch_expected);
        message_dispatch_popup_address = smedley::memory::Map::base_addr + 0x2bc91;
        message_dispatch_suppressed_address = message_suppressed_1;
        message_dispatch_2_return_address = message_dispatch_2 + sizeof(message_dispatch_ebx_expected);
        message_dispatch_2_popup_address = smedley::memory::Map::base_addr + 0x93521;
        message_dispatch_2_suppressed_address = message_suppressed_2;
        message_dispatch_3_return_address = message_dispatch_3 + sizeof(message_dispatch_expected);
        message_dispatch_3_popup_address = smedley::memory::Map::base_addr + 0xe96a1;
        message_dispatch_3_suppressed_address = message_suppressed_3;
        message_dispatch_4_return_address = message_dispatch_4 + sizeof(message_dispatch_expected);
        message_dispatch_4_popup_address = smedley::memory::Map::base_addr + 0x149c91;
        message_dispatch_4_suppressed_address = message_suppressed_4;
        message_dispatch_5_return_address = message_dispatch_5 + sizeof(message_dispatch_ebx_expected);
        message_dispatch_5_popup_address = smedley::memory::Map::base_addr + 0x1abad1;
        message_dispatch_5_suppressed_address = message_suppressed_5;
        message_dispatch_6_return_address = message_dispatch_6 + sizeof(message_dispatch_expected);
        message_dispatch_6_popup_address = smedley::memory::Map::base_addr + 0x32dfe1;
        message_dispatch_6_suppressed_address = message_suppressed_6;
        message_dispatch_7_return_address = message_dispatch_7 + sizeof(message_dispatch_expected);
        message_dispatch_7_popup_address = smedley::memory::Map::base_addr + 0x507061;
        message_dispatch_7_suppressed_address = message_suppressed_7;
        message_dispatch_8_return_address = message_dispatch_8 + sizeof(message_dispatch_expected);
        message_dispatch_8_popup_address = smedley::memory::Map::base_addr + 0x509191;
        message_dispatch_8_suppressed_address = message_suppressed_8;
        message_dispatch_9_return_address = message_dispatch_9 + sizeof(message_dispatch_expected);
        message_dispatch_9_popup_address = smedley::memory::Map::base_addr + 0x53d841;
        message_dispatch_9_suppressed_address = message_suppressed_9;
        std::vector<std::pair<uintptr_t, std::vector<uint8_t>>> installed_hooks;
        const auto install = [&installed_hooks](uintptr_t address, void *trampoline, size_t size) {
            std::vector<uint8_t> original;
            smedley::memory::Hook(address, trampoline, static_cast<int>(size), &original);
            installed_hooks.emplace_back(address, std::move(original));
        };
        try {
            installed_hooks.reserve(14);
            install(frontend_constructor, reinterpret_cast<void *>(&FrontendConstructorTrampoline), sizeof(frontend_expected));
            install(main_menu_constructor, reinterpret_cast<void *>(&MainMenuTrampoline), sizeof(main_menu_expected));
            install(frontend_destructor, reinterpret_cast<void *>(&FrontendDestructorTrampoline), sizeof(frontend_destructor_expected));
            install(main_menu_destructor, reinterpret_cast<void *>(&MainMenuDestructorTrampoline), sizeof(main_menu_destructor_expected));
            install(country_annex, reinterpret_cast<void *>(&CountryAnnexTrampoline), sizeof(country_annex_expected));
            install(message_dispatch, reinterpret_cast<void *>(&MessageDispatchTrampoline), sizeof(message_dispatch_expected));
            install(message_dispatch_2, reinterpret_cast<void *>(&MessageDispatch2Trampoline), sizeof(message_dispatch_ebx_expected));
            install(message_dispatch_3, reinterpret_cast<void *>(&MessageDispatch3Trampoline), sizeof(message_dispatch_expected));
            install(message_dispatch_4, reinterpret_cast<void *>(&MessageDispatch4Trampoline), sizeof(message_dispatch_expected));
            install(message_dispatch_5, reinterpret_cast<void *>(&MessageDispatch5Trampoline), sizeof(message_dispatch_ebx_expected));
            install(message_dispatch_6, reinterpret_cast<void *>(&MessageDispatch6Trampoline), sizeof(message_dispatch_expected));
            install(message_dispatch_7, reinterpret_cast<void *>(&MessageDispatch7Trampoline), sizeof(message_dispatch_expected));
            install(message_dispatch_8, reinterpret_cast<void *>(&MessageDispatch8Trampoline), sizeof(message_dispatch_expected));
            install(message_dispatch_9, reinterpret_cast<void *>(&MessageDispatch9Trampoline), sizeof(message_dispatch_expected));
        } catch (const std::exception &error) {
            bool restored = true;
            for (auto hook = installed_hooks.rbegin(); hook != installed_hooks.rend(); ++hook) {
                restored = smedley::memory::RestoreHook(hook->first, hook->second) && restored;
            }
            if (!restored) {
                throw std::runtime_error(std::string("campaign automation hook installation and rollback failed: ")
                                         + error.what());
            }
            logger_.Failure(std::string("campaign automation hook installation failed: ") + error.what());
            return false;
        }
        return true;
    }

    void CampaignLauncher::StartBenchmark(smedley::v2::CCurrentGameState *game_state, smedley::v2::CInGameIdler *idler)
    {
        if (!run_condition_.requested() || benchmark_started_ || game_state == nullptr || idler == nullptr) return;
        if (observer_enabled_ && !EmitObserverConfiguredIfReady(game_state)) return;
        const char *error = nullptr;
        const auto process_metrics = SampleProcessMetrics();
        if (!benchmark_.Begin(game_state->current_date_raw(), run_condition_.days, run_condition_.target_date_raw,
                              run_condition_.timeout_seconds, MonotonicMicroseconds(), &error)) {
            logger_.Failure(std::string("benchmark did not start: ") + (error == nullptr ? "invalid target" : error));
            FinishInvalidBenchmark(game_state, idler);
            return;
        }
        benchmark_started_ = true;
        benchmark_process_cpu_start_us_ = process_metrics.process_cpu_us;
        benchmark_working_set_start_bytes_ = process_metrics.working_set_bytes;
        benchmark_private_bytes_start_ = process_metrics.private_bytes;
        ReportTelemetryResult(telemetry_.BenchmarkStarted(benchmark_.start_date_raw(), benchmark_.target_date_raw(),
                                                          benchmark_.requested_days(), run_condition_.timeout_seconds));
        logger_.Info("benchmark started at raw date " + std::to_string(benchmark_.start_date_raw())
                     + " target=" + std::to_string(benchmark_.target_date_raw()));
        if (save_timer_ != 0) {
            KillTimer(nullptr, save_timer_);
            save_timer_ = 0;
        }
        if (!ScheduleTimer(USER_TIMER_MINIMUM, "failed to schedule benchmark timer")) {
            const int actual = game_state->current_date_raw();
            if (idler->pause_state() == 0) idler->TogglePause();
            const int pause_state = idler->pause_state();
            FinishBenchmark("timer_unavailable", actual, pause_state == 0 || pause_state == 1
                ? std::optional<bool>(pause_state == 1) : std::nullopt);
        }
    }

    void CampaignLauncher::FinishInvalidBenchmark(smedley::v2::CCurrentGameState *game_state, smedley::v2::CInGameIdler *idler)
    {
        if (benchmark_terminal_) return;
        benchmark_terminal_ = true;
        if (save_timer_ != 0) {
            KillTimer(nullptr, save_timer_);
            save_timer_ = 0;
        }
        observer_monitoring_ = false;
        suppress_message_popups = false;
        const int actual = game_state->current_date_raw();
        if (idler->pause_state() == 0) idler->TogglePause();
        const int pause_state = idler->pause_state();
        const std::optional<bool> paused = pause_state == 0 || pause_state == 1
            ? std::optional<bool>(pause_state == 1) : std::nullopt;
        const int target = run_condition_.target_date_raw.value_or(actual);
        ReportTelemetryResult(telemetry_.BenchmarkFailed(actual, target, actual, 1, "invalid_target", paused));
        logger_.Failure(std::string("benchmark failed: invalid_target; campaign remains ")
            + (paused && *paused ? "paused and open" : "open; pause state is unverified"));
    }

    void CampaignLauncher::FinishBenchmark(const char *reason, std::optional<int> actual_date_raw, std::optional<bool> paused)
    {
        if (benchmark_terminal_) return;
        benchmark_terminal_ = true;
        if (save_timer_ != 0) {
            KillTimer(nullptr, save_timer_);
            save_timer_ = 0;
        }
        observer_monitoring_ = false;
        suppress_message_popups = false;
        const auto process_metrics = SampleProcessMetrics();
        const uint64_t now = MonotonicMicroseconds();
        const int64_t elapsed = (std::max)(int64_t{1}, now >= benchmark_.start_monotonic_us()
            ? static_cast<int64_t>(now - benchmark_.start_monotonic_us()) : int64_t{0});
        std::optional<int64_t> process_cpu_us;
        if (benchmark_process_cpu_start_us_ && process_metrics.process_cpu_us
            && *process_metrics.process_cpu_us >= *benchmark_process_cpu_start_us_) {
            process_cpu_us = *process_metrics.process_cpu_us - *benchmark_process_cpu_start_us_;
        }
        ReportTelemetryResult(telemetry_.BenchmarkResources(actual_date_raw, process_cpu_us,
            benchmark_working_set_start_bytes_, process_metrics.working_set_bytes,
            benchmark_private_bytes_start_, process_metrics.private_bytes,
            process_metrics.process_peak_working_set_bytes));
        if (reason == nullptr) {
            ReportTelemetryResult(telemetry_.BenchmarkCompleted(benchmark_.start_date_raw(), benchmark_.target_date_raw(),
                actual_date_raw.value_or(benchmark_.target_date_raw()), benchmark_.requested_days(), elapsed));
            if (quit_after_run_ && actual_date_raw && *actual_date_raw == benchmark_.target_date_raw()) {
                QuitAfterRun();
            } else {
                logger_.Info("benchmark completed; campaign remains paused and open");
            }
        } else {
            ReportTelemetryResult(telemetry_.BenchmarkFailed(benchmark_.start_date_raw(), benchmark_.target_date_raw(),
                actual_date_raw, elapsed, reason, paused));
            logger_.Failure(std::string("benchmark failed: ") + reason + "; campaign remains open");
        }
    }

    bool CampaignLauncher::DrainTelemetryBeforeQuit()
    {
        constexpr uint32_t drain_timeout_ms = 5000;
        const auto result = telemetry_.Drain(drain_timeout_ms);
        if (TelemetryDrainAllowsQuit(result)) {
            logger_.Info(result == SMEDLEY_TELEMETRY_DRAIN_COMPLETED
                ? "telemetry drained before native game exit"
                : "telemetry drain is unavailable; continuing native game exit");
            return true;
        }
        const char *reason = result == SMEDLEY_TELEMETRY_DRAIN_BUSY ? "busy"
            : result == SMEDLEY_TELEMETRY_DRAIN_TIMEOUT ? "timeout" : "failure";
        logger_.Failure(std::string("telemetry pre-exit drain ended with ") + reason + "; campaign remains paused and open");
        return false;
    }

    void CampaignLauncher::QuitAfterRun()
    {
        const auto *game_state = smedley::v2::CCurrentGameState::instance();
        auto *idler = game_state == nullptr ? nullptr : game_state->idler();
        if (!IsInGameIdler(idler)) {
            logger_.Failure("native quit request failed because CInGameIdler is unavailable; campaign remains paused and open");
            return;
        }
        const auto vtable = *reinterpret_cast<uintptr_t **>(idler);
        const auto request_quit = vtable[0x110 / sizeof(uintptr_t)];
        if (request_quit != smedley::memory::Map::base_addr + 0x24edb0) {
            logger_.Failure("native quit virtual method mismatch; campaign remains paused and open");
            return;
        }
        // The paused UI-thread call cannot transition away from this validated idler while the synchronous drain waits.
        if (!DrainTelemetryBeforeQuit()) return;
        using RequestQuit = void (__thiscall *)(void *);
        reinterpret_cast<RequestQuit>(request_quit)(idler);
        if (*(reinterpret_cast<const unsigned char *>(idler) + 0x1d20) != 1) {
            logger_.Failure("native quit request did not set the expected state; campaign remains paused and open");
            return;
        }
        logger_.Info("requested native game exit after successful bounded run");
    }

    bool CampaignLauncher::TickBenchmark(smedley::v2::CCurrentGameState *game_state, smedley::v2::CInGameIdler *idler)
    {
        if (!benchmark_started_ || benchmark_terminal_) return benchmark_terminal_;
        const bool observer_valid = !observer_enabled_ || ObserverInvariantsValid(game_state);
        const BenchmarkDecision decision = benchmark_.Observe({true, game_state->current_date_raw(), idler->pause_state(),
                                                                observer_valid, MonotonicMicroseconds()});
        if (decision.action == BenchmarkAction::Continue) return false;
        if (idler->pause_state() == 0) idler->TogglePause();
        const int pause_state = idler->pause_state();
        const int actual = game_state->current_date_raw();
        const std::optional<bool> paused = pause_state == 0 || pause_state == 1
            ? std::optional<bool>(pause_state == 1) : std::nullopt;
        if (!paused || !*paused) {
            FinishBenchmark("pause_failed", actual, paused);
        } else if (decision.action == BenchmarkAction::Complete && actual == benchmark_.target_date_raw()) {
            FinishBenchmark(nullptr, actual, true);
        } else if (decision.action == BenchmarkAction::Complete) {
            FinishBenchmark(actual < benchmark_.target_date_raw() ? "date_regressed" : "date_overshoot", actual, true);
        } else {
            FinishBenchmark(decision.reason, actual, true);
        }
        return true;
    }

    void CALLBACK CampaignLauncher::SaveTimerCallback(HWND, UINT, UINT_PTR timer, DWORD) noexcept
    {
        try {
        const std::lock_guard<std::recursive_mutex> lock(launcher_callback_mutex);
        auto *launcher = launcher_instance;
        if (launcher == nullptr || timer != launcher->save_timer_) {
            return;
        }
        const DWORD frontend_thread = launcher->frontend_thread_id_.load(std::memory_order_acquire);
        if (frontend_thread == 0 || frontend_thread != GetCurrentThreadId()) {
            KillTimer(nullptr, timer);
            launcher->save_timer_ = 0;
            launcher->logger_.Failure("campaign automation left the captured frontend thread");
            return;
        }
        if (!launcher->observer_monitoring_ && !launcher->benchmark_.active()) {
            KillTimer(nullptr, timer);
            launcher->save_timer_ = 0;
        }
        if (launcher->play_requested_) {
            auto *game_state = smedley::v2::CCurrentGameState::instance();
            auto *idler = game_state == nullptr ? nullptr : game_state->idler();
            if (!IsInGameIdler(idler)) {
                if (launcher->benchmark_.active()) {
                    launcher->benchmark_.Observe({false, std::nullopt, -1, false, MonotonicMicroseconds()});
                    launcher->FinishBenchmark("idler_unavailable", std::nullopt, std::nullopt);
                    return;
                }
                if (launcher->observer_monitoring_) {
                    KillTimer(nullptr, timer);
                    launcher->save_timer_ = 0;
                    suppress_message_popups = false;
                    launcher->observer_monitoring_ = false;
                    launcher->logger_.Failure("observer campaign left CInGameIdler");
                    return;
                }
                ++launcher->campaign_attempts_;
                if (launcher->campaign_attempts_ < 30) {
                    launcher->ScheduleTimer(1'000, "failed to schedule campaign-entry check");
                } else {
                    launcher->logger_.Failure("campaign did not enter CInGameIdler within 30 seconds");
                }
                return;
            }
            auto pause_state = idler->pause_state();
            if (!launcher->pause_before_configuration_) launcher->pause_before_configuration_ = pause_state == 1;
            launcher->ReportTelemetryResult(launcher->telemetry_.Entered(
                launcher->observer_enabled_, launcher->target_speed_, launcher->start_paused_));
            const bool observer_recovery_pending = launcher->observer_monitoring_ && pause_state == 1;
            if (launcher->benchmark_.active() && !observer_recovery_pending
                && launcher->TickBenchmark(game_state, idler)) return;
            if (launcher->observer_monitoring_) {
                if (launcher->benchmark_.active()) {
                    const uint64_t now = MonotonicMicroseconds();
                    if (now < launcher->next_observer_watchdog_us_) return;
                    launcher->next_observer_watchdog_us_ = now + 1000000ull;
                }
                const auto suppressed = suppressed_message_count;
                if (suppressed != launcher->observed_suppressed_messages_) {
                    std::ostringstream message;
                    message << "observer mode suppressed "
                            << suppressed - launcher->observed_suppressed_messages_
                            << " generic message popup(s), total=" << suppressed;
                    launcher->logger_.Info(message.str());
                    launcher->observed_suppressed_messages_ = suppressed;
                }
                const auto stop_monitoring = [&](const char *reason, bool restore_target) {
                    auto *target = game_state->country(launcher->observer_target_ordinal_);
                    if (restore_target
                        && target != nullptr
                        && target->exists()
                        && game_state->player_control_state(launcher->observer_target_ordinal_) > 0
                        && target->ai() == nullptr) {
                        game_state->ReturnCountryToAI(target->tag());
                    }
                    const bool ownership_clean = !game_state->has_human_controlled_country()
                        && (!restore_target
                            || target == nullptr
                            || (game_state->player_control_state(launcher->observer_target_ordinal_) == 0
                                && target->ai() != nullptr
                                && game_state->is_scheduled_ai(target->ai())));
                    KillTimer(nullptr, timer);
                    launcher->save_timer_ = 0;
                    suppress_message_popups = false;
                    launcher->observer_monitoring_ = false;
                    launcher->observer_view_switch_pending_ = false;
                    launcher->observer_target_ordinal_ = 0;
                    launcher->observer_target_tag_.clear();
                    launcher->logger_.Failure(
                        std::string(reason)
                        + (ownership_clean ? "" : "; observer ownership cleanup failed"));
                };
                if (!launcher->initial_observer_view_tag_.empty()
                    && !launcher->observer_view_switch_pending_) {
                    SingleConsoleArgument arguments(launcher->initial_observer_view_tag_);
                    auto *command_manager = launcher->console_manager_.load(std::memory_order_acquire);
                    const auto result = !arguments.valid()
                        ? smedley::v2::CConsoleCmd::SResult("observer tag exceeds the native inline-string limit", false)
                        : command_manager == nullptr
                        ? smedley::v2::CConsoleCmd::SResult("console unavailable", false)
                        : command_manager->ExecuteCommand("switch", arguments);
                    launcher->initial_observer_view_tag_.clear();
                    if (!result.success) {
                        stop_monitoring("initial observer view switch failed", false);
                    }
                    return;
                }
                const auto view_ordinal = game_state->player_tag().ordinal();
                auto *view_country = game_state->country(view_ordinal);
                if (launcher->observer_view_switch_pending_
                    || view_country == nullptr
                    || !view_country->exists()) {
                    if (pause_state == 0) {
                        idler->TogglePause();
                        pause_state = idler->pause_state();
                    }
                    if (pause_state != 1) {
                        stop_monitoring("failed to pause for observer view failover", false);
                        return;
                    }

                    auto *command_manager = launcher->console_manager_.load(std::memory_order_acquire);
                    if (command_manager == nullptr) {
                        stop_monitoring("native console command manager is unavailable", false);
                        return;
                    }
                    if (!launcher->observer_view_switch_pending_) {
                        smedley::v2::CCountry *target = nullptr;
                        for (size_t ordinal = 1; ordinal < game_state->country_count(); ++ordinal) {
                            auto *candidate = game_state->country(static_cast<int>(ordinal));
                            if (candidate != nullptr
                                && candidate->exists()
                                && game_state->player_control_state(static_cast<int>(ordinal)) == 0
                                && candidate->ai() != nullptr
                                && game_state->is_scheduled_ai(candidate->ai())) {
                                target = candidate;
                                break;
                            }
                        }
                        if (target == nullptr) {
                            stop_monitoring("no living AI country is available for observer view failover", false);
                            return;
                        }
                        const auto expected_handler = smedley::memory::Map::base_addr + 0x1f720;
                        if (launcher->native_tag_handler_ == nullptr
                            || reinterpret_cast<uintptr_t>(launcher->native_tag_handler_) != expected_handler) {
                            stop_monitoring(
                                "native tag command handler does not match the supported executable",
                                false);
                            return;
                        }
                        launcher->observer_target_ordinal_ = target->tag().ordinal();
                        launcher->observer_target_tag_ = target->tag().str();
                        launcher->observer_ai_count_before_switch_ = game_state->country_ai_count();
                        launcher->observer_view_switch_pending_ = true;
                        launcher->observer_attempts_ = 0;
                        SingleConsoleArgument arguments(launcher->observer_target_tag_);
                        if (!arguments.valid()) {
                            launcher->observer_view_switch_pending_ = false;
                            stop_monitoring("observer tag exceeds the native inline-string limit", false);
                            return;
                        }
                        const auto result = launcher->native_tag_handler_(arguments);
                        if (!result.success) {
                            launcher->observer_view_switch_pending_ = false;
                            stop_monitoring("native observer view failover command failed", false);
                            return;
                        }
                        launcher->logger_.Info(
                            std::string("requested observer view failover to ")
                            + launcher->observer_target_tag_);
                        return;
                    }

                    ++launcher->observer_attempts_;
                    auto *target = game_state->country(launcher->observer_target_ordinal_);
                    if (game_state->player_tag().ordinal() != launcher->observer_target_ordinal_) {
                        if (launcher->observer_attempts_ == 30) {
                            launcher->logger_.Warn(
                                "observer view failover is still pending; simulation remains paused");
                        }
                        return;
                    }
                    if (target == nullptr
                        || !target->exists()
                        || game_state->player_control_state(launcher->observer_target_ordinal_) != 1
                        || target->ai() != nullptr
                        || game_state->country_ai_count() + 1 != launcher->observer_ai_count_before_switch_) {
                        stop_monitoring("native tag switch left unexpected observer ownership state", true);
                        return;
                    }
                    game_state->ReturnCountryToAI(target->tag());
                    if (game_state->has_human_controlled_country()
                        || target->ai() == nullptr
                        || !game_state->is_scheduled_ai(target->ai())
                        || game_state->country_ai_count() != launcher->observer_ai_count_before_switch_) {
                        stop_monitoring("observer view failover did not restore target AI", true);
                        return;
                    }
                    launcher->logger_.Info(
                        std::string("observer view failed over to ")
                        + launcher->observer_target_tag_ + " and restored its AI");
                    launcher->observer_view_switch_pending_ = false;
                    launcher->observer_target_ordinal_ = 0;
                    launcher->observer_target_tag_.clear();
                    launcher->observer_attempts_ = 0;
                    idler->TogglePause();
                    if (idler->pause_state() != 0) {
                        stop_monitoring("observer view failover could not resume simulation", false);
                        return;
                    }
                    return;
                }
                if (pause_state == 1) {
                    idler->TogglePause();
                    if (idler->pause_state() != 0) {
                        stop_monitoring("observer simulation could not recover from an unexpected pause", false);
                        return;
                    }
                    launcher->logger_.Warn("observer simulation recovered from an unexpected pause");
                } else if (pause_state != 0) {
                    stop_monitoring("observer simulation has an invalid pause state", false);
                    return;
                }
                if (launcher->EmitObserverConfiguredIfReady(game_state) && !launcher->benchmark_started_) {
                    launcher->StartBenchmark(game_state, idler);
                }
                return;
            }
            if (launcher->observe_) {
                if (!launcher->observer_console_ready_) {
                    ++launcher->observer_attempts_;
                    if (launcher->observer_attempts_ < 30) {
                        launcher->ScheduleTimer(1'000, "failed to schedule observer console setup");
                    } else {
                        launcher->logger_.Failure(
                            "safe observer console commands were not installed within 30 seconds");
                    }
                    return;
                }
                launcher->observer_attempts_ = 0;
                if (pause_state == 0) {
                    idler->TogglePause();
                    pause_state = idler->pause_state();
                    if (pause_state != 1) {
                        launcher->logger_.Failure("failed to pause campaign before observer switch");
                        return;
                    }
                    launcher->logger_.Info("paused campaign before observer switch");
                } else if (pause_state != 1) {
                    launcher->logger_.Failure("CInGameIdler pause state is neither paused nor unpaused");
                    return;
                }
                if (!launcher->observer_ai_ready_) {
                    const auto player_tag = game_state->player_tag();
                    const auto player_ordinal = player_tag.ordinal();
                    auto *player_country = game_state->country(player_ordinal);
                    if (player_ordinal <= 0 || player_country == nullptr) {
                        launcher->logger_.Failure("current player country is invalid for observer mode");
                        return;
                    }
                    const auto player_control_before = game_state->player_control_state(player_ordinal);
                    const auto ai_count_before = game_state->country_ai_count();
                    if (player_control_before <= 0 || player_country->ai() != nullptr) {
                        launcher->logger_.Failure("player country was not in the expected human-controlled state");
                        return;
                    }
                    game_state->ReturnCountryToAI(player_tag);
                    if (game_state->has_human_controlled_country()
                        || game_state->player_control_state(player_ordinal) != 0
                        || player_country->ai() == nullptr
                        || !game_state->is_scheduled_ai(player_country->ai())
                        || game_state->country_ai_count() != ai_count_before + 1) {
                        launcher->logger_.Failure("native observer transition did not restore full AI control");
                        return;
                    }
                    std::ostringstream message;
                    message << "observer mode restored AI control for " << player_tag.str()
                            << " ai=" << player_country->ai()
                            << " scheduler_count=" << ai_count_before
                            << "->" << game_state->country_ai_count();
                    launcher->logger_.Info(message.str());
                    launcher->observer_ai_ready_ = true;
                }

                auto *fog_enabled = reinterpret_cast<unsigned char *>(
                    smedley::memory::Map::base_addr + 0xb092fb);
                if (*fog_enabled != 0) {
                    auto *command_manager = launcher->console_manager_.load(std::memory_order_acquire);
                    if (command_manager == nullptr) {
                        ++launcher->observer_attempts_;
                        if (launcher->observer_attempts_ >= 30) {
                            launcher->logger_.Failure("native console command manager is unavailable");
                            return;
                        }
                        launcher->ScheduleTimer(1'000, "failed to schedule observer FOW setup");
                        return;
                    }
                    auto *debug_command = command_manager->FindCommand("debug");
                    const auto expected_handler = smedley::memory::Map::base_addr + 0x20eb0;
                    if (debug_command == nullptr
                        || reinterpret_cast<uintptr_t>(debug_command->handler) != expected_handler) {
                        launcher->logger_.Failure("native FOW command handler does not match the supported executable");
                        return;
                    }
                    SingleConsoleArgument arguments("fow");
                    const auto result = command_manager->ExecuteCommand("debug", arguments);
                    if (!result.success || *fog_enabled != 0) {
                        launcher->logger_.Failure("native FOW command did not enable full map visibility");
                        return;
                    }
                }
                launcher->logger_.Info("observer mode enabled full map visibility");
                suppress_message_popups = true;
                launcher->observe_ = false;
            }
            if (!launcher->speed_ready_) {
                if (!launcher->SelectSpeed(game_state)) {
                    suppress_message_popups = false;
                    return;
                }
                launcher->speed_ready_ = true;
            }
            if (launcher->start_paused_) {
                if (pause_state == 0) {
                    idler->TogglePause();
                    pause_state = idler->pause_state();
                }
                if (pause_state != 1) {
                    suppress_message_popups = false;
                    launcher->logger_.Failure("could not leave campaign paused at requested start state");
                } else {
                    launcher->logger_.Info("left campaign paused at requested start state");
                    if (!launcher->final_pause_recorded_) {
                        launcher->final_pause_recorded_ = true;
                        launcher->ReportTelemetryResult(launcher->telemetry_.PauseConfigured(
                            launcher->pause_before_configuration_.value_or(true), true, true));
                    }
                }
                return;
            }
            if (pause_state == 0) {
                launcher->logger_.Info("campaign is already unpaused");
                if (!launcher->final_pause_recorded_) {
                    launcher->final_pause_recorded_ = true;
                    launcher->ReportTelemetryResult(launcher->telemetry_.PauseConfigured(
                        launcher->pause_before_configuration_.value_or(false), false, false));
                }
                if (launcher->observer_ai_ready_) {
                    launcher->observer_monitoring_ = true;
                    if (!launcher->ScheduleTimer(1'000, "failed to schedule observer pause watchdog")) {
                        suppress_message_popups = false;
                        launcher->observer_monitoring_ = false;
                    }
                }
                launcher->EmitObserverConfiguredIfReady(game_state);
                launcher->StartBenchmark(game_state, idler);
                return;
            }
            if (pause_state != 1) {
                suppress_message_popups = false;
                launcher->logger_.Failure("CInGameIdler pause state is neither paused nor unpaused");
                return;
            }
            idler->TogglePause();
            if (idler->pause_state() == 0) {
                launcher->logger_.Info("unpaused campaign through CInGameIdler");
                if (!launcher->final_pause_recorded_) {
                    launcher->final_pause_recorded_ = true;
                    launcher->ReportTelemetryResult(launcher->telemetry_.PauseConfigured(
                        launcher->pause_before_configuration_.value_or(true), false, false));
                }
                if (launcher->observer_ai_ready_) {
                    launcher->observer_monitoring_ = true;
                    if (!launcher->ScheduleTimer(1'000, "failed to schedule observer pause watchdog")) {
                        suppress_message_popups = false;
                        launcher->observer_monitoring_ = false;
                    }
                }
                launcher->EmitObserverConfiguredIfReady(game_state);
                launcher->StartBenchmark(game_state, idler);
            } else {
                suppress_message_popups = false;
                launcher->logger_.Failure("CInGameIdler remained paused after toggle");
            }
            return;
        }
        auto *controller = launcher->frontend_controller_.load(std::memory_order_acquire);
        if (controller == nullptr || !ControllerVtableMatches(controller, frontend_vtable_rva)) {
            launcher->logger_.Failure("frontend controller identity failed runtime validation");
            return;
        }
        if (!launcher->lobby_requested_) {
            if (launcher->main_menu_controller_.load(std::memory_order_acquire) == nullptr) {
                launcher->ScheduleTimer(1'000, "failed to schedule main-menu controller check");
                return;
            }
            if (!launcher->DispatchMainMenuSinglePlayer()) {
                return;
            }
            launcher->lobby_requested_ = true;
            launcher->ScheduleTimer(3'000, "failed to schedule lobby save selection");
            return;
        }
        if (!launcher->save_selection_requested_) {
            const auto filename = fs::path(launcher->save_path_).filename().string();
            void *selected_save_address = OffsetAddress(controller, selected_save_offset);
            auto *selected_save = static_cast<smedley::sstd::string *>(selected_save_address);
            std::string existing;
            RawEngineString selected_save_metadata{};
            unsigned char flags[2]{};
            const void *flags_address = OffsetAddress(controller, save_request_offset);
            if (filename.empty() || filename.size() > maximum_save_basename
                || !ValidateGuiRegistry(controller, frontend_gui_offset, 0x34)
                || selected_save == nullptr || flags_address == nullptr
                || !ReadMappedString(selected_save, maximum_save_basename, &existing, &selected_save_metadata)
                || !CopyReadable(flags, flags_address, sizeof(flags))) {
                launcher->logger_.Failure("frontend save-selection fields failed runtime validation");
                return;
            }
            if (flags[0] != 0 || flags[1] != 0) {
                launcher->logger_.Failure("frontend save-selection flags were not idle");
                return;
            }
            if (!CanSelectRequestedSave(filename, existing)) {
                launcher->logger_.Failure("frontend save selection already names a different save; automation stopped");
                return;
            }
            if (existing.empty()) {
                if (selected_save_metadata.capacity != 0xf || selected_save_metadata.storage.buffer[0] != '\0'
                    || !IsAccessible(selected_save, sizeof(*selected_save), true)) {
                    launcher->logger_.Failure("frontend selected-save string is not a canonical empty engine string");
                    return;
                }
                try {
                    // Allocate before ending the verified empty object's lifetime. The ABI mirror has
                    // no destructor, so the shallow placement copy transfers the buffer to the engine.
                    const smedley::sstd::string prepared(filename.c_str());
                    new (selected_save) smedley::sstd::string(prepared);
                } catch (const std::bad_alloc &) {
                    launcher->logger_.Failure("could not allocate the native selected-save string");
                    return;
                }
                std::string selected;
                if (!ReadMappedString(selected_save, maximum_save_basename, &selected) || selected != filename) {
                    launcher->logger_.Failure("frontend selected-save string failed postcondition validation");
                    return;
                }
            }
            constexpr unsigned char request_flags[] = {1, 0};
            if (!CopyWritable(OffsetAddress(controller, save_request_offset), request_flags, sizeof(request_flags))) {
                launcher->logger_.Failure("frontend save-selection flags are not writable");
                return;
            }
            unsigned char written_flags[2]{};
            if (!CopyReadable(written_flags, OffsetAddress(controller, save_request_offset), sizeof(written_flags))
                || written_flags[0] != request_flags[0] || written_flags[1] != request_flags[1]) {
                launcher->logger_.Failure("frontend save-selection request failed postcondition validation");
                return;
            }
            launcher->save_selection_requested_ = true;
            launcher->ReportTelemetryResult(launcher->telemetry_.SaveSelectionRequested());
            launcher->ScheduleTimer(5'000, "failed to schedule save-selection check");
            return;
        }
        unsigned char flags[2]{};
        if (!CopyReadable(flags, OffsetAddress(controller, save_request_offset), sizeof(flags))
            || flags[0] > 1 || flags[1] > 1) {
            launcher->logger_.Failure("frontend save-selection status failed runtime validation");
            return;
        }
        if (flags[1] != 0) {
            if (flags[0] != 0) {
                launcher->logger_.Failure("frontend reported save completion while the request remained active");
                return;
            }
            launcher->ReportTelemetryResult(launcher->telemetry_.SaveLoadCompleted());
            if (!launcher->DispatchControlSignal("play_button")) {
                return;
            }
            launcher->play_requested_ = true;
            launcher->frontend_controller_.store(nullptr, std::memory_order_release);
            launcher->ScheduleTimer(1'000, "failed to schedule campaign unpause");
            return;
        }
        ++launcher->save_attempts_;
        if (launcher->save_attempts_ < 24) {
            launcher->ScheduleTimer(5'000, "failed to schedule save-selection check");
        } else {
            launcher->logger_.Failure("save selection did not finish within 120 seconds");
        }
        } catch (...) {}
    }

    bool CampaignLauncher::DispatchMainMenuSinglePlayer()
    {
        auto *controller = main_menu_controller_.load(std::memory_order_acquire);
        if (controller == nullptr
            || !ControllerVtableMatches(controller, main_menu_vtable_rva)
            || main_menu_thread_id_.load(std::memory_order_acquire) != GetCurrentThreadId()) {
            logger_.Failure("main-menu controller is unavailable on the captured thread");
            return false;
        }
        void *gui = nullptr;
        if (!ReadValue(OffsetAddress(controller, main_menu_gui_offset), &gui)) gui = nullptr;
        if (gui == nullptr) {
            logger_.Failure("main-menu GUI registry is unavailable");
            return false;
        }
        EngineStringArgument panel_name("mainmenu_panel");
        EngineStringArgument button_name("single_player_button");
        uintptr_t find_panel = 0;
        if (!ReadVirtualTarget(gui, 0x6c, &find_panel)) {
            logger_.Failure("main-menu GUI lookup target failed runtime validation");
            return false;
        }
        auto *panel = InvokeFindControl(gui, find_panel, &panel_name);
        if (panel == nullptr) {
            logger_.Failure("mainmenu_panel is unavailable for native dispatch");
            return false;
        }
        uintptr_t find_button = 0;
        if (!ReadVirtualTarget(panel, 0x34, &find_button)) {
            logger_.Failure("main-menu panel lookup target failed runtime validation");
            return false;
        }
        auto *button = InvokeFindControl(panel, find_button, &button_name);
        if (button == nullptr) {
            logger_.Failure("single_player_button is unavailable for native dispatch");
            return false;
        }
        auto *signal = OffsetAddress(button, control_signal_offset);
        if (!DispatchNativeSignal(signal)) {
            logger_.Failure("main-menu Single Player signal failed runtime validation");
            return false;
        }
        main_menu_controller_.store(nullptr, std::memory_order_release);
        logger_.Info("dispatched native main-menu Single Player signal");
        return true;
    }

    bool CampaignLauncher::DispatchControlSignal(const char *name)
    {
        auto *controller = frontend_controller_.load(std::memory_order_acquire);
        if (controller == nullptr
            || !ControllerVtableMatches(controller, frontend_vtable_rva)
            || frontend_thread_id_.load(std::memory_order_acquire) != GetCurrentThreadId()) {
            logger_.Failure("frontend controller is unavailable on the captured thread");
            return false;
        }
        void *gui = nullptr;
        if (!ReadValue(OffsetAddress(controller, frontend_gui_offset), &gui)) gui = nullptr;
        if (gui == nullptr) {
            logger_.Failure("frontend GUI registry is unavailable for control dispatch");
            return false;
        }
        EngineStringArgument control_name(name);
        uintptr_t find_control = 0;
        if (!ReadVirtualTarget(gui, 0x34, &find_control)) {
            logger_.Failure("frontend GUI lookup target failed runtime validation");
            return false;
        }
        auto *control = InvokeFindControl(gui, find_control, &control_name);
        if (control == nullptr) {
            logger_.Failure(std::string(name) + " is unavailable for native dispatch");
            return false;
        }
        auto *signal = OffsetAddress(control, control_signal_offset);
        uintptr_t control_vtable = 0;
        if (!ReadValue(control, &control_vtable)) {
            logger_.Failure(std::string(name) + " control vtable is unreadable");
            return false;
        }
        std::ostringstream message;
        message << "dispatching native control signal: " << name
                << " control=" << control
                << " vtable=" << reinterpret_cast<void *>(control_vtable)
                << " signal=" << static_cast<void *>(signal);
        logger_.Info(message.str());
        if (!DispatchNativeSignal(signal)) {
            logger_.Failure(std::string(name) + " native signal dispatch failed runtime validation");
            return false;
        }
        logger_.Info(std::string("dispatched native control signal: ") + name);
        return true;
    }
}
