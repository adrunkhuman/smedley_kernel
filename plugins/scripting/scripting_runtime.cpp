#include "scripting_runtime.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <memory>
#include <utility>

namespace smedley::scripting
{
    namespace
    {
        constexpr int kScriptErrorLimit = 3;
        constexpr size_t kMaxLogBytes = 512;
        constexpr size_t kMaxScheduledCallbacks = 1024;

        void UpdateHighWater(std::atomic<uint64_t> *high_water, uint64_t value)
        {
            uint64_t current = high_water->load(std::memory_order_relaxed);
            while (current < value
                   && !high_water->compare_exchange_weak(current, value, std::memory_order_relaxed)) {
            }
        }

        void SetField(lua_State *state, const char *key, lua_Number value)
        {
            lua_pushnumber(state, value);
            lua_setfield(state, -2, key);
        }

        void SetField(lua_State *state, const char *key, bool value)
        {
            lua_pushboolean(state, value ? 1 : 0);
            lua_setfield(state, -2, key);
        }

        void SetField(lua_State *state, const char *key, const char *value)
        {
            lua_pushstring(state, value);
            lua_setfield(state, -2, key);
        }
    }

    struct Runtime::Script
    {
        struct Allocator
        {
            size_t used = 0;
            size_t limit = 0;
        };

        struct ScheduledCallback
        {
            int target_date_raw = 0;
            int reference = LUA_NOREF;
        };

        Runtime *runtime = nullptr;
        std::filesystem::path path;
        std::string filename;
        Allocator allocator;
        lua_State *state = nullptr;
        int dispatcher_reference = LUA_NOREF;
        std::vector<ScheduledCallback> scheduled;
        const EventSnapshot *current_event = nullptr;
        int errors = 0;
        bool disabled = false;

        Script(Runtime *owner, std::filesystem::path script_path)
            : runtime(owner), path(std::move(script_path)), filename(path.filename().u8string()),
              allocator{0, owner->config_.memory_limit_bytes}
        {
            scheduled.reserve(32);
        }

        ~Script()
        {
            if (state != nullptr) lua_close(state);
        }

        static void *Allocate(void *user_data, void *pointer, size_t old_size, size_t new_size)
        {
            auto *allocator = static_cast<Allocator *>(user_data);
            if (new_size == 0) {
                if (pointer != nullptr) {
                    allocator->used = old_size <= allocator->used ? allocator->used - old_size : 0;
                    std::free(pointer);
                }
                return nullptr;
            }
            if (pointer == nullptr) old_size = 0;
            if (new_size > old_size && new_size - old_size > allocator->limit - (std::min)(allocator->used, allocator->limit)) {
                return nullptr;
            }
            void *replacement = std::realloc(pointer, new_size);
            if (replacement == nullptr) return nullptr;
            if (new_size >= old_size) allocator->used += new_size - old_size;
            else allocator->used -= (std::min)(allocator->used, old_size - new_size);
            return replacement;
        }

        static void InstructionLimit(lua_State *state, lua_Debug *)
        {
            luaL_error(state, "instruction budget exceeded");
        }

        static Script *FromUpvalue(lua_State *state)
        {
            return static_cast<Script *>(lua_touserdata(state, lua_upvalueindex(1)));
        }

        static int LogMessage(lua_State *state)
        {
            size_t length = 0;
            const char *message = lua_tolstring(state, 1, &length);
            if (message == nullptr) return luaL_error(state, "smedley.log expects a string");
            if (length > kMaxLogBytes) return luaL_error(state, "smedley.log messages are limited to 512 bytes");
            auto *script = FromUpvalue(state);
            try {
                script->runtime->log_(false, script->path.filename().u8string() + ": " + std::string(message, length));
            } catch (...) {
            }
            return 0;
        }

        static int RequestPause(lua_State *state)
        {
            auto *script = FromUpvalue(state);
            const bool accepted = script->runtime->request_pause_ && script->runtime->request_pause_();
            lua_pushboolean(state, accepted ? 1 : 0);
            return 1;
        }

        static int AfterDays(lua_State *state)
        {
            auto *script = FromUpvalue(state);
            if (script->current_event == nullptr) return luaL_error(state, "smedley.after_days is only available in an event callback");
            if (!lua_isnumber(state, 1) || lua_tointeger(state, 1) < 1) {
                return luaL_error(state, "smedley.after_days expects a positive integer day count");
            }
            const lua_Integer days = lua_tointeger(state, 1);
            if (static_cast<lua_Number>(days) != lua_tonumber(state, 1) || days > 1'000'000) {
                return luaL_error(state, "smedley.after_days expects a day count from 1 through 1000000");
            }
            if (!lua_isfunction(state, 2)) return luaL_error(state, "smedley.after_days expects a callback function");
            if (script->scheduled.size() >= kMaxScheduledCallbacks) return luaL_error(state, "scheduled callback limit reached");
            const int64_t target = static_cast<int64_t>(script->current_event->date_raw) + static_cast<int64_t>(days) * 24;
            if (target > (std::numeric_limits<int>::max)()) return luaL_error(state, "scheduled date exceeds the supported range");
            lua_pushvalue(state, 2);
            const int reference = luaL_ref(state, LUA_REGISTRYINDEX);
            try {
                script->scheduled.push_back({static_cast<int>(target), reference});
            } catch (...) {
                luaL_unref(state, LUA_REGISTRYINDEX, reference);
                return luaL_error(state, "could not allocate a scheduled callback");
            }
            lua_pushinteger(state, reference);
            return 1;
        }

        static int Dispatch(lua_State *state)
        {
            auto *script = FromUpvalue(state);
            if (lua_isnumber(state, 1)) {
                const int callback = static_cast<int>(lua_tointeger(state, 1));
                const char *name = callback == 0 ? "on_load" : callback == 1 ? "on_daily"
                    : callback == 2 ? "on_unload" : nullptr;
                if (name == nullptr) return luaL_error(state, "unknown host callback");
                lua_getglobal(state, name);
                if (lua_isnil(state, -1)) return 0;
                if (!lua_isfunction(state, -1)) return luaL_error(state, "%s must be a function", name);
            } else if (lua_isfunction(state, 1)) {
                lua_pushvalue(state, 1);
            } else {
                return luaL_error(state, "scheduled callback must be a function");
            }
            const auto *event = static_cast<const EventSnapshot *>(lua_touserdata(state, 2));
            if (event == nullptr) script->PushContext();
            else script->PushEvent(*event);
            lua_call(state, 1, 0);
            return 0;
        }

        void RegisterFunction(const char *name, lua_CFunction function)
        {
            lua_pushlightuserdata(state, this);
            lua_pushcclosure(state, function, 1);
            lua_setfield(state, -2, name);
        }

        void OpenLibrary(const char *name, lua_CFunction function)
        {
            lua_pushcfunction(state, function);
            lua_pushstring(state, name);
            lua_call(state, 1, 0);
        }

        bool Initialize(std::string *error)
        {
            state = lua_newstate(&Allocate, &allocator);
            if (state == nullptr) {
                *error = path.u8string() + ": could not create a Lua state within the memory limit";
                return false;
            }
            OpenLibrary("", luaopen_base);
            OpenLibrary(LUA_TABLIBNAME, luaopen_table);
            OpenLibrary(LUA_STRLIBNAME, luaopen_string);
            OpenLibrary(LUA_MATHLIBNAME, luaopen_math);
            for (const char *name : {"dofile", "loadfile", "load", "loadstring", "getfenv", "setfenv",
                                     "print", "pcall", "xpcall", "coroutine", "newproxy"}) {
                lua_pushnil(state);
                lua_setglobal(state, name);
            }
            lua_getglobal(state, LUA_STRLIBNAME);
            for (const char *name : {"dump", "find", "gmatch", "gsub", "match"}) {
                lua_pushnil(state);
                lua_setfield(state, -2, name);
            }
            lua_pop(state, 1);
            lua_newtable(state);
            SetField(state, "api_version", static_cast<lua_Number>(1));
            RegisterFunction("log", &LogMessage);
            RegisterFunction("request_pause", &RequestPause);
            RegisterFunction("after_days", &AfterDays);
            lua_setglobal(state, "smedley");
            // Reserve luaL_ref's free-list slot before user code can approach the allocator cap.
            lua_pushinteger(state, 0);
            lua_rawseti(state, LUA_REGISTRYINDEX, 0);
            lua_pushlightuserdata(state, this);
            lua_pushcclosure(state, &Dispatch, 1);
            dispatcher_reference = luaL_ref(state, LUA_REGISTRYINDEX);

            std::error_code filesystem_error;
            const auto file_size = std::filesystem::file_size(path, filesystem_error);
            if (filesystem_error || file_size == 0 || file_size > kMaxScriptBytes) {
                *error = path.u8string() + ": script must be a non-empty normal file no larger than 1 MiB";
                return false;
            }
            std::ifstream input(path, std::ios::binary);
            std::string source(static_cast<size_t>(file_size), '\0');
            input.read(source.data(), static_cast<std::streamsize>(source.size()));
            if (!input || static_cast<unsigned char>(source[0]) == 0x1b) {
                *error = path.u8string() + (input ? ": precompiled Lua bytecode is not supported" : ": could not read script");
                return false;
            }
            const std::string source_name = "@" + path.u8string();
            if (luaL_loadbuffer(state, source.data(), source.size(), source_name.c_str()) != 0) {
                *error = path.u8string() + ": " + LuaError();
                return false;
            }
            if (!ProtectedCall(0, error)) {
                *error = path.u8string() + ": " + *error;
                return false;
            }
            return Invoke(0, nullptr, error, false);
        }

        std::string LuaError() const
        {
            const char *message = lua_type(state, -1) == LUA_TSTRING ? lua_tostring(state, -1) : nullptr;
            return message == nullptr ? "unknown Lua error" : message;
        }

        bool ProtectedCall(int arguments, std::string *error)
        {
            lua_sethook(state, &InstructionLimit, LUA_MASKCOUNT, runtime->config_.instruction_budget);
            const int result = lua_pcall(state, arguments, 0, 0);
            lua_sethook(state, nullptr, 0, 0);
            if (result == 0) return true;
            *error = LuaError();
            lua_settop(state, 0);
            return false;
        }

        void PushContext()
        {
            lua_newtable(state);
            SetField(state, "api_version", static_cast<lua_Number>(1));
            SetField(state, "script", filename.c_str());
        }

        void PushEvent(const EventSnapshot &event)
        {
            lua_newtable(state);
            SetField(state, "kind", "daily");
            SetField(state, "mapping_id", "v2game-3.04");
            SetField(state, "quality", "provisional");
            SetField(state, "date_raw", static_cast<lua_Number>(event.date_raw));

            lua_newtable(state);
            SetField(state, "country_slot_count", static_cast<lua_Number>(event.country_slot_count));
            SetField(state, "ai_scheduler_entry_count", static_cast<lua_Number>(event.ai_scheduler_entry_count));
            SetField(state, "human_control_present", event.human_control_present);
            lua_setfield(state, -2, "world");

            lua_newtable(state);
            SetField(state, "tag", event.country_tag.data());
            SetField(state, "exists", event.country_exists);
            SetField(state, "treasury_raw", static_cast<lua_Number>(event.treasury_raw));
            SetField(state, "treasury", static_cast<lua_Number>(event.treasury_raw) / 32768.0);
            lua_setfield(state, -2, "country");
        }

        bool Invoke(int callback, const EventSnapshot *event, std::string *error, bool count_error = true)
        {
            if (disabled) return false;
            current_event = event;
            lua_rawgeti(state, LUA_REGISTRYINDEX, dispatcher_reference);
            lua_pushinteger(state, callback);
            if (event == nullptr) lua_pushnil(state);
            else lua_pushlightuserdata(state, const_cast<EventSnapshot *>(event));
            const bool success = ProtectedCall(2, error);
            current_event = nullptr;
            if (!success && count_error) Fail(*error);
            return success;
        }

        void Process(const EventSnapshot &event)
        {
            if (disabled) return;
            std::string error;
            Invoke(1, &event, &error);
            if (disabled) return;
            for (size_t index = 0; index < scheduled.size() && !disabled;) {
                if (scheduled[index].target_date_raw > event.date_raw) {
                    ++index;
                    continue;
                }
                const int reference = scheduled[index].reference;
                scheduled.erase(scheduled.begin() + static_cast<std::ptrdiff_t>(index));
                current_event = &event;
                lua_rawgeti(state, LUA_REGISTRYINDEX, dispatcher_reference);
                lua_rawgeti(state, LUA_REGISTRYINDEX, reference);
                lua_pushlightuserdata(state, const_cast<EventSnapshot *>(&event));
                if (!ProtectedCall(2, &error)) Fail(error);
                current_event = nullptr;
                luaL_unref(state, LUA_REGISTRYINDEX, reference);
            }
        }

        void Unload()
        {
            if (disabled || state == nullptr) return;
            std::string error;
            if (!Invoke(2, nullptr, &error, false)) {
                try {
                    runtime->log_(true, path.filename().u8string() + ": on_unload failed: " + error);
                } catch (...) {
                }
            }
        }

        void Fail(const std::string &error)
        {
            runtime->script_errors_.fetch_add(1, std::memory_order_relaxed);
            ++errors;
            try {
                runtime->log_(true, path.filename().u8string() + ": " + error);
            } catch (...) {
            }
            if (errors < kScriptErrorLimit) return;
            disabled = true;
            runtime->disabled_scripts_.fetch_add(1, std::memory_order_relaxed);
            try {
                runtime->log_(true, path.filename().u8string() + ": disabled after three callback errors");
            } catch (...) {
            }
        }
    };

    bool ValidateConfig(const Config &config, std::string *error)
    {
        if (config.scripts.empty() || config.scripts.size() > kMaxScripts) {
            *error = "script count must be from 1 through 16";
            return false;
        }
        if (config.instruction_budget < kMinInstructionBudget || config.instruction_budget > kMaxInstructionBudget) {
            *error = "instruction budget must be from 1000 through 10000000";
            return false;
        }
        if (config.memory_limit_bytes < kMinMemoryBytes || config.memory_limit_bytes > kMaxMemoryBytes) {
            *error = "memory limit must be from 262144 through 67108864 bytes per script";
            return false;
        }
        if (config.queue_capacity < kMinQueueCapacity || config.queue_capacity > kMaxQueueCapacity) {
            *error = "queue capacity must be from 16 through 4096";
            return false;
        }
        for (size_t index = 0; index < config.scripts.size(); ++index) {
            if (config.scripts[index].empty()) {
                *error = "script paths must not be empty";
                return false;
            }
            if (std::find(config.scripts.begin(), config.scripts.begin() + index, config.scripts[index])
                != config.scripts.begin() + index) {
                *error = "script paths must not be repeated";
                return false;
            }
        }
        return true;
    }

    bool ParseLaunchArguments(const std::vector<std::wstring> &arguments, Config *config, std::string *error)
    {
        Config parsed;
        bool have_instruction_budget = false;
        bool have_memory_limit = false;
        bool have_queue_capacity = false;
        auto parse_number = [&](const std::wstring &value, uint64_t minimum, uint64_t maximum, uint64_t *destination) {
            if (value.empty() || value[0] == L'-') return false;
            wchar_t *end = nullptr;
            errno = 0;
            const unsigned long long number = std::wcstoull(value.c_str(), &end, 10);
            if (errno == ERANGE || end != value.c_str() + value.size() || number < minimum || number > maximum) return false;
            *destination = static_cast<uint64_t>(number);
            return true;
        };
        for (const auto &argument : arguments) {
            constexpr const wchar_t *script_prefix = L"-smedley-script=";
            constexpr const wchar_t *instruction_prefix = L"-smedley-script-instruction-budget=";
            constexpr const wchar_t *memory_prefix = L"-smedley-script-memory-bytes=";
            constexpr const wchar_t *queue_prefix = L"-smedley-script-queue-capacity=";
            if (argument.rfind(instruction_prefix, 0) == 0) {
                uint64_t value = 0;
                if (have_instruction_budget || !parse_number(argument.substr(std::wcslen(instruction_prefix)),
                        kMinInstructionBudget, kMaxInstructionBudget, &value)) {
                    *error = "-smedley-script-instruction-budget must appear once with a value from 1000 through 10000000";
                    return false;
                }
                parsed.instruction_budget = static_cast<int>(value);
                have_instruction_budget = true;
            } else if (argument.rfind(memory_prefix, 0) == 0) {
                uint64_t value = 0;
                if (have_memory_limit || !parse_number(argument.substr(std::wcslen(memory_prefix)),
                        kMinMemoryBytes, kMaxMemoryBytes, &value)) {
                    *error = "-smedley-script-memory-bytes must appear once with a value from 262144 through 67108864";
                    return false;
                }
                parsed.memory_limit_bytes = static_cast<size_t>(value);
                have_memory_limit = true;
            } else if (argument.rfind(queue_prefix, 0) == 0) {
                uint64_t value = 0;
                if (have_queue_capacity || !parse_number(argument.substr(std::wcslen(queue_prefix)),
                        kMinQueueCapacity, kMaxQueueCapacity, &value)) {
                    *error = "-smedley-script-queue-capacity must appear once with a value from 16 through 4096";
                    return false;
                }
                parsed.queue_capacity = static_cast<size_t>(value);
                have_queue_capacity = true;
            } else if (argument.rfind(script_prefix, 0) == 0) {
                const auto value = argument.substr(std::wcslen(script_prefix));
                if (value.empty()) {
                    *error = "-smedley-script requires a non-empty path";
                    return false;
                }
                parsed.scripts.emplace_back(value);
            } else if (argument.rfind(L"-smedley-script", 0) == 0) {
                *error = "malformed scripting argument";
                return false;
            }
        }
        if (!have_instruction_budget || !have_memory_limit || !have_queue_capacity) {
            *error = "scripting requires explicit instruction, memory, and queue limits";
            return false;
        }
        if (!ValidateConfig(parsed, error)) return false;
        *config = std::move(parsed);
        return true;
    }

    Runtime::Runtime(Config config, Log log, RequestPause request_pause, PauseResultConsumed pause_result_consumed)
        : config_(std::move(config)), log_(std::move(log)), request_pause_(std::move(request_pause)),
          pause_result_consumed_(std::move(pause_result_consumed))
    {
    }

    Runtime::~Runtime()
    {
        Stop();
    }

    bool Runtime::Start(std::string *error)
    {
        if (started_) return true;
        if (!ValidateConfig(config_, error)) return false;
        queue_.resize(config_.queue_capacity);
        try {
            started_ = true;
            stopping_ = false;
            initialized_ = false;
            thread_ = std::thread(&Runtime::Run, this);
        } catch (const std::exception &exception) {
            started_ = false;
            *error = std::string("could not start script worker: ") + exception.what();
            return false;
        }
        std::unique_lock<std::mutex> lock(mutex_);
        initialized_wake_.wait(lock, [this] { return initialized_; });
        if (!initialization_error_.empty()) {
            *error = initialization_error_;
            lock.unlock();
            if (thread_.joinable()) thread_.join();
            started_ = false;
            return false;
        }
        return true;
    }

    bool Runtime::TryPush(const EventSnapshot &event)
    {
        if (!started_) return false;
        if (worker_failed_.load(std::memory_order_acquire)) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock() || stopping_ || size_ == queue_.size()) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        queue_[tail_] = event;
        tail_ = (tail_ + 1) % queue_.size();
        ++size_;
        accepted_.fetch_add(1, std::memory_order_relaxed);
        UpdateHighWater(&high_water_, size_);
        lock.unlock();
        wake_.notify_one();
        return true;
    }

    void Runtime::ReportPauseResult(PauseResult result)
    {
        pause_result_.store(static_cast<int>(result), std::memory_order_release);
        wake_.notify_one();
    }

    bool Runtime::Pop(EventSnapshot *event)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (size_ == 0) return false;
        *event = queue_[head_];
        head_ = (head_ + 1) % queue_.size();
        --size_;
        return true;
    }

    bool Runtime::Initialize(std::string *error)
    {
        for (const auto &path : config_.scripts) {
            auto script = std::make_unique<Script>(this, path);
            if (!script->Initialize(error)) return false;
            scripts_.push_back(script.release());
        }
        return true;
    }

    void Runtime::Run()
    {
        std::string error;
        bool ready = false;
        try {
            ready = Initialize(&error);
        } catch (const std::exception &exception) {
            error = std::string("could not initialize script worker: ") + exception.what();
        } catch (...) {
            error = "could not initialize script worker";
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            initialization_error_ = ready ? std::string{} : error;
            initialized_ = true;
        }
        initialized_wake_.notify_one();
        if (ready) {
            for (;;) {
                if (ProcessPauseResult()) continue;
                EventSnapshot event;
                if (Pop(&event)) {
                    try {
                        Process(event);
                    } catch (const std::exception &exception) {
                        worker_failed_.store(true, std::memory_order_release);
                        try { log_(true, std::string("script worker failed: ") + exception.what()); } catch (...) {}
                        break;
                    } catch (...) {
                        worker_failed_.store(true, std::memory_order_release);
                        try { log_(true, "script worker failed"); } catch (...) {}
                        break;
                    }
                    processed_.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
                std::unique_lock<std::mutex> lock(mutex_);
                if (stopping_) break;
                wake_.wait(lock, [this] {
                    return stopping_ || size_ != 0 || pause_result_.load(std::memory_order_acquire) != 0;
                });
                if (stopping_) {
                    lock.unlock();
                    ProcessPauseResult();
                    break;
                }
            }
            for (auto *script : scripts_) script->Unload();
        }
        for (auto *script : scripts_) delete script;
        scripts_.clear();
    }

    void Runtime::Process(const EventSnapshot &event)
    {
        for (auto *script : scripts_) script->Process(event);
    }

    bool Runtime::ProcessPauseResult()
    {
        const auto result = static_cast<PauseResult>(pause_result_.exchange(0, std::memory_order_acq_rel));
        const char *message = nullptr;
        bool failure = true;
        switch (result) {
        case PauseResult::Completed:
            message = "script pause request completed with paused readback";
            failure = false;
            break;
        case PauseResult::OutsideCampaign: message = "script pause request rejected outside CInGameIdler"; break;
        case PauseResult::InvalidState: message = "script pause request rejected because pause state is invalid"; break;
        case PauseResult::SignatureMismatch: message = "script pause request rejected because the native signature changed"; break;
        case PauseResult::ReadbackFailed: message = "script pause request did not produce paused readback"; break;
        case PauseResult::Shutdown: message = "script pause request cancelled during shutdown"; break;
        default: return false;
        }
        try {
            log_(failure, message);
        } catch (...) {
        }
        try {
            if (pause_result_consumed_) pause_result_consumed_();
        } catch (...) {
        }
        return true;
    }

    void Runtime::Stop()
    {
        if (!started_) return;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
            size_ = 0;
        }
        wake_.notify_one();
        if (thread_.joinable()) thread_.join();
        started_ = false;
    }

    Stats Runtime::stats() const
    {
        return {accepted_.load(std::memory_order_relaxed), processed_.load(std::memory_order_relaxed),
                dropped_.load(std::memory_order_relaxed), script_errors_.load(std::memory_order_relaxed),
                disabled_scripts_.load(std::memory_order_relaxed), high_water_.load(std::memory_order_relaxed),
                worker_failed_.load(std::memory_order_relaxed)};
    }
}
