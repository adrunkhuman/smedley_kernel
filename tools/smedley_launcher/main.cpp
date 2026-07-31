#include <smedley/launcher/launcher.hpp>

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace launcher = smedley::launcher;
namespace fs = std::filesystem;

namespace
{
    constexpr wchar_t window_class_name[] = L"SmedleyLauncherWindow";

    enum ControlId
    {
        profile_path_edit = 100,
        load_profile_button,
        save_profile_button,
        profile_name_edit,
        game_dir_edit,
        browse_game_button,
        refresh_button,
        mod_combo,
        plugin_list,
        safe_mode_check,
        save_path_edit,
        browse_save_button,
        observer_check,
        view_tag_edit,
        speed_combo,
        start_paused_check,
        diagnostics_edit,
        launch_button,
        status_text,
        recent_runs_button,
        telemetry_enabled_check,
        telemetry_output_edit,
        browse_telemetry_button,
        telemetry_categories_combo,
        telemetry_sample_days_edit,
        telemetry_queue_capacity_edit,
        telemetry_overwrite_check,
        telemetry_country_tags_edit,
        telemetry_start_date_edit,
        telemetry_end_date_edit,
    };

    std::wstring Utf8ToWide(const std::string &value)
    {
        if (value.empty()) return {};
        const int length = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
        if (length == 0) return L"<invalid UTF-8 diagnostic>";
        std::wstring result(length, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), length);
        return result;
    }

    std::string WideToUtf8(const std::wstring &value)
    {
        if (value.empty()) return {};
        const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        std::string result(length, '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), length, nullptr, nullptr);
        return result;
    }

    std::wstring SeverityName(launcher::Severity severity)
    {
        switch (severity) {
        case launcher::Severity::Info: return L"info";
        case launcher::Severity::Warning: return L"warning";
        case launcher::Severity::Error: return L"error";
        }
        return L"unknown";
    }

    std::wstring GetText(HWND control)
    {
        const int length = GetWindowTextLengthW(control);
        std::wstring result(length + 1, L'\0');
        GetWindowTextW(control, result.data(), static_cast<int>(result.size()));
        result.resize(length);
        return result;
    }

    void SetText(HWND control, const std::wstring &value)
    {
        SetWindowTextW(control, value.c_str());
    }

    fs::path AbsolutePath(const fs::path &path)
    {
        std::error_code error;
        const auto result = fs::absolute(path, error);
        return error ? path.lexically_normal() : result.lexically_normal();
    }

    fs::path ResolveSelectedPath(const fs::path &game_dir, const fs::path &selected)
    {
        return AbsolutePath(selected.is_absolute() ? selected : game_dir / selected);
    }

    fs::path ExecutableDirectory()
    {
        std::vector<wchar_t> buffer(32768, L'\0');
        const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0 || length == buffer.size()) return {};
        return fs::path(std::wstring(buffer.data(), length)).parent_path();
    }

    bool BrowseForFolder(HWND owner, std::wstring *path)
    {
        IFileDialog *dialog = nullptr;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return false;
        FILEOPENDIALOGOPTIONS options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        dialog->SetTitle(L"Select Victoria II game directory");
        const HRESULT shown = dialog->Show(owner);
        if (shown != S_OK) {
            dialog->Release();
            return false;
        }
        IShellItem *item = nullptr;
        const HRESULT selected = dialog->GetResult(&item);
        dialog->Release();
        if (FAILED(selected)) return false;
        PWSTR selected_path = nullptr;
        const HRESULT displayed = item->GetDisplayName(SIGDN_FILESYSPATH, &selected_path);
        item->Release();
        if (FAILED(displayed)) return false;
        *path = selected_path;
        CoTaskMemFree(selected_path);
        return true;
    }

    bool BrowseForFile(HWND owner, const wchar_t *title, const wchar_t *filter, std::wstring *path, bool save, const wchar_t *extension = nullptr)
    {
        std::vector<wchar_t> buffer(32768, L'\0');
        if (!path->empty()) wcsncpy_s(buffer.data(), buffer.size(), path->c_str(), _TRUNCATE);
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = owner;
        dialog.lpstrFilter = filter;
        dialog.lpstrFile = buffer.data();
        dialog.nMaxFile = static_cast<DWORD>(buffer.size());
        dialog.lpstrTitle = title;
        dialog.lpstrDefExt = save ? extension : nullptr;
        dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
        if (!(save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog))) return false;
        *path = buffer.data();
        return true;
    }

    class RecentRunsWindow
    {
    public:
        static void Show(HWND owner)
        {
            WNDCLASSW window_class{};
            window_class.hInstance = GetModuleHandleW(nullptr);
            window_class.lpszClassName = L"SmedleyRecentRunsWindow";
            window_class.lpfnWndProc = WindowProc;
            window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            RegisterClassW(&window_class);
            auto *self = new RecentRunsWindow;
            const HWND window = CreateWindowExW(WS_EX_CONTROLPARENT, window_class.lpszClassName, L"Smedley Recent Runs",
                                                WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                                                860, 440, owner, nullptr, window_class.hInstance, self);
            if (!window) delete self;
        }

    private:
        enum : int { run_list = 1, reload_button, metadata_button, link_combo, open_link_button, trace_summary_button, history_status };

        static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
        {
            auto *self = reinterpret_cast<RecentRunsWindow *>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (message == WM_NCCREATE) {
                self = static_cast<RecentRunsWindow *>(reinterpret_cast<const CREATESTRUCTW *>(lparam)->lpCreateParams);
                SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
                self->window_ = window;
            }
            if (!self) return DefWindowProcW(window, message, wparam, lparam);
            if (message == WM_NCDESTROY) {
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                delete self;
                return 0;
            }
            return self->HandleMessage(message, wparam, lparam);
        }

        LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam)
        {
            switch (message) {
            case WM_CREATE:
                CreateControls();
                Reload();
                return 0;
            case WM_SIZE:
                Layout(LOWORD(lparam), HIWORD(lparam));
                return 0;
            case WM_COMMAND:
                if (LOWORD(wparam) == reload_button && HIWORD(wparam) == BN_CLICKED) Reload();
                else if (LOWORD(wparam) == metadata_button && HIWORD(wparam) == BN_CLICKED) OpenMetadata();
                else if (LOWORD(wparam) == open_link_button && HIWORD(wparam) == BN_CLICKED) OpenSelectedLink();
                else if (LOWORD(wparam) == trace_summary_button && HIWORD(wparam) == BN_CLICKED) OpenTraceSummary();
                else if (LOWORD(wparam) == link_combo && HIWORD(wparam) == CBN_SELCHANGE) EnableWindow(open_link_, SelectedLink().has_value());
                return 0;
            case WM_NOTIFY: {
                const auto *notification = reinterpret_cast<const NMHDR *>(lparam);
                if (notification->idFrom == run_list && notification->code == LVN_ITEMCHANGED) UpdateSelection();
                if (notification->idFrom == run_list && notification->code == NM_DBLCLK) OpenMetadata();
                return 0;
            }
            }
            return DefWindowProcW(window_, message, wparam, lparam);
        }

        HWND AddControl(DWORD style, const wchar_t *class_name, const wchar_t *text, int id)
        {
            const HWND control = CreateWindowExW(0, class_name, text, WS_CHILD | WS_VISIBLE | style,
                                                  0, 0, 0, 0, window_, reinterpret_cast<HMENU>(id), nullptr, nullptr);
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
            return control;
        }

        void CreateControls()
        {
            list_ = AddControl(LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL | WS_TABSTOP | WS_BORDER,
                               WC_LISTVIEWW, L"", run_list);
            ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            AddColumn(L"Started (UTC)", 195);
            AddColumn(L"Status", 130);
            AddColumn(L"Profile", 250);
            AddColumn(L"PID", 90);
            AddControl(BS_PUSHBUTTON | WS_TABSTOP, L"BUTTON", L"&Reload", reload_button);
            metadata_ = AddControl(BS_PUSHBUTTON | WS_TABSTOP, L"BUTTON", L"Open &metadata", metadata_button);
            links_ = AddControl(CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, L"COMBOBOX", L"", link_combo);
            open_link_ = AddControl(BS_PUSHBUTTON | WS_TABSTOP, L"BUTTON", L"Open &link", open_link_button);
            trace_summary_ = AddControl(BS_PUSHBUTTON | WS_TABSTOP, L"BUTTON", L"Trace &summary", trace_summary_button);
            status_ = AddControl(SS_LEFT, L"STATIC", L"", history_status);
        }

        void AddColumn(const wchar_t *name, int width)
        {
            LVCOLUMNW column{};
            column.mask = LVCF_TEXT | LVCF_WIDTH;
            column.pszText = const_cast<wchar_t *>(name);
            column.cx = width;
            ListView_InsertColumn(list_, columns_++, &column);
        }

        void Layout(int width, int height)
        {
            const int margin = 12;
            MoveWindow(list_, margin, margin, std::max(300, width - margin * 2), std::max(130, height - 100), TRUE);
            const int y = std::max(150, height - 78);
            MoveWindow(GetDlgItem(window_, reload_button), margin, y, 85, 26, TRUE);
            MoveWindow(metadata_, margin + 92, y, 115, 26, TRUE);
            MoveWindow(links_, margin + 214, y, std::max(150, width - 490), 220, TRUE);
            MoveWindow(open_link_, std::max(310, width - 266), y, 100, 26, TRUE);
            MoveWindow(trace_summary_, std::max(415, width - 158), y, 145, 26, TRUE);
            MoveWindow(status_, margin, y + 32, std::max(200, width - margin * 2), 24, TRUE);
        }

        std::optional<size_t> SelectedRecord() const
        {
            const int index = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
            if (index < 0 || static_cast<size_t>(index) >= records_.size()) return std::nullopt;
            return static_cast<size_t>(index);
        }

        std::optional<fs::path> SelectedLink() const
        {
            const int index = static_cast<int>(SendMessageW(links_, CB_GETCURSEL, 0, 0));
            if (index < 0 || static_cast<size_t>(index) >= link_paths_.size()) return std::nullopt;
            return link_paths_[index];
        }

        void Reload()
        {
            diagnostics_.clear();
            records_ = launcher::LoadRunHistory(100, &diagnostics_);
            ListView_DeleteAllItems(list_);
            for (size_t index = 0; index < records_.size(); ++index) {
                const auto started = Utf8ToWide(records_[index].started_at_utc);
                const auto status = Utf8ToWide(launcher::RunStatusName(records_[index].status));
                const auto profile = Utf8ToWide(records_[index].profile_name);
                const auto pid = records_[index].process_id ? std::to_wstring(*records_[index].process_id) : L"-";
                LVITEMW item{};
                item.mask = LVIF_TEXT;
                item.iItem = static_cast<int>(index);
                item.pszText = const_cast<wchar_t *>(started.c_str());
                ListView_InsertItem(list_, &item);
                ListView_SetItemText(list_, static_cast<int>(index), 1, const_cast<wchar_t *>(status.c_str()));
                ListView_SetItemText(list_, static_cast<int>(index), 2, const_cast<wchar_t *>(profile.c_str()));
                ListView_SetItemText(list_, static_cast<int>(index), 3, const_cast<wchar_t *>(pid.c_str()));
            }
            const auto message = diagnostics_.empty() ? L"Select a run to open its metadata or an available linked location."
                                                      : L"Some run records could not be read. Reload after correcting them.";
            SetText(status_, message);
            UpdateSelection();
        }

        void UpdateSelection()
        {
            SendMessageW(links_, CB_RESETCONTENT, 0, 0);
            link_paths_.clear();
            EnableWindow(metadata_, SelectedRecord().has_value());
            if (const auto selected = SelectedRecord()) {
                const auto &links = records_[*selected].links;
                AddAvailableLink(L"Smedley log", links.smedley_log);
                AddAvailableLink(L"Victoria II system log", links.victoria_system_log);
                AddAvailableLink(L"Victoria II user directory", links.victoria_user_dir);
                AddAvailableLink(L"Economy trace", links.economy_trace);
                AddAvailableLink(L"Telemetry trace", links.telemetry_trace);
                AddAvailableLink(L"Source save", links.source_save);
            }
            if (!link_paths_.empty()) SendMessageW(links_, CB_SETCURSEL, 0, 0);
            EnableWindow(open_link_, !link_paths_.empty());
            const auto selected = SelectedRecord();
            const bool trace = selected && records_[*selected].links.telemetry_trace && IsSafeTrace(*records_[*selected].links.telemetry_trace);
            EnableWindow(trace_summary_, trace);
        }

        void AddAvailableLink(const wchar_t *name, const std::optional<fs::path> &path)
        {
            std::error_code error;
            const auto destination = path ? fs::absolute(*path, error) : fs::path{};
            bool is_directory = false;
            if (!path || error || !IsSafeLinkedTarget(destination, &is_directory)) return;
            const auto label = std::wstring(name) + L": " + destination.wstring();
            SendMessageW(links_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            link_paths_.push_back(destination);
        }

        bool IsSafeLinkedTarget(const fs::path &path, bool *is_directory) const
        {
            const DWORD attributes = GetFileAttributesW(path.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) return false;
            *is_directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (*is_directory) return true;
            auto extension = path.extension().wstring();
            std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
            return extension != L".lnk" && extension != L".exe" && extension != L".com" && extension != L".bat"
                && extension != L".cmd" && extension != L".ps1" && extension != L".vbs" && extension != L".js"
                && extension != L".msi" && extension != L".scr";
        }

        void OpenWithExplorer(const fs::path &path)
        {
            bool is_directory = false;
            if (!IsSafeLinkedTarget(path, &is_directory)) {
                MessageBoxW(window_, L"The linked target is missing or is not safe to open.", L"Smedley Launcher", MB_OK | MB_ICONWARNING);
                return;
            }
            const auto argument = is_directory ? launcher::QuoteWindowsArgument(path.wstring())
                                               : L"/select," + launcher::QuoteWindowsArgument(path.wstring());
            if (reinterpret_cast<intptr_t>(ShellExecuteW(window_, L"open", L"explorer.exe", argument.c_str(), nullptr, SW_SHOWNORMAL)) <= 32) {
                MessageBoxW(window_, L"Windows Explorer could not open the selected path.", L"Smedley Launcher", MB_OK | MB_ICONERROR);
            }
        }

        void OpenMetadata()
        {
            if (const auto selected = SelectedRecord()) {
                const auto &path = records_[*selected].metadata_path;
                bool is_directory = false;
                if (!launcher::IsPathContained(launcher::DefaultRunDirectory(), path) || path.extension() != L".toml"
                    || !IsSafeLinkedTarget(path, &is_directory) || is_directory) {
                    MessageBoxW(window_, L"The metadata file is missing or is not safe to open.", L"Smedley Launcher", MB_OK | MB_ICONWARNING);
                    return;
                }
                const auto argument = launcher::QuoteWindowsArgument(path.wstring());
                if (reinterpret_cast<intptr_t>(ShellExecuteW(window_, L"open", L"notepad.exe", argument.c_str(), nullptr, SW_SHOWNORMAL)) <= 32) {
                    MessageBoxW(window_, L"Notepad could not open the metadata file.", L"Smedley Launcher", MB_OK | MB_ICONERROR);
                }
            }
        }

        void OpenSelectedLink()
        {
            if (const auto link = SelectedLink()) OpenWithExplorer(*link);
        }

        bool IsSafeTrace(const fs::path &path) const
        {
            bool directory = false;
            return IsSafeLinkedTarget(path, &directory) && !directory && _wcsicmp(path.extension().c_str(), L".jsonl") == 0;
        }

        void OpenTraceSummary()
        {
            const auto selected = SelectedRecord();
            if (!selected || !records_[*selected].links.telemetry_trace || !IsSafeTrace(*records_[*selected].links.telemetry_trace)) return;
            const auto tool = ExecutableDirectory() / L"smedley_trace.exe";
            const DWORD attributes = GetFileAttributesW(tool.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
                MessageBoxW(window_, L"smedley_trace.exe is not installed beside the launcher.", L"Smedley Launcher", MB_OK | MB_ICONWARNING);
                return;
            }
            const std::wstring command_line = launcher::BuildWindowsCommandLine({tool.wstring(), L"summary", records_[*selected].links.telemetry_trace->wstring(), L"--wait"});
            std::vector<wchar_t> command(command_line.begin(), command_line.end());
            command.push_back(L'\0');
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION process{};
            if (!CreateProcessW(tool.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, tool.parent_path().c_str(), &startup, &process)) {
                MessageBoxW(window_, L"Could not start smedley_trace.exe.", L"Smedley Launcher", MB_OK | MB_ICONERROR);
                return;
            }
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        }

        HWND window_ = nullptr;
        HWND list_ = nullptr;
        HWND metadata_ = nullptr;
        HWND links_ = nullptr;
        HWND open_link_ = nullptr;
        HWND trace_summary_ = nullptr;
        HWND status_ = nullptr;
        int columns_ = 0;
        std::vector<launcher::RunRecord> records_;
        std::vector<launcher::Diagnostic> diagnostics_;
        std::vector<fs::path> link_paths_;
    };

    class LauncherWindow
    {
    public:
        bool Create(HINSTANCE instance)
        {
            WNDCLASSW window_class{};
            window_class.hInstance = instance;
            window_class.lpszClassName = window_class_name;
            window_class.lpfnWndProc = WindowProc;
            window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            if (!RegisterClassW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

            dpi_ = GetDeviceDpi();
            window_ = CreateWindowExW(WS_EX_CONTROLPARENT, window_class_name, L"Smedley Launcher", WS_OVERLAPPEDWINDOW,
                                      CW_USEDEFAULT, CW_USEDEFAULT, Scale(900), Scale(790), nullptr, nullptr, instance, this);
            if (!window_) return false;
            CreateControls();
            RefreshDiscovery();
            ShowWindow(window_, SW_SHOWNORMAL);
            UpdateWindow(window_);
            return true;
        }

        HWND window() const { return window_; }

    private:
        static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
        {
            auto *self = reinterpret_cast<LauncherWindow *>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (message == WM_NCCREATE) {
                const auto *create = reinterpret_cast<const CREATESTRUCTW *>(lparam);
                self = static_cast<LauncherWindow *>(create->lpCreateParams);
                SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
                self->window_ = window;
            }
            return self ? self->HandleMessage(message, wparam, lparam) : DefWindowProcW(window, message, wparam, lparam);
        }

        LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam)
        {
            switch (message) {
            case WM_GETMINMAXINFO: {
                auto *info = reinterpret_cast<MINMAXINFO *>(lparam);
                info->ptMinTrackSize.x = Scale(690);
                info->ptMinTrackSize.y = Scale(540);
                return 0;
            }
            case WM_SIZE:
                Layout(LOWORD(lparam), HIWORD(lparam));
                return 0;
            case WM_DPICHANGED: {
                dpi_ = HIWORD(wparam);
                const auto *suggested = reinterpret_cast<const RECT *>(lparam);
                SetWindowPos(window_, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left, suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                return 0;
            }
            case WM_COMMAND:
                OnCommand(LOWORD(wparam), HIWORD(wparam));
                return 0;
            case WM_NOTIFY:
                OnNotify(reinterpret_cast<const NMHDR *>(lparam));
                return 0;
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            }
            return DefWindowProcW(window_, message, wparam, lparam);
        }

        UINT GetDeviceDpi() const
        {
            HDC device = GetDC(nullptr);
            const UINT dpi = device ? static_cast<UINT>(GetDeviceCaps(device, LOGPIXELSX)) : 96;
            if (device) ReleaseDC(nullptr, device);
            return dpi;
        }

        int Scale(int value) const { return MulDiv(value, static_cast<int>(dpi_), 96); }

        HWND AddControl(DWORD style, const wchar_t *class_name, const wchar_t *text, int id)
        {
            const HWND control = CreateWindowExW(0, class_name, text, WS_CHILD | WS_VISIBLE | style,
                                                  0, 0, 0, 0, window_, reinterpret_cast<HMENU>(id), nullptr, nullptr);
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
            return control;
        }

        HWND AddLabel(const wchar_t *text)
        {
            return AddControl(SS_LEFT, L"STATIC", text, 0);
        }

        void CreateControls()
        {
            profile_label_ = AddLabel(L"&Profile file:");
            profile_path_ = AddControl(WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, L"EDIT", L"", profile_path_edit);
            AddControl(BS_PUSHBUTTON | WS_TABSTOP, L"BUTTON", L"&Load...", load_profile_button);
            AddControl(BS_PUSHBUTTON | WS_TABSTOP, L"BUTTON", L"&Save...", save_profile_button);

            name_label_ = AddLabel(L"Profile &name:");
            profile_name_ = AddControl(WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, L"EDIT", L"Smedley", profile_name_edit);

            game_label_ = AddLabel(L"&Game directory:");
            game_dir_ = AddControl(WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, L"EDIT", L"", game_dir_edit);
            const auto executable_dir = ExecutableDirectory();
            if (fs::is_regular_file(executable_dir / L"v2game.exe")) SetText(game_dir_, executable_dir.wstring());
            AddControl(BS_PUSHBUTTON | WS_TABSTOP, L"BUTTON", L"&Browse...", browse_game_button);
            AddControl(BS_PUSHBUTTON | WS_TABSTOP, L"BUTTON", L"&Refresh", refresh_button);

            mod_label_ = AddLabel(L"&Mod (optional):");
            mods_ = AddControl(CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, L"COMBOBOX", L"", mod_combo);

            plugin_label_ = AddLabel(L"&Native plugins (trusted DLLs):");
            plugins_ = AddControl(LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL | WS_TABSTOP | WS_BORDER,
                                  WC_LISTVIEWW, L"", plugin_list);
            ListView_SetExtendedListViewStyle(plugins_, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            AddPluginColumn(L"Plugin", 230);
            AddPluginColumn(L"Version", 85);
            AddPluginColumn(L"Manifest", 300);

            safe_mode_ = AddControl(BS_AUTOCHECKBOX | WS_TABSTOP, L"BUTTON", L"&Safe mode (do not inject Smedley)", safe_mode_check);

            telemetry_enabled_ = AddControl(BS_AUTOCHECKBOX | WS_TABSTOP, L"BUTTON", L"Enable structured &telemetry", telemetry_enabled_check);
            telemetry_overwrite_ = AddControl(BS_AUTOCHECKBOX | WS_TABSTOP, L"BUTTON", L"Allow telemetry output &overwrite", telemetry_overwrite_check);
            telemetry_output_label_ = AddLabel(L"Telemetry &output:");
            telemetry_output_ = AddControl(WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, L"EDIT", L"", telemetry_output_edit);
            AddControl(BS_PUSHBUTTON | WS_TABSTOP, L"BUTTON", L"Browse...", browse_telemetry_button);
            telemetry_categories_label_ = AddLabel(L"Telemetry &categories:");
            telemetry_categories_ = AddControl(CBS_DROPDOWNLIST | WS_TABSTOP, L"COMBOBOX", L"", telemetry_categories_combo);
            SendMessageW(telemetry_categories_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Lifecycle + state"));
            SendMessageW(telemetry_categories_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Lifecycle only"));
            SendMessageW(telemetry_categories_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"State only"));
            SendMessageW(telemetry_categories_, CB_SETCURSEL, 0, 0);
            telemetry_sample_days_label_ = AddLabel(L"Sample &days:");
            telemetry_sample_days_ = AddControl(WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, L"EDIT", L"1", telemetry_sample_days_edit);
            telemetry_queue_capacity_label_ = AddLabel(L"&Queue capacity:");
            telemetry_queue_capacity_ = AddControl(WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, L"EDIT", L"1024", telemetry_queue_capacity_edit);
            telemetry_country_tags_label_ = AddLabel(L"Country &tags:");
            telemetry_country_tags_ = AddControl(WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, L"EDIT", L"", telemetry_country_tags_edit);
            telemetry_start_date_label_ = AddLabel(L"Start raw date:");
            telemetry_start_date_ = AddControl(WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, L"EDIT", L"", telemetry_start_date_edit);
            telemetry_end_date_label_ = AddLabel(L"End raw date:");
            telemetry_end_date_ = AddControl(WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, L"EDIT", L"", telemetry_end_date_edit);

            save_label_ = AddLabel(L"Campaign &save:");
            save_path_ = AddControl(WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, L"EDIT", L"", save_path_edit);
            AddControl(BS_PUSHBUTTON | WS_TABSTOP, L"BUTTON", L"Browse...", browse_save_button);

            observer_ = AddControl(BS_AUTOCHECKBOX | WS_TABSTOP, L"BUTTON", L"&Observer mode", observer_check);
            view_tag_label_ = AddLabel(L"View &tag:");
            view_tag_ = AddControl(WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, L"EDIT", L"", view_tag_edit);
            speed_label_ = AddLabel(L"&Speed:");
            speed_ = AddControl(CBS_DROPDOWNLIST | WS_TABSTOP, L"COMBOBOX", L"", speed_combo);
            for (int value = 1; value <= 5; ++value) {
                const auto text = std::to_wstring(value);
                SendMessageW(speed_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
            }
            SendMessageW(speed_, CB_SETCURSEL, 4, 0);
            start_paused_ = AddControl(BS_AUTOCHECKBOX | WS_TABSTOP, L"BUTTON", L"Start &paused", start_paused_check);

            diagnostics_label_ = AddLabel(L"&Diagnostics:");
            diagnostics_ = AddControl(WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL | WS_TABSTOP,
                                      L"EDIT", L"", diagnostics_edit);
            status_ = AddControl(SS_LEFT, L"STATIC", L"", status_text);
            AddControl(BS_PUSHBUTTON | WS_TABSTOP, L"BUTTON", L"&Recent runs", recent_runs_button);
            launch_ = AddControl(BS_DEFPUSHBUTTON | WS_TABSTOP, L"BUTTON", L"&Launch Victoria II", launch_button);
        }

        void AddPluginColumn(const wchar_t *heading, int width)
        {
            LVCOLUMNW column{};
            column.mask = LVCF_TEXT | LVCF_WIDTH;
            column.pszText = const_cast<wchar_t *>(heading);
            column.cx = Scale(width);
            ListView_InsertColumn(plugins_, plugin_column_count_++, &column);
        }

        void PlaceLabel(HWND label, int y, int label_width, int row, int margin)
        {
            MoveWindow(label, margin, y + Scale(4), label_width - Scale(4), row - Scale(4), TRUE);
        }

        void Layout(int width, int height)
        {
            if (!profile_path_) return;
            const int margin = Scale(12);
            const int label_width = Scale(122);
            const int row = Scale(26);
            const int button_width = Scale(82);
            const int small_button_width = Scale(76);
            const int right = std::max(width - margin, margin + Scale(500));
            int y = margin;

            PlaceLabel(profile_label_, y, label_width, row, margin);
            MoveWindow(profile_path_, margin + label_width, y, right - margin - label_width - button_width * 2 - Scale(8), Scale(22), TRUE);
            MoveWindow(GetDlgItem(window_, load_profile_button), right - button_width * 2 - Scale(4), y, button_width, Scale(22), TRUE);
            MoveWindow(GetDlgItem(window_, save_profile_button), right - button_width, y, button_width, Scale(22), TRUE);
            y += row;
            PlaceLabel(telemetry_country_tags_label_, y, label_width, row, margin);
            MoveWindow(telemetry_country_tags_, margin + label_width, y, Scale(150), Scale(22), TRUE);
            PlaceLabel(telemetry_start_date_label_, y, Scale(100), row, margin + label_width + Scale(165));
            MoveWindow(telemetry_start_date_, margin + label_width + Scale(265), y, Scale(85), Scale(22), TRUE);
            PlaceLabel(telemetry_end_date_label_, y, Scale(90), row, margin + label_width + Scale(360));
            MoveWindow(telemetry_end_date_, margin + label_width + Scale(450), y, Scale(85), Scale(22), TRUE);
            y += row;

            PlaceLabel(name_label_, y, label_width, row, margin);
            MoveWindow(profile_name_, margin + label_width, y, right - margin - label_width, Scale(22), TRUE);
            y += row;

            PlaceLabel(game_label_, y, label_width, row, margin);
            MoveWindow(game_dir_, margin + label_width, y, right - margin - label_width - button_width - small_button_width - Scale(8), Scale(22), TRUE);
            MoveWindow(GetDlgItem(window_, browse_game_button), right - button_width - small_button_width - Scale(4), y, button_width, Scale(22), TRUE);
            MoveWindow(GetDlgItem(window_, refresh_button), right - small_button_width, y, small_button_width, Scale(22), TRUE);
            y += row;

            PlaceLabel(mod_label_, y, label_width, row, margin);
            MoveWindow(mods_, margin + label_width, y, right - margin - label_width, Scale(300), TRUE);
            y += row;

            PlaceLabel(plugin_label_, y, label_width, row, margin);
            y += row;
            const int bottom_fixed = Scale(26 * 10 + 30 + 130 + 44);
            const int plugin_height = std::max(Scale(100), height - y - bottom_fixed);
            MoveWindow(plugins_, margin, y, right - margin, plugin_height, TRUE);
            y += plugin_height + Scale(4);

            MoveWindow(safe_mode_, margin, y, right - margin, Scale(22), TRUE);
            y += row;

            MoveWindow(telemetry_enabled_, margin, y, right - margin, Scale(22), TRUE);
            y += row;
            MoveWindow(telemetry_overwrite_, margin, y, right - margin, Scale(22), TRUE);
            y += row;
            PlaceLabel(telemetry_output_label_, y, label_width, row, margin);
            MoveWindow(telemetry_output_, margin + label_width, y, right - margin - label_width - button_width - Scale(4), Scale(22), TRUE);
            MoveWindow(GetDlgItem(window_, browse_telemetry_button), right - button_width, y, button_width, Scale(22), TRUE);
            y += row;
            PlaceLabel(telemetry_categories_label_, y, label_width, row, margin);
            MoveWindow(telemetry_categories_, margin + label_width, y, Scale(180), Scale(200), TRUE);
            PlaceLabel(telemetry_sample_days_label_, y, Scale(90), row, margin + label_width + Scale(195));
            MoveWindow(telemetry_sample_days_, margin + label_width + Scale(285), y, Scale(70), Scale(22), TRUE);
            PlaceLabel(telemetry_queue_capacity_label_, y, Scale(100), row, margin + label_width + Scale(370));
            MoveWindow(telemetry_queue_capacity_, margin + label_width + Scale(470), y, Scale(80), Scale(22), TRUE);
            y += row;

            PlaceLabel(save_label_, y, label_width, row, margin);
            MoveWindow(save_path_, margin + label_width, y, right - margin - label_width - button_width - Scale(4), Scale(22), TRUE);
            MoveWindow(GetDlgItem(window_, browse_save_button), right - button_width, y, button_width, Scale(22), TRUE);
            y += row;

            MoveWindow(observer_, margin, y, Scale(140), Scale(22), TRUE);
            PlaceLabel(view_tag_label_, y, Scale(80), row, margin + Scale(150));
            MoveWindow(view_tag_, margin + Scale(225), y, Scale(86), Scale(22), TRUE);
            y += row;
            PlaceLabel(speed_label_, y, Scale(60), row, margin);
            MoveWindow(speed_, margin + Scale(62), y, Scale(70), Scale(300), TRUE);
            MoveWindow(start_paused_, margin + Scale(145), y, Scale(150), Scale(22), TRUE);
            y += row;

            PlaceLabel(diagnostics_label_, y, label_width, row, margin);
            y += row;
            const int button_height = Scale(28);
            const int diagnostic_height = std::max(Scale(80), height - y - button_height - margin * 2);
            MoveWindow(diagnostics_, margin, y, right - margin, diagnostic_height, TRUE);
            y += diagnostic_height + Scale(5);
            MoveWindow(status_, margin, y + Scale(5), right - margin - Scale(270), Scale(20), TRUE);
            MoveWindow(GetDlgItem(window_, recent_runs_button), right - Scale(260), y, Scale(94), button_height, TRUE);
            MoveWindow(launch_, right - Scale(160), y, Scale(160), button_height, TRUE);
            ListView_SetColumnWidth(plugins_, 0, Scale(230));
            ListView_SetColumnWidth(plugins_, 1, Scale(85));
            ListView_SetColumnWidth(plugins_, 2, std::max(Scale(100), right - margin - Scale(315)));
        }

        void OnCommand(int id, int notification)
        {
            if (id == load_profile_button && notification == BN_CLICKED) LoadProfile();
            else if (id == save_profile_button && notification == BN_CLICKED) SaveProfile();
            else if (id == browse_game_button && notification == BN_CLICKED) {
                std::wstring path = GetText(game_dir_);
                if (BrowseForFolder(window_, &path)) {
                    SetText(game_dir_, path);
                    RefreshDiscovery();
                }
            } else if (id == refresh_button && notification == BN_CLICKED) RefreshDiscovery();
            else if (id == browse_save_button && notification == BN_CLICKED) {
                std::wstring path = GetText(save_path_);
                if (BrowseForFile(window_, L"Select Victoria II save", L"Victoria II saves (*.v2)\0*.v2\0All files\0*.*\0", &path, false)) {
                    SetText(save_path_, path);
                    RefreshPlan();
                }
            } else if (id == browse_telemetry_button && notification == BN_CLICKED) {
                std::wstring path = GetText(telemetry_output_);
                if (BrowseForFile(window_, L"Save telemetry trace", L"JSON Lines traces (*.jsonl)\0*.jsonl\0All files\0*.*\0", &path, true, L"jsonl")) {
                    SetText(telemetry_output_, path);
                    RefreshPlan();
                }
            } else if (id == launch_button && notification == BN_CLICKED) LaunchGame();
            else if (id == recent_runs_button && notification == BN_CLICKED) RecentRunsWindow::Show(window_);
            else if ((id == safe_mode_check || id == observer_check || id == start_paused_check || id == telemetry_enabled_check || id == telemetry_overwrite_check) && notification == BN_CLICKED) RefreshPlan();
            else if (id == mod_combo && notification == CBN_SELCHANGE) {
                retained_mods_.clear();
                unsupported_multi_mod_ = false;
                RefreshPlan();
            } else if ((id == speed_combo || id == telemetry_categories_combo) && notification == CBN_SELCHANGE) RefreshPlan();
            else if ((id == game_dir_edit || id == save_path_edit || id == view_tag_edit || id == profile_name_edit || id == telemetry_output_edit
                      || id == telemetry_sample_days_edit || id == telemetry_queue_capacity_edit || id == telemetry_country_tags_edit
                      || id == telemetry_start_date_edit || id == telemetry_end_date_edit) && notification == EN_KILLFOCUS) {
                if (id == game_dir_edit) RefreshDiscovery();
                else RefreshPlan();
            }
        }

        void OnNotify(const NMHDR *notification)
        {
            if (!suppress_notifications_ && notification->idFrom == plugin_list && notification->code == LVN_ITEMCHANGED) {
                const auto *change = reinterpret_cast<const NMLISTVIEW *>(notification);
                if ((change->uChanged & LVIF_STATE) != 0
                    && ((change->uOldState ^ change->uNewState) & LVIS_STATEIMAGEMASK) != 0) {
                    RefreshPlan();
                }
            }
        }

        launcher::Profile BuildProfile() const
        {
            launcher::Profile profile;
            profile.name = WideToUtf8(GetText(profile_name_));
            profile.game_dir = GetText(game_dir_);
            profile.kernel = retained_kernel_;
            profile.inject = SendMessageW(safe_mode_, BM_GETCHECK, 0, 0) != BST_CHECKED;
            profile.detach = retained_detach_;
            if (const auto selected = SelectedMod()) profile.mods.push_back(*selected);
            profile.mods.insert(profile.mods.end(), retained_mods_.begin(), retained_mods_.end());
            profile.plugins = SelectedPlugins();
            profile.plugins.insert(profile.plugins.end(), retained_plugins_.begin(), retained_plugins_.end());
            const auto save = GetText(save_path_);
            if (!save.empty()) profile.save = fs::path(save);
            profile.observer = SendMessageW(observer_, BM_GETCHECK, 0, 0) == BST_CHECKED;
            const auto view_tag = GetText(view_tag_);
            if (!view_tag.empty()) profile.view_tag = view_tag;
            profile.speed = static_cast<int>(SendMessageW(speed_, CB_GETCURSEL, 0, 0)) + 1;
            profile.start_paused = SendMessageW(start_paused_, BM_GETCHECK, 0, 0) == BST_CHECKED;
            profile.telemetry_enabled = SendMessageW(telemetry_enabled_, BM_GETCHECK, 0, 0) == BST_CHECKED;
            profile.telemetry_overwrite = SendMessageW(telemetry_overwrite_, BM_GETCHECK, 0, 0) == BST_CHECKED;
            const auto telemetry_output = GetText(telemetry_output_);
            if (!telemetry_output.empty()) profile.telemetry_output = fs::path(telemetry_output);
            const int category_selection = static_cast<int>(SendMessageW(telemetry_categories_, CB_GETCURSEL, 0, 0));
            if (category_selection == 1) profile.telemetry_categories = {"lifecycle"};
            else if (category_selection == 2) profile.telemetry_categories = {"state"};
            else profile.telemetry_categories = {"lifecycle", "state"};
            try {
                profile.telemetry_sample_days = std::stoi(GetText(telemetry_sample_days_));
                profile.telemetry_queue_capacity = std::stoi(GetText(telemetry_queue_capacity_));
            } catch (const std::exception &) {
                profile.telemetry_sample_days = 0;
                profile.telemetry_queue_capacity = 0;
            }
            std::wstring tags = GetText(telemetry_country_tags_);
            size_t begin = 0;
            while (begin <= tags.size()) {
                const size_t end = tags.find(L',', begin);
                std::wstring tag = tags.substr(begin, end == std::wstring::npos ? end : end - begin);
                const auto first = tag.find_first_not_of(L" \t");
                tag = first == std::wstring::npos ? L"" : tag.substr(first, tag.find_last_not_of(L" \t") - first + 1);
                if (!tag.empty()) { std::transform(tag.begin(), tag.end(), tag.begin(), towupper); profile.telemetry_country_tags.push_back(WideToUtf8(tag)); }
                else if (end != std::wstring::npos) profile.telemetry_country_tags.push_back("");
                if (end == std::wstring::npos) break;
                begin = end + 1;
            }
            auto parse_date = [&](HWND control, std::optional<int> *destination, const wchar_t *name) {
                const auto value = GetText(control);
                if (value.empty()) return;
                try {
                    size_t used = 0;
                    const int parsed = std::stoi(value, &used);
                    if (used != value.size()) throw std::invalid_argument("trailing characters");
                    *destination = parsed;
                } catch (const std::exception &) {
                    profile.telemetry_filter_parse_error = std::string("telemetry ") + WideToUtf8(name) + " must be an integer";
                }
            };
            parse_date(telemetry_start_date_, &profile.telemetry_start_date_raw, L"start raw date");
            parse_date(telemetry_end_date_, &profile.telemetry_end_date_raw, L"end raw date");
            return profile;
        }

        std::vector<fs::path> SelectedPlugins() const
        {
            std::vector<fs::path> paths;
            for (size_t index = 0; index < discovered_plugins_.size(); ++index) {
                if ((ListView_GetItemState(plugins_, static_cast<int>(index), LVIS_STATEIMAGEMASK) & LVIS_STATEIMAGEMASK)
                    == INDEXTOSTATEIMAGEMASK(2)) {
                    paths.push_back(discovered_plugins_[index].manifest_path);
                }
            }
            return paths;
        }

        std::optional<fs::path> SelectedMod() const
        {
            const int selected = static_cast<int>(SendMessageW(mods_, CB_GETCURSEL, 0, 0));
            if (selected <= 0 || static_cast<size_t>(selected - 1) >= discovered_mods_.size()) return std::nullopt;
            return discovered_mods_[selected - 1].descriptor_path;
        }

        void RefreshDiscovery(std::vector<fs::path> requested_mods = {},
                               std::vector<fs::path> requested_plugins = {})
        {
            if (requested_mods.empty()) {
                if (const auto selected = SelectedMod()) requested_mods.push_back(*selected);
                requested_mods.insert(requested_mods.end(), retained_mods_.begin(), retained_mods_.end());
            }
            if (requested_plugins.empty()) {
                requested_plugins = SelectedPlugins();
                requested_plugins.insert(requested_plugins.end(), retained_plugins_.begin(), retained_plugins_.end());
            }
            const fs::path game_dir = GetText(game_dir_);
            discovery_diagnostics_.clear();
            discovered_mods_.clear();
            discovered_plugins_.clear();
            if (!game_dir.empty()) {
                const auto mods = launcher::DiscoverMods(game_dir);
                const auto plugins = launcher::DiscoverPlugins(game_dir);
                discovered_mods_ = mods.mods;
                discovered_plugins_ = plugins.plugins;
                discovery_diagnostics_.insert(discovery_diagnostics_.end(), mods.diagnostics.begin(), mods.diagnostics.end());
                discovery_diagnostics_.insert(discovery_diagnostics_.end(), plugins.diagnostics.begin(), plugins.diagnostics.end());
            }
            PopulateDiscovery(game_dir, requested_mods, requested_plugins);
            RefreshPlan();
        }

        void PopulateDiscovery(const fs::path &game_dir, const std::vector<fs::path> &requested_mods,
                                const std::vector<fs::path> &requested_plugins)
        {
            suppress_notifications_ = true;
            SendMessageW(mods_, CB_RESETCONTENT, 0, 0);
            SendMessageW(mods_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"None"));
            int selected_mod = 0;
            retained_mods_ = requested_mods;
            unsupported_multi_mod_ = requested_mods.size() > 1;
            const auto selected_mod_path = requested_mods.empty() ? fs::path{} : ResolveSelectedPath(game_dir, requested_mods.front());
            for (size_t index = 0; index < discovered_mods_.size(); ++index) {
                const auto name = Utf8ToWide(discovered_mods_[index].name);
                SendMessageW(mods_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
                if (!requested_mods.empty() && AbsolutePath(discovered_mods_[index].descriptor_path) == selected_mod_path) {
                    selected_mod = static_cast<int>(index + 1);
                    retained_mods_.erase(retained_mods_.begin());
                }
            }
            SendMessageW(mods_, CB_SETCURSEL, selected_mod, 0);

            ListView_DeleteAllItems(plugins_);
            retained_plugins_ = requested_plugins;
            for (size_t index = 0; index < discovered_plugins_.size(); ++index) {
                const auto name = Utf8ToWide(discovered_plugins_[index].name);
                const auto version = Utf8ToWide(discovered_plugins_[index].version);
                const auto manifest = discovered_plugins_[index].manifest_path.wstring();
                LVITEMW item{};
                item.mask = LVIF_TEXT;
                item.iItem = static_cast<int>(index);
                item.pszText = const_cast<wchar_t *>(name.c_str());
                ListView_InsertItem(plugins_, &item);
                ListView_SetItemText(plugins_, static_cast<int>(index), 1, const_cast<wchar_t *>(version.c_str()));
                ListView_SetItemText(plugins_, static_cast<int>(index), 2, const_cast<wchar_t *>(manifest.c_str()));
                const auto selected = std::find_if(retained_plugins_.begin(), retained_plugins_.end(), [&](const auto &path) {
                    return ResolveSelectedPath(game_dir, path) == AbsolutePath(discovered_plugins_[index].manifest_path);
                });
                if (selected != retained_plugins_.end()) {
                    ListView_SetCheckState(plugins_, static_cast<int>(index), TRUE);
                    retained_plugins_.erase(selected);
                }
            }
            if (unsupported_multi_mod_) {
                discovery_diagnostics_.push_back({
                    launcher::Severity::Error,
                    "gui.multiple_mods",
                    "this launcher UI supports one mod; choose one mod to replace the loaded multi-mod selection",
                    {}});
            }
            suppress_notifications_ = false;
        }

        void RefreshPlan()
        {
            operation_diagnostics_.clear();
            plan_ = launcher::BuildLaunchPlan(BuildProfile());
            DisplayDiagnostics(plan_.diagnostics);
            const bool has_errors = unsupported_multi_mod_ || launcher::HasErrors(plan_.diagnostics);
            EnableWindow(launch_, !has_errors);
            SetText(status_, has_errors ? L"Fix preflight errors before launching." : L"Ready. Launches detached.");
        }

        void DisplayDiagnostics(const std::vector<launcher::Diagnostic> &current)
        {
            std::wstring text;
            auto append = [&](const std::vector<launcher::Diagnostic> &diagnostics) {
                for (const auto &diagnostic : diagnostics) {
                    text += SeverityName(diagnostic.severity) + L" [" + Utf8ToWide(diagnostic.code) + L"] "
                        + Utf8ToWide(diagnostic.message);
                    if (!diagnostic.path.empty()) text += L" (" + diagnostic.path.wstring() + L")";
                    text += L"\r\n";
                }
            };
            append(discovery_diagnostics_);
            append(current);
            append(operation_diagnostics_);
            if (text.empty()) text = L"No diagnostics.";
            SetText(diagnostics_, text);
        }

        void LoadProfile()
        {
            std::wstring path = GetText(profile_path_);
            if (!BrowseForFile(window_, L"Load Smedley profile", L"Smedley profiles (*.toml)\0*.toml\0All files\0*.*\0", &path, false)) return;
            launcher::Profile profile;
            std::vector<launcher::Diagnostic> diagnostics;
            if (!launcher::LoadProfile(path, &profile, &diagnostics)) {
                operation_diagnostics_ = std::move(diagnostics);
                DisplayDiagnostics(plan_.diagnostics);
                SetText(status_, L"Could not load profile.");
                return;
            }
            SetText(profile_path_, path);
            SetText(profile_name_, Utf8ToWide(profile.name));
            SetText(game_dir_, profile.game_dir.wstring());
            retained_kernel_ = profile.kernel;
            SetText(save_path_, profile.save ? profile.save->wstring() : L"");
            SetText(view_tag_, profile.view_tag.value_or(L""));
            SendMessageW(safe_mode_, BM_SETCHECK, profile.inject ? BST_UNCHECKED : BST_CHECKED, 0);
            SendMessageW(observer_, BM_SETCHECK, profile.observer ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(speed_, CB_SETCURSEL, profile.speed >= 1 && profile.speed <= 5 ? profile.speed - 1 : -1, 0);
            SendMessageW(start_paused_, BM_SETCHECK, profile.start_paused ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(telemetry_enabled_, BM_SETCHECK, profile.telemetry_enabled ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(telemetry_overwrite_, BM_SETCHECK, profile.telemetry_overwrite ? BST_CHECKED : BST_UNCHECKED, 0);
            SetText(telemetry_output_, profile.telemetry_output ? profile.telemetry_output->wstring() : L"");
            const int telemetry_categories = profile.telemetry_categories == std::vector<std::string>{"lifecycle"} ? 1
                : profile.telemetry_categories == std::vector<std::string>{"state"} ? 2 : 0;
            SendMessageW(telemetry_categories_, CB_SETCURSEL, telemetry_categories, 0);
            SetText(telemetry_sample_days_, std::to_wstring(profile.telemetry_sample_days));
            SetText(telemetry_queue_capacity_, std::to_wstring(profile.telemetry_queue_capacity));
            std::wstring tags;
            for (const auto &tag : profile.telemetry_country_tags) { if (!tags.empty()) tags += L","; tags.append(tag.begin(), tag.end()); }
            SetText(telemetry_country_tags_, tags);
            SetText(telemetry_start_date_, profile.telemetry_start_date_raw ? std::to_wstring(*profile.telemetry_start_date_raw) : L"");
            SetText(telemetry_end_date_, profile.telemetry_end_date_raw ? std::to_wstring(*profile.telemetry_end_date_raw) : L"");
            retained_detach_ = profile.detach;
            // A loaded profile intentionally replaces, rather than merges with, old selections.
            discovered_mods_.clear();
            discovered_plugins_.clear();
            retained_mods_.clear();
            retained_plugins_.clear();
            RefreshDiscovery(profile.mods, profile.plugins);
            operation_diagnostics_ = std::move(diagnostics);
            DisplayDiagnostics(plan_.diagnostics);
        }

        void SaveProfile()
        {
            std::wstring path = GetText(profile_path_);
            if (!BrowseForFile(window_, L"Save Smedley profile", L"Smedley profiles (*.toml)\0*.toml\0All files\0*.*\0", &path, true, L"toml")) return;
            std::vector<launcher::Diagnostic> diagnostics;
            if (!launcher::SaveProfile(path, BuildProfile(), &diagnostics)) {
                operation_diagnostics_ = std::move(diagnostics);
                DisplayDiagnostics(plan_.diagnostics);
                SetText(status_, L"Could not save profile.");
                return;
            }
            SetText(profile_path_, path);
            RefreshPlan();
            SetText(status_, L"Profile saved.");
        }

        void LaunchGame()
        {
            RefreshPlan();
            if (unsupported_multi_mod_ || launcher::HasErrors(plan_.diagnostics)) return;
            auto detached_plan = plan_;
            detached_plan.profile.detach = true;
            const auto result = launcher::Launch(detached_plan);
            operation_diagnostics_ = result.diagnostics;
            DisplayDiagnostics({});
            if (result.started) SetText(status_, L"Victoria II launched (PID " + std::to_wstring(result.process_id) + L").");
            else SetText(status_, L"Launch failed. See diagnostics.");
        }

        HWND window_ = nullptr;
        HWND profile_label_ = nullptr;
        HWND profile_path_ = nullptr;
        HWND name_label_ = nullptr;
        HWND profile_name_ = nullptr;
        HWND game_label_ = nullptr;
        HWND game_dir_ = nullptr;
        HWND mod_label_ = nullptr;
        HWND mods_ = nullptr;
        HWND plugin_label_ = nullptr;
        HWND plugins_ = nullptr;
        HWND safe_mode_ = nullptr;
        HWND telemetry_enabled_ = nullptr;
        HWND telemetry_overwrite_ = nullptr;
        HWND telemetry_output_label_ = nullptr;
        HWND telemetry_output_ = nullptr;
        HWND telemetry_categories_label_ = nullptr;
        HWND telemetry_categories_ = nullptr;
        HWND telemetry_sample_days_label_ = nullptr;
        HWND telemetry_sample_days_ = nullptr;
        HWND telemetry_queue_capacity_label_ = nullptr;
        HWND telemetry_queue_capacity_ = nullptr;
        HWND telemetry_country_tags_label_ = nullptr;
        HWND telemetry_country_tags_ = nullptr;
        HWND telemetry_start_date_label_ = nullptr;
        HWND telemetry_start_date_ = nullptr;
        HWND telemetry_end_date_label_ = nullptr;
        HWND telemetry_end_date_ = nullptr;
        HWND save_label_ = nullptr;
        HWND save_path_ = nullptr;
        HWND observer_ = nullptr;
        HWND view_tag_label_ = nullptr;
        HWND view_tag_ = nullptr;
        HWND speed_label_ = nullptr;
        HWND speed_ = nullptr;
        HWND start_paused_ = nullptr;
        HWND diagnostics_label_ = nullptr;
        HWND diagnostics_ = nullptr;
        HWND status_ = nullptr;
        HWND launch_ = nullptr;
        UINT dpi_ = 96;
        int plugin_column_count_ = 0;
        bool suppress_notifications_ = false;
        bool unsupported_multi_mod_ = false;
        bool retained_detach_ = true;
        launcher::LaunchPlan plan_;
        std::optional<fs::path> retained_kernel_;
        std::vector<fs::path> retained_mods_;
        std::vector<fs::path> retained_plugins_;
        std::vector<launcher::ModDescriptor> discovered_mods_;
        std::vector<launcher::PluginManifest> discovered_plugins_;
        std::vector<launcher::Diagnostic> discovery_diagnostics_;
        std::vector<launcher::Diagnostic> operation_diagnostics_;
    };
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    static_assert(sizeof(void *) == 4, "build smedley_launcher for x86 Victoria 2");
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX common_controls{sizeof(common_controls), ICC_LISTVIEW_CLASSES};
    if (!InitCommonControlsEx(&common_controls)) return 1;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    LauncherWindow launcher_window;
    if (!launcher_window.Create(instance)) return 1;
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(launcher_window.window(), &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (SUCCEEDED(com)) CoUninitialize();
    return static_cast<int>(message.wParam);
}
