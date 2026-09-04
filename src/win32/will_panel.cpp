#include "will_panel.h"

#include "control_helpers.h"
#include "ui_ids.h"

namespace win32mqtt {
namespace {

constexpr int kControlGap = 8;
constexpr int kLabelWidth = 120;
constexpr int kRowHeight = 23;

} // namespace

void WillPanel::Create(HWND parent, AppLanguage language) {
    toggle_ = AddButton(parent, IDC_WILL_TOGGLE, L"", BS_OWNERDRAW);
    enabled_ = AddButton(parent, IDC_WILL_ENABLED, L"", BS_AUTOCHECKBOX);
    topic_label_ = AddText(parent, 60011, L"");
    topic_ = AddEdit(parent, IDC_WILL_TOPIC, L"");
    payload_label_ = AddText(parent, 60012, L"");
    payload_ = AddEdit(parent, IDC_WILL_PAYLOAD, L"");
    UpdateText(language, MqttConnectionState::Disconnected);
}

void WillPanel::Layout(const RECT& bounds, const RECT& status_bounds) const {
    const int status_height = status_bounds.bottom - status_bounds.top;
    const int toggle_x = status_bounds.right - GetSystemMetrics(SM_CXVSCROLL) -
                         kStatusToggleButtonGap - kStatusToggleButtonSize;
    const int toggle_y = status_bounds.top +
                         (status_height - kStatusToggleButtonSize) / 2;
    PositionControl(toggle_, toggle_x, toggle_y, kStatusToggleButtonSize,
                    kStatusToggleButtonSize);
    SetWindowPos(toggle_, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW);

    if (!expanded_) {
        return;
    }

    const int width = bounds.right - bounds.left;
    const int field_x = bounds.left + kLabelWidth + kControlGap;
    const int enabled_y = bounds.top;
    const int topic_y = enabled_y + kRowHeight + kControlGap;
    const int payload_y = topic_y + kRowHeight + kControlGap;

    PositionControl(enabled_, bounds.left, enabled_y, width, kRowHeight);
    PositionControl(topic_label_, bounds.left, topic_y + 4, kLabelWidth, kRowHeight - 4);
    PositionControl(topic_, field_x, topic_y, width - kLabelWidth - kControlGap, kRowHeight);
    PositionControl(payload_label_, bounds.left, payload_y + 4, kLabelWidth, kRowHeight - 4);
    PositionControl(payload_, field_x, payload_y, width - kLabelWidth - kControlGap, kRowHeight);
}

void WillPanel::UpdateText(AppLanguage language, MqttConnectionState state) const {
    state_ = state;
    SetWindowTextW(enabled_, Text(language, UiText::EnableLastWill).data());
    SetWindowTextW(topic_label_, Text(language, UiText::LastWillTopic).data());
    SetWindowTextW(payload_label_, Text(language, UiText::LastWillPayload).data());
    UpdateHeader(language);
    UpdateControls(state);
}

bool WillPanel::HandleCommand(AppLanguage language, WORD id, WORD notification,
                              bool& layout_changed) {
    layout_changed = false;
    if (notification != BN_CLICKED) {
        return false;
    }
    if (id == IDC_WILL_TOGGLE) {
        expanded_ = !expanded_;
        UpdateHeader(language);
        UpdateControls(state_);
        InvalidateRect(toggle_, nullptr, TRUE);
        layout_changed = true;
        return true;
    }
    if (id == IDC_WILL_ENABLED) {
        UpdateHeader(language);
        UpdateControls(state_);
        return true;
    }
    return false;
}

bool WillPanel::HandleDrawItem(const DRAWITEMSTRUCT& draw_item) const {
    if (draw_item.CtlID != IDC_WILL_TOGGLE) {
        return false;
    }

    FillRect(draw_item.hDC, &draw_item.rcItem, GetSysColorBrush(COLOR_BTNFACE));

    UINT state = expanded_ ? DFCS_SCROLLUP : DFCS_SCROLLDOWN;
    if ((draw_item.itemState & ODS_DISABLED) != 0) {
        state |= DFCS_INACTIVE;
    }
    RECT arrow = draw_item.rcItem;
    DrawFrameControl(draw_item.hDC, &arrow, DFC_SCROLL, state);
    if ((draw_item.itemState & ODS_FOCUS) != 0) {
        DrawFocusRect(draw_item.hDC, &draw_item.rcItem);
    }
    return true;
}

LastWillSettings WillPanel::Settings() const {
    LastWillSettings settings;
    settings.enabled = IsEnabled();
    if (settings.enabled) {
        settings.topic = ControlText(topic_);
        settings.payload = ControlText(payload_);
    }
    return settings;
}

bool WillPanel::IsExpanded() const {
    return expanded_;
}

bool WillPanel::IsEnabled() const {
    return SendMessageW(enabled_, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

bool WillPanel::IsEditable(MqttConnectionState state) const {
    return state == MqttConnectionState::Disconnected || state == MqttConnectionState::Failed;
}

void WillPanel::UpdateControls(MqttConnectionState state) const {
    const BOOL visible = expanded_;
    const BOOL editable = IsEditable(state);
    const BOOL fields_enabled = visible && editable && IsEnabled();
    ShowWindow(enabled_, visible ? SW_SHOW : SW_HIDE);
    ShowWindow(topic_label_, visible ? SW_SHOW : SW_HIDE);
    ShowWindow(topic_, visible ? SW_SHOW : SW_HIDE);
    ShowWindow(payload_label_, visible ? SW_SHOW : SW_HIDE);
    ShowWindow(payload_, visible ? SW_SHOW : SW_HIDE);
    EnableWindow(enabled_, visible && editable);
    EnableWindow(topic_label_, fields_enabled);
    EnableWindow(topic_, fields_enabled);
    EnableWindow(payload_label_, fields_enabled);
    EnableWindow(payload_, fields_enabled);
}

void WillPanel::UpdateHeader(AppLanguage /*language*/) const {
    SetWindowTextW(toggle_, L"");
    InvalidateRect(toggle_, nullptr, TRUE);
}

} // namespace win32mqtt
