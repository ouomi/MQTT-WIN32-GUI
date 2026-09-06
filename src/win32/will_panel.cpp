#include "will_panel.h"

#include <algorithm>
#include <utility>

#include "control_helpers.h"
#include "ui_ids.h"

namespace win32mqtt {
namespace {
constexpr wchar_t kWindowClass[] = L"Win32MqttLastWillWindow";
}

void WillPanel::Create(HWND parent, AppLanguage language, std::function<void()> test,
                       std::function<void()> relayout) {
    language_ = language;
    test_callback_ = std::move(test);
    toggle_ = AddButton(parent, IDC_WILL_TOGGLE, L"", WS_TABSTOP);
    WNDCLASSW cls{};
    cls.lpfnWndProc = WindowProc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    cls.lpszClassName = kWindowClass;
    if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        EnableWindow(toggle_, FALSE);
        return;
    }
    RECT bounds{0, 0, 600, 440};
    constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_CLIPCHILDREN;
    AdjustWindowRectEx(&bounds, style, FALSE, WS_EX_CONTROLPARENT);
    RECT owner{};
    GetWindowRect(parent, &owner);
    window_ = CreateWindowExW(WS_EX_CONTROLPARENT, kWindowClass, L"", style,
                             owner.left + 40, owner.top + 40,
                             bounds.right - bounds.left, bounds.bottom - bounds.top,
                             parent, nullptr, cls.hInstance, this);
    if (!window_) {
        EnableWindow(toggle_, FALSE);
        return;
    }
    UseClassicWindowFrame(window_);
    enabled_ = AddButton(window_, IDC_WILL_ENABLED, L"", BS_AUTOCHECKBOX | WS_TABSTOP);
    hint_ = AddText(window_, 60013, L"");
    topic_label_ = AddText(window_, 60011, L"");
    topic_ = AddEdit(window_, IDC_WILL_TOPIC, L"", WS_TABSTOP);
    payload_label_ = AddText(window_, 60012, L"");
    payload_ = AddControl(L"EDIT", WS_TABSTOP | WS_VSCROLL | ES_MULTILINE |
                          ES_AUTOVSCROLL | ES_WANTRETURN, IDC_WILL_PAYLOAD, window_, WS_EX_CLIENTEDGE);
    test_ = AddButton(window_, IDC_WILL_TEST, L"", WS_TABSTOP);
    test_hint_ = AddText(window_, 60014, L"");
    result_ = AddText(window_, 60015, L"");
    UpdateText(language, MqttConnectionState::Disconnected);
    LayoutWindow();
    relayout_callback_ = std::move(relayout);
}

int WillPanel::ButtonWidth() const {
    constexpr int horizontal_padding = 24;
    const std::wstring text = ControlText(toggle_);
    HDC dc = GetDC(toggle_);
    if (!dc) return 150;
    const HFONT font = reinterpret_cast<HFONT>(SendMessageW(toggle_, WM_GETFONT, 0, 0));
    const HGDIOBJ previous = font ? SelectObject(dc, font) : nullptr;
    SIZE size{};
    const BOOL measured = GetTextExtentPoint32W(dc, text.c_str(),
                                               static_cast<int>(text.size()), &size);
    if (previous) SelectObject(dc, previous);
    ReleaseDC(toggle_, dc);
    return measured ? static_cast<int>(size.cx) + horizontal_padding : 150;
}

void WillPanel::Layout(const RECT& bounds) const {
    PositionControl(toggle_, bounds.left, bounds.top,
                    bounds.right - bounds.left, bounds.bottom - bounds.top);
}

void WillPanel::LayoutWindow() const {
    RECT bounds{};
    GetClientRect(window_, &bounds);
    const int width = std::max(0L, bounds.right - 32);
    const int bottom = bounds.bottom;
    ControlLayoutBatch geometry;
    PositionControl(enabled_, 16, 16, width, 24);
    PositionControl(hint_, 16, 48, width, 44);
    PositionControl(topic_label_, 16, 100, 120, 24);
    PositionControl(topic_, 144, 96, width - 128, 26);
    PositionControl(payload_label_, 16, 136, width, 22);
    PositionControl(payload_, 16, 162, width, std::max(40, bottom - 306));
    PositionControl(test_, 16, bottom - 128, 260, 30);
    PositionControl(test_hint_, 16, bottom - 88, width, 44);
    PositionControl(result_, 16, bottom - 38, width, 32);
}

void WillPanel::UpdateText(AppLanguage language, MqttConnectionState state) const {
    language_ = language;
    state_ = state;
    std::wstring title(Text(language, UiText::LastWill));
    if (IsEnabled()) title += L" (" + std::wstring(Text(language, UiText::LastWillEnabled)) + L")";
    if (ControlText(toggle_) != title) {
        SetWindowTextW(toggle_, title.c_str());
        if (relayout_callback_) relayout_callback_();
    }
    if (!window_) return;
    SetWindowTextW(window_, Text(language, UiText::LastWill).data());
    SetWindowTextW(enabled_, Text(language, UiText::EnableLastWill).data());
    SetWindowTextW(hint_, Text(language, UiText::LastWillHint).data());
    SetWindowTextW(topic_label_, Text(language, UiText::LastWillTopic).data());
    SetWindowTextW(payload_label_, Text(language, UiText::LastWillPayload).data());
    SetWindowTextW(test_, Text(language, UiText::LastWillTest).data());
    SetWindowTextW(test_hint_, Text(language, UiText::LastWillTestHint).data());
    if (state == MqttConnectionState::Connecting) SetWindowTextW(result_, L"");
    UpdateControls();
}

bool WillPanel::HandleCommand(WORD id, WORD notification) {
    if (id != IDC_WILL_TOGGLE || notification != BN_CLICKED) return false;
    if (window_) {
        ShowWindow(window_, SW_SHOW);
        SetForegroundWindow(window_);
        SetFocus(IsWindowEnabled(enabled_) ? enabled_ : IsWindowEnabled(test_) ? test_ : window_);
    }
    return true;
}

void WillPanel::ShowTestCompleted() const {
    SetWindowTextW(result_, Text(language_, UiText::LastWillTestCompleted).data());
}

LastWillSettings WillPanel::Settings() const {
    return {IsEnabled(), ControlText(topic_), ControlText(payload_)};
}

bool WillPanel::IsEnabled() const {
    return enabled_ && SendMessageW(enabled_, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void WillPanel::UpdateControls() const {
    const bool editable = state_ == MqttConnectionState::Disconnected || state_ == MqttConnectionState::Failed;
    const bool fields_enabled = editable && IsEnabled();
    EnableWindow(enabled_, editable);
    EnableWindow(topic_label_, fields_enabled);
    EnableWindow(topic_, fields_enabled);
    EnableWindow(payload_label_, fields_enabled);
    EnableWindow(payload_, fields_enabled);
    EnableWindow(test_, state_ == MqttConnectionState::Connected && IsEnabled());
}

bool WillPanel::TranslateDialogMessage(MSG& message) {
    HWND root = GetAncestor(message.hwnd, GA_ROOT);
    wchar_t name[64]{};
    return root && GetClassNameW(root, name, 64) && lstrcmpW(name, kWindowClass) == 0 &&
           IsDialogMessageW(root, &message);
}

LRESULT CALLBACK WillPanel::WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<WillPanel*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<WillPanel*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, wparam, lparam);
    switch (message) {
    case WM_CLOSE:
        ShowWindow(window, SW_HIDE);
        return 0;
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(lparam)->ptMinTrackSize = {560, 460};
        return 0;
    case WM_SIZE:
        self->LayoutWindow();
        return 0;
    case WM_COMMAND:
        if (LOWORD(wparam) == IDCANCEL) {
            ShowWindow(window, SW_HIDE);
            return 0;
        }
        if (HIWORD(wparam) == BN_CLICKED) {
            if (LOWORD(wparam) == IDC_WILL_ENABLED) self->UpdateText(self->language_, self->state_);
            if (LOWORD(wparam) == IDC_WILL_TEST && self->state_ == MqttConnectionState::Connected &&
                self->IsEnabled() && self->test_callback_) self->test_callback_();
        }
        return 0;
    case WM_NCDESTROY:
        self->window_ = nullptr;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace win32mqtt
