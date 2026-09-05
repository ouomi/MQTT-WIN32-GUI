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
    SendMessageW(output_, EM_SETLIMITTEXT, MessageLog::MaxCharacters, 0);
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
    log_.Clear();
    dirty_ = true;
    Refresh();
    return true;
}

void MessagePanel::Append(const std::wstring& message) const {
    log_.Append(message);
    dirty_ = true;
    if (!batching_) Refresh();
}

void MessagePanel::EndBatch() const {
    batching_ = false;
    if (dirty_) Refresh();
}

void MessagePanel::Refresh() const {
    const auto text = log_.Text();
    SendMessageW(output_, WM_SETREDRAW, FALSE, 0);
    SetWindowTextW(output_, text.c_str());
    SendMessageW(output_, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
    SendMessageW(output_, EM_SCROLLCARET, 0, 0);
    SendMessageW(output_, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(output_, nullptr, TRUE);
    dirty_ = false;
}

} // namespace win32mqtt
