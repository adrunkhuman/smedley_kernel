#include <smedley/launcher/launcher.hpp>

#include <windows.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <toml.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <sstream>

namespace smedley::launcher
{
    namespace
    {
        constexpr uintmax_t supported_executable_size = 12294656;
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

        bool IsSaveInGameDirectory(const fs::path &save)
        {
            PWSTR documents = nullptr;
            if (SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documents) != S_OK) {
                return false;
            }
            const fs::path save_root = fs::path(documents) / L"Paradox Interactive" / L"Victoria II" / L"save games";
            CoTaskMemFree(documents);
            return IsPathContained(save_root, save);
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
            // A timed-out thread may still be reading this allocation. The failed
            // launch path terminates the suspended child and lets Windows reclaim it.
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
            WaitForRemoteThread(thread, "plugin initialization", diagnostics);
            CloseHandle(thread);
        }

        std::string EscapeToml(const std::string &value)
        {
            std::string escaped;
            for (const char character : value) {
                if (character == '\\' || character == '"') escaped += '\\';
                escaped += character;
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
    }

    bool HasErrors(const std::vector<Diagnostic> &diagnostics)
    {
        return std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic &diagnostic) {
            return diagnostic.severity == Severity::Error;
        });
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
            if (const auto value = table["detach"].value<bool>()) loaded.detach = *value;
            auto read_paths = [&](const char *key, std::vector<fs::path> *paths) {
                if (const auto array = table[key].as_array()) {
                    for (const auto &node : *array) {
                        const auto value = node.value<std::string>();
                        if (!value) {
                            AddDiagnostic(diagnostics, "profile.schema", std::string(key) + " must contain strings", path);
                            return false;
                        }
                        paths->emplace_back(fs::u8path(*value));
                    }
                }
                return true;
            };
            if (!read_paths("mods", &loaded.mods) || !read_paths("plugins", &loaded.plugins)) return false;
            *profile = std::move(loaded);
            return true;
        } catch (const std::exception &error) {
            AddDiagnostic(diagnostics, "profile.parse", error.what(), path);
            return false;
        }
    }

    bool SaveProfile(const fs::path &path, const Profile &profile, std::vector<Diagnostic> *diagnostics)
    {
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
        if (profile.view_tag) output << "view_tag = \"" << WideToUtf8(*profile.view_tag) << "\"\n";
        output << "speed = " << profile.speed << "\n";
        output << "start_paused = " << (profile.start_paused ? "true" : "false") << "\n";
        output << "detach = " << (profile.detach ? "true" : "false") << "\n";
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

    LaunchPlan BuildLaunchPlan(Profile profile)
    {
        LaunchPlan plan;
        plan.profile = std::move(profile);
        try {
            plan.profile.game_dir = Absolute(plan.profile.game_dir);
            plan.game_executable = plan.profile.game_dir / L"v2game.exe";
            ValidateSupportedExecutable(plan.game_executable, &plan.diagnostics);
            plan.kernel = plan.profile.kernel ? Absolute(*plan.profile.kernel) : plan.profile.game_dir / L"smedley_kernel.dll";
            const bool run_controls_requested = plan.profile.speed != 5 || plan.profile.start_paused;
            bool campaign_runner_selected = false;

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
                    if (IsX86PE(manifest.module_path, &plan.diagnostics, "plugin module", true)) {
                        HasExport(manifest.module_path, "CreatePlugin", "plugin.export",
                                  "plugin module does not export CreatePlugin", &plan.diagnostics);
                    }
                    plan.plugins.push_back(std::move(manifest));
                }
                const bool campaign_automation_requested = plan.profile.save.has_value()
                    || plan.profile.observer || plan.profile.view_tag.has_value() || run_controls_requested;
                if (campaign_automation_requested && !campaign_runner_selected) {
                    AddDiagnostic(&plan.diagnostics, "campaign.plugin", "save and campaign controls require the campaign_runner plugin");
                }
                if (run_controls_requested && !plan.profile.save) {
                    AddDiagnostic(&plan.diagnostics, "campaign.save", "speed and start_paused controls require a save");
                }
                if (plan.profile.speed < 1 || plan.profile.speed > 5) {
                    AddDiagnostic(&plan.diagnostics, "campaign.speed", "speed must be from 1 through 5");
                }
                if (plan.profile.observer && plan.profile.start_paused) {
                    AddDiagnostic(&plan.diagnostics, "observer.start_paused", "observer mode cannot start paused because its watchdog requires advancement");
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
            } else {
                if (!plan.profile.plugins.empty()) AddWarning(&plan.diagnostics, "safe_mode.plugins_ignored", "plugins are ignored when injection is disabled");
                if (plan.profile.save) AddWarning(&plan.diagnostics, "safe_mode.save_ignored", "save automation is ignored when injection is disabled", *plan.profile.save);
                if (plan.profile.observer) AddWarning(&plan.diagnostics, "safe_mode.observer_ignored", "observer settings are ignored when injection is disabled");
                if (plan.profile.view_tag) AddWarning(&plan.diagnostics, "safe_mode.view_tag_ignored", "observer view tag is ignored when injection is disabled");
                if (plan.profile.speed != 5) AddWarning(&plan.diagnostics, "safe_mode.speed_ignored", "speed control is ignored when injection is disabled");
                if (plan.profile.start_paused) AddWarning(&plan.diagnostics, "safe_mode.start_paused_ignored", "start_paused is ignored when injection is disabled");
            }

            if (plan.profile.inject && plan.profile.save) {
                plan.profile.save = Absolute(*plan.profile.save);
                if (!IsRegularFile(*plan.profile.save)) {
                    AddDiagnostic(&plan.diagnostics, "save.missing", "save file does not exist", *plan.profile.save);
                } else if (!IsSaveInGameDirectory(*plan.profile.save)) {
                    AddDiagnostic(&plan.diagnostics, "save.outside_user_dir", "save must be inside Victoria II save games", *plan.profile.save);
                }
                if (plan.profile.inject) {
                    arguments.push_back(L"-smedley-save=" + plan.profile.save->wstring());
                    if (plan.profile.speed >= 1 && plan.profile.speed <= 5) {
                        arguments.push_back(L"-smedley-speed=" + std::to_wstring(plan.profile.speed));
                    }
                    if (plan.profile.start_paused) arguments.push_back(L"-smedley-start-paused");
                }
            }
            if (plan.profile.inject && plan.profile.observer && !plan.profile.save) {
                AddDiagnostic(&plan.diagnostics, "observer.save", "observer requires a save");
            }
            if (plan.profile.observer && plan.profile.inject) arguments.push_back(L"-smedley-observe");
            if (plan.profile.inject && plan.profile.view_tag) {
                const auto &tag = *plan.profile.view_tag;
                const bool valid_tag = tag.size() == 3 && std::all_of(tag.begin(), tag.end(), [](wchar_t character) {
                    return (character >= L'A' && character <= L'Z') || (character >= L'a' && character <= L'z');
                });
                if (!plan.profile.observer || !valid_tag) {
                    AddDiagnostic(&plan.diagnostics, "observer.view_tag", "view_tag requires observer and exactly three ASCII letters");
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
        if (HasErrors(result.diagnostics)) return result;

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        std::vector<wchar_t> command(plan.command_line.begin(), plan.command_line.end());
        command.push_back(L'\0');
        const DWORD flags = plan.profile.inject ? CREATE_SUSPENDED : 0;
        if (!CreateProcessW(plan.game_executable.c_str(), command.data(), nullptr, nullptr, FALSE, flags,
                            nullptr, plan.profile.game_dir.c_str(), &startup, &process)) {
            AddDiagnostic(&result.diagnostics, "launch.create_process", WindowsError("CreateProcessW"), plan.game_executable);
            return result;
        }
        result.process_id = process.dwProcessId;
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
            CloseHandle(process.hProcess);
            return result;
        }
        result.started = true;
        if (!plan.profile.detach) {
            WaitForSingleObject(process.hProcess, INFINITE);
            DWORD exit_code = 1;
            if (GetExitCodeProcess(process.hProcess, &exit_code)) result.exit_code = exit_code;
        }
        CloseHandle(process.hProcess);
        return result;
    }
}
