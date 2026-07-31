#include "hooks.hpp"
#include "loader.hpp"
#include "memory.hpp"
#include "util.hpp"
#include <cstdio>
#include <cstdio>
#include <filesystem>
#include <windows.h>
#include <shellapi.h>
#include <direct.h>
#include <psapi.h>
#include <shlobj.h>

PLUGIN_API void LoadPlugins()
{
    (void)smedley::PluginLoader::instance()->LoadPlugins();
}

PLUGIN_API DWORD WINAPI LoadPluginsThread(LPVOID)
{
    try {
        return smedley::PluginLoader::instance()->LoadPlugins() ? 0 : 1;
    } catch (const std::exception &error) {
        OutputDebugStringA((std::string("Smedley plugin initialization failed: ") + error.what()).c_str());
        return 1;
    } catch (...) {
        OutputDebugStringA("Smedley plugin initialization failed with an unknown exception");
        return 1;
    }
}


namespace smedley
{

    std::string NarrowCommandLineArgument(const std::wstring &value)
    {
        const auto length = WideCharToMultiByte(CP_ACP, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        std::string result(length, '\0');
        WideCharToMultiByte(CP_ACP, 0, value.c_str(), static_cast<int>(value.size()), result.data(), length, nullptr, nullptr);
        return result;
    }

    // a PLUGIN_API export required by all smedley plugins. for most plugins this
    // should just be simple boilerplate allocating and instantiating the plugin
    // subclass.
    typedef Plugin *(*PluginCreator)();

    struct PluginModule
    {
        Plugin *(*CreatePlugin)();

        std::string (*PluginID)();
    };

    PluginLoader *PluginLoader::_instance = nullptr;

    void DumpLoadedModules(smedley::Logger &logger)
    {
        constexpr size_t hmods_size = 1024;
        constexpr size_t text_buf_size = 512;

        HMODULE hmods[hmods_size];
        HANDLE hproc;
        DWORD cb_needed;

        hproc = GetCurrentProcess();
        if (EnumProcessModules(hproc, hmods, sizeof(hmods), &cb_needed)) {
            for (uint32_t i = 0; i < (cb_needed / sizeof(HMODULE)); i++) {
                char mod_name[MAX_PATH];
                char text_buf[text_buf_size];

                if (GetModuleFileNameEx(hproc, hmods[i], mod_name,
                                        sizeof(mod_name) / sizeof(char))) {
                    std::snprintf(text_buf, text_buf_size, TEXT("%s (0x%08X)"), mod_name, reinterpret_cast<uint32_t>(hmods[i]));
                    logger.Info("module detected: " + std::string(text_buf));
                }
            }
        }
    }


    PluginLoader::PluginLoader() : _loaded(false)
    {
        wchar_t cwd_buf[MAX_PATH];
        wchar_t *documents_path_ws;

        // currently victoria 2 always assumes its launched within the game directory.
        // if it isn't it will crash failing to find some map file. we may want to
        // fix this in the future, but for now we can ride on this assumption.
        if (!_wgetcwd(cwd_buf, MAX_PATH)) {
            auto err_no = errno;
            throw std::runtime_error("failed to get current working directory: " + err_no);
        }

        // keep smedley logs near game logs
        if (SHGetKnownFolderPath(FOLDERID_Documents, 0, NULL, &documents_path_ws) == E_FAIL) {
            throw std::runtime_error("failed to find documents folder");
        }
        std::wstring tmp_ws(documents_path_ws);
        std::string documents_path = NarrowCommandLineArgument(tmp_ws);
        tmp_ws = std::wstring(cwd_buf);
        
        _gamedir = NarrowCommandLineArgument(tmp_ws);
        _userdir = documents_path + "\\Paradox Interactive\\Victoria II";
        _plugindir = _gamedir + "\\plugins";
        _log_filepath = _userdir + "\\logs\\smedley.log";

        _logger = std::make_unique<FileLogger>(_log_filepath, "smedley");
        _logger->Info("initializing plugin loader...");
    }

    bool PluginLoader::LoadPlugins()
    {
        if (_loaded) return true;
        try {
            memory::Map::Init();
            InstallHooks();
            std::vector<PluginDefinition> definitions;
            for (auto &filename : ParsePluginArguments(GetCommandLineW())) {
                definitions.push_back(PluginDefinition::Read(filename));
            }
            _plugins.reserve(definitions.size());
            DumpLoadedModules(*_logger);
            for (auto &def : definitions) {
                const auto path = std::filesystem::absolute("plugins" / std::filesystem::path(def.module_name));
                HMODULE hmod = GetModuleHandleW(path.c_str());
                if (hmod == nullptr || hmod == INVALID_HANDLE_VALUE) {
                    throw std::runtime_error("plugin module not found: " + NarrowCommandLineArgument(path.native()));
                }
                const auto get_api = reinterpret_cast<SmedleyPluginGetApiV1Fn>(
                    GetProcAddress(hmod, SMEDLEY_PLUGIN_GET_API_V1_SYMBOL));
                if (get_api != nullptr) {
                    SmedleyPluginApiV1 api{};
                    api.struct_size = sizeof(api);
                    api.version = SMEDLEY_PLUGIN_ABI_VERSION_V1;
                    SmedleyPluginResult result = SMEDLEY_PLUGIN_FAILURE;
                    try {
                        result = get_api(&api);
                    } catch (...) {
                        throw std::runtime_error("plugin ABI v1 discovery threw an exception: " + def.id);
                    }
                    if (result != SMEDLEY_PLUGIN_SUCCESS) {
                        throw std::runtime_error("plugin ABI v1 discovery failed with result "
                            + std::to_string(result) + ": " + def.id);
                    }
                    auto instance = std::make_unique<PluginAbiV1Instance>(api);
                    std::string error;
                    if (!instance->Start(&error)) throw std::runtime_error(error + ": " + def.id);
                    LoadedPlugin loaded;
                    loaded.abi_v1 = std::move(instance);
                    _plugins.push_back(std::move(loaded));
                    _logger->Info("loaded plugin through C ABI v1: " + def.id);
                    continue;
                }

                const auto creator = reinterpret_cast<PluginCreator>(GetProcAddress(hmod, "CreatePlugin"));
                if (creator == nullptr) {
                    throw std::runtime_error("plugin exports neither SmedleyPluginGetApiV1 nor CreatePlugin: " + def.id);
                }
                std::unique_ptr<Plugin> plugin(creator());
                if (!plugin) throw std::runtime_error("CreatePlugin returned null: " + def.id);
                plugin->_definition = def;
                plugin->_hmod = hmod;
                plugin->_logger = std::make_unique<FileLogger>(_log_filepath, def.id, true);
                try {
                    plugin->OnLoad();
                } catch (...) {
                    try {
                        plugin->OnUnload();
                    } catch (...) {
                        _logger->Failure("legacy plugin rollback unload failed: " + def.id);
                    }
                    throw;
                }
                LoadedPlugin loaded;
                loaded.legacy = plugin.release();
                _plugins.push_back(std::move(loaded));
                _logger->Info("loaded plugin through legacy C++ ABI: " + def.id);
            }
            _plugin_defs = std::move(definitions);
            _loaded = true;
            return true;
        } catch (const std::exception &error) {
            _logger->Critical("plugin initialization failed: " + std::string(error.what()));
        } catch (...) {
            _logger->Critical("plugin initialization failed with an unknown exception");
        }
        UnloadPlugins();
        return false;
    }

    void PluginLoader::UnloadPlugins()
    {
        for (auto it = _plugins.rbegin(); it != _plugins.rend(); ++it) {
            if (it->abi_v1) {
                std::vector<std::string> errors;
                it->abi_v1->Stop(&errors);
                for (const auto &error : errors) _logger->Failure(error);
                continue;
            }
            if (it->legacy != nullptr) {
                try {
                    it->legacy->OnUnload();
                } catch (const std::exception &error) {
                    _logger->Failure("plugin unload failed: " + std::string(error.what()));
                } catch (...) {
                    _logger->Failure("plugin unload failed with an unknown exception");
                }
                try {
                    delete it->legacy;
                } catch (...) {
                    _logger->Failure("plugin destruction failed with an unknown exception");
                }
            }
        }

        _plugins.clear();
        _plugin_defs.clear();
        _loaded = false;
    }

    std::vector<std::filesystem::path> PluginLoader::ParsePluginArguments(const wchar_t *command_line)
    {
        const std::wstring prefix = L"-plugin=";
        std::vector<std::filesystem::path> targets;
        int argc = 0;
        wchar_t **argv = CommandLineToArgvW(command_line, &argc);
        if (argv == nullptr) {
            return targets;
        }
        for (int i = 1; i < argc; i++) {
            const std::wstring argument = argv[i];
            if (argument.rfind(prefix, 0) != 0) {
                continue;
            }
            const auto filename = argument.substr(prefix.length());
            targets.emplace_back(filename);
        }
        LocalFree(argv);
        return targets;
    }

}
