#include "message_panel.h"

#include "control_helpers.h"
#include "ui_ids.h"

namespace win32mqtt {
namespace {

constexpr int kButtonWidth = 108;
constexpr int kPanelContentMargin = 8;
constexpr int kControlGap = 8;
constexpr int kHeadingHeight = 18;
constexpr int kHeadingOffset = 24;
constexpr int kRowHeight = 23;

} // namespace

void MessagePanel::Create(HWND parent, AppLanguage language) {
    title_ = AddText(parent, 60005, L"");
    output_ = AddEdit(parent, IDC_MESSAGES, L"",
                      ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_HSCROLL | WS_VSCROLL);
    clear_button_ = AddButton(parent, IDC_CLEAR_MESSAGES, L"");
    UpdateText(language);
}

void MessagePanel::UpdateText(AppLanguage language) const {
    SetWindowTextW(title_, Text(language, UiText::SubscriptionMessages).data());
    SetWindowTextW(clear_button_, Text(language, UiText::ClearMessages).data());
}

void MessagePanel::Layout(const RECT& bounds) const {
    const int content_left = bounds.left + kPanelContentMargin;
    const int content_top = bounds.top + kPanelContentMargin;
    const int content_width = bounds.right - bounds.left - 2 * kPanelContentMargin;
    const int output_y = content_top + kHeadingOffset;
    const int output_bottom = bounds.bottom - kPanelContentMargin;

    PositionControl(title_, content_left, content_top,
                    content_width - kButtonWidth - kControlGap, kHeadingHeight);
    PositionControl(clear_button_, bounds.right - kPanelContentMargin - kButtonWidth,
                    content_top, kButtonWidth, kRowHeight);
    PositionControl(output_, content_left, output_y, content_width, output_bottom - output_y);
}

bool MessagePanel::HandleCommand(WORD id, WORD notification) const {
    if (id != IDC_CLEAR_MESSAGES || notification != BN_CLICKED) {
        return false;
    }
    SetWindowTextW(output_, L"");
    return true;
}

void MessagePanel::Append(const std::wstring& message) const {
    const std::wstring line = message + L"\r\n";
    SendMessageW(output_, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
    SendMessageW(output_, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
}

} // namespace win32mqtt
