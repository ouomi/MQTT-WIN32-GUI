#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "localization.h"

namespace win32mqtt {

enum class PublishQos {
    Qos0,
    Qos1,
    Qos2,
};

struct PublishPanelRequest {
    std::wstring topic;
    std::wstring payload;
    PublishQos qos{};
};

class PublishPanel {
public:
    void Create(HWND parent, AppLanguage language);
    // Lay out publishing controls and return the Last Will button bounds.
    RECT Layout(const RECT& bounds, int last_will_button_width) const;
    void UpdateText(AppLanguage language) const;
    void SetConnected(bool connected) const;
    void SetTopics(const std::vector<std::wstring>& topics) const;
    bool HandleCommand(HWND owner, AppLanguage language, WORD id, WORD notification,
                       PublishPanelRequest& request) const;

private:
    std::wstring SelectedTopic() const;
    PublishQos SelectedQos() const;

    HWND payload_label_{};
    HWND payload_{};
    HWND topic_label_{};
    HWND topic_{};
    HWND qos_label_{};
    HWND qos_{};
    HWND publish_button_{};
};

} // namespace win32mqtt
