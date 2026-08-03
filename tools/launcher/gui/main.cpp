#include <smedley/launcher/launcher.hpp>

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <charconv>
#include <cwctype>
#include <filesystem>
#include <limits>
#include <map>
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
        mod_list,
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
        options_button,
        options_close_button,
        mod_move_up_button,
        mod_move_down_button,
        options_page_list,
        options_show_advanced_check,
        options_field_base = 1000,
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

    std::wstring FormatDouble(double value)
    {
        char buffer[64];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general,
                                          std::numeric_limits<double>::max_digits10);
        if (result.ec != std::errc{}) return {};
        const std::string text(buffer, result.ptr);
        return {text.begin(), text.end()};
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

    struct CaptureEditResult
    {
        std::optional<size_t> index;
        size_t generation = 0;
        std::optional<launcher::TelemetryCaptureRule> original;
        launcher::TelemetryCaptureRule replacement;
    };

    class CaptureEditorWindow
    {
    public:
        static void Show(HWND owner, std::optional<size_t> index, size_t generation,
                         const launcher::TelemetryCaptureRule &rule = {})
        {
            auto *self = new CaptureEditorWindow(owner, index, generation, rule);
            WNDCLASSW window_class{};
            window_class.hInstance = GetModuleHandleW(nullptr);
            window_class.lpszClassName = L"SmedleyCaptureEditor";
            window_class.lpfnWndProc = WindowProc;
            window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            RegisterClassW(&window_class);
            self->modal_owner_ = GetAncestor(owner, GA_ROOTOWNER);
            if (self->modal_owner_ && self->modal_owner_ != owner) EnableWindow(self->modal_owner_, FALSE);
            EnableWindow(owner, FALSE);
            const HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, window_class.lpszClassName, index ? L"Edit capture rule" : L"Add capture rule",
                                                WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                                                540, 480, owner, nullptr, window_class.hInstance, self);
            if (!window) {
                self->EnableOwners();
                delete self;
                return;
            }
            MSG message{};
            while (IsWindow(window) && GetMessageW(&message, nullptr, 0, 0) > 0) {
                if (!IsDialogMessageW(window, &message)) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
            }
            if (message.message == WM_QUIT) PostQuitMessage(static_cast<int>(message.wParam));
        }

    private:
        enum : int { family = 1, cadence, fields, countries, provinces, start, end, save, cancel };
        CaptureEditorWindow(HWND owner, std::optional<size_t> index, size_t generation,
                            const launcher::TelemetryCaptureRule &rule)
            : owner_(owner), index_(index), generation_(generation), rule_(rule)
        {
            if (index_) original_ = rule;
        }

        static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
        {
            auto *self = reinterpret_cast<CaptureEditorWindow *>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (message == WM_NCCREATE) {
                self = static_cast<CaptureEditorWindow *>(reinterpret_cast<const CREATESTRUCTW *>(lparam)->lpCreateParams);
                SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
                self->window_ = window;
            }
            if (!self) return DefWindowProcW(window, message, wparam, lparam);
            if (message == WM_NCDESTROY) {
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                self->EnableOwners();
                delete self;
                return 0;
            }
            return self->Handle(message, wparam, lparam);
        }

        HWND Add(const wchar_t *class_name, const wchar_t *text, DWORD style, int id)
        {
            const HWND control = CreateWindowExW(0, class_name, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
                                                 window_, reinterpret_cast<HMENU>(id), nullptr, nullptr);
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
            return control;
        }

        void Create()
        {
            Add(L"STATIC", L"Family", SS_LEFT, 0); family_ = Add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, family);
            Add(L"STATIC", L"Cadence", SS_LEFT, 0); cadence_ = Add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, cadence);
            Add(L"STATIC", L"Fields (none means all)", SS_LEFT, 0); fields_ = Add(L"LISTBOX", L"", LBS_MULTIPLESEL | WS_BORDER | WS_TABSTOP | WS_VSCROLL, fields);
            Add(L"STATIC", L"Country tags (comma-separated)", SS_LEFT, 0); countries_ = Add(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, countries);
            Add(L"STATIC", L"Province IDs (comma-separated)", SS_LEFT, 0); provinces_ = Add(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, provinces);
            Add(L"STATIC", L"Start date (DD-MM-YYYY)", SS_LEFT, 0); start_ = Add(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, start);
            Add(L"STATIC", L"End date (DD-MM-YYYY)", SS_LEFT, 0); end_ = Add(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, end);
            Add(L"BUTTON", L"Save", BS_DEFPUSHBUTTON | WS_TABSTOP, save); Add(L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, cancel);
            for (const auto &item : launcher::TelemetryCaptureFamilies()) {
                const auto text = Utf8ToWide(item.id); SendMessageW(family_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
            }
            for (const auto *item : {L"daily", L"weekly", L"monthly", L"yearly"}) SendMessageW(cadence_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
            const auto family_text = Utf8ToWide(rule_.family.empty() ? launcher::TelemetryCaptureFamilies().front().id : rule_.family);
            SendMessageW(family_, CB_SELECTSTRING, -1, reinterpret_cast<LPARAM>(family_text.c_str()));
            PopulateFields(rule_.fields);
            const auto cadence_text = Utf8ToWide(rule_.cadence); SendMessageW(cadence_, CB_SELECTSTRING, -1, reinterpret_cast<LPARAM>(cadence_text.c_str()));
            SetText(countries_, Join(rule_.country_tags)); SetText(provinces_, JoinNumbers(rule_.province_ids));
            const auto set_date = [](HWND control, const std::optional<int> &raw) {
                if (!raw) return;
                if (const auto date = launcher::DecodeClausewitzDate(*raw)) SetText(control, Utf8ToWide(launcher::FormatClausewitzDate(*date)));
                else SetText(control, L"raw: " + std::to_wstring(*raw));
            };
            set_date(start_, rule_.start_date_raw);
            set_date(end_, rule_.end_date_raw);
            Layout();
        }

        void PopulateFields(const std::vector<std::string> &selected)
        {
            SendMessageW(fields_, LB_RESETCONTENT, 0, 0);
            const int selected_family = static_cast<int>(SendMessageW(family_, CB_GETCURSEL, 0, 0));
            if (selected_family < 0) return;
            const auto &item = launcher::TelemetryCaptureFamilies()[static_cast<size_t>(selected_family)];
            for (const auto &field : item.fields) {
                const auto text = Utf8ToWide(field); const auto row = SendMessageW(fields_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
                if (std::find(selected.begin(), selected.end(), field) != selected.end()) SendMessageW(fields_, LB_SETSEL, TRUE, row);
            }
        }

        static std::wstring Join(const std::vector<std::string> &values)
        {
            std::wstring result; for (const auto &value : values) { if (!result.empty()) result += L","; result += Utf8ToWide(value); } return result;
        }
        static std::wstring JoinNumbers(const std::vector<int> &values)
        {
            std::wstring result; for (const auto value : values) { if (!result.empty()) result += L","; result += std::to_wstring(value); } return result;
        }
        static std::vector<std::string> SplitTags(const std::wstring &text, bool *valid)
        {
            std::vector<std::string> values; *valid = true; size_t begin = 0;
            while (begin < text.size()) {
                const auto end = text.find(L',', begin); std::wstring value = text.substr(begin, end == std::wstring::npos ? end : end - begin);
                const auto first = value.find_first_not_of(L" \t"); value = first == std::wstring::npos ? L"" : value.substr(first, value.find_last_not_of(L" \t") - first + 1);
                if (value.size() != 3) { *valid = false; return {}; }
                for (auto &character : value) { if (!iswalnum(character)) { *valid = false; return {}; } character = towupper(character); }
                values.push_back(WideToUtf8(value)); if (end == std::wstring::npos) break; begin = end + 1;
            }
            return values;
        }
        static std::vector<int> SplitNumbers(const std::wstring &text, bool *valid)
        {
            std::vector<int> values; *valid = true; size_t begin = 0;
            while (begin < text.size()) {
                const auto end = text.find(L',', begin); const auto value = text.substr(begin, end == std::wstring::npos ? end : end - begin);
                try { size_t used = 0; const int number = std::stoi(value, &used); if (used != value.size()) throw std::invalid_argument("bad"); values.push_back(number); }
                catch (const std::exception &) { *valid = false; return {}; }
                if (end == std::wstring::npos) break; begin = end + 1;
            }
            return values;
        }
        std::vector<std::string> SelectedFields() const
        {
            std::vector<std::string> values; const auto count = SendMessageW(fields_, LB_GETSELCOUNT, 0, 0); std::vector<int> rows(static_cast<size_t>(std::max<LRESULT>(0, count)));
            if (count > 0) SendMessageW(fields_, LB_GETSELITEMS, count, reinterpret_cast<LPARAM>(rows.data()));
            for (const auto row : rows) { const auto length = SendMessageW(fields_, LB_GETTEXTLEN, row, 0); std::wstring value(static_cast<size_t>(length) + 1, L'\0'); SendMessageW(fields_, LB_GETTEXT, row, reinterpret_cast<LPARAM>(value.data())); value.resize(static_cast<size_t>(length)); values.push_back(WideToUtf8(value)); }
            return values;
        }
        void Save()
        {
            const int family_index = static_cast<int>(SendMessageW(family_, CB_GETCURSEL, 0, 0)); const int cadence_index = static_cast<int>(SendMessageW(cadence_, CB_GETCURSEL, 0, 0));
            bool valid_tags = false, valid_provinces = false;
            auto tags = SplitTags(GetText(countries_), &valid_tags); auto provinces = SplitNumbers(GetText(provinces_), &valid_provinces);
            const auto parse_date = [&](HWND control) -> std::optional<int> {
                const auto text = GetText(control);
                if (text.empty()) return 0;
                try {
                    if (text.rfind(L"raw: ", 0) == 0) {
                        size_t used = 0;
                        const auto raw = std::stoll(text.substr(5), &used);
                        if (used != text.size() - 5 || raw < (std::numeric_limits<int>::min)()
                            || raw > (std::numeric_limits<int>::max)()) return std::nullopt;
                        return static_cast<int>(raw);
                    }
                } catch (const std::exception &) { return std::nullopt; }
                const auto date = launcher::ParseClausewitzDate(WideToUtf8(text));
                return date ? launcher::EncodeClausewitzDate(*date) : std::nullopt;
            };
            const auto start = parse_date(start_); const auto end = parse_date(end_);
            if (family_index < 0 || cadence_index < 0 || !valid_tags || !valid_provinces || !start || !end) { MessageBoxW(window_, L"Use known values, comma-separated tags/IDs, and DD-MM-YYYY dates.", L"Capture rule", MB_OK | MB_ICONWARNING); return; }
            launcher::TelemetryCaptureRule rule; rule.family = launcher::TelemetryCaptureFamilies()[static_cast<size_t>(family_index)].id;
            static constexpr const char *cadences[] = {"daily", "weekly", "monthly", "yearly"};
            rule.cadence = cadences[cadence_index]; rule.fields = SelectedFields(); rule.country_tags = std::move(tags); rule.province_ids = std::move(provinces);
            if (*start != 0 || !GetText(start_).empty()) rule.start_date_raw = *start; if (*end != 0 || !GetText(end_).empty()) rule.end_date_raw = *end;
            CaptureEditResult result{index_, generation_, original_, std::move(rule)};
            if (IsWindow(owner_)) SendMessageW(owner_, WM_APP + 1, 0, reinterpret_cast<LPARAM>(&result));
            DestroyWindow(window_);
        }
        void Layout()
        {
            const int label = 14, height = 22, list_height = 95, x = 12, width = 505; int y = 12;
            // Win32 enumerates children in reverse creation order; restore it before pairing labels and inputs.
            std::vector<HWND> children; for (HWND child = GetWindow(window_, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) children.push_back(child);
            std::reverse(children.begin(), children.end());
            for (size_t row = 0; row < 7; ++row) { MoveWindow(children[row * 2], x, y, width, label, TRUE); y += label; const int h = row == 2 ? list_height : height; MoveWindow(children[row * 2 + 1], x, y, width, h, TRUE); y += h + 8; }
            MoveWindow(children[14], width - 150, y, 70, 26, TRUE); MoveWindow(children[15], width - 70, y, 70, 26, TRUE);
        }
        void EnableOwners()
        {
            if (IsWindow(owner_)) EnableWindow(owner_, TRUE);
            if (modal_owner_ && modal_owner_ != owner_ && IsWindow(modal_owner_)) EnableWindow(modal_owner_, TRUE);
        }
        LRESULT Handle(UINT message, WPARAM wparam, LPARAM lparam)
        {
            if (message == WM_CREATE) { Create(); return 0; }
            if (message == WM_COMMAND) { const auto id = LOWORD(wparam); if (id == family && HIWORD(wparam) == CBN_SELCHANGE) PopulateFields({}); else if (id == save) Save(); else if (id == cancel) DestroyWindow(window_); return 0; }
            if (message == WM_CLOSE) { DestroyWindow(window_); return 0; }
            return DefWindowProcW(window_, message, wparam, lparam);
        }
        HWND owner_ = nullptr, window_ = nullptr, family_ = nullptr, cadence_ = nullptr, fields_ = nullptr, countries_ = nullptr, provinces_ = nullptr, start_ = nullptr, end_ = nullptr;
        HWND modal_owner_ = nullptr;
        std::optional<size_t> index_;
        size_t generation_ = 0;
        launcher::TelemetryCaptureRule rule_;
        std::optional<launcher::TelemetryCaptureRule> original_;
    };

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
                                       CW_USEDEFAULT, CW_USEDEFAULT, Scale(900), Scale(620), nullptr, nullptr, instance, this);
            if (!window_) return false;
            CreateControls();
            RefreshDiscovery();
            ShowWindow(window_, SW_SHOWNORMAL);
            UpdateWindow(window_);
            return true;
        }

        HWND window() const { return window_; }
        HWND options_window() const { return options_window_; }

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
                info->ptMinTrackSize.y = Scale(500);
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
            case WM_CLOSE:
                if (!ApplyPendingOptions()) return 0;
                if (options_window_) SendMessageW(options_window_, WM_CLOSE, 0, 0);
                DestroyWindow(window_);
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

            mod_label_ = AddLabel(L"&Mods (profile order):");
            mods_ = AddControl(LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL | WS_TABSTOP | WS_BORDER,
                               WC_LISTVIEWW, L"", mod_list);
            ListView_SetExtendedListViewStyle(mods_, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            AddModColumn(L"Mod", 220);
            AddModColumn(L"Descriptor", 420);
            mod_move_up_ = AddControl(BS_PUSHBUTTON | WS_TABSTOP, L"BUTTON", L"Move &Up", mod_move_up_button);
            mod_move_down_ = AddControl(BS_PUSHBUTTON | WS_TABSTOP, L"BUTTON", L"Move &Down", mod_move_down_button);
            UpdateModOrderButtons();

            plugin_label_ = AddLabel(L"&Native plugins (trusted DLLs):");
            plugins_ = AddControl(LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL | WS_TABSTOP | WS_BORDER,
                                  WC_LISTVIEWW, L"", plugin_list);
            ListView_SetExtendedListViewStyle(plugins_, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            AddPluginColumn(L"Plugin", 230);
            AddPluginColumn(L"Version", 85);
            AddPluginColumn(L"Manifest", 300);

            safe_mode_ = AddControl(BS_AUTOCHECKBOX | WS_TABSTOP, L"BUTTON", L"&Safe mode (do not inject Smedley)", safe_mode_check);

            diagnostics_label_ = AddLabel(L"&Diagnostics:");
            diagnostics_ = AddControl(WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL | WS_TABSTOP,
                                       L"EDIT", L"", diagnostics_edit);
            status_ = AddControl(SS_LEFT, L"STATIC", L"", status_text);
            AddControl(BS_PUSHBUTTON | WS_TABSTOP, L"BUTTON", L"&Recent runs", recent_runs_button);
            AddControl(BS_PUSHBUTTON | WS_TABSTOP, L"BUTTON", L"&Options...", options_button);
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

        void AddModColumn(const wchar_t *heading, int width)
        {
            LVCOLUMNW column{};
            column.mask = LVCF_TEXT | LVCF_WIDTH;
            column.pszText = const_cast<wchar_t *>(heading);
            column.cx = Scale(width);
            ListView_InsertColumn(mods_, mod_column_count_++, &column);
        }

        void PlaceLabel(HWND label, int y, int label_width, int row, int margin)
        {
            MoveWindow(label, margin, y + Scale(4), label_width - Scale(4), row - Scale(4), TRUE);
        }

        class OptionsWindow
        {
        public:
            static void Show(LauncherWindow *owner)
            {
                if (owner->options_) {
                    SetForegroundWindow(owner->options_->window_);
                    return;
                }
                auto *options = new OptionsWindow(owner);
                WNDCLASSW window_class{};
                window_class.hInstance = GetModuleHandleW(nullptr);
                window_class.lpszClassName = L"SmedleyLauncherOptionsWindow";
                window_class.lpfnWndProc = WindowProc;
                window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
                window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
                RegisterClassW(&window_class);
                options->window_ = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_DLGMODALFRAME, window_class.lpszClassName,
                                                    L"Smedley Launcher Options", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                                                    WS_THICKFRAME | WS_VSCROLL | WS_CLIPCHILDREN | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, owner->Scale(800),
                                                   owner->Scale(760), owner->window_, nullptr, window_class.hInstance, options);
                if (!options->window_) delete options;
            }

            bool UpdateContext()
            {
                if (!current_) return false;
                const bool supported = launcher::SupportsPluginSettings(*current_);
                const bool editable = supported && owner_->IsPluginSelected(*current_) && owner_->BuildProfile().inject;
                if (!editable) owner_->settings_diagnostics_.clear();
                SetText(state_, !supported ? L"This third-party settings schema is read-only because no profile adapter is available."
                                         : editable ? L"Settings are editable because this plugin is selected in the main window."
                                                    : L"Select this plugin in the main window and turn off Safe mode to edit its settings.");
                const bool has_advanced = std::any_of(fields_.begin(), fields_.end(), [](const auto &field) {
                    return field.schema->advanced;
                });
                const bool advanced_changed = (IsWindowVisible(advanced_) != FALSE) != has_advanced;
                Show(advanced_, has_advanced);
                bool visibility_changed = advanced_changed;
                for (auto &field : fields_) {
                    const bool visible = !field.schema->advanced || (has_advanced && ShowAdvanced());
                    const bool condition = !field.schema->visible_when || ConditionMatches(*field.schema->visible_when);
                    const bool show = visible && condition;
                    visibility_changed = visibility_changed || (IsWindowVisible(field.input) != FALSE) != show;
                    Show(field.label, show);
                    Show(field.input, show);
                    Show(field.help, show);
                    Show(field.action, show);
                    Show(field.edit, show);
                    Show(field.remove, show);
                    EnableWindow(field.input, editable && show);
                    EnableWindow(field.action, editable && show);
                    EnableWindow(field.edit, editable && show);
                    EnableWindow(field.remove, editable && show);
                    const bool required = !field.schema->optional
                        || (field.schema->required_when && ConditionMatches(*field.schema->required_when));
                    SetText(field.label, Utf8ToWide(field.schema->label + (required ? " *" : "")));
                }
                return visibility_changed;
            }

            void Refresh()
            {
                RebuildPages();
            }

            bool ApplyPendingPage()
            {
                if (ApplyPage()) return true;
                ShowWindow(window_, SW_RESTORE);
                SetForegroundWindow(window_);
                return false;
            }

        private:
            struct FieldControl
            {
                const launcher::PluginSettingField *schema = nullptr;
                int id = 0;
                HWND label = nullptr;
                HWND input = nullptr;
                HWND help = nullptr;
                HWND action = nullptr;
                HWND edit = nullptr;
                HWND remove = nullptr;
            };

            explicit OptionsWindow(LauncherWindow *owner) : owner_(owner) {}

            static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
            {
                auto *self = reinterpret_cast<OptionsWindow *>(GetWindowLongPtrW(window, GWLP_USERDATA));
                if (message == WM_NCCREATE) {
                    self = static_cast<OptionsWindow *>(reinterpret_cast<const CREATESTRUCTW *>(lparam)->lpCreateParams);
                    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
                    self->window_ = window;
                }
                if (!self) return DefWindowProcW(window, message, wparam, lparam);
                if (message == WM_NCDESTROY) {
                    SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                    self->owner_->options_ = nullptr;
                    self->owner_->options_window_ = nullptr;
                    delete self;
                    return 0;
                }
                return self->HandleMessage(message, wparam, lparam);
            }

            LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam)
            {
                switch (message) {
                case WM_CREATE:
                    owner_->options_ = this;
                    owner_->options_window_ = window_;
                    CreateControls();
                    RebuildPages();
                    return 0;
                case WM_SIZE:
                    Layout(LOWORD(lparam), HIWORD(lparam));
                    return 0;
                case WM_COMMAND:
                    return OnCommand(LOWORD(wparam), HIWORD(wparam), reinterpret_cast<HWND>(lparam));
                case WM_NOTIFY:
                    return OnNotify(reinterpret_cast<const NMHDR *>(lparam));
                case WM_VSCROLL:
                    ScrollVertically(LOWORD(wparam));
                    return 0;
                case WM_MOUSEWHEEL:
                    ScrollWheel(GET_WHEEL_DELTA_WPARAM(wparam));
                    return 0;
                case WM_APP + 1:
                    if (lparam) CommitCaptureEdit(*reinterpret_cast<const CaptureEditResult *>(lparam));
                    RefreshObjectLists();
                    owner_->RefreshPlan();
                    return 0;
                case WM_CLOSE:
                    if (!ApplyPage()) return 0;
                    owner_->RefreshPlan();
                    DestroyWindow(window_);
                    return 0;
                }
                return DefWindowProcW(window_, message, wparam, lparam);
            }

            HWND Add(DWORD style, const wchar_t *class_name, const wchar_t *text, int id = 0)
            {
                if (wcscmp(class_name, L"BUTTON") == 0) style |= BS_NOTIFY;
                const HWND control = CreateWindowExW(0, class_name, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
                                                      window_, reinterpret_cast<HMENU>(id), nullptr, nullptr);
                SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
                return control;
            }

            void CreateControls()
            {
                pages_ = Add(LBS_NOTIFY | WS_BORDER | WS_TABSTOP | WS_VSCROLL, L"LISTBOX", L"", options_page_list);
                title_ = Add(SS_LEFT, L"STATIC", L"", 0);
                state_ = Add(SS_LEFT, L"STATIC", L"", 0);
                advanced_ = Add(BS_AUTOCHECKBOX | WS_TABSTOP, L"BUTTON", L"Show advanced settings", options_show_advanced_check);
                notice_ = Add(SS_LEFT, L"STATIC", L"", 0);
                empty_ = Add(SS_LEFT, L"STATIC", L"", 0);
                close_ = Add(BS_DEFPUSHBUTTON | WS_TABSTOP, L"BUTTON", L"Close", options_close_button);
            }

            void RebuildPages()
            {
                ++capture_generation_;
                current_ = nullptr;
                DestroyFields();
                SendMessageW(pages_, LB_RESETCONTENT, 0, 0);
                for (const auto &plugin : owner_->discovered_plugins_) {
                    const auto name = Utf8ToWide(plugin.name);
                    SendMessageW(pages_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
                }
                if (!owner_->discovered_plugins_.empty()) {
                    SendMessageW(pages_, LB_SETCURSEL, 0, 0);
                    SelectPage(0);
                }
                Layout(Scale(800), Scale(760), true);
            }

            void DestroyFields()
            {
                for (const auto &field : fields_) {
                    for (const auto control : {field.label, field.input, field.help, field.action, field.edit, field.remove}) {
                        if (control) DestroyWindow(control);
                    }
                }
                fields_.clear();
            }

            void SelectPage(int index)
            {
                if (!ApplyPage()) return;
                DestroyFields();
                current_ = index >= 0 && static_cast<size_t>(index) < owner_->discovered_plugins_.size()
                    ? &owner_->discovered_plugins_[index] : nullptr;
                if (!current_) return;
                SetText(title_, Utf8ToWide(current_->name));
                SetText(empty_, current_->settings.fields.empty()
                    ? L"Selecting this plugin enables it. This plugin has no configurable settings." : L"");
                SetText(notice_, ObjectListNotice());
                const auto settings = launcher::ResolvePluginSettings(*current_, owner_->draft_profile_);
                creating_fields_ = true;
                for (size_t index = 0; index < current_->settings.fields.size(); ++index) {
                    const auto &schema = current_->settings.fields[index];
                    FieldControl field;
                    field.schema = &schema;
                    field.id = options_field_base + static_cast<int>(index) * 4;
                    field.label = Add(SS_LEFT, L"STATIC", L"", 0);
                    const bool choice = schema.type == launcher::PluginSettingType::Enum;
                    const bool multiple = schema.type == launcher::PluginSettingType::MultiEnum;
                    const bool file_list = schema.type == launcher::PluginSettingType::FileList;
                    const bool object_list = schema.type == launcher::PluginSettingType::ObjectList;
                    const bool list = file_list || object_list;
                    const bool check = schema.type == launcher::PluginSettingType::Bool;
                    field.input = Add(check ? BS_AUTOCHECKBOX | WS_TABSTOP : choice ? CBS_DROPDOWNLIST | WS_TABSTOP
                                           : multiple || file_list ? LVS_REPORT | LVS_SHOWSELALWAYS | WS_BORDER | WS_TABSTOP
                                           : object_list ? LBS_NOTIFY | WS_BORDER | WS_TABSTOP | WS_VSCROLL : WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                       check ? L"BUTTON" : choice ? L"COMBOBOX" : multiple || file_list ? WC_LISTVIEWW : object_list ? L"LISTBOX" : L"EDIT", L"", field.id);
                    if (multiple || file_list) {
                        ListView_SetExtendedListViewStyle(field.input, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
                        LVCOLUMNW column{}; column.mask = LVCF_TEXT | LVCF_WIDTH; column.pszText = const_cast<wchar_t *>(multiple ? L"Choices" : L"Files"); column.cx = Scale(560);
                        ListView_InsertColumn(field.input, 0, &column);
                    }
                    if (choice || multiple) for (const auto &value : schema.choices) {
                        const auto text = Utf8ToWide(value);
                        if (choice) SendMessageW(field.input, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
                        else AddCheckListItem(field.input, text, false);
                    }
                    if (schema.type == launcher::PluginSettingType::File || schema.type == launcher::PluginSettingType::Directory || list) {
                        field.action = Add(BS_PUSHBUTTON | WS_TABSTOP, L"BUTTON", list ? L"Add..." : L"Browse...", field.id + 1);
                    }
                    if (schema.type == launcher::PluginSettingType::ObjectList) field.edit = Add(BS_PUSHBUTTON | WS_TABSTOP, L"BUTTON", L"Edit...", field.id + 2);
                    if (list) field.remove = Add(BS_PUSHBUTTON | WS_TABSTOP, L"BUTTON", L"Remove", field.id + 3);
                    field.help = Add(SS_LEFT, L"STATIC", Utf8ToWide(schema.help).c_str(), 0);
                    const auto setting = std::find_if(settings.begin(), settings.end(), [&](const auto &value) { return value.key == schema.key; });
                    SetFieldValue(field, setting == settings.end() ? schema.default_value : setting->value);
                    fields_.push_back(std::move(field));
                }
                creating_fields_ = false;
                UpdateContext();
                Layout(Scale(800), Scale(760), true);
            }

            void SetFieldValue(const FieldControl &field, const std::optional<launcher::PluginSettingValue> &value)
            {
                if (!value) return;
                const auto type = field.schema->type;
                if (type == launcher::PluginSettingType::Bool) SendMessageW(field.input, BM_SETCHECK, std::get<bool>(*value) ? BST_CHECKED : BST_UNCHECKED, 0);
                else if (type == launcher::PluginSettingType::Integer) SetText(field.input, std::to_wstring(std::get<std::int64_t>(*value)));
                else if (type == launcher::PluginSettingType::Date) {
                    const auto raw = std::get<std::int64_t>(*value);
                    if (raw >= (std::numeric_limits<int>::min)() && raw <= (std::numeric_limits<int>::max)()) {
                        if (const auto date = launcher::DecodeClausewitzDate(static_cast<int>(raw))) {
                            SetText(field.input, Utf8ToWide(launcher::FormatClausewitzDate(*date)));
                            return;
                        }
                    }
                    SetText(field.input, L"raw: " + std::to_wstring(raw));
                }
                else if (type == launcher::PluginSettingType::Number) SetText(field.input, FormatDouble(std::get<double>(*value)));
                else if (type == launcher::PluginSettingType::String || type == launcher::PluginSettingType::File || type == launcher::PluginSettingType::Directory) {
                    SetText(field.input, type == launcher::PluginSettingType::String ? Utf8ToWide(std::get<std::string>(*value)) : std::get<fs::path>(*value).wstring());
                } else if (type == launcher::PluginSettingType::Enum) {
                    const auto text = Utf8ToWide(std::get<std::string>(*value));
                    SendMessageW(field.input, CB_SELECTSTRING, -1, reinterpret_cast<LPARAM>(text.c_str()));
                } else if (type == launcher::PluginSettingType::MultiEnum) {
                    for (const auto &choice : std::get<std::vector<std::string>>(*value)) {
                        const auto text = Utf8ToWide(choice);
                        int index = FindCheckListItem(field.input, text);
                        if (index < 0) index = AddCheckListItem(field.input, text, false);
                        ListView_SetCheckState(field.input, index, TRUE);
                    }
                } else if (type == launcher::PluginSettingType::FileList) {
                    const auto selected = std::get<std::vector<fs::path>>(*value);
                    std::vector<fs::path> available;
                    if (field.schema->discovery_root) available = launcher::DiscoverFiles(owner_->BuildProfile().game_dir, *field.schema->discovery_root, field.schema->extensions).files;
                    for (const auto &path : selected) if (std::find(available.begin(), available.end(), path) == available.end()) {
                        const auto label = L"Unavailable selected: " + path.wstring();
                        AddCheckListItem(field.input, label, true);
                    }
                    for (const auto &path : available) AddCheckListItem(field.input, path.wstring(), false);
                    const auto count = ListView_GetItemCount(field.input);
                    for (int row = 0; row < count; ++row) {
                        const auto text = CheckListText(field.input, row);
                        const auto path = text.rfind(L"Unavailable selected: ", 0) == 0 ? fs::path(text.substr(22)) : fs::path(text);
                        if (std::find(selected.begin(), selected.end(), path) != selected.end()) ListView_SetCheckState(field.input, row, TRUE);
                    }
                } else if (type == launcher::PluginSettingType::ObjectList) {
                    for (const auto &rule : std::get<std::vector<launcher::TelemetryCaptureRule>>(*value)) {
                        std::wstring text = Utf8ToWide(rule.family + " | " + rule.cadence + " | fields=" + (rule.fields.empty() ? "all" : rule.fields.front()));
                        for (size_t index = 1; index < rule.fields.size(); ++index) text += L"," + Utf8ToWide(rule.fields[index]);
                        text += L" | countries=" + (rule.country_tags.empty() ? L"all" : Utf8ToWide(rule.country_tags.front()));
                        for (size_t index = 1; index < rule.country_tags.size(); ++index) text += L"," + Utf8ToWide(rule.country_tags[index]);
                        text += L" | provinces=" + (rule.province_ids.empty() ? L"all" : std::to_wstring(rule.province_ids.front()));
                        for (size_t index = 1; index < rule.province_ids.size(); ++index) text += L"," + std::to_wstring(rule.province_ids[index]);
                        const auto format_date = [](const std::optional<int> &raw) {
                            if (!raw) return std::wstring(L"any");
                            const auto date = launcher::DecodeClausewitzDate(*raw);
                            return date ? Utf8ToWide(launcher::FormatClausewitzDate(*date)) : L"raw: " + std::to_wstring(*raw);
                        };
                        text += L" | dates=" + format_date(rule.start_date_raw) + L".." + format_date(rule.end_date_raw);
                        SendMessageW(field.input, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
                    }
                }
            }

            std::optional<launcher::PluginSettingValue> ReadValue(const FieldControl &field) const
            {
                const auto type = field.schema->type;
                if (type == launcher::PluginSettingType::Bool) return SendMessageW(field.input, BM_GETCHECK, 0, 0) == BST_CHECKED;
                if (type == launcher::PluginSettingType::MultiEnum) return SelectedStrings(field.input);
                if (type == launcher::PluginSettingType::FileList) return SelectedPaths(field.input);
                if (type == launcher::PluginSettingType::ObjectList && field.schema->item_schema == "telemetry_capture_v1") return owner_->draft_profile_.telemetry_captures;
                if (type == launcher::PluginSettingType::Enum) {
                    const auto index = SendMessageW(field.input, CB_GETCURSEL, 0, 0);
                    if (index == CB_ERR) return std::nullopt;
                    return WideToUtf8(ComboText(field.input, static_cast<int>(index), true));
                }
                const auto text = GetText(field.input);
                if (text.empty()) return std::nullopt;
                try {
                    if (type == launcher::PluginSettingType::Integer) {
                        size_t used = 0;
                        const auto value = std::stoll(text, &used);
                        if (used != text.size()) return std::nullopt;
                        return static_cast<std::int64_t>(value);
                    }
                    if (type == launcher::PluginSettingType::Date) {
                        if (text.rfind(L"raw: ", 0) == 0) {
                            size_t used = 0;
                            const auto raw = std::stoll(text.substr(5), &used);
                            if (used != text.size() - 5 || raw < (std::numeric_limits<int>::min)()
                                || raw > (std::numeric_limits<int>::max)()) return std::nullopt;
                            return static_cast<std::int64_t>(raw);
                        }
                        const auto date = launcher::ParseClausewitzDate(WideToUtf8(text));
                        if (!date) return std::nullopt;
                        const auto encoded = launcher::EncodeClausewitzDate(*date);
                        return encoded ? std::optional<launcher::PluginSettingValue>(static_cast<std::int64_t>(*encoded)) : std::nullopt;
                    }
                    if (type == launcher::PluginSettingType::Number) {
                        size_t used = 0;
                        const auto value = std::stod(text, &used);
                        if (used != text.size()) return std::nullopt;
                        return value;
                    }
                } catch (const std::exception &) { return std::nullopt; }
                if (type == launcher::PluginSettingType::File || type == launcher::PluginSettingType::Directory) return fs::path(text);
                return WideToUtf8(text);
            }

            bool ConditionMatches(const launcher::PluginSettingCondition &condition) const
            {
                const auto found = std::find_if(fields_.begin(), fields_.end(), [&](const auto &field) { return field.schema->key == condition.key; });
                if (found == fields_.end()) return false;
                const auto value = ReadValue(*found);
                if (condition.kind == launcher::PluginSettingConditionKind::Equals) return value && condition.value && *value == *condition.value;
                return launcher::IsPluginSettingValuePresent(value);
            }

            bool ApplyPage()
            {
                if (!current_ || fields_.empty()) return true;
                if (!launcher::SupportsPluginSettings(*current_) || !owner_->IsPluginSelected(*current_)
                    || !owner_->BuildProfile().inject) {
                    owner_->settings_diagnostics_.clear();
                    return true;
                }
                launcher::PluginSettings settings;
                for (const auto &field : fields_) {
                    const auto value = ReadValue(field);
                    const auto type = field.schema->type;
                    if (!value && !GetText(field.input).empty()
                        && (type == launcher::PluginSettingType::Integer || type == launcher::PluginSettingType::Number
                            || type == launcher::PluginSettingType::Date)) {
                        owner_->settings_diagnostics_ = {{launcher::Severity::Error, "settings.invalid_value",
                                                          field.schema->label + " has an invalid value", {}}};
                        owner_->RefreshPlan();
                        EnsureVisible(field.input);
                        SetFocus(field.input);
                        return false;
                    }
                    settings.push_back({field.schema->key, value});
                }
                std::vector<launcher::Diagnostic> diagnostics;
                if (!launcher::ApplyPluginSettings(*current_, settings, &owner_->draft_profile_, &diagnostics)) {
                    owner_->settings_diagnostics_ = std::move(diagnostics);
                    owner_->RefreshPlan();
                    return false;
                } else {
                    owner_->settings_diagnostics_.clear();
                    const auto enabled = std::find_if(settings.begin(), settings.end(), [](const auto &setting) {
                        return setting.key == "enabled" && setting.value && std::holds_alternative<bool>(*setting.value)
                            && std::get<bool>(*setting.value);
                    });
                    if (enabled != settings.end()) owner_->SelectPlugin(Utf8ToWide(current_->id).c_str());
                }
                return true;
            }

            LRESULT OnCommand(int id, int notification, HWND control)
            {
                if (creating_fields_) return 0;
                if (id == options_close_button && notification == BN_CLICKED) {
                    SendMessageW(window_, WM_CLOSE, 0, 0);
                    return 0;
                }
                if (id == options_page_list && notification == LBN_SELCHANGE) {
                    const int current_index = current_ ? static_cast<int>(current_ - owner_->discovered_plugins_.data()) : LB_ERR;
                    SelectPage(static_cast<int>(SendMessageW(pages_, LB_GETCURSEL, 0, 0)));
                    if (current_ && current_index == static_cast<int>(current_ - owner_->discovered_plugins_.data())) {
                        SendMessageW(pages_, LB_SETCURSEL, current_index, 0);
                    }
                    return 0;
                }
                if (id == options_show_advanced_check && notification == BN_CLICKED) {
                    UpdateContext();
                    Layout(0, 0, true);
                    return 0;
                }
                for (auto &field : fields_) {
                    if ((id == field.id + 1 || id == field.id + 2 || id == field.id + 3) && notification == BN_CLICKED) {
                        if (!ApplyPage()) return 0;
                        if (id == field.id + 1) Browse(field);
                        else if (id == field.id + 2) EditObject(field);
                        else RemoveFile(field);
                        UpdateContext();
                        Layout(0, 0);
                        owner_->RefreshPlan();
                        return 0;
                    }
                }
                ApplyPage();
                const bool visibility_changed = UpdateContext();
                Layout(0, 0, visibility_changed);
                if (notification == BN_SETFOCUS || notification == CBN_SETFOCUS || notification == EN_SETFOCUS || notification == LBN_SETFOCUS) {
                    EnsureVisible(control);
                }
                owner_->RefreshPlan();
                return 0;
            }

            LRESULT OnNotify(const NMHDR *notification)
            {
                if (!notification || creating_fields_) return 0;
                if (notification->code == NM_SETFOCUS) EnsureVisible(notification->hwndFrom);
                const auto field = std::find_if(fields_.begin(), fields_.end(), [&](const auto &candidate) {
                    return candidate.input == notification->hwndFrom;
                });
                if (field == fields_.end() || notification->code != LVN_ITEMCHANGED) return 0;
                const auto *change = reinterpret_cast<const NMLISTVIEW *>(notification);
                if (!(change->uChanged & LVIF_STATE)
                    || !((change->uOldState ^ change->uNewState) & LVIS_STATEIMAGEMASK)) return 0;
                ApplyPage();
                const bool visibility_changed = UpdateContext();
                Layout(0, 0, visibility_changed);
                owner_->RefreshPlan();
                return 0;
            }

            void Browse(const FieldControl &field)
            {
                if (field.schema->type == launcher::PluginSettingType::ObjectList && field.schema->item_schema == "telemetry_capture_v1") {
                    CaptureEditorWindow::Show(window_, std::nullopt, capture_generation_);
                    return;
                }
                std::wstring path = field.schema->type == launcher::PluginSettingType::FileList ? L"" : GetText(field.input);
                const bool directory = field.schema->type == launcher::PluginSettingType::Directory;
                if (!(directory ? BrowseForFolder(window_, &path) : BrowseForFile(window_, L"Select file", L"All files\0*.*\0", &path, false))) return;
                if (field.schema->type == launcher::PluginSettingType::FileList) {
                    const auto row = AddCheckListItem(field.input, path, false);
                    ListView_SetCheckState(field.input, row, TRUE);
                }
                else SetText(field.input, path);
            }

            void EditObject(const FieldControl &field)
            {
                if (field.schema->item_schema != "telemetry_capture_v1") return;
                const auto row = SendMessageW(field.input, LB_GETCURSEL, 0, 0);
                if (row == LB_ERR || static_cast<size_t>(row) >= owner_->draft_profile_.telemetry_captures.size()) return;
                CaptureEditorWindow::Show(window_, static_cast<size_t>(row), capture_generation_,
                                          owner_->draft_profile_.telemetry_captures[static_cast<size_t>(row)]);
            }

            void RemoveFile(const FieldControl &field)
            {
                const auto selected = field.schema->type == launcher::PluginSettingType::FileList
                    ? ListView_GetNextItem(field.input, -1, LVNI_SELECTED) : SendMessageW(field.input, LB_GETCURSEL, 0, 0);
                if (selected == LB_ERR) return;
                if (field.schema->type == launcher::PluginSettingType::ObjectList && field.schema->item_schema == "telemetry_capture_v1") {
                    if (selected < 0 || static_cast<size_t>(selected) >= owner_->draft_profile_.telemetry_captures.size()) return;
                    owner_->draft_profile_.telemetry_captures.erase(owner_->draft_profile_.telemetry_captures.begin() + selected);
                    ++capture_generation_;
                    RefreshObjectLists();
                } else if (field.schema->type == launcher::PluginSettingType::FileList) ListView_DeleteItem(field.input, selected);
                else SendMessageW(field.input, LB_DELETESTRING, selected, 0);
            }

            void CommitCaptureEdit(const CaptureEditResult &result)
            {
                auto &captures = owner_->draft_profile_.telemetry_captures;
                if (!result.index) {
                    if (result.generation == capture_generation_) {
                        captures.push_back(result.replacement);
                        ++capture_generation_;
                    }
                    return;
                }
                if (result.generation != capture_generation_ || !result.original
                    || *result.index >= captures.size() || !(captures[*result.index] == *result.original)) return;
                captures[*result.index] = result.replacement;
                ++capture_generation_;
            }

            std::vector<std::string> SelectedStrings(HWND control) const
            {
                std::vector<std::string> values;
                for (int row = 0; row < ListView_GetItemCount(control); ++row) if (ListView_GetCheckState(control, row)) values.push_back(WideToUtf8(CheckListText(control, row)));
                return values;
            }

            std::vector<fs::path> SelectedPaths(HWND control) const
            {
                std::vector<fs::path> paths;
                for (int row = 0; row < ListView_GetItemCount(control); ++row) if (ListView_GetCheckState(control, row)) {
                    const auto text = CheckListText(control, row); paths.emplace_back(text.rfind(L"Unavailable selected: ", 0) == 0 ? text.substr(22) : text);
                }
                return paths;
            }

            static int AddCheckListItem(HWND control, const std::wstring &text, bool checked)
            {
                LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = ListView_GetItemCount(control); item.pszText = const_cast<wchar_t *>(text.c_str());
                const int row = ListView_InsertItem(control, &item); ListView_SetCheckState(control, row, checked ? TRUE : FALSE); return row;
            }

            static std::wstring CheckListText(HWND control, int row)
            {
                std::vector<wchar_t> buffer(32768, L'\0');
                LVITEMW item{}; item.iSubItem = 0; item.pszText = buffer.data(); item.cchTextMax = static_cast<int>(buffer.size());
                SendMessageW(control, LVM_GETITEMTEXTW, row, reinterpret_cast<LPARAM>(&item));
                return buffer.data();
            }

            static int FindCheckListItem(HWND control, const std::wstring &text)
            {
                for (int row = 0; row < ListView_GetItemCount(control); ++row) if (CheckListText(control, row) == text) return row;
                return -1;
            }

            void RefreshObjectLists()
            {
                for (const auto &field : fields_) if (field.schema->type == launcher::PluginSettingType::ObjectList) {
                    SendMessageW(field.input, LB_RESETCONTENT, 0, 0);
                    SetFieldValue(field, launcher::PluginSettingValue(owner_->draft_profile_.telemetry_captures));
                }
            }

            std::wstring ComboText(HWND control, int index, bool combo = false) const
            {
                const auto length = SendMessageW(control, combo ? CB_GETLBTEXTLEN : LB_GETTEXTLEN, index, 0);
                std::wstring value(static_cast<size_t>(length) + 1, L'\0');
                SendMessageW(control, combo ? CB_GETLBTEXT : LB_GETTEXT, index, reinterpret_cast<LPARAM>(value.data()));
                value.resize(static_cast<size_t>(length));
                return value;
            }

            bool ShowAdvanced() const { return SendMessageW(advanced_, BM_GETCHECK, 0, 0) == BST_CHECKED; }
            void Show(HWND control, bool show) const { if (control) ShowWindow(control, show ? SW_SHOW : SW_HIDE); }
            int Scale(int value) const { return owner_->Scale(value); }

            std::wstring ObjectListNotice() const
            {
                if (!std::any_of(current_->settings.notices.begin(), current_->settings.notices.end(), [](const auto &notice) { return notice.capability == "object_list"; })) return {};
                std::wstring text = L"Loaded capture rules: " + std::to_wstring(owner_->draft_profile_.telemetry_captures.size()) + L". Editing is not yet available. ";
                for (const auto &rule : owner_->draft_profile_.telemetry_captures) {
                    text += Utf8ToWide(rule.family + " / " + rule.cadence + " / fields=" + (rule.fields.empty() ? "all" : rule.fields.front()));
                    for (size_t index = 1; index < rule.fields.size(); ++index) text += L"," + Utf8ToWide(rule.fields[index]);
                    text += L" / countries=" + (rule.country_tags.empty() ? L"all" : Utf8ToWide(rule.country_tags.front()));
                    for (size_t index = 1; index < rule.country_tags.size(); ++index) text += L"," + Utf8ToWide(rule.country_tags[index]);
                    text += L" / provinces=" + (rule.province_ids.empty() ? L"all" : std::to_wstring(rule.province_ids.front()));
                    for (size_t index = 1; index < rule.province_ids.size(); ++index) text += L"," + std::to_wstring(rule.province_ids[index]);
                    text += L" / dates=" + (rule.start_date_raw ? std::to_wstring(*rule.start_date_raw) : L"any") + L".." + (rule.end_date_raw ? std::to_wstring(*rule.end_date_raw) : L"any") + L"; ";
                }
                return text;
            }

            void UpdateScrollBar(int height, bool reset)
            {
                const int maximum = std::max(0, content_height_ - height);
                scroll_y_ = reset ? 0 : std::clamp(scroll_y_, 0, maximum);
                SCROLLINFO info{};
                info.cbSize = sizeof(info);
                info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
                info.nMin = 0;
                info.nMax = std::max(0, content_height_ - 1);
                info.nPage = static_cast<UINT>(std::max(1, height));
                info.nPos = scroll_y_;
                SetScrollInfo(window_, SB_VERT, &info, TRUE);
            }

            void ScrollTo(int position)
            {
                RECT rect{};
                GetClientRect(window_, &rect);
                const int maximum = std::max(0, content_height_ - static_cast<int>(rect.bottom));
                position = std::clamp(position, 0, maximum);
                if (position == scroll_y_) return;
                scroll_y_ = position;
                Layout(0, 0);
            }

            void ScrollVertically(int command)
            {
                RECT rect{};
                GetClientRect(window_, &rect);
                const int line = Scale(24);
                int position = scroll_y_;
                if (command == SB_TOP) position = 0;
                else if (command == SB_BOTTOM) position = content_height_;
                else if (command == SB_LINEUP) position -= line;
                else if (command == SB_LINEDOWN) position += line;
                else if (command == SB_PAGEUP) position -= rect.bottom;
                else if (command == SB_PAGEDOWN) position += rect.bottom;
                else if (command == SB_THUMBPOSITION || command == SB_THUMBTRACK) {
                    SCROLLINFO info{};
                    info.cbSize = sizeof(info);
                    info.fMask = SIF_TRACKPOS;
                    GetScrollInfo(window_, SB_VERT, &info);
                    position = info.nTrackPos;
                } else return;
                ScrollTo(position);
            }

            void ScrollWheel(short delta)
            {
                wheel_delta_ += delta;
                const int steps = wheel_delta_ / WHEEL_DELTA;
                wheel_delta_ -= steps * WHEEL_DELTA;
                if (steps == 0) return;
                UINT lines = 3;
                SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
                RECT rect{};
                GetClientRect(window_, &rect);
                const int distance = lines == WHEEL_PAGESCROLL ? rect.bottom : static_cast<int>(lines) * Scale(24);
                ScrollTo(scroll_y_ - steps * distance);
            }

            void EnsureVisible(HWND control)
            {
                if (!control || !IsWindowVisible(control)) return;
                RECT bounds{};
                GetWindowRect(control, &bounds);
                MapWindowPoints(HWND_DESKTOP, window_, reinterpret_cast<POINT *>(&bounds), 2);
                RECT client{};
                GetClientRect(window_, &client);
                if (bounds.top < 0) ScrollTo(scroll_y_ + bounds.top);
                else if (bounds.bottom > client.bottom) ScrollTo(scroll_y_ + bounds.bottom - client.bottom);
            }

            void Layout(int width, int height, bool reset_scroll = false)
            {
                if (!width || !height) { RECT rect{}; GetClientRect(window_, &rect); width = rect.right; height = rect.bottom; }
                const int margin = Scale(12), left = Scale(165), right = width - margin;
                MoveWindow(pages_, margin, margin, left - margin * 2, std::max(Scale(80), height - margin * 2), TRUE);

                int y = margin;
                const auto content_y = [&]() { return y - scroll_y_; };
                MoveWindow(title_, left, content_y(), right - left, Scale(22), TRUE);
                y += Scale(26);
                MoveWindow(state_, left, content_y(), right - left, Scale(34), TRUE);
                y += Scale(38);
                MoveWindow(notice_, left, content_y(), right - left, Scale(58), TRUE);
                y += Scale(62);
                MoveWindow(empty_, left, content_y(), right - left, Scale(34), TRUE);
                y += Scale(38);

                const auto place_fields = [&](bool advanced) {
                    for (const auto &field : fields_) {
                        if (field.schema->advanced != advanced || !IsWindowVisible(field.input)) continue;
                        MoveWindow(field.label, left, content_y(), right - left, Scale(18), TRUE);
                        if (field.schema->type == launcher::PluginSettingType::Bool) {
                            MoveWindow(field.input, left, content_y() + Scale(18), right - left, Scale(22), TRUE);
                        } else if (field.schema->type == launcher::PluginSettingType::MultiEnum || field.schema->type == launcher::PluginSettingType::FileList
                                   || field.schema->type == launcher::PluginSettingType::ObjectList) {
                            int button_x = right;
                            const auto place_button = [&](HWND button) {
                                if (!button) return;
                                button_x -= Scale(70);
                                MoveWindow(button, button_x, content_y() + Scale(18), Scale(70), Scale(22), TRUE);
                                button_x -= Scale(6);
                            };
                            place_button(field.remove);
                            place_button(field.edit);
                            place_button(field.action);
                            MoveWindow(field.input, left, content_y() + Scale(18), button_x - left + Scale(6), Scale(54), TRUE);
                        } else {
                            MoveWindow(field.input, left, content_y() + Scale(18), right - left - (field.action ? Scale(76) : 0), Scale(22), TRUE);
                            if (field.action) MoveWindow(field.action, right - Scale(70), content_y() + Scale(18), Scale(70), Scale(22), TRUE);
                        }
                        const int help_offset = field.schema->type == launcher::PluginSettingType::MultiEnum
                            || field.schema->type == launcher::PluginSettingType::FileList
                            || field.schema->type == launcher::PluginSettingType::ObjectList ? 74 : 42;
                        MoveWindow(field.help, left, content_y() + Scale(help_offset), right - left, Scale(28), TRUE);
                        y += Scale(help_offset + 32);
                    }
                };
                place_fields(false);
                if (IsWindowVisible(advanced_)) {
                    MoveWindow(advanced_, left, content_y(), Scale(180), Scale(22), TRUE);
                    y += Scale(26);
                }
                place_fields(true);
                MoveWindow(close_, right - Scale(80), content_y(), Scale(80), Scale(28), TRUE);
                content_height_ = y + Scale(40);
                const int previous_scroll_y = scroll_y_;
                UpdateScrollBar(height, reset_scroll);
                if (scroll_y_ != previous_scroll_y) Layout(width, height);
            }

            LauncherWindow *owner_;
            HWND window_ = nullptr, pages_ = nullptr, title_ = nullptr, state_ = nullptr, advanced_ = nullptr, notice_ = nullptr, empty_ = nullptr, close_ = nullptr;
            const launcher::PluginManifest *current_ = nullptr;
            std::vector<FieldControl> fields_;
            size_t capture_generation_ = 0;
            int content_height_ = 0;
            int scroll_y_ = 0;
            int wheel_delta_ = 0;
            bool creating_fields_ = false;
        };

        bool ApplyPendingOptions()
        {
            if (!options_ || applying_options_) return true;
            applying_options_ = true;
            const bool applied = options_->ApplyPendingPage();
            applying_options_ = false;
            if (!applied) SetForegroundWindow(options_window_);
            return applied;
        }

        void Layout(int width, int height)
        {
            if (!profile_path_) return;
            LayoutLaunchControls(width, height);
        }

        void LayoutLaunchControls(int width, int height)
        {
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
            PlaceLabel(name_label_, y, label_width, row, margin);
            MoveWindow(profile_name_, margin + label_width, y, right - margin - label_width, Scale(22), TRUE);
            y += row;
            PlaceLabel(game_label_, y, label_width, row, margin);
            MoveWindow(game_dir_, margin + label_width, y, right - margin - label_width - button_width - small_button_width - Scale(8), Scale(22), TRUE);
            MoveWindow(GetDlgItem(window_, browse_game_button), right - button_width - small_button_width - Scale(4), y, button_width, Scale(22), TRUE);
            MoveWindow(GetDlgItem(window_, refresh_button), right - small_button_width, y, small_button_width, Scale(22), TRUE);
            y += row;
            PlaceLabel(mod_label_, y, label_width, row, margin);
            MoveWindow(mod_move_up_, right - Scale(178), y, Scale(84), Scale(22), TRUE);
            MoveWindow(mod_move_down_, right - Scale(88), y, Scale(88), Scale(22), TRUE);
            y += row;
            const int mod_height = Scale(108);
            MoveWindow(mods_, margin, y, right - margin, mod_height, TRUE);
            ListView_SetColumnWidth(mods_, 0, Scale(220));
            ListView_SetColumnWidth(mods_, 1, std::max(Scale(100), right - margin - Scale(220)));
            y += mod_height + Scale(4);
            PlaceLabel(plugin_label_, y, label_width, row, margin);
            y += row;
            const int fixed_height = Scale(26 + 30 + 26 + 70 + 34);
            const int plugin_height = std::max(Scale(100), height - y - fixed_height - margin);
            MoveWindow(plugins_, margin, y, right - margin, plugin_height, TRUE);
            y += plugin_height + Scale(4);
            MoveWindow(safe_mode_, margin, y, right - margin - Scale(100), Scale(22), TRUE);
            MoveWindow(GetDlgItem(window_, options_button), right - Scale(92), y, Scale(92), Scale(22), TRUE);
            y += row;
            PlaceLabel(diagnostics_label_, y, label_width, row, margin);
            y += row;
            MoveWindow(diagnostics_, margin, y, right - margin, Scale(64), TRUE);
            y += Scale(70);
            MoveWindow(status_, margin, y + Scale(5), right - margin - Scale(270), Scale(20), TRUE);
            MoveWindow(GetDlgItem(window_, recent_runs_button), right - Scale(260), y, Scale(94), Scale(28), TRUE);
            MoveWindow(launch_, right - Scale(160), y, Scale(160), Scale(28), TRUE);
            ListView_SetColumnWidth(plugins_, 0, Scale(230));
            ListView_SetColumnWidth(plugins_, 1, Scale(85));
            ListView_SetColumnWidth(plugins_, 2, std::max(Scale(100), right - margin - Scale(315)));
        }

        bool HasSelectedPlugin(const wchar_t *id) const
        {
            for (size_t index = 0; index < discovered_plugins_.size(); ++index) {
                if (Utf8ToWide(discovered_plugins_[index].id) == id && ListView_GetCheckState(plugins_, static_cast<int>(index))) return true;
            }
            return false;
        }

        void SelectPlugin(const wchar_t *id)
        {
            for (size_t index = 0; index < discovered_plugins_.size(); ++index) {
                if (Utf8ToWide(discovered_plugins_[index].id) == id) {
                    suppress_notifications_ = true;
                    ListView_SetCheckState(plugins_, static_cast<int>(index), TRUE);
                    suppress_notifications_ = false;
                    return;
                }
            }
        }

        bool IsPluginSelected(const launcher::PluginManifest &plugin) const
        {
            return HasSelectedPlugin(Utf8ToWide(plugin.id).c_str());
        }

        void AddModItem(const std::wstring &name, const std::wstring &descriptor, const fs::path &path, bool checked)
        {
            LVITEMW item{};
            item.mask = LVIF_TEXT;
            item.iItem = ListView_GetItemCount(mods_);
            item.pszText = const_cast<wchar_t *>(name.c_str());
            ListView_InsertItem(mods_, &item);
            ListView_SetItemText(mods_, item.iItem, 1, const_cast<wchar_t *>(descriptor.c_str()));
            ListView_SetCheckState(mods_, item.iItem, checked ? TRUE : FALSE);
            mod_paths_.push_back(path);
        }

        std::optional<int> SelectedModIndex() const
        {
            const int index = ListView_GetNextItem(mods_, -1, LVNI_SELECTED);
            if (index < 0 || static_cast<size_t>(index) >= mod_paths_.size()) return std::nullopt;
            return index;
        }

        void UpdateModOrderButtons()
        {
            const auto selected = SelectedModIndex();
            const bool movable = selected && ListView_GetCheckState(mods_, *selected);
            EnableWindow(mod_move_up_, movable && *selected > 0);
            EnableWindow(mod_move_down_, movable && static_cast<size_t>(*selected + 1) < mod_paths_.size());
        }

        void MoveSelectedMod(int direction)
        {
            const auto selected = SelectedModIndex();
            if (!selected || !ListView_GetCheckState(mods_, *selected)) return;
            const int destination = *selected + direction;
            if (destination < 0 || static_cast<size_t>(destination) >= mod_paths_.size()) return;

            auto item_text = [&](int item, int subitem) {
                std::vector<wchar_t> text(32768, L'\0');
                ListView_GetItemText(mods_, item, subitem, text.data(), static_cast<int>(text.size()));
                return std::wstring(text.data());
            };
            const bool selected_checked = ListView_GetCheckState(mods_, *selected);
            const bool destination_checked = ListView_GetCheckState(mods_, destination);
            suppress_notifications_ = true;
            for (int subitem = 0; subitem < mod_column_count_; ++subitem) {
                const auto selected_text = item_text(*selected, subitem);
                const auto destination_text = item_text(destination, subitem);
                ListView_SetItemText(mods_, *selected, subitem, const_cast<wchar_t *>(destination_text.c_str()));
                ListView_SetItemText(mods_, destination, subitem, const_cast<wchar_t *>(selected_text.c_str()));
            }
            ListView_SetCheckState(mods_, *selected, destination_checked ? TRUE : FALSE);
            ListView_SetCheckState(mods_, destination, selected_checked ? TRUE : FALSE);
            std::swap(mod_paths_[*selected], mod_paths_[destination]);
            ListView_SetItemState(mods_, *selected, 0, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_SetItemState(mods_, destination, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            suppress_notifications_ = false;
            UpdateModOrderButtons();
            RefreshPlan();
        }

        void OnCommand(int id, int notification)
        {
            if (id == load_profile_button && notification == BN_CLICKED) LoadProfile();
            else if (id == save_profile_button && notification == BN_CLICKED) SaveProfile();
            else if (id == options_button && notification == BN_CLICKED) OptionsWindow::Show(this);
            else if (id == browse_game_button && notification == BN_CLICKED) {
                if (!ApplyPendingOptions()) return;
                std::wstring path = GetText(game_dir_);
                if (BrowseForFolder(window_, &path)) { SetText(game_dir_, path); RefreshDiscovery(); }
            } else if (id == refresh_button && notification == BN_CLICKED) RefreshDiscovery();
            else if (id == launch_button && notification == BN_CLICKED) LaunchGame();
            else if (id == recent_runs_button && notification == BN_CLICKED) RecentRunsWindow::Show(window_);
            else if (id == safe_mode_check && notification == BN_CLICKED) RefreshPlan();
            else if (id == mod_move_up_button && notification == BN_CLICKED) MoveSelectedMod(-1);
            else if (id == mod_move_down_button && notification == BN_CLICKED) MoveSelectedMod(1);
            else if ((id == game_dir_edit || id == profile_name_edit) && notification == EN_KILLFOCUS) {
                if (id == game_dir_edit) RefreshDiscovery(); else RefreshPlan();
            }
        }

        void OnNotify(const NMHDR *notification)
        {
            if (!suppress_notifications_ && notification->idFrom == mod_list && notification->code == LVN_ITEMCHANGED) {
                const auto *change = reinterpret_cast<const NMLISTVIEW *>(notification);
                if ((change->uChanged & LVIF_STATE) != 0) {
                    if (((change->uOldState ^ change->uNewState) & LVIS_STATEIMAGEMASK) != 0) RefreshPlan();
                    if (((change->uOldState ^ change->uNewState) & LVIS_SELECTED) != 0) UpdateModOrderButtons();
                }
            }
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
            auto profile = draft_profile_;
            profile.name = WideToUtf8(GetText(profile_name_));
            profile.game_dir = GetText(game_dir_);
            profile.kernel = retained_kernel_;
            profile.inject = SendMessageW(safe_mode_, BM_GETCHECK, 0, 0) != BST_CHECKED;
            profile.detach = retained_detach_;
            profile.mods = SelectedMods();
            profile.plugins = SelectedPlugins();
            profile.plugins.insert(profile.plugins.end(), retained_plugins_.begin(), retained_plugins_.end());
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

        std::vector<fs::path> SelectedMods() const
        {
            std::vector<fs::path> paths;
            for (size_t index = 0; index < mod_paths_.size(); ++index) {
                if (ListView_GetCheckState(mods_, static_cast<int>(index))) paths.push_back(mod_paths_[index]);
            }
            return paths;
        }

        void RefreshDiscovery(std::optional<std::vector<fs::path>> requested_mods = std::nullopt,
                              std::optional<std::vector<fs::path>> requested_plugins = std::nullopt,
                              bool apply_options = true)
        {
            if (apply_options && !ApplyPendingOptions()) return;
            if (!requested_mods) requested_mods = SelectedMods();
            if (!requested_plugins) {
                requested_plugins = SelectedPlugins();
                requested_plugins->insert(requested_plugins->end(), retained_plugins_.begin(), retained_plugins_.end());
            }
            const fs::path game_dir = GetText(game_dir_);
            discovery_diagnostics_.clear();
            discovered_mods_.clear();
            discovered_plugins_.clear();
            if (!game_dir.empty()) {
                const auto mods = launcher::DiscoverMods(game_dir);
                const auto plugins = launcher::DiscoverPlugins(game_dir, fs::path(SMEDLEY_SOURCE_DIR) / L"plugins");
                discovered_mods_ = mods.mods;
                discovered_plugins_ = plugins.plugins;
                discovery_diagnostics_.insert(discovery_diagnostics_.end(), mods.diagnostics.begin(), mods.diagnostics.end());
                discovery_diagnostics_.insert(discovery_diagnostics_.end(), plugins.diagnostics.begin(), plugins.diagnostics.end());
            }
            PopulateDiscovery(game_dir, *requested_mods, *requested_plugins);
            if (options_) options_->Refresh();
            RefreshPlan();
        }

        void PopulateDiscovery(const fs::path &game_dir, const std::vector<fs::path> &requested_mods,
                                const std::vector<fs::path> &requested_plugins)
        {
            suppress_notifications_ = true;
            ListView_DeleteAllItems(mods_);
            mod_paths_.clear();
            std::vector<bool> discovered_selected(discovered_mods_.size(), false);
            for (const auto &path : requested_mods) {
                const auto selected = std::find_if(discovered_mods_.begin(), discovered_mods_.end(), [&](const auto &mod) {
                    return ResolveSelectedPath(game_dir, path) == AbsolutePath(mod.descriptor_path);
                });
                if (selected == discovered_mods_.end()) {
                    AddModItem(L"Unavailable selected mod", path.wstring(), path, true);
                    continue;
                }
                const size_t index = static_cast<size_t>(selected - discovered_mods_.begin());
                discovered_selected[index] = true;
                AddModItem(Utf8ToWide(selected->name), selected->descriptor_path.wstring(), path, true);
            }
            for (size_t index = 0; index < discovered_mods_.size(); ++index) {
                if (!discovered_selected[index]) {
                    AddModItem(Utf8ToWide(discovered_mods_[index].name), discovered_mods_[index].descriptor_path.wstring(),
                               discovered_mods_[index].descriptor_path, false);
                }
            }
            UpdateModOrderButtons();

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
            suppress_notifications_ = false;
        }

        void RefreshPlan()
        {
            auto profile = BuildProfile();
            // The GUI always launches detached; retain the profile value only
            // when saving it again.
            profile.detach = true;
            plan_ = launcher::BuildLaunchPlan(profile);
            if (options_) options_->UpdateContext();
            DisplayDiagnostics(plan_.diagnostics);
            const bool has_errors = launcher::HasErrors(plan_.diagnostics) || launcher::HasErrors(settings_diagnostics_);
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
            append(settings_diagnostics_);
            append(operation_diagnostics_);
            if (text.empty()) text = L"No diagnostics.";
            SetText(diagnostics_, text);
        }

        void LoadProfile()
        {
            if (!ApplyPendingOptions()) return;
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
            draft_profile_ = profile;
            SetText(profile_path_, path);
            SetText(profile_name_, Utf8ToWide(profile.name));
            SetText(game_dir_, profile.game_dir.wstring());
            SendMessageW(safe_mode_, BM_SETCHECK, profile.inject ? BST_UNCHECKED : BST_CHECKED, 0);
            retained_kernel_ = profile.kernel;
            retained_detach_ = profile.detach;
            discovered_mods_.clear();
            discovered_plugins_.clear();
            retained_plugins_.clear();
            operation_diagnostics_ = std::move(diagnostics);
            RefreshDiscovery(profile.mods, profile.plugins, false);
        }

        void SaveProfile()
        {
            if (!ApplyPendingOptions()) return;
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
            if (!ApplyPendingOptions()) return;
            RefreshPlan();
            if (launcher::HasErrors(plan_.diagnostics) || launcher::HasErrors(settings_diagnostics_)) return;
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
        HWND mod_move_up_ = nullptr;
        HWND mod_move_down_ = nullptr;
        HWND plugin_label_ = nullptr;
        HWND plugins_ = nullptr;
        HWND safe_mode_ = nullptr;
        HWND diagnostics_label_ = nullptr;
        HWND diagnostics_ = nullptr;
        HWND status_ = nullptr;
        HWND launch_ = nullptr;
        UINT dpi_ = 96;
        int mod_column_count_ = 0;
        int plugin_column_count_ = 0;
        bool suppress_notifications_ = false;
        bool applying_options_ = false;
        bool retained_detach_ = true;
        OptionsWindow *options_ = nullptr;
        HWND options_window_ = nullptr;
        launcher::LaunchPlan plan_;
        launcher::Profile draft_profile_;
        std::optional<fs::path> retained_kernel_;
        std::vector<fs::path> mod_paths_;
        std::vector<fs::path> retained_plugins_;
        std::vector<launcher::ModDescriptor> discovered_mods_;
        std::vector<launcher::PluginManifest> discovered_plugins_;
        std::vector<launcher::Diagnostic> discovery_diagnostics_;
        std::vector<launcher::Diagnostic> settings_diagnostics_;
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
        const HWND options = launcher_window.options_window();
        if ((!options || !IsDialogMessageW(options, &message))
            && !IsDialogMessageW(launcher_window.window(), &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (SUCCEEDED(com)) CoUninitialize();
    return static_cast<int>(message.wParam);
}
