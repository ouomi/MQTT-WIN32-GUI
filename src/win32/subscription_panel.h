#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "../subscription_catalog.hpp"
#include "localization.h"

namespace win32mqtt {

struct SubscriptionPanelChanges {
    bool handled{false};
    bool active_topics_changed{false};
    std::vector<std::wstring> subscribe_topics;
    std::vector<std::wstring> unsubscribe_topics;
    std::vector<std::wstring> messages;
};

class SubscriptionPanel {
public:
    void Create(HWND parent, AppLanguage language,
                const std::vector<SubscriptionRecord>& subscriptions);
    void Layout(const RECT& bounds) const;
    void UpdateText(AppLanguage language) const;
    SubscriptionPanelChanges HandleCommand(HWND owner, AppLanguage language, WORD id,
                                           WORD notification);
    SubscriptionPanelChanges HandleNotification(AppLanguage language,
                                                 const NMHDR& notification);
    bool HandleContextMenu(HWND owner, AppLanguage language, HWND source,
                           LPARAM position) const;
    std::vector<std::wstring> ActiveTopics() const;
    std::vector<SubscriptionRecord> Snapshot() const;

private:
    void Add(HWND owner, AppLanguage language, SubscriptionPanelChanges& changes);
    void RemoveSelected(AppLanguage language, SubscriptionPanelChanges& changes);
    void ToggleAt(int index) const;
    void ShowContextMenu(HWND owner, AppLanguage language, LPARAM position) const;
    const SubscriptionRecord* RecordAt(int index) const;

    SubscriptionCatalog catalog_;
    bool restoring_{};
    HWND topic_label_{};
    HWND topic_input_{};
    HWND add_button_{};
    HWND list_{};
};

} // namespace win32mqtt
