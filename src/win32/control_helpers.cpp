#include "control_helpers.h"

#include <dwmapi.h>
#include <uxtheme.h>

#include <cwchar>
#include <iterator>

namespace win32mqtt {
namespace {

thread_local ControlLayoutBatch* active_layout = nullptr;

void InvalidateStaticText(HWND control) {
    wchar_t class_name[16]{};
    if (GetClassNameW(control, class_name, static_cast<int>(std::size(class_name))) > 0 &&
        _wcsicmp(class_name, L"STATIC") == 0) {
        // Resizing can reflow text across the entire label. Repaint its final
        // bounds instead of retaining pixels copied from an earlier layout.
        // Keep this asynchronous so rapid splitter moves can coalesce.
        InvalidateRect(control, nullptr, TRUE);
    }
}

constexpr DWORD kDwmwaWindowCornerPreference = 33;
constexpr int kDwmcpDoNotRound = 1;

HFONT ChineseFriendlyFont() {
    static HFONT font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    DEFAULT_QUALITY, FF_DONTCARE, L"SimSun");
    return font != nullptr ? font : static_cast<HFONT>(GetStockObject(SYSTEM_FONT));
}

void SetControlFont(HWND control) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(ChineseFriendlyFont()), TRUE);
}

BOOL CALLBACK ConfigureClassicMessageBoxChild(HWND child, LPARAM) {
    UseClassicControlTheme(child);
    SetControlFont(child);
    return TRUE;
}

LRESULT CALLBACK ClassicMessageBoxHook(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HCBT_ACTIVATE) {
        HWND dialog = reinterpret_cast<HWND>(wparam);
        wchar_t class_name[16]{};
        if (GetClassNameW(dialog, class_name, static_cast<int>(std::size(class_name))) > 0 &&
            wcscmp(class_name, L"#32770") == 0) {
            UseClassicWindowFrame(dialog);
            EnumChildWindows(dialog, ConfigureClassicMessageBoxChild, 0);
        }
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

} // namespace

void UseClassicControlTheme(HWND control) {
    if (control != nullptr) {
        // Empty sub-app and ID lists opt this control out of visual styles.
        SetWindowTheme(control, L"", L"");
    }
}

void UseClassicWindowFrame(HWND window) {
    UseClassicControlTheme(window);

    // Request a classic non-client frame for this window. If unsupported,
    // leave frame rendering to the system without preventing startup.
    const DWMNCRENDERINGPOLICY rendering_policy = DWMNCRP_DISABLED;
    DwmSetWindowAttribute(window, DWMWA_NCRENDERING_POLICY, &rendering_policy,
                         sizeof(rendering_policy));

    const int corner_preference = kDwmcpDoNotRound;
    DwmSetWindowAttribute(window, kDwmwaWindowCornerPreference, &corner_preference,
                          sizeof(corner_preference));
}

ControlLayoutBatch::ControlLayoutBatch() : previous_(active_layout) {
    active_layout = this;
}

ControlLayoutBatch::~ControlLayoutBatch() {
    active_layout = previous_;
    if (positions_.empty()) return;
    HDWP batch = BeginDeferWindowPos(static_cast<int>(positions_.size()));
    for (const auto& p : positions_) {
        if (!batch) break;
        batch = DeferWindowPos(batch, p.control, nullptr, p.x, p.y, p.width, p.height,
                               SWP_NOACTIVATE | SWP_NOZORDER);
    }
    if (!batch || !EndDeferWindowPos(batch)) {
        // A failed deferred batch must not leave the UI with an incomplete layout.
        for (const auto& p : positions_) {
            SetWindowPos(p.control, nullptr, p.x, p.y, p.width, p.height,
                         SWP_NOACTIVATE | SWP_NOZORDER);
        }
    }
    // Invalidate after committing the batch, using each label's final bounds.
    for (const auto& p : positions_) {
        InvalidateStaticText(p.control);
    }
}

void PositionControl(HWND control, int x, int y, int width, int height) {
    if (control == nullptr) return;
    RECT current{};
    if (GetWindowRect(control, &current)) {
        MapWindowPoints(HWND_DESKTOP, GetParent(control), reinterpret_cast<LPPOINT>(&current), 2);
        if (current.left == x && current.top == y &&
            current.right - current.left == width && current.bottom - current.top == height) return;
    }
    if (active_layout) {
        active_layout->positions_.push_back({control, x, y, width, height});
        return;
    }
    // Let Windows invalidate moved/resized controls and exposed areas only.
    // Layout no longer forces every child to erase and repaint synchronously.
    SetWindowPos(control, nullptr, x, y, width, height,
                 SWP_NOACTIVATE | SWP_NOZORDER);
    InvalidateStaticText(control);
}

int ShowClassicMessageBox(HWND owner, const wchar_t* text, const wchar_t* caption, UINT type) {
    HHOOK hook = SetWindowsHookExW(WH_CBT, ClassicMessageBoxHook, nullptr, GetCurrentThreadId());
    const int result = MessageBoxW(owner, text, caption, type);
    if (hook != nullptr) {
        UnhookWindowsHookEx(hook);
    }
    return result;
}

HWND AddControl(const wchar_t* class_name, DWORD style, int id, HWND parent, DWORD ex_style) {
    HWND control = CreateWindowExW(
        ex_style, class_name, L"", WS_CHILD | WS_VISIBLE | style,
        0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    UseClassicControlTheme(control);
    SetControlFont(control);
    return control;
}

HWND AddText(HWND parent, int id, const wchar_t* value) {
    HWND control = AddControl(L"STATIC", SS_LEFT, id, parent);
    SetWindowTextW(control, value);
    return control;
}

HWND AddEdit(HWND parent, int id, const wchar_t* value, DWORD extra_style) {
    HWND control = AddControl(L"EDIT", ES_AUTOHSCROLL | extra_style, id, parent, WS_EX_CLIENTEDGE);
    SetWindowTextW(control, value);
    return control;
}

HWND AddButton(HWND parent, int id, const wchar_t* value, DWORD extra_style) {
    HWND control = AddControl(L"BUTTON", BS_PUSHBUTTON | extra_style, id, parent);
    SetWindowTextW(control, value);
    return control;
}

std::wstring ControlText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring value(static_cast<size_t>(length) + 1, L'\0');
    if (length > 0) {
        GetWindowTextW(control, value.data(), length + 1);
    }
    value.resize(static_cast<size_t>(length));
    return value;
}

} // namespace win32mqtt
