#include <windows.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <toml.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct Options
{
    fs::path game_dir;
    fs::path kernel;
    fs::path save;
    std::vector<fs::path> plugins;
    bool detach = false;
    bool dry_run = false;
};

std::string WindowsError(const std::string &operation)
{
    return operation + " failed with Windows error " + std::to_string(GetLastError());
}

void PrintUsage()
{
    std::cout
        << "Usage: smedley_cli --game-dir PATH [options]\n\n"
        << "  --kernel PATH  Kernel DLL (default: GAME_DIR/smedley_kernel.dll)\n"
        << "  --plugin PATH  Plugin TOML file; may be repeated\n"
        << "  --save PATH    Save file for an automation plugin to load\n"
        << "  --detach       Return after Victoria 2 starts\n"
        << "  --dry-run      Check paths without starting Victoria 2\n"
        << "  --help         Show this help\n";
}

Options ParseArguments(int argc, wchar_t **argv)
{
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--help") {
            PrintUsage();
            ExitProcess(0);
        }
        if (arg == L"--detach") {
            options.detach = true;
            continue;
        }
        if (arg == L"--dry-run") {
            options.dry_run = true;
            continue;
        }
        if (arg != L"--game-dir" && arg != L"--kernel" && arg != L"--plugin" && arg != L"--save") {
            throw std::runtime_error("unknown argument");
        }
        if (++i == argc) {
            throw std::runtime_error("missing argument value");
        }
        if (arg == L"--game-dir") {
            options.game_dir = argv[i];
        } else if (arg == L"--kernel") {
            options.kernel = argv[i];
        } else if (arg == L"--save") {
            options.save = argv[i];
        } else {
            options.plugins.emplace_back(argv[i]);
        }
    }

    if (options.game_dir.empty()) {
        throw std::runtime_error("--game-dir is required");
    }
    options.game_dir = fs::absolute(options.game_dir).lexically_normal();
    options.kernel = options.kernel.empty()
        ? options.game_dir / L"smedley_kernel.dll"
        : fs::absolute(options.kernel).lexically_normal();
    if (!options.save.empty()) {
        options.save = fs::absolute(options.save).lexically_normal();
    }
    return options;
}

std::string ReadPluginModule(const fs::path &definition)
{
    const auto table = toml::parse_file(definition.string());
    const auto id = table["id"].value<std::string>();
    const auto name = table["name"].value<std::string>();
    const auto module = table["module"].value<std::string>();
    if (!id.has_value() || !name.has_value() || !module.has_value()) {
        throw std::runtime_error("plugin definition requires id, name, and module: " + definition.string());
    }
    return *module;
}

void RequireFile(const fs::path &path, const std::string &description)
{
    if (!fs::is_regular_file(path)) {
        throw std::runtime_error(description + " not found: " + path.string());
    }
}

void RequireSaveInGameDirectory(const fs::path &save)
{
    PWSTR documents = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documents) != S_OK) {
        throw std::runtime_error("cannot locate the Documents directory");
    }
    const fs::path save_root = fs::path(documents)
        / L"Paradox Interactive" / L"Victoria II" / L"save games";
    CoTaskMemFree(documents);

    const auto canonical_root = fs::weakly_canonical(save_root);
    const auto canonical_save = fs::weakly_canonical(save);
    const auto relative = canonical_save.lexically_relative(canonical_root);
    if (relative.empty() || relative.is_absolute() || *relative.begin() == L"..") {
        throw std::runtime_error("save file must be inside the Victoria II save games directory");
    }
}

void RequireSupportedExecutable(const fs::path &path)
{
    constexpr uintmax_t expected_size = 12294656;
    constexpr std::array<unsigned char, 32> expected_hash = {
        0x62, 0xd4, 0x8c, 0x20, 0x43, 0x64, 0xdd, 0x70,
        0x65, 0x84, 0x77, 0x7c, 0x2e, 0x2b, 0x3c, 0x7a,
        0xb3, 0xc5, 0xf1, 0xdd, 0x01, 0x70, 0x87, 0x25,
        0x54, 0x94, 0x35, 0x75, 0xd5, 0x3d, 0x66, 0x48,
    };
    if (fs::file_size(path) != expected_size) {
        throw std::runtime_error("unsupported v2game.exe size");
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    auto cleanup = [&] {
        if (hash != nullptr) {
            BCryptDestroyHash(hash);
        }
        if (algorithm != nullptr) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
        }
    };
    auto check = [&](NTSTATUS status, const char *operation) {
        if (status < 0) {
            cleanup();
            throw std::runtime_error(std::string(operation) + " failed");
        }
    };

    check(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0), "BCryptOpenAlgorithmProvider");
    DWORD object_size = 0;
    DWORD copied = 0;
    check(BCryptGetProperty(
        algorithm,
        BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&object_size),
        sizeof(object_size),
        &copied,
        0), "BCryptGetProperty");
    std::vector<unsigned char> hash_object(object_size);
    check(BCryptCreateHash(
        algorithm, &hash, hash_object.data(), object_size, nullptr, 0, 0), "BCryptCreateHash");

    std::ifstream input(path, std::ios::binary);
    std::array<char, 64 * 1024> buffer{};
    while (input.read(buffer.data(), buffer.size()) || input.gcount() > 0) {
        check(BCryptHashData(
            hash,
            reinterpret_cast<PUCHAR>(buffer.data()),
            static_cast<ULONG>(input.gcount()),
            0), "BCryptHashData");
    }
    if (!input.eof()) {
        cleanup();
        throw std::runtime_error("failed to read v2game.exe");
    }

    std::array<unsigned char, 32> actual_hash{};
    check(BCryptFinishHash(hash, actual_hash.data(), actual_hash.size(), 0), "BCryptFinishHash");
    cleanup();
    hash = nullptr;
    algorithm = nullptr;
    if (actual_hash != expected_hash) {
        throw std::runtime_error("unsupported v2game.exe SHA-256");
    }
}

std::vector<fs::path> ResolvePlugins(const Options &options)
{
    std::vector<fs::path> modules;
    for (const auto &argument : options.plugins) {
        const auto definition = argument.is_absolute() ? argument : options.game_dir / argument;
        if (argument.wstring().find_first_of(L" \t") != std::wstring::npos) {
            throw std::runtime_error("plugin definition paths cannot contain spaces");
        }
        modules.push_back(options.game_dir / L"plugins" / ReadPluginModule(definition));
        RequireFile(modules.back(), "plugin DLL");
    }
    return modules;
}

void WaitForRemoteThread(HANDLE thread, const std::string &operation)
{
    constexpr DWORD injection_timeout_ms = 30000;
    const auto result = WaitForSingleObject(thread, injection_timeout_ms);
    if (result == WAIT_TIMEOUT) {
        throw std::runtime_error(operation + " timed out");
    }
    if (result != WAIT_OBJECT_0) {
        throw std::runtime_error(WindowsError(operation));
    }
}

uintptr_t InjectLibrary(HANDLE process, const fs::path &library)
{
    const auto path = library.wstring();
    const SIZE_T bytes = (path.size() + 1) * sizeof(wchar_t);
    void *remote_path = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remote_path == nullptr) {
        throw std::runtime_error(WindowsError("VirtualAllocEx"));
    }
    if (!WriteProcessMemory(process, remote_path, path.c_str(), bytes, nullptr)) {
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        throw std::runtime_error(WindowsError("WriteProcessMemory"));
    }

    const auto load_library = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    HANDLE thread = CreateRemoteThread(process, nullptr, 0, load_library, remote_path, 0, nullptr);
    if (thread == nullptr) {
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        throw std::runtime_error(WindowsError("CreateRemoteThread"));
    }

    try {
        WaitForRemoteThread(thread, "DLL injection");
    } catch (...) {
        CloseHandle(thread);
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        throw;
    }
    DWORD remote_module = 0;
    GetExitCodeThread(thread, &remote_module);
    CloseHandle(thread);
    VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
    if (remote_module == 0) {
        throw std::runtime_error("target could not load " + library.string());
    }
    return remote_module;
}

void CallLoadPlugins(HANDLE process, const fs::path &kernel, uintptr_t remote_kernel)
{
    HMODULE local_kernel = LoadLibraryExW(kernel.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (local_kernel == nullptr) {
        throw std::runtime_error(WindowsError("LoadLibraryExW"));
    }
    const auto local_entry = reinterpret_cast<uintptr_t>(GetProcAddress(local_kernel, "LoadPluginsThread"));
    if (local_entry == 0) {
        FreeLibrary(local_kernel);
        throw std::runtime_error("smedley_kernel.dll does not export LoadPluginsThread");
    }

    const auto remote_entry = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        remote_kernel + local_entry - reinterpret_cast<uintptr_t>(local_kernel));
    HANDLE thread = CreateRemoteThread(process, nullptr, 0, remote_entry, nullptr, 0, nullptr);
    FreeLibrary(local_kernel);
    if (thread == nullptr) {
        throw std::runtime_error(WindowsError("CreateRemoteThread"));
    }
    try {
        WaitForRemoteThread(thread, "plugin initialization");
    } catch (...) {
        CloseHandle(thread);
        throw;
    }
    CloseHandle(thread);
}

int wmain(int argc, wchar_t **argv)
{
    static_assert(sizeof(void *) == 4, "build smedley_cli for x86 Victoria 2");

    PROCESS_INFORMATION process{};
    try {
        const auto options = ParseArguments(argc, argv);
        const auto game = options.game_dir / L"v2game.exe";
        RequireFile(game, "Victoria 2 executable");
        RequireSupportedExecutable(game);
        RequireFile(options.kernel, "Smedley kernel");
        if (!options.save.empty()) {
            RequireFile(options.save, "save file");
            RequireSaveInGameDirectory(options.save);
        }
        const auto plugin_modules = ResolvePlugins(options);

        std::wstring command_line = L"\"" + game.wstring() + L"\"";
        for (const auto &plugin : options.plugins) {
            command_line += L" -plugin=" + plugin.wstring();
        }
        if (!options.save.empty()) {
            command_line += L" -smedley-save=\"" + options.save.wstring() + L"\"";
        }

        std::wcout << L"game:   " << game << L"\n"
                   << L"kernel: " << options.kernel << L"\n";
        for (const auto &plugin : plugin_modules) {
            std::wcout << L"plugin: " << plugin << L"\n";
        }
        if (!options.save.empty()) {
            std::wcout << L"save:   " << options.save << L"\n";
        }
        if (options.dry_run) {
            return 0;
        }

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
        mutable_command.push_back(L'\0');
        if (!CreateProcessW(game.c_str(), mutable_command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED,
                            nullptr, options.game_dir.c_str(), &startup, &process)) {
            throw std::runtime_error(WindowsError("CreateProcessW"));
        }

        const auto remote_kernel = InjectLibrary(process.hProcess, options.kernel);
        for (const auto &plugin : plugin_modules) {
            InjectLibrary(process.hProcess, plugin);
        }
        CallLoadPlugins(process.hProcess, options.kernel, remote_kernel);
        if (ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
            throw std::runtime_error(WindowsError("ResumeThread"));
        }
        CloseHandle(process.hThread);
        process.hThread = nullptr;

        std::cout << "Victoria 2 started (PID " << process.dwProcessId << ")\n";
        if (options.detach) {
            CloseHandle(process.hProcess);
            return 0;
        }

        WaitForSingleObject(process.hProcess, INFINITE);
        DWORD exit_code = 1;
        GetExitCodeProcess(process.hProcess, &exit_code);
        CloseHandle(process.hProcess);
        return static_cast<int>(exit_code);
    } catch (const std::exception &error) {
        if (process.hProcess != nullptr) {
            TerminateProcess(process.hProcess, 1);
            if (process.hThread != nullptr) {
                CloseHandle(process.hThread);
            }
            CloseHandle(process.hProcess);
        }
        std::cerr << "smedley_cli: " << error.what() << '\n';
        return 1;
    }
}
