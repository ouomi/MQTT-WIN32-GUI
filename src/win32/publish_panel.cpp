#include "publish_panel.h"

#include <commctrl.h>

#include <algorithm>
#include <string>

#include "control_helpers.h"
#include "../mqtt/mqtt_topic.hpp"
#include "ui_ids.h"

namespace win32mqtt {
namespace {

constexpr int kButtonWidth = 108;
constexpr int kLastWillButtonWidth = 150;
constexpr int kControlGap = 8;
constexpr int kComboOffset = 90;
constexpr int kLabelHeight = 18;
constexpr int kPublishQosDropDownHeight = 90;
constexpr int kPublishQosWidth = 72;
constexpr int kPublishTopicDropDownHeight = 180;
constexpr int kRowHeight = 23;

// The popup is a separate window. Constrain its actual size after the combo
// has calculated it, including when it reuses the size of a previously full list.
LRESULT CALLBACK TopicListSubclassProc(HWND window, UINT message, WPARAM wparam,
                                       LPARAM lparam, UINT_PTR subclass_id,
                                       DWORD_PTR /*reference_data*/) {
    if (message == WM_WINDOWPOSCHANGING) {
        const LRESULT result = DefSubclassProc(window, message, wparam, lparam);
        auto* position = reinterpret_cast<WINDOWPOS*>(lparam);
        if (!(position->flags & SWP_NOSIZE) &&
            SendMessageW(window, LB_GETCOUNT, 0, 0) == 0) {
            RECT frame{};
            AdjustWindowRectEx(&frame,
                              static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE)),
                              FALSE,
                              static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE)));
            position->cy = std::max<LONG>(1, frame.bottom - frame.top);
        }
        return result;
    }
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, TopicListSubclassProc, subclass_id);
    }
    return DefSubclassProc(window, message, wparam, lparam);
}

} // namespace

void PublishPanel::Create(HWND parent, AppLanguage language) {
    payload_label_ = AddText(parent, 60007, L"");
    payload_ = AddEdit(parent, IDC_PAYLOAD, Text(language, UiText::DefaultPayload).data(),
                       ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL);
    topic_label_ = AddText(parent, 60008, L"");
    topic_ = AddControl(L"COMBOBOX", CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL,
                        IDC_PUB_TOPIC, parent, WS_EX_CLIENTEDGE);
    COMBOBOXINFO topic_info{};
    topic_info.cbSize = sizeof(topic_info);
    if (GetComboBoxInfo(topic_, &topic_info)) {
        SetWindowSubclass(topic_info.hwndList, TopicListSubclassProc, 0, 0);
    }
    qos_label_ = AddText(parent, 60009, L"");
    qos_ = AddControl(L"COMBOBOX", CBS_DROPDOWNLIST | WS_VSCROLL,
                      IDC_PUB_QOS, parent, WS_EX_CLIENTEDGE);
    SendMessageW(qos_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"0"));
    SendMessageW(qos_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"1"));
    SendMessageW(qos_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"2"));
    SendMessageW(qos_, CB_SETCURSEL, 0, 0);
    publish_button_ = AddButton(parent, IDC_PUBLISH, L"");
    UpdateText(language);
}

void PublishPanel::UpdateText(AppLanguage language) const {
    SetWindowTextW(payload_label_, Text(language, UiText::Message).data());
    SetWindowTextW(topic_label_, Text(language, UiText::PublishTo).data());
    SetWindowTextW(qos_label_, Text(language, UiText::PublishQos).data());
    SetWindowTextW(publish_button_, Text(language, UiText::Publish).data());
}

void PublishPanel::SetConnected(bool connected) const {
    EnableWindow(publish_button_, connected ? TRUE : FALSE);
}

RECT PublishPanel::Layout(const RECT& bounds) const {
    const int width = bounds.right - bounds.left;
    const int payload_y = bounds.top;
    const int target_y = payload_y + 69;
    const int qos_y = target_y + kRowHeight + kControlGap;

    PositionControl(payload_label_, bounds.left, payload_y, width, kLabelHeight);
    PositionControl(payload_, bounds.left, payload_y + 21, width, 42);
    PositionControl(topic_label_, bounds.left, target_y + 4, kComboOffset - 4, kLabelHeight);
    PositionControl(topic_, bounds.left + kComboOffset, target_y,
                    width - kComboOffset - kButtonWidth - kControlGap,
                    kPublishTopicDropDownHeight);
    PositionControl(publish_button_, bounds.right - kButtonWidth, target_y,
                    kButtonWidth, kRowHeight);
    // Keep the button clear of QoS even at the splitter's minimum panel width.
    const int qos_offset = std::min(kComboOffset,
        width - kLastWillButtonWidth - kControlGap - kPublishQosWidth);
    PositionControl(qos_label_, bounds.left, qos_y + 4, qos_offset - 4, kLabelHeight);
    PositionControl(qos_, bounds.left + qos_offset, qos_y,
                    kPublishQosWidth, kPublishQosDropDownHeight);
    return {bounds.right - kLastWillButtonWidth, qos_y, bounds.right, qos_y + kRowHeight};
}

void PublishPanel::SetTopics(const std::vector<std::wstring>& topics) const {
    const std::wstring previously_selected = SelectedTopic();
    SendMessageW(topic_, CB_RESETCONTENT, 0, 0);

    for (const std::wstring& topic : topics) {
        if (IsValidPublishTopic(topic)) {
            SendMessageW(topic_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(topic.c_str()));
        }
    }
    // Suggestions must never replace a manually entered publishing destination.
    SetWindowTextW(topic_, previously_selected.c_str());
    if (SendMessageW(topic_, CB_GETCOUNT, 0, 0) == 0) {
        // Close a popup that was already open when its last suggestion vanished.
        SendMessageW(topic_, CB_SHOWDROPDOWN, FALSE, 0);
    }
}

bool PublishPanel::HandleCommand(HWND owner, AppLanguage language, WORD id, WORD notification,
                                 PublishPanelRequest& request) const {
    if (id != IDC_PUBLISH || notification != BN_CLICKED) {
        return false;
    }

    request.topic = SelectedTopic();
    request.payload = ControlText(payload_);
    request.qos = SelectedQos();
    if (request.topic.empty()) {
        ShowClassicMessageBox(owner, Text(language, UiText::SelectPublishTopic).data(),
                              Text(language, UiText::ApplicationTitle).data(),
                              MB_OK | MB_ICONINFORMATION);
        return true;
    }
    if (!IsValidPublishTopic(request.topic)) {
        ShowClassicMessageBox(owner, Text(language, UiText::InvalidPublishTopic).data(),
                              Text(language, UiText::ApplicationTitle).data(),
                              MB_OK | MB_ICONINFORMATION);
        request.topic.clear();
    }
    return true;
}

std::wstring PublishPanel::SelectedTopic() const {
    return ControlText(topic_);
}

PublishQos PublishPanel::SelectedQos() const {
    switch (static_cast<int>(SendMessageW(qos_, CB_GETCURSEL, 0, 0))) {
    case 1: return PublishQos::Qos1;
    case 2: return PublishQos::Qos2;
    default: return PublishQos::Qos0;
    }
}

} // namespace win32mqtt
