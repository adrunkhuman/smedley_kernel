#include <smedley/launcher/launcher.hpp>
#include <smedley/plugin_abi.h>

#include <windows.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <toml.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cwctype>
#include <fstream>
#include <limits>
#include <sstream>

namespace smedley::launcher
{
    namespace
    {
        constexpr uintmax_t supported_executable_size = 12294656;
        constexpr int telemetry_min_sample_days = 1;
        constexpr int telemetry_max_sample_days = 365;
        constexpr int telemetry_min_queue_capacity = 64;
        constexpr int telemetry_max_queue_capacity = 8192;
        constexpr int run_min_days = 1;
        constexpr int run_max_days = 1000000;
        constexpr int run_min_timeout_seconds = 1;
        constexpr int run_max_timeout_seconds = 86400;
        constexpr int script_min_instruction_budget = 1000;
        constexpr int script_max_instruction_budget = 10000000;
        constexpr int script_min_memory_bytes = 262144;
        constexpr int script_max_memory_bytes = 67108864;
        constexpr int script_min_queue_capacity = 16;
        constexpr int script_max_queue_capacity = 4096;
        constexpr size_t script_max_count = 16;
        constexpr uintmax_t script_max_file_bytes = 1024 * 1024;
        constexpr std::array<unsigned char, 32> supported_executable_hash = {
            0x62, 0xd4, 0x8c, 0x20, 0x43, 0x64, 0xdd, 0x70,
            0x65, 0x84, 0x77, 0x7c, 0x2e, 0x2b, 0x3c, 0x7a,
            0xb3, 0xc5, 0xf1, 0xdd, 0x01, 0x70, 0x87, 0x25,
            0x54, 0x94, 0x35, 0x75, 0xd5, 0x3d, 0x66, 0x48,
        };

        void AddDiagnostic(std::vector<Diagnostic> *diagnostics, const std::string &code,
                           const std::string &message, const fs::path &path = {})
        {
            diagnostics->push_back({Severity::Error, code, message, path});
        }

        void AddWarning(std::vector<Diagnostic> *diagnostics, const std::string &code,
                        const std::string &message, const fs::path &path = {})
        {
            diagnostics->push_back({Severity::Warning, code, message, path});
        }

        bool ValidateRunProfile(const Profile &profile, std::vector<Diagnostic> *diagnostics, const fs::path &path)
        {
            if (profile.run_parse_error) {
                AddDiagnostic(diagnostics, "campaign.run_parse", *profile.run_parse_error, path);
                return false;
            }
            if (profile.run_days && (*profile.run_days < run_min_days || *profile.run_days > run_max_days)) {
                AddDiagnostic(diagnostics, "campaign.run_days", "run_days must be from 1 through 1000000", path);
                return false;
            }
            if (profile.run_days && profile.run_until_date_raw) {
                AddDiagnostic(diagnostics, "campaign.run_target", "run_days and run_until_date_raw are mutually exclusive", path);
                return false;
            }
            if (profile.run_timeout_seconds < run_min_timeout_seconds || profile.run_timeout_seconds > run_max_timeout_seconds) {
                AddDiagnostic(diagnostics, "campaign.run_timeout", "run_timeout_seconds must be from 1 through 86400", path);
                return false;
            }
            return true;
        }

        bool ValidateScriptingProfile(const Profile &profile, std::vector<Diagnostic> *diagnostics, const fs::path &path)
        {
            if (profile.scripts.size() > script_max_count) {
                AddDiagnostic(diagnostics, "scripting.scripts", "no more than 16 scripts may be selected", path);
                return false;
            }
            if (profile.script_instruction_budget < script_min_instruction_budget
                || profile.script_instruction_budget > script_max_instruction_budget) {
                AddDiagnostic(diagnostics, "scripting.instruction_budget", "script_instruction_budget must be from 1000 through 10000000", path);
                return false;
            }
            if (profile.script_memory_bytes < script_min_memory_bytes || profile.script_memory_bytes > script_max_memory_bytes) {
                AddDiagnostic(diagnostics, "scripting.memory_bytes", "script_memory_bytes must be from 262144 through 67108864", path);
                return false;
            }
            if (profile.script_queue_capacity < script_min_queue_capacity
                || profile.script_queue_capacity > script_max_queue_capacity) {
                AddDiagnostic(diagnostics, "scripting.queue_capacity", "script_queue_capacity must be from 16 through 4096", path);
                return false;
            }
            return true;
        }

        std::string WindowsError(const char *operation)
        {
            return std::string(operation) + " failed with Windows error " + std::to_string(GetLastError());
        }

        fs::path Absolute(const fs::path &path)
        {
            return fs::absolute(path).lexically_normal();
        }

        bool IsRegularFile(const fs::path &path)
        {
            std::error_code error;
            return fs::is_regular_file(path, error);
        }

        bool ContainsParentTraversal(const fs::path &path)
        {
            return std::any_of(path.begin(), path.end(), [](const fs::path &part) { return part == L".."; });
        }

        std::string Trim(std::string value)
        {
            const auto first = value.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) {
                return {};
            }
            return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
        }

        bool ReadQuoted(const std::string &value, std::string *result)
        {
            const auto first = value.find('"');
            const auto last = value.find_last_of('"');
            if (first == std::string::npos || first == last) {
                return false;
            }
            *result = value.substr(first + 1, last - first - 1);
            return true;
        }

        bool ParsePluginManifest(const fs::path &manifest_path, const fs::path &plugin_root,
                                 PluginManifest *manifest, std::vector<Diagnostic> *diagnostics)
        {
            try {
                const auto table = toml::parse_file(manifest_path.wstring());
                const auto id = table["id"].value<std::string>();
                const auto name = table["name"].value<std::string>();
                const auto module = table["module"].value<std::string>();
                const auto version = table["version"].value<std::string>();
                if (!id || !name || !module || !version) {
                    AddDiagnostic(diagnostics, "plugin.manifest", "plugin manifest requires id, name, module, and version", manifest_path);
                    return false;
                }
                manifest->id = *id;
                manifest->name = *name;
                manifest->module = *module;
                manifest->version = *version;
                auto read_ids = [&](const char *key, std::vector<std::string> *ids) {
                    const auto array = table[key].as_array();
                    if (array == nullptr) return true;
                    for (const auto &node : *array) {
                        const auto value = node.value<std::string>();
                        if (!value || value->empty()) {
                            AddDiagnostic(diagnostics, "plugin.manifest", std::string(key) + " must contain non-empty plugin IDs", manifest_path);
                            return false;
                        }
                        ids->push_back(*value);
                    }
                    return true;
                };
                if (!read_ids("dependencies", &manifest->dependencies)
                    || !read_ids("conflicts", &manifest->conflicts)) return false;
                manifest->manifest_path = Absolute(manifest_path);
                const auto module_path = fs::u8path(*module);
                manifest->module_path = Absolute(plugin_root / module_path);
                if (ContainsParentTraversal(module_path) || !IsPathContained(plugin_root, manifest->module_path)) {
                    AddDiagnostic(diagnostics, "plugin.module_traversal", "plugin module must be inside GAME_DIR/plugins", manifest_path);
                    return false;
                }
                if (!IsRegularFile(manifest->module_path)) {
                    AddDiagnostic(diagnostics, "plugin.module_missing", "plugin module does not exist", manifest->module_path);
                    return false;
                }
                return true;
            } catch (const std::exception &error) {
                AddDiagnostic(diagnostics, "plugin.parse", error.what(), manifest_path);
                return false;
            }
        }

        bool ParseModDescriptor(const fs::path &descriptor_path, const fs::path &game_dir,
                                ModDescriptor *descriptor, std::vector<Diagnostic> *diagnostics)
        {
            std::ifstream input(descriptor_path);
            if (!input) {
                AddDiagnostic(diagnostics, "mod.open", "could not open mod descriptor", descriptor_path);
                return false;
            }

            std::string line;
            bool valid = true;
            while (std::getline(input, line)) {
                const auto comment = line.find('#');
                line = Trim(line.substr(0, comment));
                const auto equals = line.find('=');
                if (equals == std::string::npos) {
                    continue;
                }
                const auto key = Trim(line.substr(0, equals));
                const auto value = Trim(line.substr(equals + 1));
                std::string parsed;
                if (key == "name" || key == "path" || key == "user_dir") {
                    if (!ReadQuoted(value, &parsed)) {
                        AddDiagnostic(diagnostics, "mod.parse", "mod descriptor values must be quoted", descriptor_path);
                        valid = false;
                        continue;
                    }
                    if (key == "name") descriptor->name = parsed;
                    if (key == "path") descriptor->path = parsed;
                    if (key == "user_dir") descriptor->user_dir = parsed;
                } else if (key == "dependencies") {
                    size_t position = 0;
                    while ((position = value.find('"', position)) != std::string::npos) {
                        const auto end = value.find('"', position + 1);
                        if (end == std::string::npos) {
                            AddDiagnostic(diagnostics, "mod.parse", "unterminated mod dependency", descriptor_path);
                            valid = false;
                            break;
                        }
                        descriptor->dependencies.push_back(value.substr(position + 1, end - position - 1));
                        position = end + 1;
                    }
                }
            }
            if (!valid || descriptor->name.empty() || descriptor->path.empty()) {
                AddDiagnostic(diagnostics, "mod.manifest", "mod descriptor requires name and path", descriptor_path);
                return false;
            }
            descriptor->descriptor_path = Absolute(descriptor_path);
            const auto content_path = fs::u8path(descriptor->path);
            descriptor->content_path = Absolute(game_dir / content_path);
            if (ContainsParentTraversal(content_path) || !IsPathContained(game_dir, descriptor->content_path)) {
                AddDiagnostic(diagnostics, "mod.path_traversal", "mod content path must be inside GAME_DIR", descriptor_path);
                return false;
            }
            if (!fs::is_directory(descriptor->content_path)) {
                AddDiagnostic(diagnostics, "mod.content_missing", "mod content directory does not exist", descriptor->content_path);
                return false;
            }
            return true;
        }

        bool IsX86PE(const fs::path &path, std::vector<Diagnostic> *diagnostics, const char *kind, bool require_dll = false)
        {
            std::ifstream input(path, std::ios::binary);
            IMAGE_DOS_HEADER dos{};
            input.read(reinterpret_cast<char *>(&dos), sizeof(dos));
            if (!input || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0) {
                AddDiagnostic(diagnostics, "binary.pe", std::string(kind) + " is not a PE file", path);
                return false;
            }
            input.seekg(dos.e_lfanew, std::ios::beg);
            DWORD signature = 0;
            IMAGE_FILE_HEADER header{};
            input.read(reinterpret_cast<char *>(&signature), sizeof(signature));
            input.read(reinterpret_cast<char *>(&header), sizeof(header));
            if (!input || signature != IMAGE_NT_SIGNATURE || header.Machine != IMAGE_FILE_MACHINE_I386) {
                AddDiagnostic(diagnostics, "binary.x86", std::string(kind) + " must be an x86 PE", path);
                return false;
            }
            WORD optional_magic = 0;
            input.read(reinterpret_cast<char *>(&optional_magic), sizeof(optional_magic));
            if (!input || header.SizeOfOptionalHeader < sizeof(optional_magic) || optional_magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
                AddDiagnostic(diagnostics, "binary.optional_header", std::string(kind) + " must have an x86 optional header", path);
                return false;
            }
            if (require_dll && (header.Characteristics & IMAGE_FILE_DLL) == 0) {
                AddDiagnostic(diagnostics, "binary.dll", std::string(kind) + " must be a DLL", path);
                return false;
            }
            return true;
        }

        bool ValidateSupportedExecutable(const fs::path &path, std::vector<Diagnostic> *diagnostics)
        {
            if (!IsRegularFile(path)) {
                AddDiagnostic(diagnostics, "game.missing", "Victoria II executable does not exist", path);
                return false;
            }
            if (fs::file_size(path) != supported_executable_size) {
                AddDiagnostic(diagnostics, "game.size", "unsupported v2game.exe size", path);
                return false;
            }
            BCRYPT_ALG_HANDLE algorithm = nullptr;
            BCRYPT_HASH_HANDLE hash = nullptr;
            std::vector<unsigned char> hash_object;
            auto cleanup = [&] {
                if (hash) BCryptDestroyHash(hash);
                if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
            };
            if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
                AddDiagnostic(diagnostics, "game.hash", "could not initialize SHA-256", path);
                return false;
            }
            DWORD object_size = 0;
            DWORD copied = 0;
            if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size), &copied, 0) < 0) {
                cleanup();
                AddDiagnostic(diagnostics, "game.hash", "could not initialize SHA-256", path);
                return false;
            }
            hash_object.resize(object_size);
            if (BCryptCreateHash(algorithm, &hash, hash_object.data(), object_size, nullptr, 0, 0) < 0) {
                cleanup();
                AddDiagnostic(diagnostics, "game.hash", "could not initialize SHA-256", path);
                return false;
            }
            std::ifstream input(path, std::ios::binary);
            std::array<char, 64 * 1024> buffer{};
            while (input.read(buffer.data(), buffer.size()) || input.gcount() > 0) {
                if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(input.gcount()), 0) < 0) {
                    cleanup();
                    AddDiagnostic(diagnostics, "game.hash", "could not hash v2game.exe", path);
                    return false;
                }
            }
            std::array<unsigned char, 32> actual_hash{};
            const bool complete = input.eof() && BCryptFinishHash(hash, actual_hash.data(), actual_hash.size(), 0) >= 0;
            cleanup();
            if (!complete || actual_hash != supported_executable_hash) {
                AddDiagnostic(diagnostics, "game.hash", "unsupported v2game.exe SHA-256", path);
                return false;
            }
            return IsX86PE(path, diagnostics, "Victoria II executable");
        }

        bool HasExport(const fs::path &module_path, const char *name, const char *code,
                       const char *description, std::vector<Diagnostic> *diagnostics)
        {
            HMODULE module = LoadLibraryExW(module_path.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
            if (!module) {
                AddDiagnostic(diagnostics, code, WindowsError("LoadLibraryExW"), module_path);
                return false;
            }
            const bool present = GetProcAddress(module, name) != nullptr;
            FreeLibrary(module);
            if (!present) {
                AddDiagnostic(diagnostics, code, description, module_path);
            }
            return present;
        }

        bool HasPluginExport(const fs::path &module_path, std::vector<Diagnostic> *diagnostics)
        {
            HMODULE module = LoadLibraryExW(module_path.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
            if (!module) {
                AddDiagnostic(diagnostics, "plugin.export", WindowsError("LoadLibraryExW"), module_path);
                return false;
            }
            const bool present = GetProcAddress(module, SMEDLEY_PLUGIN_GET_API_V1_SYMBOL) != nullptr
                || GetProcAddress(module, "CreatePlugin") != nullptr;
            FreeLibrary(module);
            if (!present) {
                AddDiagnostic(diagnostics, "plugin.export",
                              "plugin module exports neither SmedleyPluginGetApiV1 nor CreatePlugin", module_path);
            }
            return present;
        }

        bool WaitForRemoteThread(HANDLE thread, const char *operation, std::vector<Diagnostic> *diagnostics)
        {
            const auto result = WaitForSingleObject(thread, 30000);
            if (result == WAIT_TIMEOUT) {
                AddDiagnostic(diagnostics, "launch.timeout", std::string(operation) + " timed out");
                return false;
            } else if (result != WAIT_OBJECT_0) {
                AddDiagnostic(diagnostics, "launch.wait", WindowsError(operation));
                return false;
            }
            return true;
        }

        uintptr_t InjectLibrary(HANDLE process, const fs::path &library, std::vector<Diagnostic> *diagnostics)
        {
            const auto path = library.wstring();
            const SIZE_T bytes = (path.size() + 1) * sizeof(wchar_t);
            void *remote_path = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (!remote_path) {
                AddDiagnostic(diagnostics, "inject.allocate", WindowsError("VirtualAllocEx"), library);
                return 0;
            }
            if (!WriteProcessMemory(process, remote_path, path.c_str(), bytes, nullptr)) {
                AddDiagnostic(diagnostics, "inject.write", WindowsError("WriteProcessMemory"), library);
                VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
                return 0;
            }
            const auto load_library = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
            HANDLE thread = CreateRemoteThread(process, nullptr, 0, load_library, remote_path, 0, nullptr);
            if (!thread) {
                AddDiagnostic(diagnostics, "inject.thread", WindowsError("CreateRemoteThread"), library);
                VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
                return 0;
            }
            const bool completed = WaitForRemoteThread(thread, "DLL injection", diagnostics);
            DWORD remote_module = 0;
            if (completed) GetExitCodeThread(thread, &remote_module);
            CloseHandle(thread);
            // A timed-out thread may still read this allocation. The failed
            // launch path terminates the suspended child, allowing Windows to
            // reclaim the allocation safely.
            if (completed) VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
            if (completed && remote_module == 0) {
                AddDiagnostic(diagnostics, "inject.load", "target could not load library", library);
            }
            return remote_module;
        }

        void CallLoadPlugins(HANDLE process, const fs::path &kernel, uintptr_t remote_kernel, std::vector<Diagnostic> *diagnostics)
        {
            HMODULE local_kernel = LoadLibraryExW(kernel.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
            const auto local_entry = local_kernel ? reinterpret_cast<uintptr_t>(GetProcAddress(local_kernel, "LoadPluginsThread")) : 0;
            if (!local_entry) {
                if (local_kernel) FreeLibrary(local_kernel);
                AddDiagnostic(diagnostics, "kernel.export", "kernel does not export LoadPluginsThread", kernel);
                return;
            }
            const auto remote_entry = reinterpret_cast<LPTHREAD_START_ROUTINE>(remote_kernel + local_entry - reinterpret_cast<uintptr_t>(local_kernel));
            HANDLE thread = CreateRemoteThread(process, nullptr, 0, remote_entry, nullptr, 0, nullptr);
            FreeLibrary(local_kernel);
            if (!thread) {
                AddDiagnostic(diagnostics, "inject.thread", WindowsError("CreateRemoteThread"), kernel);
                return;
            }
            if (WaitForRemoteThread(thread, "plugin initialization", diagnostics)) {
                DWORD exit_code = 1;
                if (!GetExitCodeThread(thread, &exit_code)) {
                    AddDiagnostic(diagnostics, "kernel.plugin_initialization", WindowsError("GetExitCodeThread"), kernel);
                } else if (exit_code != 0) {
                    AddDiagnostic(diagnostics, "kernel.plugin_initialization", "plugin initialization failed in the injected process", kernel);
                }
            }
            CloseHandle(thread);
        }

        std::string EscapeToml(const std::string &value)
        {
            std::string escaped;
            for (const unsigned char character : value) {
                switch (character) {
                case '\\': escaped += "\\\\"; break;
                case '"': escaped += "\\\""; break;
                case '\b': escaped += "\\b"; break;
                case '\t': escaped += "\\t"; break;
                case '\n': escaped += "\\n"; break;
                case '\f': escaped += "\\f"; break;
                case '\r': escaped += "\\r"; break;
                default:
                    if (character < 0x20 || character == 0x7f) {
                        char encoded[7];
                        std::snprintf(encoded, sizeof(encoded), "\\u%04x", character);
                        escaped += encoded;
                    } else {
                        escaped += static_cast<char>(character);
                    }
                }
            }
            return escaped;
        }

        std::string WideToUtf8(const std::wstring &value)
        {
            const auto length = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
            std::string result(length, '\0');
            WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), length, nullptr, nullptr);
            return result;
        }

        void WriteTomlString(std::ofstream &output, const char *key, const fs::path &value)
        {
            output << key << " = \"" << EscapeToml(value.u8string()) << "\"\n";
        }

        std::wstring Utf8ToWide(const std::string &value)
        {
            if (value.empty()) return {};
            const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
            if (length <= 0) return {};
            std::wstring result(length, L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), length);
            return result;
        }

        std::string NewRunId()
        {
            GUID guid{};
            if (CoCreateGuid(&guid) == S_OK) {
                char value[37];
                std::snprintf(value, sizeof(value), "%08lx-%04x-%04x-%04x-%012llx",
                              guid.Data1, guid.Data2, guid.Data3,
                              (static_cast<unsigned>(guid.Data4[0]) << 8) | guid.Data4[1],
                              (static_cast<unsigned long long>(guid.Data4[2]) << 40)
                                  | (static_cast<unsigned long long>(guid.Data4[3]) << 32)
                                  | (static_cast<unsigned long long>(guid.Data4[4]) << 24)
                                  | (static_cast<unsigned long long>(guid.Data4[5]) << 16)
                                  | (static_cast<unsigned long long>(guid.Data4[6]) << 8)
                                  | guid.Data4[7]);
                return value;
            }
            return std::to_string(GetTickCount64()) + "-" + std::to_string(GetCurrentProcessId());
        }

        std::string UtcNow()
        {
            SYSTEMTIME time{};
            GetSystemTime(&time);
            char value[32];
            std::snprintf(value, sizeof(value), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
                          time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
            return value;
        }

        std::optional<fs::path> VictoriaBaseUserDirectory()
        {
            PWSTR documents = nullptr;
            if (SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documents) != S_OK) return std::nullopt;
            const fs::path result = fs::path(documents) / L"Paradox Interactive" / L"Victoria II";
            CoTaskMemFree(documents);
            return result;
        }

        bool IsSafeRunId(const std::string &run_id)
        {
            return !run_id.empty() && std::all_of(run_id.begin(), run_id.end(), [](unsigned char character) {
                return std::isalnum(character) || character == '-';
            });
        }

        bool IsUtcTimestamp(const std::string &timestamp)
        {
            if (timestamp.size() != 24 || timestamp[4] != '-' || timestamp[7] != '-' || timestamp[10] != 'T'
                || timestamp[13] != ':' || timestamp[16] != ':' || timestamp[19] != '.' || timestamp[23] != 'Z') return false;
            const bool digits = std::all_of(timestamp.begin(), timestamp.end(), [index = size_t{0}](unsigned char character) mutable {
                const bool separator = index == 4 || index == 7 || index == 10 || index == 13 || index == 16 || index == 19 || index == 23;
                ++index;
                return separator || std::isdigit(character);
            });
            if (!digits) return false;
            const auto number = [&](size_t offset, size_t length) { return std::stoi(timestamp.substr(offset, length)); };
            const int year = number(0, 4);
            const int month = number(5, 2);
            const int day = number(8, 2);
            const int hour = number(11, 2);
            const int minute = number(14, 2);
            const int second = number(17, 2);
            const bool leap_year = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
            const std::array<int, 12> days_in_month = {31, leap_year ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            return year >= 1 && month >= 1 && month <= 12 && day >= 1 && day <= days_in_month[month - 1]
                && hour <= 23 && minute <= 59 && second <= 59;
        }

        bool IsSafeModUserDirectory(const std::string &value)
        {
            const auto path = fs::u8path(value);
            return !value.empty() && !path.is_absolute() && !path.has_root_name()
                && !path.has_root_directory() && !ContainsParentTraversal(path);
        }

        bool IsKnownRunStatus(RunStatus status)
        {
            return status == RunStatus::PreflightFailed || status == RunStatus::CreateFailed
                || status == RunStatus::InjectionFailed || status == RunStatus::Started
                || status == RunStatus::Exited;
        }

        bool IsTelemetryCategory(const std::string &category)
        {
            return category == "lifecycle" || category == "state";
        }

        bool IsTelemetryCountryTag(const std::string &tag)
        {
            return tag.size() == 3 && std::all_of(tag.begin(), tag.end(), [](unsigned char character) {
                return (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9');
            });
        }

        bool ValidateTelemetryProfile(const Profile &profile, std::vector<Diagnostic> *diagnostics, const fs::path &path = {})
        {
            if (profile.telemetry_filter_parse_error) {
                AddDiagnostic(diagnostics, "telemetry.filter_parse", *profile.telemetry_filter_parse_error, path);
                return false;
            }
            if (profile.telemetry_output && profile.telemetry_output->empty()) {
                AddDiagnostic(diagnostics, "telemetry.output", "telemetry_output must not be empty", path);
                return false;
            }
            if (profile.telemetry_categories.empty()) {
                AddDiagnostic(diagnostics, "telemetry.categories", "telemetry_categories must not be empty", path);
                return false;
            }
            for (const auto &category : profile.telemetry_categories) {
                if (!IsTelemetryCategory(category)) {
                    AddDiagnostic(diagnostics, "telemetry.categories", "telemetry_categories must contain lifecycle and/or state", path);
                    return false;
                }
            }
            for (size_t index = 0; index < profile.telemetry_categories.size(); ++index) {
                if (std::find(profile.telemetry_categories.begin(), profile.telemetry_categories.begin() + index,
                              profile.telemetry_categories[index]) != profile.telemetry_categories.begin() + index) {
                    AddDiagnostic(diagnostics, "telemetry.categories", "telemetry_categories must not contain duplicates", path);
                    return false;
                }
            }
            for (size_t index = 0; index < profile.telemetry_country_tags.size(); ++index) {
                if (!IsTelemetryCountryTag(profile.telemetry_country_tags[index])) {
                    AddDiagnostic(diagnostics, "telemetry.country_tags", "telemetry_country_tags must contain normalized three-character ASCII tags", path);
                    return false;
                }
                if (std::find(profile.telemetry_country_tags.begin(), profile.telemetry_country_tags.begin() + index,
                              profile.telemetry_country_tags[index]) != profile.telemetry_country_tags.begin() + index) {
                    AddDiagnostic(diagnostics, "telemetry.country_tags", "telemetry_country_tags must not contain duplicates", path);
                    return false;
                }
            }
            if (profile.telemetry_start_date_raw && profile.telemetry_end_date_raw
                && *profile.telemetry_start_date_raw > *profile.telemetry_end_date_raw) {
                AddDiagnostic(diagnostics, "telemetry.date_range", "telemetry_start_date_raw must not exceed telemetry_end_date_raw", path);
                return false;
            }
            if (profile.telemetry_sample_days < telemetry_min_sample_days || profile.telemetry_sample_days > telemetry_max_sample_days) {
                AddDiagnostic(diagnostics, "telemetry.sample_days", "telemetry_sample_days must be from 1 through 365", path);
                return false;
            }
            if (profile.telemetry_queue_capacity < telemetry_min_queue_capacity || profile.telemetry_queue_capacity > telemetry_max_queue_capacity) {
                AddDiagnostic(diagnostics, "telemetry.queue_capacity", "telemetry_queue_capacity must be from 64 through 8192", path);
                return false;
            }
            return true;
        }

        bool ValidateTelemetryOutput(const Profile &profile, const fs::path &output, const LaunchPlan &plan, std::vector<Diagnostic> *diagnostics)
        {
            std::vector<fs::path> inputs = {plan.game_executable, plan.kernel};
            if (profile.save) inputs.push_back(*profile.save);
            for (const auto &mod : plan.mods) inputs.push_back(mod.descriptor_path);
            for (const auto &plugin : plan.plugins) {
                inputs.push_back(plugin.manifest_path);
                inputs.push_back(plugin.module_path);
            }
            for (const auto &script : plan.profile.scripts) inputs.push_back(script);
            const auto collides = std::any_of(inputs.begin(), inputs.end(), [&](const fs::path &input) {
                return _wcsicmp(input.lexically_normal().c_str(), output.lexically_normal().c_str()) == 0;
            });
            if (collides) {
                AddDiagnostic(diagnostics, "telemetry.output_collision", "telemetry output collides with a launch input", output);
                return false;
            }
            if (_wcsicmp(output.extension().c_str(), L".jsonl") != 0) {
                AddDiagnostic(diagnostics, "telemetry.output", "telemetry_output must end in .jsonl", output);
                return false;
            }
            const DWORD attributes = GetFileAttributesW(output.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES) {
                if ((attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
                    AddDiagnostic(diagnostics, "telemetry.output", "telemetry_output must be a normal file", output);
                    return false;
                }
                if (!profile.telemetry_overwrite) {
                    AddDiagnostic(diagnostics, "telemetry.output_exists", "telemetry output exists; set telemetry_overwrite to replace it", output);
                    return false;
                }
                auto identity = [](const fs::path &path, BY_HANDLE_FILE_INFORMATION *information) {
                    const HANDLE file = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                                    nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
                    if (file == INVALID_HANDLE_VALUE) return false;
                    const bool success = GetFileInformationByHandle(file, information) != FALSE;
                    CloseHandle(file);
                    return success;
                };
                BY_HANDLE_FILE_INFORMATION output_identity{};
                if (!identity(output, &output_identity)) {
                    AddDiagnostic(diagnostics, "telemetry.output_identity", "could not verify existing telemetry output identity", output);
                    return false;
                }
                for (const auto &input : inputs) {
                    if (GetFileAttributesW(input.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
                    BY_HANDLE_FILE_INFORMATION input_identity{};
                    if (!identity(input, &input_identity)) {
                        AddDiagnostic(diagnostics, "telemetry.output_identity", "could not verify launch input identity", input);
                        return false;
                    }
                    if (output_identity.dwVolumeSerialNumber == input_identity.dwVolumeSerialNumber
                        && output_identity.nFileIndexHigh == input_identity.nFileIndexHigh
                        && output_identity.nFileIndexLow == input_identity.nFileIndexLow) {
                        AddDiagnostic(diagnostics, "telemetry.output_collision", "telemetry output aliases a launch input", output);
                        return false;
                    }
                }
            }
            return true;
        }

        std::wstring TelemetryCategoriesArgument(const std::vector<std::string> &categories)
        {
            std::wstring result;
            for (const auto &category : categories) {
                if (!result.empty()) result += L',';
                result.append(category.begin(), category.end());
            }
            return result;
        }

        RunRecord BuildRunRecord(const LaunchPlan &plan)
        {
            RunRecord record;
            record.run_id = NewRunId();
            record.started_at_utc = UtcNow();
            record.profile_name = plan.profile.name;
            record.injected = plan.profile.inject;
            record.safe_mode = !plan.profile.inject;
            record.executable = plan.game_executable;
            record.command_line = plan.command_line;
            record.save = plan.profile.save;
            record.observer = plan.profile.observer;
            record.speed = plan.profile.speed;
            record.start_paused = plan.profile.start_paused;
            for (const auto &mod : plan.mods) record.mod_descriptors.push_back(mod.descriptor_path);
            for (const auto &plugin : plan.plugins) record.plugins.push_back({plugin.id, plugin.manifest_path});
            record.scripts = plan.profile.scripts;
            if (const auto base_user_dir = VictoriaBaseUserDirectory()) {
                record.links.smedley_log = *base_user_dir / L"logs" / L"smedley.log";
                if (const auto user_dir = ResolveVictoriaUserDirectory(*base_user_dir, plan.mods)) {
                    record.links.victoria_user_dir = *user_dir;
                    record.links.victoria_system_log = *user_dir / L"logs" / L"system.log";
                }
            }
            if (std::any_of(plan.plugins.begin(), plan.plugins.end(), [](const PluginManifest &plugin) { return plugin.id == "economy_trace"; })) {
                record.links.economy_trace = plan.profile.game_dir / L"economy_trace.csv";
            }
            if (plan.profile.inject && plan.profile.telemetry_enabled) {
                record.links.telemetry_trace = plan.profile.telemetry_output
                    ? *plan.profile.telemetry_output
                    : DefaultTraceDirectory() / fs::u8path(record.run_id + ".jsonl");
            }
            record.links.source_save = plan.profile.save;
            return record;
        }

        void WriteRunPathArray(std::ofstream &output, const char *key, const std::vector<fs::path> &paths)
        {
            output << key << " = [";
            for (size_t index = 0; index < paths.size(); ++index) {
                if (index != 0) output << ", ";
                output << "\"" << EscapeToml(paths[index].u8string()) << "\"";
            }
            output << "]\n";
        }

        void WriteOptionalRunPath(std::ofstream &output, const char *key, const std::optional<fs::path> &path)
        {
            if (path) WriteTomlString(output, key, *path);
        }

        const char *SeverityName(Severity severity)
        {
            switch (severity) {
            case Severity::Info: return "info";
            case Severity::Warning: return "warning";
            case Severity::Error: return "error";
            }
            return "error";
        }

        std::optional<Severity> ParseSeverity(const std::string &value)
        {
            if (value == "info") return Severity::Info;
            if (value == "warning") return Severity::Warning;
            if (value == "error") return Severity::Error;
            return std::nullopt;
        }

        bool ReadRunRecord(const fs::path &path, RunRecord *record, std::vector<Diagnostic> *diagnostics)
        {
            try {
                const auto table = toml::parse_file(path.wstring());
                const auto schema_version = table["schema_version"].value<int>();
                const auto run_id = table["run_id"].value<std::string>();
                const auto started_at_utc = table["started_at_utc"].value<std::string>();
                const auto status = table["status"].value<std::string>();
                const auto profile_name = table["profile_name"].value<std::string>();
                const auto injected = table["injected"].value<bool>();
                const auto safe_mode = table["safe_mode"].value<bool>();
                const auto executable = table["executable"].value<std::string>();
                const auto command_line = table["command_line"].value<std::string>();
                const auto observer = table["observer"].value<bool>();
                const auto speed = table["speed"].value<int>();
                const auto start_paused = table["start_paused"].value<bool>();
                if (!schema_version || *schema_version != 1 || !run_id || !IsSafeRunId(*run_id) || !started_at_utc || !IsUtcTimestamp(*started_at_utc) || !status
                    || !profile_name || !injected || !safe_mode || !executable || !command_line || !observer || !speed || *speed < 1 || *speed > 5 || !start_paused
                    || path.stem().u8string() != *run_id) {
                    AddDiagnostic(diagnostics, "run.schema", "run record has missing or invalid required fields", path);
                    return false;
                }
                RunRecord loaded;
                loaded.schema_version = *schema_version;
                loaded.run_id = *run_id;
                loaded.started_at_utc = *started_at_utc;
                if (*status == "preflight_failed") loaded.status = RunStatus::PreflightFailed;
                else if (*status == "create_failed") loaded.status = RunStatus::CreateFailed;
                else if (*status == "injection_failed") loaded.status = RunStatus::InjectionFailed;
                else if (*status == "started") loaded.status = RunStatus::Started;
                else if (*status == "exited") loaded.status = RunStatus::Exited;
                else {
                    AddDiagnostic(diagnostics, "run.schema", "run record has an unknown status", path);
                    return false;
                }
                loaded.profile_name = *profile_name;
                loaded.injected = *injected;
                loaded.safe_mode = *safe_mode;
                loaded.executable = fs::u8path(*executable);
                loaded.command_line = Utf8ToWide(*command_line);
                if (!command_line->empty() && loaded.command_line.empty()) {
                    AddDiagnostic(diagnostics, "run.schema", "run record command_line is not valid UTF-8", path);
                    return false;
                }
                loaded.observer = *observer;
                loaded.speed = *speed;
                loaded.start_paused = *start_paused;
                auto read_u32 = [&](const char *key, std::optional<std::uint32_t> *destination) {
                    if (!table.contains(key)) return;
                    const auto value = table[key].value<std::int64_t>();
                    if (!value || *value < 0 || static_cast<std::uint64_t>(*value) > (std::numeric_limits<std::uint32_t>::max)()) {
                        throw std::runtime_error(std::string(key) + " must be a 32-bit unsigned integer");
                    }
                    *destination = static_cast<std::uint32_t>(*value);
                };
                read_u32("process_id", &loaded.process_id);
                read_u32("exit_code", &loaded.exit_code);
                if (const auto value = table["save"].value<std::string>()) loaded.save = fs::u8path(*value);
                if (const auto mods = table["mod_descriptors"].as_array()) {
                    for (const auto &node : *mods) {
                        const auto value = node.value<std::string>();
                        if (!value) throw std::runtime_error("mod_descriptors must contain strings");
                        loaded.mod_descriptors.push_back(fs::u8path(*value));
                    }
                }
                if (const auto plugins = table["plugins"].as_array()) {
                    for (const auto &node : *plugins) {
                        const auto *plugin = node.as_table();
                        if (!plugin) throw std::runtime_error("plugins must contain tables");
                        const auto id = (*plugin)["id"].value<std::string>();
                        const auto manifest_path = (*plugin)["manifest_path"].value<std::string>();
                        if (!id || !manifest_path) throw std::runtime_error("plugins require id and manifest_path");
                        loaded.plugins.push_back({*id, fs::u8path(*manifest_path)});
                    }
                }
                if (const auto scripts = table["scripts"].as_array()) {
                    for (const auto &node : *scripts) {
                        const auto value = node.value<std::string>();
                        if (!value) throw std::runtime_error("scripts must contain strings");
                        loaded.scripts.push_back(fs::u8path(*value));
                    }
                }
                if (const auto *links = table["links"].as_table()) {
                    auto read_link = [&](const char *key, std::optional<fs::path> *destination) {
                        if (const auto value = (*links)[key].value<std::string>()) *destination = fs::u8path(*value);
                    };
                    read_link("smedley_log", &loaded.links.smedley_log);
                    read_link("victoria_system_log", &loaded.links.victoria_system_log);
                    read_link("victoria_user_dir", &loaded.links.victoria_user_dir);
                    read_link("economy_trace", &loaded.links.economy_trace);
                    read_link("telemetry_trace", &loaded.links.telemetry_trace);
                    read_link("source_save", &loaded.links.source_save);
                }
                if (const auto diagnostic_nodes = table["diagnostics"].as_array()) {
                    for (const auto &node : *diagnostic_nodes) {
                        const auto *diagnostic = node.as_table();
                        if (!diagnostic) throw std::runtime_error("diagnostics must contain tables");
                        const auto severity = (*diagnostic)["severity"].value<std::string>();
                        const auto code = (*diagnostic)["code"].value<std::string>();
                        const auto message = (*diagnostic)["message"].value<std::string>();
                        if (!severity || !code || !message) throw std::runtime_error("diagnostics require severity, code, and message");
                        const auto parsed_severity = ParseSeverity(*severity);
                        if (!parsed_severity) throw std::runtime_error("diagnostics contain an unknown severity");
                        Diagnostic parsed{*parsed_severity, *code, *message, {}};
                        if (const auto diagnostic_path = (*diagnostic)["path"].value<std::string>()) parsed.path = fs::u8path(*diagnostic_path);
                        loaded.diagnostics.push_back(std::move(parsed));
                    }
                }
                std::error_code absolute_error;
                loaded.metadata_path = fs::absolute(path, absolute_error);
                if (absolute_error) loaded.metadata_path = path;
                *record = std::move(loaded);
                return true;
            } catch (const std::exception &error) {
                AddDiagnostic(diagnostics, "run.parse", error.what(), path);
                return false;
            }
        }
    }

    bool HasErrors(const std::vector<Diagnostic> &diagnostics)
    {
        return std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic &diagnostic) {
            return diagnostic.severity == Severity::Error;
        });
    }

    const char *RunStatusName(RunStatus status)
    {
        switch (status) {
        case RunStatus::PreflightFailed: return "preflight_failed";
        case RunStatus::CreateFailed: return "create_failed";
        case RunStatus::InjectionFailed: return "injection_failed";
        case RunStatus::Started: return "started";
        case RunStatus::Exited: return "exited";
        }
        return "unknown";
    }

    fs::path DefaultRunDirectory()
    {
        PWSTR local_app_data = nullptr;
        if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local_app_data) != S_OK) return {};
        const fs::path result = fs::path(local_app_data) / L"Smedley" / L"runs";
        CoTaskMemFree(local_app_data);
        return result;
    }

    fs::path DefaultTraceDirectory()
    {
        PWSTR local_app_data = nullptr;
        if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local_app_data) != S_OK) return {};
        const fs::path result = fs::path(local_app_data) / L"Smedley" / L"traces";
        CoTaskMemFree(local_app_data);
        return result;
    }

    RunRecord CreateRunRecord(const LaunchPlan &plan)
    {
        return BuildRunRecord(plan);
    }

    bool SaveRunRecord(const fs::path &run_directory, const RunRecord &record, std::vector<Diagnostic> *diagnostics)
    {
        if (run_directory.empty() || !IsSafeRunId(record.run_id) || record.schema_version != 1
            || !IsKnownRunStatus(record.status)
            || !IsUtcTimestamp(record.started_at_utc) || record.speed < 1 || record.speed > 5) {
            AddDiagnostic(diagnostics, "run.write", "run directory or record fields are invalid", run_directory);
            return false;
        }
        std::error_code error;
        fs::create_directories(run_directory, error);
        if (error) {
            AddDiagnostic(diagnostics, "run.write", error.message(), run_directory);
            return false;
        }
        const auto destination = run_directory / fs::u8path(record.run_id + ".toml");
        const auto temporary = run_directory / fs::u8path(record.run_id + "." + std::to_string(GetCurrentProcessId())
                                                           + "." + std::to_string(GetTickCount64()) + ".tmp");
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) {
                AddDiagnostic(diagnostics, "run.write", "could not create temporary run record", temporary);
                return false;
            }
            output << "# Smedley launcher run schema v1\n";
            output << "schema_version = " << record.schema_version << "\n";
            output << "run_id = \"" << EscapeToml(record.run_id) << "\"\n";
            output << "started_at_utc = \"" << EscapeToml(record.started_at_utc) << "\"\n";
            output << "status = \"" << RunStatusName(record.status) << "\"\n";
            if (record.process_id) output << "process_id = " << *record.process_id << "\n";
            if (record.exit_code) output << "exit_code = " << *record.exit_code << "\n";
            output << "profile_name = \"" << EscapeToml(record.profile_name) << "\"\n";
            output << "injected = " << (record.injected ? "true" : "false") << "\n";
            output << "safe_mode = " << (record.safe_mode ? "true" : "false") << "\n";
            WriteTomlString(output, "executable", record.executable);
            output << "command_line = \"" << EscapeToml(WideToUtf8(record.command_line)) << "\"\n";
            WriteRunPathArray(output, "mod_descriptors", record.mod_descriptors);
            WriteRunPathArray(output, "scripts", record.scripts);
            if (record.save) WriteTomlString(output, "save", *record.save);
            output << "observer = " << (record.observer ? "true" : "false") << "\n";
            output << "speed = " << record.speed << "\n";
            output << "start_paused = " << (record.start_paused ? "true" : "false") << "\n\n";
            for (const auto &plugin : record.plugins) {
                output << "[[plugins]]\n";
                output << "id = \"" << EscapeToml(plugin.id) << "\"\n";
                WriteTomlString(output, "manifest_path", plugin.manifest_path);
                output << "\n";
            }
            for (const auto &diagnostic : record.diagnostics) {
                output << "[[diagnostics]]\n";
                output << "severity = \"" << SeverityName(diagnostic.severity) << "\"\n";
                output << "code = \"" << EscapeToml(diagnostic.code) << "\"\n";
                output << "message = \"" << EscapeToml(diagnostic.message) << "\"\n";
                if (!diagnostic.path.empty()) WriteTomlString(output, "path", diagnostic.path);
                output << "\n";
            }
            output << "[links]\n";
            WriteOptionalRunPath(output, "smedley_log", record.links.smedley_log);
            WriteOptionalRunPath(output, "victoria_system_log", record.links.victoria_system_log);
            WriteOptionalRunPath(output, "victoria_user_dir", record.links.victoria_user_dir);
            WriteOptionalRunPath(output, "economy_trace", record.links.economy_trace);
            WriteOptionalRunPath(output, "telemetry_trace", record.links.telemetry_trace);
            WriteOptionalRunPath(output, "source_save", record.links.source_save);
            output.flush();
            if (!output) {
                output.close();
                fs::remove(temporary, error);
                AddDiagnostic(diagnostics, "run.write", "could not write temporary run record", temporary);
                return false;
            }
        }
        if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            const auto message = WindowsError("MoveFileExW");
            fs::remove(temporary, error);
            AddDiagnostic(diagnostics, "run.write", message, destination);
            return false;
        }
        return true;
    }

    std::vector<RunRecord> LoadRunHistory(const fs::path &run_directory, size_t limit, std::vector<Diagnostic> *diagnostics)
    {
        std::vector<RunRecord> records;
        std::error_code error;
        const bool exists = fs::exists(run_directory, error);
        if (error) {
            AddDiagnostic(diagnostics, "run.directory", error.message(), run_directory);
            return records;
        }
        if (!exists) {
            return records;
        }
        fs::directory_iterator iterator(run_directory, fs::directory_options::skip_permission_denied, error);
        if (error) {
            AddDiagnostic(diagnostics, "run.directory", error.message(), run_directory);
            return records;
        }
        const fs::directory_iterator end;
        while (iterator != end) {
            const auto entry = *iterator;
            iterator.increment(error);
            if (error) {
                AddDiagnostic(diagnostics, "run.directory", error.message(), run_directory);
                break;
            }
            std::error_code entry_error;
            if (!entry.is_regular_file(entry_error)) {
                if (entry_error) AddDiagnostic(diagnostics, "run.entry", entry_error.message(), entry.path());
                continue;
            }
            if (entry.path().extension() != L".toml") continue;
            RunRecord record;
            if (ReadRunRecord(entry.path(), &record, diagnostics)) records.push_back(std::move(record));
        }
        std::sort(records.begin(), records.end(), [](const RunRecord &left, const RunRecord &right) {
            if (left.started_at_utc != right.started_at_utc) return left.started_at_utc > right.started_at_utc;
            return left.run_id > right.run_id;
        });
        if (records.size() > limit) records.resize(limit);
        return records;
    }

    std::vector<RunRecord> LoadRunHistory(size_t limit, std::vector<Diagnostic> *diagnostics)
    {
        return LoadRunHistory(DefaultRunDirectory(), limit, diagnostics);
    }

    bool IsPathContained(const fs::path &root, const fs::path &path)
    {
        std::error_code error;
        const auto canonical_root = fs::weakly_canonical(root, error);
        if (error) return false;
        const auto canonical_path = fs::weakly_canonical(path, error);
        if (error) return false;
        const auto relative = canonical_path.lexically_relative(canonical_root);
        if (relative.empty() || relative.is_absolute()) return false;
        return std::none_of(relative.begin(), relative.end(), [](const fs::path &part) { return part == L".."; });
    }

    std::optional<fs::path> ResolveVictoriaUserDirectory(const fs::path &base,
                                                         const std::vector<ModDescriptor> &mods)
    {
        std::vector<fs::path> selected;
        for (const auto &mod : mods) {
            if (mod.user_dir.empty()) continue;
            if (!IsSafeModUserDirectory(mod.user_dir)) return std::nullopt;
            const auto candidate = (base / fs::u8path(mod.user_dir)).lexically_normal();
            const auto duplicate = std::find_if(selected.begin(), selected.end(), [&](const fs::path &existing) {
                const auto &existing_value = existing.native();
                const auto &candidate_value = candidate.native();
                return CompareStringOrdinal(existing_value.data(), static_cast<int>(existing_value.size()),
                                            candidate_value.data(), static_cast<int>(candidate_value.size()), TRUE)
                    == CSTR_EQUAL;
            });
            if (duplicate == selected.end()) selected.push_back(candidate);
        }
        if (selected.empty()) return base.lexically_normal();
        if (selected.size() == 1) return selected.front();
        return std::nullopt;
    }

    PluginDiscovery DiscoverPlugins(const fs::path &game_dir)
    {
        PluginDiscovery result;
        const auto root = Absolute(game_dir) / L"plugins";
        std::error_code error;
        if (!fs::is_directory(root, error)) {
            AddDiagnostic(&result.diagnostics, "plugin.directory_missing", "GAME_DIR/plugins does not exist", root);
            return result;
        }
        for (const auto &entry : fs::directory_iterator(root, error)) {
            if (error) {
                AddDiagnostic(&result.diagnostics, "plugin.directory", error.message(), root);
                break;
            }
            if (!entry.is_regular_file() || entry.path().extension() != L".toml") continue;
            PluginManifest manifest;
            if (ParsePluginManifest(entry.path(), root, &manifest, &result.diagnostics)) result.plugins.push_back(std::move(manifest));
        }
        std::sort(result.plugins.begin(), result.plugins.end(), [](const auto &left, const auto &right) { return left.id < right.id; });
        for (size_t i = 1; i < result.plugins.size(); ++i) {
            if (result.plugins[i - 1].id == result.plugins[i].id) {
                AddDiagnostic(&result.diagnostics, "plugin.duplicate_id", "duplicate plugin ID", result.plugins[i].manifest_path);
            }
        }
        return result;
    }

    ModDiscovery DiscoverMods(const fs::path &game_dir)
    {
        ModDiscovery result;
        const auto absolute_game_dir = Absolute(game_dir);
        const auto root = absolute_game_dir / L"mod";
        std::error_code error;
        if (!fs::is_directory(root, error)) {
            AddDiagnostic(&result.diagnostics, "mod.directory_missing", "GAME_DIR/mod does not exist", root);
            return result;
        }
        for (const auto &entry : fs::directory_iterator(root, error)) {
            if (error) {
                AddDiagnostic(&result.diagnostics, "mod.directory", error.message(), root);
                break;
            }
            if (!entry.is_regular_file() || entry.path().extension() != L".mod") continue;
            ModDescriptor descriptor;
            if (ParseModDescriptor(entry.path(), absolute_game_dir, &descriptor, &result.diagnostics)) result.mods.push_back(std::move(descriptor));
        }
        std::sort(result.mods.begin(), result.mods.end(), [](const auto &left, const auto &right) { return left.descriptor_path.filename() < right.descriptor_path.filename(); });
        return result;
    }

    bool LoadProfile(const fs::path &path, Profile *profile, std::vector<Diagnostic> *diagnostics)
    {
        try {
            const auto table = toml::parse_file(path.wstring());
            const auto name = table["name"].value<std::string>();
            const auto game_dir = table["game_dir"].value<std::string>();
            if (!name || !game_dir) {
                AddDiagnostic(diagnostics, "profile.schema", "profile requires name and game_dir", path);
                return false;
            }
            Profile loaded;
            loaded.name = *name;
            loaded.game_dir = fs::u8path(*game_dir);
            if (const auto value = table["kernel"].value<std::string>()) loaded.kernel = fs::u8path(*value);
            if (const auto value = table["inject"].value<bool>()) loaded.inject = *value;
            if (const auto value = table["save"].value<std::string>()) loaded.save = fs::u8path(*value);
            if (const auto value = table["observer"].value<bool>()) loaded.observer = *value;
            if (const auto value = table["view_tag"].value<std::string>()) loaded.view_tag = std::wstring(value->begin(), value->end());
            if (table.contains("speed")) {
                const auto value = table["speed"].value<int>();
                if (!value) {
                    AddDiagnostic(diagnostics, "profile.schema", "speed must be an integer", path);
                    return false;
                }
                loaded.speed = *value;
            }
            if (table.contains("start_paused")) {
                const auto value = table["start_paused"].value<bool>();
                if (!value) {
                    AddDiagnostic(diagnostics, "profile.schema", "start_paused must be a boolean", path);
                    return false;
                }
                loaded.start_paused = *value;
            }
            auto read_optional_run_integer = [&](const char *key, std::optional<int> *destination) {
                if (!table.contains(key)) return true;
                const auto value = table[key].value<int>();
                if (!value) {
                    AddDiagnostic(diagnostics, "profile.schema", std::string(key) + " must be an integer", path);
                    return false;
                }
                *destination = *value;
                return true;
            };
            if (!read_optional_run_integer("run_days", &loaded.run_days)
                || !read_optional_run_integer("run_until_date_raw", &loaded.run_until_date_raw)) return false;
            if (table.contains("run_timeout_seconds")) {
                const auto value = table["run_timeout_seconds"].value<int>();
                if (!value) {
                    AddDiagnostic(diagnostics, "profile.schema", "run_timeout_seconds must be an integer", path);
                    return false;
                }
                loaded.run_timeout_seconds = *value;
            }
            if (const auto value = table["detach"].value<bool>()) loaded.detach = *value;
            if (table.contains("telemetry_enabled")) {
                const auto value = table["telemetry_enabled"].value<bool>();
                if (!value) {
                    AddDiagnostic(diagnostics, "profile.schema", "telemetry_enabled must be a boolean", path);
                    return false;
                }
                loaded.telemetry_enabled = *value;
            }
            if (table.contains("telemetry_output")) {
                const auto value = table["telemetry_output"].value<std::string>();
                if (!value || value->empty()) {
                    AddDiagnostic(diagnostics, "profile.schema", "telemetry_output must be a non-empty string", path);
                    return false;
                }
                loaded.telemetry_output = fs::u8path(*value);
            }
            if (table.contains("telemetry_categories")) {
                const auto *categories = table["telemetry_categories"].as_array();
                if (categories == nullptr) {
                    AddDiagnostic(diagnostics, "profile.schema", "telemetry_categories must be an array of strings", path);
                    return false;
                }
                loaded.telemetry_categories.clear();
                for (const auto &node : *categories) {
                    const auto value = node.value<std::string>();
                    if (!value || value->empty()) {
                        AddDiagnostic(diagnostics, "profile.schema", "telemetry_categories must contain non-empty strings", path);
                        return false;
                    }
                    loaded.telemetry_categories.push_back(*value);
                }
            }
            if (table.contains("telemetry_sample_days")) {
                const auto value = table["telemetry_sample_days"].value<int>();
                if (!value) {
                    AddDiagnostic(diagnostics, "profile.schema", "telemetry_sample_days must be an integer", path);
                    return false;
                }
                loaded.telemetry_sample_days = *value;
            }
            if (table.contains("telemetry_country_tags")) {
                const auto *tags = table["telemetry_country_tags"].as_array();
                if (tags == nullptr) {
                    AddDiagnostic(diagnostics, "profile.schema", "telemetry_country_tags must be an array of strings", path);
                    return false;
                }
                for (const auto &node : *tags) {
                    const auto value = node.value<std::string>();
                    if (!value) {
                        AddDiagnostic(diagnostics, "profile.schema", "telemetry_country_tags must contain strings", path);
                        return false;
                    }
                    loaded.telemetry_country_tags.push_back(*value);
                }
            }
            auto read_optional_raw_date = [&](const char *key, std::optional<int> *destination) {
                if (!table.contains(key)) return true;
                const auto value = table[key].value<int>();
                if (!value) {
                    AddDiagnostic(diagnostics, "profile.schema", std::string(key) + " must be an integer", path);
                    return false;
                }
                *destination = *value;
                return true;
            };
            if (!read_optional_raw_date("telemetry_start_date_raw", &loaded.telemetry_start_date_raw)
                || !read_optional_raw_date("telemetry_end_date_raw", &loaded.telemetry_end_date_raw)) return false;
            if (table.contains("telemetry_queue_capacity")) {
                const auto value = table["telemetry_queue_capacity"].value<int>();
                if (!value) {
                    AddDiagnostic(diagnostics, "profile.schema", "telemetry_queue_capacity must be an integer", path);
                    return false;
                }
                loaded.telemetry_queue_capacity = *value;
            }
            if (table.contains("telemetry_overwrite")) {
                const auto value = table["telemetry_overwrite"].value<bool>();
                if (!value) {
                    AddDiagnostic(diagnostics, "profile.schema", "telemetry_overwrite must be a boolean", path);
                    return false;
                }
                loaded.telemetry_overwrite = *value;
            }
            auto read_script_integer = [&](const char *key, int *destination) {
                if (!table.contains(key)) return true;
                const auto value = table[key].value<int>();
                if (!value) {
                    AddDiagnostic(diagnostics, "profile.schema", std::string(key) + " must be an integer", path);
                    return false;
                }
                *destination = *value;
                return true;
            };
            if (!read_script_integer("script_instruction_budget", &loaded.script_instruction_budget)
                || !read_script_integer("script_memory_bytes", &loaded.script_memory_bytes)
                || !read_script_integer("script_queue_capacity", &loaded.script_queue_capacity)) return false;
            auto read_paths = [&](const char *key, std::vector<fs::path> *paths) {
                if (!table.contains(key)) return true;
                const auto array = table[key].as_array();
                if (array == nullptr) {
                    AddDiagnostic(diagnostics, "profile.schema", std::string(key) + " must be an array of strings", path);
                    return false;
                }
                for (const auto &node : *array) {
                    const auto value = node.value<std::string>();
                    if (!value) {
                        AddDiagnostic(diagnostics, "profile.schema", std::string(key) + " must contain strings", path);
                        return false;
                    }
                    paths->emplace_back(fs::u8path(*value));
                }
                return true;
            };
            if (!read_paths("mods", &loaded.mods) || !read_paths("plugins", &loaded.plugins)
                || !read_paths("scripts", &loaded.scripts)) return false;
            if (!ValidateTelemetryProfile(loaded, diagnostics, path)) return false;
            if (!ValidateRunProfile(loaded, diagnostics, path)) return false;
            if (!ValidateScriptingProfile(loaded, diagnostics, path)) return false;
            *profile = std::move(loaded);
            return true;
        } catch (const std::exception &error) {
            AddDiagnostic(diagnostics, "profile.parse", error.what(), path);
            return false;
        }
    }

    bool SaveProfile(const fs::path &path, const Profile &profile, std::vector<Diagnostic> *diagnostics)
    {
        if (!ValidateTelemetryProfile(profile, diagnostics, path) || !ValidateRunProfile(profile, diagnostics, path)
            || !ValidateScriptingProfile(profile, diagnostics, path)) return false;
        std::ofstream output(path, std::ios::trunc);
        if (!output) {
            AddDiagnostic(diagnostics, "profile.write", "could not write profile", path);
            return false;
        }
        output << "# Smedley launcher profile schema v1\n";
        output << "name = \"" << EscapeToml(profile.name) << "\"\n";
        WriteTomlString(output, "game_dir", profile.game_dir);
        if (profile.kernel) WriteTomlString(output, "kernel", *profile.kernel);
        output << "inject = " << (profile.inject ? "true" : "false") << "\n";
        auto write_paths = [&](const char *key, const std::vector<fs::path> &paths) {
            output << key << " = [";
            for (size_t i = 0; i < paths.size(); ++i) {
                if (i) output << ", ";
                output << "\"" << EscapeToml(paths[i].u8string()) << "\"";
            }
            output << "]\n";
        };
        write_paths("mods", profile.mods);
        write_paths("plugins", profile.plugins);
        if (profile.save) WriteTomlString(output, "save", *profile.save);
        output << "observer = " << (profile.observer ? "true" : "false") << "\n";
        if (profile.view_tag) output << "view_tag = \"" << EscapeToml(WideToUtf8(*profile.view_tag)) << "\"\n";
        output << "speed = " << profile.speed << "\n";
        output << "start_paused = " << (profile.start_paused ? "true" : "false") << "\n";
        output << "detach = " << (profile.detach ? "true" : "false") << "\n";
        if (profile.run_days) output << "run_days = " << *profile.run_days << "\n";
        if (profile.run_until_date_raw) output << "run_until_date_raw = " << *profile.run_until_date_raw << "\n";
        output << "run_timeout_seconds = " << profile.run_timeout_seconds << "\n";
        output << "telemetry_enabled = " << (profile.telemetry_enabled ? "true" : "false") << "\n";
        if (profile.telemetry_output) WriteTomlString(output, "telemetry_output", *profile.telemetry_output);
        output << "telemetry_categories = [";
        for (size_t i = 0; i < profile.telemetry_categories.size(); ++i) {
            if (i) output << ", ";
            output << "\"" << EscapeToml(profile.telemetry_categories[i]) << "\"";
        }
        output << "]\n";
        output << "telemetry_country_tags = [";
        for (size_t i = 0; i < profile.telemetry_country_tags.size(); ++i) {
            if (i) output << ", ";
            output << "\"" << EscapeToml(profile.telemetry_country_tags[i]) << "\"";
        }
        output << "]\n";
        if (profile.telemetry_start_date_raw) output << "telemetry_start_date_raw = " << *profile.telemetry_start_date_raw << "\n";
        if (profile.telemetry_end_date_raw) output << "telemetry_end_date_raw = " << *profile.telemetry_end_date_raw << "\n";
        output << "telemetry_sample_days = " << profile.telemetry_sample_days << "\n";
        output << "telemetry_queue_capacity = " << profile.telemetry_queue_capacity << "\n";
        output << "telemetry_overwrite = " << (profile.telemetry_overwrite ? "true" : "false") << "\n";
        write_paths("scripts", profile.scripts);
        output << "script_instruction_budget = " << profile.script_instruction_budget << "\n";
        output << "script_memory_bytes = " << profile.script_memory_bytes << "\n";
        output << "script_queue_capacity = " << profile.script_queue_capacity << "\n";
        if (!output) {
            AddDiagnostic(diagnostics, "profile.write", "could not write profile", path);
            return false;
        }
        return true;
    }

    std::wstring QuoteWindowsArgument(const std::wstring &argument)
    {
        if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) return argument;
        std::wstring result = L"\"";
        size_t slashes = 0;
        for (const wchar_t character : argument) {
            if (character == L'\\') {
                ++slashes;
            } else if (character == L'"') {
                result.append(slashes * 2 + 1, L'\\');
                result += character;
                slashes = 0;
            } else {
                result.append(slashes, L'\\');
                result += character;
                slashes = 0;
            }
        }
        result.append(slashes * 2, L'\\');
        return result + L"\"";
    }

    std::wstring BuildWindowsCommandLine(const std::vector<std::wstring> &arguments)
    {
        std::wstring command_line;
        for (const auto &argument : arguments) {
            if (!command_line.empty()) command_line += L' ';
            command_line += QuoteWindowsArgument(argument);
        }
        return command_line;
    }

    std::wstring BuildInjectedCommandLine(const LaunchPlan &plan, const RunRecord &record)
    {
        if (!plan.profile.inject) return plan.command_line;
        std::wstring command_line = plan.command_line;
        if (plan.profile.telemetry_enabled && !plan.profile.telemetry_output && record.links.telemetry_trace) {
            command_line += L" " + QuoteWindowsArgument(L"-smedley-telemetry-output=" + record.links.telemetry_trace->wstring());
        }
        command_line += L" " + QuoteWindowsArgument(L"-smedley-run-id=" + std::wstring(record.run_id.begin(), record.run_id.end()));
        return command_line;
    }

    LaunchPlan BuildLaunchPlan(Profile profile)
    {
        LaunchPlan plan;
        plan.profile = std::move(profile);
        try {
            plan.profile.game_dir = Absolute(plan.profile.game_dir);
            plan.game_executable = plan.profile.game_dir / L"v2game.exe";
            ValidateSupportedExecutable(plan.game_executable, &plan.diagnostics);
            plan.kernel = plan.profile.kernel ? Absolute(*plan.profile.kernel) : plan.profile.game_dir / L"smedley_kernel.dll";
            if (plan.profile.inject && plan.profile.save) plan.profile.save = Absolute(*plan.profile.save);
            const bool target_run_requested = plan.profile.run_days.has_value() || plan.profile.run_until_date_raw.has_value();
            const bool run_controls_requested = plan.profile.speed != 5 || plan.profile.start_paused || target_run_requested;
            bool campaign_runner_selected = false;
            bool telemetry_selected = false;
            bool scripting_selected = false;

            std::vector<std::wstring> arguments = {plan.game_executable.wstring()};
            std::vector<fs::path> selected_descriptors;
            for (const auto &selected : plan.profile.mods) {
                const auto descriptor_path = Absolute(selected.is_absolute() ? selected : plan.profile.game_dir / selected);
                if (ContainsParentTraversal(selected) || !IsPathContained(plan.profile.game_dir / L"mod", descriptor_path)) {
                    AddDiagnostic(&plan.diagnostics, "mod.descriptor_traversal", "selected mod descriptor must be inside GAME_DIR/mod", descriptor_path);
                    continue;
                }
                if (!IsRegularFile(descriptor_path)) {
                    AddDiagnostic(&plan.diagnostics, "mod.descriptor_missing", "selected mod descriptor does not exist", descriptor_path);
                    continue;
                }
                if (std::find(selected_descriptors.begin(), selected_descriptors.end(), descriptor_path) != selected_descriptors.end()) {
                    AddDiagnostic(&plan.diagnostics, "mod.duplicate", "mod descriptor was selected more than once", descriptor_path);
                    continue;
                }
                selected_descriptors.push_back(descriptor_path);
                ModDescriptor descriptor;
                if (ParseModDescriptor(descriptor_path, plan.profile.game_dir, &descriptor, &plan.diagnostics)) {
                    plan.mods.push_back(std::move(descriptor));
                    arguments.push_back(L"-mod=mod/" + descriptor_path.filename().wstring());
                }
            }

            if (plan.profile.inject) {
                ValidateTelemetryProfile(plan.profile, &plan.diagnostics);
                ValidateRunProfile(plan.profile, &plan.diagnostics, {});
                ValidateScriptingProfile(plan.profile, &plan.diagnostics, {});
                if (plan.profile.telemetry_output) {
                    plan.profile.telemetry_output = Absolute(plan.profile.telemetry_output->is_absolute()
                        ? *plan.profile.telemetry_output
                        : plan.profile.game_dir / *plan.profile.telemetry_output);
                }
                if (!IsRegularFile(plan.kernel)) {
                    AddDiagnostic(&plan.diagnostics, "kernel.missing", "Smedley kernel does not exist", plan.kernel);
                } else if (IsX86PE(plan.kernel, &plan.diagnostics, "kernel")) {
                    HasExport(plan.kernel, "LoadPluginsThread", "kernel.export",
                              "kernel does not export LoadPluginsThread", &plan.diagnostics);
                }
                std::vector<std::string> plugin_ids;
                const auto plugin_root = plan.profile.game_dir / L"plugins";
                for (const auto &selected : plan.profile.plugins) {
                    const auto manifest_path = Absolute(selected.is_absolute() ? selected : plan.profile.game_dir / selected);
                    if (ContainsParentTraversal(selected) || !IsPathContained(plugin_root, manifest_path)) {
                        AddDiagnostic(&plan.diagnostics, "plugin.manifest_traversal", "selected plugin manifest must be inside GAME_DIR/plugins", manifest_path);
                        continue;
                    }
                    PluginManifest manifest;
                    if (!ParsePluginManifest(manifest_path, plugin_root, &manifest, &plan.diagnostics)) continue;
                    if (std::find(plugin_ids.begin(), plugin_ids.end(), manifest.id) != plugin_ids.end()) {
                        AddDiagnostic(&plan.diagnostics, "plugin.duplicate_id", "duplicate selected plugin ID", manifest_path);
                        continue;
                    }
                    plugin_ids.push_back(manifest.id);
                    if (manifest.id == "campaign_runner") campaign_runner_selected = true;
                    if (manifest.id == "telemetry") telemetry_selected = true;
                    if (manifest.id == "scripting") scripting_selected = true;
                    if (IsX86PE(manifest.module_path, &plan.diagnostics, "plugin module", true)) {
                        HasPluginExport(manifest.module_path, &plan.diagnostics);
                    }
                    plan.plugins.push_back(std::move(manifest));
                }
                const bool campaign_automation_requested = plan.profile.save.has_value()
                    || plan.profile.observer || plan.profile.view_tag.has_value() || run_controls_requested;
                if (campaign_automation_requested && !campaign_runner_selected) {
                    AddDiagnostic(&plan.diagnostics, "campaign.plugin", "save and campaign controls require the campaign_runner plugin");
                }
                if (plan.profile.telemetry_enabled && !telemetry_selected) {
                    AddDiagnostic(&plan.diagnostics, "telemetry.plugin", "telemetry requires the telemetry plugin");
                }
                std::vector<fs::path> resolved_scripts;
                const auto script_root = plan.profile.game_dir / L"scripts";
                for (const auto &selected : plan.profile.scripts) {
                    const auto script_path = Absolute(selected.is_absolute() ? selected : plan.profile.game_dir / selected);
                    if (ContainsParentTraversal(selected) || !IsPathContained(script_root, script_path)) {
                        AddDiagnostic(&plan.diagnostics, "scripting.path_traversal", "selected scripts must be inside GAME_DIR/scripts", script_path);
                        continue;
                    }
                    if (!IsRegularFile(script_path)) {
                        AddDiagnostic(&plan.diagnostics, "scripting.missing", "selected script does not exist", script_path);
                        continue;
                    }
                    if (_wcsicmp(script_path.extension().c_str(), L".lua") != 0) {
                        AddDiagnostic(&plan.diagnostics, "scripting.extension", "selected scripts must end in .lua", script_path);
                        continue;
                    }
                    std::error_code size_error;
                    const auto size = fs::file_size(script_path, size_error);
                    if (size_error || size == 0 || size > script_max_file_bytes) {
                        AddDiagnostic(&plan.diagnostics, "scripting.size", "selected scripts must be non-empty and no larger than 1 MiB", script_path);
                        continue;
                    }
                    if (std::any_of(resolved_scripts.begin(), resolved_scripts.end(), [&](const fs::path &resolved) {
                            return _wcsicmp(resolved.c_str(), script_path.c_str()) == 0;
                        })) {
                        AddDiagnostic(&plan.diagnostics, "scripting.duplicate", "script was selected more than once", script_path);
                        continue;
                    }
                    resolved_scripts.push_back(script_path);
                }
                plan.profile.scripts = std::move(resolved_scripts);
                if (!plan.profile.scripts.empty() && !scripting_selected) {
                    AddDiagnostic(&plan.diagnostics, "scripting.plugin", "scripts require the scripting plugin");
                }
                if (plan.profile.scripts.empty()
                    && (plan.profile.script_instruction_budget != 100000 || plan.profile.script_memory_bytes != 8388608
                        || plan.profile.script_queue_capacity != 256)) {
                    AddWarning(&plan.diagnostics, "scripting.settings_ignored", "script limits are ignored when no scripts are selected");
                }
                if (plan.profile.telemetry_enabled && plan.profile.telemetry_output) {
                    ValidateTelemetryOutput(plan.profile, *plan.profile.telemetry_output, plan, &plan.diagnostics);
                }
                if (run_controls_requested && !plan.profile.save) {
                    AddDiagnostic(&plan.diagnostics, "campaign.save", "campaign run controls require a save");
                }
                if (plan.profile.speed < 1 || plan.profile.speed > 5) {
                    AddDiagnostic(&plan.diagnostics, "campaign.speed", "speed must be from 1 through 5");
                }
                if (plan.profile.observer && plan.profile.start_paused) {
                    AddDiagnostic(&plan.diagnostics, "observer.start_paused", "observer mode cannot start paused because its watchdog requires advancement");
                }
                if (target_run_requested) {
                    if (plan.profile.start_paused) AddDiagnostic(&plan.diagnostics, "campaign.run_start_paused", "target runs require start_paused=false");
                    if (!plan.profile.detach) AddDiagnostic(&plan.diagnostics, "campaign.run_detach", "target runs require detach=true until a verified clean exit exists");
                    if (plan.profile.view_tag) AddDiagnostic(&plan.diagnostics, "campaign.run_view_tag", "target runs reject view_tag because observer switching is asynchronous after simulation resumes");
                }
                if (!target_run_requested && plan.profile.run_timeout_seconds != 600) {
                    AddWarning(&plan.diagnostics, "campaign.run_timeout_ignored", "benchmark timeout is ignored without a benchmark target");
                }
                for (const auto &manifest : plan.plugins) {
                    for (const auto &dependency : manifest.dependencies) {
                        if (std::find(plugin_ids.begin(), plugin_ids.end(), dependency) == plugin_ids.end()) {
                            AddDiagnostic(&plan.diagnostics, "plugin.dependency", manifest.id + " requires " + dependency, manifest.manifest_path);
                        }
                    }
                    for (const auto &conflict : manifest.conflicts) {
                        if (std::find(plugin_ids.begin(), plugin_ids.end(), conflict) != plugin_ids.end()) {
                            AddDiagnostic(&plan.diagnostics, "plugin.conflict", manifest.id + " conflicts with " + conflict, manifest.manifest_path);
                        }
                    }
                }
                std::vector<size_t> indegree(plan.plugins.size(), 0);
                std::vector<std::vector<size_t>> dependents(plan.plugins.size());
                for (size_t plugin_index = 0; plugin_index < plan.plugins.size(); ++plugin_index) {
                    for (const auto &dependency : plan.plugins[plugin_index].dependencies) {
                        const auto found = std::find_if(plan.plugins.begin(), plan.plugins.end(), [&](const auto &candidate) {
                            return candidate.id == dependency;
                        });
                        if (found == plan.plugins.end()) continue;
                        const auto dependency_index = static_cast<size_t>(std::distance(plan.plugins.begin(), found));
                        indegree[plugin_index]++;
                        dependents[dependency_index].push_back(plugin_index);
                    }
                }
                std::vector<size_t> ready;
                for (size_t index = 0; index < indegree.size(); ++index) {
                    if (indegree[index] == 0) ready.push_back(index);
                }
                std::vector<size_t> ordered_indices;
                while (!ready.empty()) {
                    const auto next = ready.front();
                    ready.erase(ready.begin());
                    ordered_indices.push_back(next);
                    for (const auto dependent : dependents[next]) {
                        if (--indegree[dependent] == 0) {
                            const auto position = std::lower_bound(ready.begin(), ready.end(), dependent);
                            ready.insert(position, dependent);
                        }
                    }
                }
                if (ordered_indices.size() != plan.plugins.size()) {
                    AddDiagnostic(&plan.diagnostics, "plugin.dependency_cycle", "selected plugins contain a dependency cycle");
                } else {
                    std::vector<PluginManifest> ordered;
                    ordered.reserve(plan.plugins.size());
                    for (const auto index : ordered_indices) ordered.push_back(std::move(plan.plugins[index]));
                    plan.plugins = std::move(ordered);
                }
                for (const auto &manifest : plan.plugins) {
                    arguments.push_back(L"-plugin=" + manifest.manifest_path.wstring());
                }
                if (plan.profile.telemetry_enabled && telemetry_selected) {
                    if (plan.profile.telemetry_output) {
                        arguments.push_back(L"-smedley-telemetry-output=" + plan.profile.telemetry_output->wstring());
                    }
                    arguments.push_back(L"-smedley-telemetry-categories=" + TelemetryCategoriesArgument(plan.profile.telemetry_categories));
                    if (!plan.profile.telemetry_country_tags.empty()) {
                        arguments.push_back(L"-smedley-telemetry-country-tags=" + TelemetryCategoriesArgument(plan.profile.telemetry_country_tags));
                    }
                    if (plan.profile.telemetry_start_date_raw) {
                        arguments.push_back(L"-smedley-telemetry-start-date-raw=" + std::to_wstring(*plan.profile.telemetry_start_date_raw));
                    }
                    if (plan.profile.telemetry_end_date_raw) {
                        arguments.push_back(L"-smedley-telemetry-end-date-raw=" + std::to_wstring(*plan.profile.telemetry_end_date_raw));
                    }
                    arguments.push_back(L"-smedley-telemetry-sample-days=" + std::to_wstring(plan.profile.telemetry_sample_days));
                    arguments.push_back(L"-smedley-telemetry-queue-capacity=" + std::to_wstring(plan.profile.telemetry_queue_capacity));
                    arguments.push_back(L"-smedley-telemetry-overwrite=" + std::to_wstring(plan.profile.telemetry_overwrite ? 1 : 0));
                }
                if (!plan.profile.scripts.empty() && scripting_selected) {
                    for (const auto &script : plan.profile.scripts) {
                        arguments.push_back(L"-smedley-script=" + script.wstring());
                    }
                    arguments.push_back(L"-smedley-script-instruction-budget=" + std::to_wstring(plan.profile.script_instruction_budget));
                    arguments.push_back(L"-smedley-script-memory-bytes=" + std::to_wstring(plan.profile.script_memory_bytes));
                    arguments.push_back(L"-smedley-script-queue-capacity=" + std::to_wstring(plan.profile.script_queue_capacity));
                }
            } else {
                if (!plan.profile.plugins.empty()) AddWarning(&plan.diagnostics, "safe_mode.plugins_ignored", "plugins are ignored when injection is disabled");
                if (plan.profile.save) AddWarning(&plan.diagnostics, "safe_mode.save_ignored", "save automation is ignored when injection is disabled", *plan.profile.save);
                if (plan.profile.observer) AddWarning(&plan.diagnostics, "safe_mode.observer_ignored", "observer settings are ignored when injection is disabled");
                if (plan.profile.view_tag) AddWarning(&plan.diagnostics, "safe_mode.view_tag_ignored", "observer view tag is ignored when injection is disabled");
                if (plan.profile.speed != 5) AddWarning(&plan.diagnostics, "safe_mode.speed_ignored", "speed control is ignored when injection is disabled");
                if (plan.profile.start_paused) AddWarning(&plan.diagnostics, "safe_mode.start_paused_ignored", "start_paused is ignored when injection is disabled");
                if (target_run_requested) AddWarning(&plan.diagnostics, "safe_mode.run_target_ignored", "benchmark run target is ignored when injection is disabled");
                else if (plan.profile.run_timeout_seconds != 600) AddWarning(&plan.diagnostics, "safe_mode.run_timeout_ignored", "benchmark timeout is ignored without a benchmark target");
                if (plan.profile.telemetry_enabled) AddWarning(&plan.diagnostics, "safe_mode.telemetry_ignored", "telemetry is ignored when injection is disabled");
                if (!plan.profile.scripts.empty()) AddWarning(&plan.diagnostics, "safe_mode.scripts_ignored", "scripts are ignored when injection is disabled");
            }

            if (plan.profile.inject && plan.profile.save) {
                if (!IsRegularFile(*plan.profile.save)) {
                    AddDiagnostic(&plan.diagnostics, "save.missing", "save file does not exist", *plan.profile.save);
                } else if (const auto base_user_dir = VictoriaBaseUserDirectory()) {
                    const auto user_dir = ResolveVictoriaUserDirectory(*base_user_dir, plan.mods);
                    if (!user_dir) {
                        AddDiagnostic(&plan.diagnostics, "save.user_dir_ambiguous", "could not derive one safe Victoria II user directory from the selected mods", *plan.profile.save);
                    } else if (!IsPathContained(*user_dir / L"save games", *plan.profile.save)) {
                        AddDiagnostic(&plan.diagnostics, "save.outside_user_dir", "save must be inside the selected Victoria II user directory's save games", *plan.profile.save);
                    }
                } else {
                    AddDiagnostic(&plan.diagnostics, "save.user_dir", "could not resolve the Victoria II user directory", *plan.profile.save);
                }
                if (plan.profile.inject) {
                    arguments.push_back(L"-smedley-save=" + plan.profile.save->wstring());
                    if (plan.profile.speed >= 1 && plan.profile.speed <= 5) {
                        arguments.push_back(L"-smedley-speed=" + std::to_wstring(plan.profile.speed));
                    }
                    if (plan.profile.start_paused) arguments.push_back(L"-smedley-start-paused");
                    if (plan.profile.run_days) arguments.push_back(L"-smedley-run-days=" + std::to_wstring(*plan.profile.run_days));
                    if (plan.profile.run_until_date_raw) arguments.push_back(L"-smedley-run-until-date-raw=" + std::to_wstring(*plan.profile.run_until_date_raw));
                    if (target_run_requested) arguments.push_back(L"-smedley-run-timeout-seconds=" + std::to_wstring(plan.profile.run_timeout_seconds));
                }
            }
            if (plan.profile.inject && plan.profile.observer && !plan.profile.save) {
                AddDiagnostic(&plan.diagnostics, "observer.save", "observer requires a save");
            }
            if (plan.profile.observer && plan.profile.inject) arguments.push_back(L"-smedley-observe");
            if (plan.profile.inject && plan.profile.view_tag) {
                const auto &tag = *plan.profile.view_tag;
                const bool valid_tag = tag.size() == 3 && std::all_of(tag.begin(), tag.end(), [](wchar_t character) {
                    return (character >= L'A' && character <= L'Z') || (character >= L'a' && character <= L'z')
                        || (character >= L'0' && character <= L'9');
                });
                if (!plan.profile.observer || !valid_tag) {
                    AddDiagnostic(&plan.diagnostics, "observer.view_tag", "view_tag requires observer and exactly three ASCII alphanumeric characters");
                } else if (plan.profile.inject) {
                    std::wstring normalized = tag;
                    std::transform(normalized.begin(), normalized.end(), normalized.begin(), towupper);
                    plan.profile.view_tag = normalized;
                    arguments.push_back(L"-smedley-view-tag=" + normalized);
                }
            }
            plan.command_line = BuildWindowsCommandLine(arguments);
        } catch (const std::exception &error) {
            AddDiagnostic(&plan.diagnostics, "plan.exception", error.what());
        }
        return plan;
    }

    LaunchResult Launch(const LaunchPlan &plan)
    {
        LaunchResult result;
        result.diagnostics = plan.diagnostics;
        auto record = CreateRunRecord(plan);
        result.run_id = record.run_id;
        const auto run_directory = DefaultRunDirectory();
        result.metadata_path = run_directory / fs::u8path(record.run_id + ".toml");
        bool persistence_warning_added = false;
        auto persist = [&] {
            record.diagnostics = result.diagnostics;
            std::vector<Diagnostic> persistence_diagnostics;
            if (!SaveRunRecord(run_directory, record, &persistence_diagnostics) && !persistence_warning_added) {
                AddWarning(&result.diagnostics, "run.persistence", "could not persist run metadata; the launch result is unchanged", result.metadata_path);
                persistence_warning_added = true;
            }
        };
        if (HasErrors(result.diagnostics)) {
            record.status = RunStatus::PreflightFailed;
            persist();
            return result;
        }
        if (plan.profile.inject && plan.profile.telemetry_enabled && record.links.telemetry_trace
            && !ValidateTelemetryOutput(plan.profile, *record.links.telemetry_trace, plan, &result.diagnostics)) {
            record.status = RunStatus::PreflightFailed;
            persist();
            return result;
        }

        const std::wstring command_line = BuildInjectedCommandLine(plan, record);
        record.command_line = command_line;

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        std::vector<wchar_t> command(command_line.begin(), command_line.end());
        command.push_back(L'\0');
        const DWORD flags = plan.profile.inject ? CREATE_SUSPENDED : 0;
        if (!CreateProcessW(plan.game_executable.c_str(), command.data(), nullptr, nullptr, FALSE, flags,
                            nullptr, plan.profile.game_dir.c_str(), &startup, &process)) {
            AddDiagnostic(&result.diagnostics, "launch.create_process", WindowsError("CreateProcessW"), plan.game_executable);
            record.status = RunStatus::CreateFailed;
            persist();
            return result;
        }
        result.process_id = process.dwProcessId;
        record.process_id = result.process_id;
        bool suspended = plan.profile.inject;
        if (suspended) {
            const auto remote_kernel = InjectLibrary(process.hProcess, plan.kernel, &result.diagnostics);
            for (const auto &plugin : plan.plugins) {
                if (HasErrors(result.diagnostics)) break;
                InjectLibrary(process.hProcess, plugin.module_path, &result.diagnostics);
            }
            if (!HasErrors(result.diagnostics)) CallLoadPlugins(process.hProcess, plan.kernel, remote_kernel, &result.diagnostics);
            if (!HasErrors(result.diagnostics)) {
                if (ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
                    AddDiagnostic(&result.diagnostics, "launch.resume", WindowsError("ResumeThread"));
                } else {
                    suspended = false;
                }
            }
        }
        if (suspended) {
            if (!TerminateProcess(process.hProcess, 1)) {
                AddDiagnostic(&result.diagnostics, "launch.terminate", WindowsError("TerminateProcess"));
            } else if (WaitForSingleObject(process.hProcess, 30000) != WAIT_OBJECT_0) {
                AddDiagnostic(&result.diagnostics, "launch.terminate_timeout", "timed out waiting for failed child process to terminate");
            }
        }
        CloseHandle(process.hThread);
        if (HasErrors(result.diagnostics)) {
            record.status = RunStatus::InjectionFailed;
            persist();
            CloseHandle(process.hProcess);
            return result;
        }
        result.started = true;
        record.status = RunStatus::Started;
        persist();
        if (!plan.profile.detach) {
            const auto wait_result = WaitForSingleObject(process.hProcess, INFINITE);
            if (wait_result != WAIT_OBJECT_0) {
                const auto message = wait_result == WAIT_FAILED ? WindowsError("WaitForSingleObject")
                                                               : "unexpected WaitForSingleObject result";
                AddDiagnostic(&result.diagnostics, "launch.process_wait", message, plan.game_executable);
            }
            DWORD exit_code = 1;
            if (wait_result == WAIT_OBJECT_0 && !GetExitCodeProcess(process.hProcess, &exit_code)) {
                AddDiagnostic(&result.diagnostics, "launch.exit_code", WindowsError("GetExitCodeProcess"), plan.game_executable);
            } else if (wait_result == WAIT_OBJECT_0 && exit_code == STILL_ACTIVE) {
                AddDiagnostic(&result.diagnostics, "launch.exit_code", "process remained active after it was signaled", plan.game_executable);
            } else if (wait_result == WAIT_OBJECT_0) {
                result.exit_code = exit_code;
                record.status = RunStatus::Exited;
                record.exit_code = result.exit_code;
            }
            persist();
        }
        CloseHandle(process.hProcess);
        return result;
    }
}
