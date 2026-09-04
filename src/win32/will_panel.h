#pragma once

#include <windows.h>

#include <string>

#include "../mqtt/mqtt_session.h"
#include "localization.h"

namespace win32mqtt {

struct LastWillSettings {
    bool enabled{};
    std::wstring topic;
    std::wstring payload;
};

class WillPanel {
public:
    static constexpr int kExpandedContentHeight = 85;
    static constexpr int kStatusToggleButtonSize = 16;
    static constexpr int kStatusToggleButtonGap = 4;

    void Create(HWND parent, AppLanguage language);
    void Layout(const RECT& bounds, const RECT& status_bounds) const;
    void UpdateText(AppLanguage language, MqttConnectionState state) const;
    bool HandleCommand(AppLanguage language, WORD id, WORD notification,
                       bool& layout_changed);
    bool HandleDrawItem(const DRAWITEMSTRUCT& draw_item) const;
    LastWillSettings Settings() const;
    bool IsExpanded() const;

private:
    bool IsEnabled() const;
    bool IsEditable(MqttConnectionState state) const;
    void UpdateControls(MqttConnectionState state) const;
    void UpdateHeader(AppLanguage language) const;

    HWND toggle_{};
    HWND enabled_{};
    HWND topic_label_{};
    HWND topic_{};
    HWND payload_label_{};
    HWND payload_{};
    bool expanded_{};
    mutable MqttConnectionState state_{};
};

} // namespace win32mqtt
