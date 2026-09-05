#include "publish_panel.h"

#include <string>

#include "control_helpers.h"
#include "../mqtt/mqtt_topic.hpp"
#include "ui_ids.h"

namespace win32mqtt {
namespace {

constexpr int kButtonWidth = 108;
constexpr int kControlGap = 8;
constexpr int kComboOffset = 90;
constexpr int kLabelHeight = 18;
constexpr int kPublishQosDropDownHeight = 90;
constexpr int kPublishQosWidth = 72;
constexpr int kPublishTopicDropDownHeight = 180;
constexpr int kRowHeight = 23;

} // namespace

void PublishPanel::Create(HWND parent, AppLanguage language) {
    payload_label_ = AddText(parent, 60007, L"");
    payload_ = AddEdit(parent, IDC_PAYLOAD, Text(language, UiText::DefaultPayload).data(),
                       ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL);
    topic_label_ = AddText(parent, 60008, L"");
    topic_ = AddControl(L"COMBOBOX", CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL,
                        IDC_PUB_TOPIC, parent, WS_EX_CLIENTEDGE);
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

void PublishPanel::Layout(const RECT& bounds) const {
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
    PositionControl(qos_label_, bounds.left, qos_y + 4, kComboOffset - 4, kLabelHeight);
    PositionControl(qos_, bounds.left + kComboOffset, qos_y,
                    kPublishQosWidth, kPublishQosDropDownHeight);
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
