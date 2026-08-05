#include "scripting_runtime.hpp"

#include <gtest/gtest.h>

#include <windows.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <thread>

namespace scripting = smedley::scripting;
namespace fs = std::filesystem;

namespace
{
    class ScriptingRuntimeTest : public testing::Test
    {
    protected:
        fs::path root;

        void SetUp() override
        {
            wchar_t temporary[MAX_PATH];
            ASSERT_NE(GetTempPathW(MAX_PATH, temporary), 0u);
            root = fs::path(temporary) / (L"smedley scripting test " + std::to_wstring(GetCurrentProcessId())
                                         + L" " + std::to_wstring(GetTickCount64()));
            fs::create_directories(root);
        }

        void TearDown() override
        {
            std::error_code error;
            fs::remove_all(root, error);
        }

        fs::path Write(const std::string &name, const std::string &source)
        {
            const auto path = root / fs::u8path(name);
            std::ofstream output(path, std::ios::binary);
            EXPECT_TRUE(output);
            output << source;
            return path;
        }

        static bool WaitFor(const std::function<bool()> &condition)
        {
            for (int attempt = 0; attempt < 100; ++attempt) {
                if (condition()) return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return false;
        }
    };
}

TEST_F(ScriptingRuntimeTest, RunsRestrictedDailyCallbacksAndScheduledPause)
{
    const auto script = Write("observer.lua", R"(
assert(smedley.api_version == 1)
assert(io == nil and os == nil and package == nil and debug == nil and coroutine == nil)
assert(dofile == nil and loadfile == nil and load == nil and loadstring == nil)
assert(pcall == nil and xpcall == nil and newproxy == nil)
assert(string.find == nil and string.match == nil and string.gmatch == nil)
assert(string.gsub == nil and string.dump == nil)
function on_load(context)
    assert(context.api_version == 1)
    smedley.log("loaded")
end
function on_daily(event)
    assert(event.kind == "daily")
    assert(event.country.tag == "ENG")
    if event.date_raw == 100 then
        smedley.after_days(1, function(later)
            assert(later.date_raw == 124)
            assert(smedley.request_pause())
        end)
    end
end
)");
    std::atomic<int> logs{0};
    std::atomic<int> pauses{0};
    scripting::Config config;
    config.scripts = {script};
    scripting::Runtime runtime(config,
        [&](bool failure, const std::string &) { if (!failure) logs.fetch_add(1); },
        [&] { pauses.fetch_add(1); return true; });
    std::string error;

    ASSERT_TRUE(runtime.Start(&error)) << error;
    scripting::EventSnapshot first{100, 32768, 272, 271, {'E', 'N', 'G', '\0'}, true, false};
    scripting::EventSnapshot second = first;
    second.date_raw = 124;
    ASSERT_TRUE(runtime.TryPush(first));
    ASSERT_TRUE(runtime.TryPush(second));
    ASSERT_TRUE(WaitFor([&] { return pauses.load() == 1; }));
    runtime.Stop();

    EXPECT_EQ(logs.load(), 1);
    EXPECT_EQ(runtime.stats().processed, 2u);
    EXPECT_EQ(runtime.stats().script_errors, 0u);
}

TEST_F(ScriptingRuntimeTest, LogsPauseResultsOnWorker)
{
    const auto script = Write("pause.lua", "function on_daily(event) smedley.request_pause() end\n");
    const DWORD caller_thread = GetCurrentThreadId();
    std::atomic<DWORD> log_thread{caller_thread};
    std::atomic<bool> consumed{false};
    scripting::Config config;
    config.scripts = {script};
    scripting::Runtime runtime(config,
        [&](bool, const std::string &message) {
            if (message.find("completed with paused readback") != std::string::npos) {
                log_thread.store(GetCurrentThreadId());
            }
        }, [] { return true; }, [&] { consumed.store(true); });
    std::string error;

    ASSERT_TRUE(runtime.Start(&error)) << error;
    scripting::EventSnapshot event{100, 0, 1, 1, {'J', 'A', 'N', '\0'}, true, false};
    ASSERT_TRUE(runtime.TryPush(event));
    ASSERT_TRUE(WaitFor([&] { return runtime.stats().processed == 1; }));
    runtime.ReportPauseResult(scripting::PauseResult::Completed);
    ASSERT_TRUE(WaitFor([&] { return log_thread.load() != caller_thread; }));
    EXPECT_TRUE(WaitFor([&] { return consumed.load(); }));
    runtime.Stop();
}

TEST_F(ScriptingRuntimeTest, DisablesInfiniteCallbackAtInstructionLimit)
{
    const auto script = Write("loop.lua", "function on_daily(event) while true do end end\n");
    std::atomic<int> failures{0};
    scripting::Config config;
    config.scripts = {script};
    config.instruction_budget = 1'000;
    scripting::Runtime runtime(config,
        [&](bool failure, const std::string &) { if (failure) failures.fetch_add(1); }, [] { return false; });
    std::string error;

    ASSERT_TRUE(runtime.Start(&error)) << error;
    scripting::EventSnapshot event{100, 0, 1, 1, {'J', 'A', 'N', '\0'}, true, false};
    ASSERT_TRUE(runtime.TryPush(event));
    ASSERT_TRUE(runtime.TryPush(event));
    ASSERT_TRUE(runtime.TryPush(event));
    ASSERT_TRUE(WaitFor([&] { return runtime.stats().disabled_scripts == 1; }));
    runtime.Stop();

    EXPECT_EQ(runtime.stats().script_errors, 3u);
    EXPECT_GE(failures.load(), 4);
}

TEST_F(ScriptingRuntimeTest, RejectsBytecodeAndMemoryLimitFailures)
{
    const auto bytecode = Write("bytecode.lua", std::string("\x1bLua", 4));
    scripting::Config bytecode_config;
    bytecode_config.scripts = {bytecode};
    scripting::Runtime bytecode_runtime(bytecode_config, [](bool, const std::string &) {}, [] { return false; });
    std::string error;
    EXPECT_FALSE(bytecode_runtime.Start(&error));
    EXPECT_NE(error.find("bytecode"), std::string::npos);

    const auto memory = Write("memory.lua", "local value = string.rep('x', 1048576)\n");
    scripting::Config memory_config;
    memory_config.scripts = {memory};
    memory_config.memory_limit_bytes = scripting::kMinMemoryBytes;
    scripting::Runtime memory_runtime(memory_config, [](bool, const std::string &) {}, [] { return false; });
    error.clear();
    EXPECT_FALSE(memory_runtime.Start(&error));
    EXPECT_FALSE(error.empty());
}

TEST_F(ScriptingRuntimeTest, ContainsAllocatorFailureWhileBuildingNextEvent)
{
    const auto script = Write("event-memory.lua", R"(
local held
function on_daily(event)
    if held == nil then
        collectgarbage("stop")
        local used = collectgarbage("count") * 1024
        held = string.rep("x", math.floor(262144 - used - 2048))
    end
end
)");
    scripting::Config config;
    config.scripts = {script};
    config.memory_limit_bytes = scripting::kMinMemoryBytes;
    scripting::Runtime runtime(config, [](bool, const std::string &) {}, [] { return false; });
    std::string error;

    ASSERT_TRUE(runtime.Start(&error)) << error;
    scripting::EventSnapshot event{100, 0, 1, 1, {'J', 'A', 'N', '\0'}, true, false};
    ASSERT_TRUE(runtime.TryPush(event));
    ASSERT_TRUE(WaitFor([&] { return runtime.stats().processed == 1; }));
    ASSERT_TRUE(runtime.TryPush(event));
    ASSERT_TRUE(WaitFor([&] { return runtime.stats().script_errors != 0; }));
    runtime.Stop();

    EXPECT_GE(runtime.stats().script_errors, 1u);
    EXPECT_LT(runtime.stats().script_errors, 3u);
    EXPECT_EQ(runtime.stats().disabled_scripts, 0u);
}

TEST(ScriptingEventApiTest, CopiesTheDailyAbiSnapshot)
{
    SmedleyDailyEventV1 event{};
    event.struct_size = sizeof(event);
    event.version = SMEDLEY_DAILY_EVENT_VERSION_V1;
    event.game_date_raw = 100;
    event.treasury_raw = 32768;
    event.country_slot_count = 272;
    event.ai_scheduler_entry_count = 271;
    event.country_tag[0] = 'E';
    event.country_tag[1] = 'N';
    event.country_tag[2] = 'G';
    event.has_owned_province = 1;
    event.human_control_present = 1;
    scripting::EventSnapshot snapshot;

    ASSERT_TRUE(scripting::CopyDailyEventSnapshot(event, &snapshot));
    EXPECT_EQ(snapshot.date_raw, 100);
    EXPECT_EQ(snapshot.treasury_raw, 32768);
    EXPECT_EQ(snapshot.country_slot_count, 272u);
    EXPECT_EQ(snapshot.ai_scheduler_entry_count, 271u);
    EXPECT_EQ(snapshot.country_tag, (std::array<char, 4>{'E', 'N', 'G', '\0'}));
    EXPECT_TRUE(snapshot.country_exists);
    EXPECT_TRUE(snapshot.human_control_present);

    event.version = 0;
    EXPECT_FALSE(scripting::CopyDailyEventSnapshot(event, &snapshot));
}

TEST(ScriptingConfigTest, RejectsOutOfRangeLimits)
{
    scripting::Config config;
    std::string error;
    EXPECT_FALSE(scripting::ValidateConfig(config, &error));
    config.scripts = {L"script.lua"};
    config.instruction_budget = scripting::kMinInstructionBudget - 1;
    EXPECT_FALSE(scripting::ValidateConfig(config, &error));
    config.instruction_budget = scripting::kMinInstructionBudget;
    config.queue_capacity = scripting::kMaxQueueCapacity + 1;
    EXPECT_FALSE(scripting::ValidateConfig(config, &error));
}

TEST(ScriptingConfigTest, ParsesExplicitLaunchArguments)
{
    scripting::Config config;
    std::string error;
    ASSERT_TRUE(scripting::ParseLaunchArguments({L"-smedley-script=C:\\Game\\scripts\\one.lua",
        L"-smedley-script-instruction-budget=200000", L"-smedley-script-memory-bytes=4194304",
        L"-smedley-script-queue-capacity=128"}, &config, &error)) << error;
    ASSERT_EQ(config.scripts.size(), 1u);
    EXPECT_EQ(config.scripts[0], fs::path(L"C:\\Game\\scripts\\one.lua"));
    EXPECT_EQ(config.instruction_budget, 200'000);
    EXPECT_EQ(config.memory_limit_bytes, 4'194'304u);
    EXPECT_EQ(config.queue_capacity, 128u);

    EXPECT_FALSE(scripting::ParseLaunchArguments({L"-smedley-script=one.lua",
        L"-smedley-script-instruction-budget=999", L"-smedley-script-memory-bytes=4194304",
        L"-smedley-script-queue-capacity=128"}, &config, &error));
}
