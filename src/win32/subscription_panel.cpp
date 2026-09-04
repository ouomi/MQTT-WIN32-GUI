#include "subscription_panel.h"

#include <commctrl.h>
#include <windowsx.h>

#include <cstddef>
#include <string>

#include "control_helpers.h"
#include "ui_ids.h"

namespace win32mqtt {
namespace {

constexpr int kButtonWidth = 108;
constexpr int kControlGap = 8;
constexpr int kPanelContentMargin = 8;
constexpr int kRowHeight = 23;
constexpr int kTopicLabelWidth = 68;
constexpr int kTopicInputOffset = 72;

} // namespace

void SubscriptionPanel::Create(HWND parent, AppLanguage language,
                               const std::vector<SubscriptionRecord>& subscriptions) {
    catalog_.Replace(subscriptions);
    topic_label_ = AddText(parent, 60006, L"");
    topic_input_ = AddEdit(parent, IDC_SUB_TOPIC, L"demo/topic");
    add_button_ = AddButton(parent, IDC_SUB_ADD, L"");
    list_ = AddControl(WC_LISTVIEWW, LVS_REPORT | LVS_SHOWSELALWAYS,
                       IDC_SUBSCRIPTIONS, parent, WS_EX_CLIENTEDGE);
    ListView_SetExtendedListViewStyle(list_,
                                      LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    ListView_SetBkColor(list_, GetSysColor(COLOR_WINDOW));
    ListView_SetTextBkColor(list_, GetSysColor(COLOR_WINDOW));
    ListView_SetTextColor(list_, GetSysColor(COLOR_WINDOWTEXT));

    LVCOLUMNW topic_column{};
    topic_column.mask = LVCF_TEXT | LVCF_WIDTH;
    topic_column.pszText = const_cast<LPWSTR>(Text(language, UiText::Topic).data());
    topic_column.cx = 340;
    ListView_InsertColumn(list_, 0, &topic_column);

    restoring_ = true;
    for (std::size_t index = 0; index < catalog_.Size(); ++index) {
        const SubscriptionRecord* record = catalog_.At(index);
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(index);
        item.pszText = const_cast<LPWSTR>(record->topic.c_str());
        ListView_InsertItem(list_, &item);
        ListView_SetCheckState(list_, static_cast<int>(index), record->active);
    }
    restoring_ = false;
    ListView_SetColumnWidth(list_, 0, LVSCW_AUTOSIZE_USEHEADER);
    UpdateText(language);
}

void SubscriptionPanel::UpdateText(AppLanguage language) const {
    SetWindowTextW(topic_label_, Text(language, UiText::NewTopic).data());
    SetWindowTextW(add_button_, Text(language, UiText::AddSubscription).data());
    LVCOLUMNW topic_column{};
    topic_column.mask = LVCF_TEXT;
    topic_column.pszText = const_cast<LPWSTR>(Text(language, UiText::Topic).data());
    ListView_SetColumn(list_, 0, &topic_column);
    ListView_SetColumnWidth(list_, 0, LVSCW_AUTOSIZE_USEHEADER);
}

void SubscriptionPanel::Layout(const RECT& bounds) const {
    const int content_left = bounds.left + kPanelContentMargin;
    const int content_right = bounds.right - kPanelContentMargin;
    const int content_width = content_right - content_left;
    const int topic_row_y = bounds.top + kPanelContentMargin;
    const int list_y = topic_row_y + kRowHeight + kControlGap;
    const int list_bottom = bounds.bottom - kPanelContentMargin;

    PositionControl(topic_label_, content_left, topic_row_y + 4, kTopicLabelWidth, 18);
    PositionControl(topic_input_, content_left + kTopicInputOffset, topic_row_y,
                    content_width - kTopicInputOffset - kButtonWidth - kControlGap, kRowHeight);
    PositionControl(add_button_, content_right - kButtonWidth, topic_row_y,
                    kButtonWidth, kRowHeight);
    PositionControl(list_, content_left, list_y, content_width, list_bottom - list_y);
}

SubscriptionPanelChanges SubscriptionPanel::HandleCommand(HWND owner, AppLanguage language,
                                                            WORD id, WORD notification) {
    SubscriptionPanelChanges changes;
    if (id == IDC_SUB_ADD && notification == BN_CLICKED) {
        changes.handled = true;
        Add(owner, language, changes);
    } else if (id == IDM_SUB_REMOVE) {
        changes.handled = true;
        RemoveSelected(language, changes);
    }
    return changes;
}

SubscriptionPanelChanges SubscriptionPanel::HandleNotification(AppLanguage language,
                                                                 const NMHDR& notification) {
    SubscriptionPanelChanges changes;
    if (restoring_ || notification.idFrom != IDC_SUBSCRIPTIONS) {
        return changes;
    }

    if (notification.code == LVN_ITEMCHANGED) {
        changes.handled = true;
        const auto& changed = *reinterpret_cast<const NMLISTVIEW*>(&notification);
        if ((changed.uChanged & LVIF_STATE) == 0 ||
            (changed.uOldState & LVIS_STATEIMAGEMASK) ==
                (changed.uNewState & LVIS_STATEIMAGEMASK)) {
            return changes;
        }

        const bool active = ((changed.uNewState & LVIS_STATEIMAGEMASK) >> 12) == 2;
        if (!catalog_.SetActive(static_cast<std::size_t>(changed.iItem), active)) {
            return changes;
        }
        const SubscriptionRecord* record = RecordAt(changed.iItem);
        if (record == nullptr) {
            return changes;
        }

        changes.messages.push_back(
            L"[" + record->topic + L"] " +
            std::wstring(Text(language, active ? UiText::SubscriptionActivated
                                                : UiText::SubscriptionDeactivated)));
        (active ? changes.subscribe_topics : changes.unsubscribe_topics).push_back(record->topic);
        changes.active_topics_changed = true;
        return changes;
    }

    if (notification.code == NM_DBLCLK) {
        changes.handled = true;
        const auto& activation = *reinterpret_cast<const NMITEMACTIVATE*>(&notification);
        LVHITTESTINFO hit_test{};
        hit_test.pt = activation.ptAction;
        ListView_SubItemHitTest(list_, &hit_test);
        if (activation.iItem >= 0 && (hit_test.flags & LVHT_ONITEMSTATEICON) == 0) {
            ToggleAt(activation.iItem);
        }
    }
    return changes;
}

bool SubscriptionPanel::HandleContextMenu(HWND owner, AppLanguage language, HWND source,
                                           LPARAM position) const {
    if (source != list_) {
        return false;
    }
    ShowContextMenu(owner, language, position);
    return true;
}

std::vector<std::wstring> SubscriptionPanel::ActiveTopics() const {
    return catalog_.ActiveTopics();
}

std::vector<SubscriptionRecord> SubscriptionPanel::Snapshot() const {
    return catalog_.Snapshot();
}

void SubscriptionPanel::Add(HWND owner, AppLanguage language,
                            SubscriptionPanelChanges& changes) {
    const std::wstring topic = ControlText(topic_input_);
    if (topic.empty()) {
        ShowClassicMessageBox(owner, Text(language, UiText::EnterTopicFirst).data(),
                              Text(language, UiText::ApplicationTitle).data(),
                              MB_OK | MB_ICONINFORMATION);
        return;
    }

    const std::size_t existing_index = catalog_.Find(topic);
    if (existing_index != SubscriptionCatalog::npos) {
        ListView_SetItemState(list_, static_cast<int>(existing_index),
                              LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        ShowClassicMessageBox(owner, Text(language, UiText::DuplicateSubscription).data(),
                              Text(language, UiText::ApplicationTitle).data(),
                              MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (!catalog_.Add(topic)) {
        return;
    }
    const int index = static_cast<int>(catalog_.Size() - 1);
    const SubscriptionRecord* record = RecordAt(index);
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = index;
    item.pszText = const_cast<LPWSTR>(record->topic.c_str());
    if (ListView_InsertItem(list_, &item) == -1) {
        catalog_.Remove(static_cast<std::size_t>(index));
        ShowClassicMessageBox(owner, Text(language, UiText::AddSubscriptionFailed).data(),
                              Text(language, UiText::ApplicationTitle).data(),
                              MB_OK | MB_ICONERROR);
        return;
    }

    ListView_SetItemState(list_, index, LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);
    SetWindowTextW(topic_input_, L"");
    ListView_SetColumnWidth(list_, 0, LVSCW_AUTOSIZE_USEHEADER);
    changes.messages.push_back(
        std::wstring(Text(language, UiText::AddedSubscription)) + topic +
        (language == AppLanguage::Chinese ? L"。勾选复选框以启用订阅。"
                                          : L". Tick its checkbox to activate it."));
}

void SubscriptionPanel::RemoveSelected(AppLanguage language,
                                        SubscriptionPanelChanges& changes) {
    for (int index = ListView_GetItemCount(list_) - 1; index >= 0; --index) {
        if ((ListView_GetItemState(list_, index, LVIS_SELECTED) & LVIS_SELECTED) == 0) {
            continue;
        }

        const SubscriptionRecord* record = RecordAt(index);
        if (record == nullptr) {
            continue;
        }
        const std::wstring topic = record->topic;
        if (record->active) {
            changes.unsubscribe_topics.push_back(topic);
            changes.active_topics_changed = true;
        }
        changes.messages.push_back(
            std::wstring(Text(language, UiText::RemovedSubscription)) + topic);
        ListView_DeleteItem(list_, index);
        catalog_.Remove(static_cast<std::size_t>(index));
    }
    ListView_SetColumnWidth(list_, 0, LVSCW_AUTOSIZE_USEHEADER);
}

void SubscriptionPanel::ToggleAt(int index) const {
    const SubscriptionRecord* record = RecordAt(index);
    if (record != nullptr) {
        ListView_SetCheckState(list_, index, !record->active);
    }
}

void SubscriptionPanel::ShowContextMenu(HWND owner, AppLanguage language,
                                         LPARAM position) const {
    POINT screen_point{};
    if (position == static_cast<LPARAM>(-1)) {
        RECT rect{};
        GetWindowRect(list_, &rect);
        screen_point.x = rect.left + 8;
        screen_point.y = rect.top + 8;
    } else {
        screen_point.x = GET_X_LPARAM(position);
        screen_point.y = GET_Y_LPARAM(position);

        POINT client_point = screen_point;
        ScreenToClient(list_, &client_point);
        LVHITTESTINFO hit_test{};
        hit_test.pt = client_point;
        const int index = ListView_HitTest(list_, &hit_test);
        if (index >= 0 &&
            (ListView_GetItemState(list_, index, LVIS_SELECTED) & LVIS_SELECTED) == 0) {
            ListView_SetItemState(list_, index, LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
        }
    }

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_SUB_REMOVE,
                Text(language, UiText::DeleteSelectedSubscriptions).data());
    const UINT command = TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
                                        screen_point.x, screen_point.y, 0, owner, nullptr);
    DestroyMenu(menu);
    if (command != 0) {
        SendMessageW(owner, WM_COMMAND, command, 0);
    }
}

const SubscriptionRecord* SubscriptionPanel::RecordAt(int index) const {
    return index >= 0 ? catalog_.At(static_cast<std::size_t>(index)) : nullptr;
}

} // namespace win32mqtt
