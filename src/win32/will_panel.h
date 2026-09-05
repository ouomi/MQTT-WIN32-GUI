#pragma once

#include <windows.h>
#include <functional>
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
    static constexpr int kStatusButtonWidth = 150;
    static constexpr int kStatusButtonGap = 4;

    void Create(HWND parent, AppLanguage language, std::function<void()> test);
    void Layout(const RECT& status_bounds) const;
    void UpdateText(AppLanguage language, MqttConnectionState state) const;
    bool HandleCommand(WORD id, WORD notification);
    void ShowTestCompleted() const;
    LastWillSettings Settings() const;
    static bool TranslateDialogMessage(MSG& message);

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    void LayoutWindow() const;
    bool IsEnabled() const;
    void UpdateControls() const;

    HWND window_{};
    HWND toggle_{};
    HWND enabled_{};
    HWND hint_{};
    HWND topic_label_{};
    HWND topic_{};
    HWND payload_label_{};
    HWND payload_{};
    HWND test_{};
    HWND test_hint_{};
    HWND result_{};
    std::function<void()> test_callback_;
    mutable AppLanguage language_{};
    mutable MqttConnectionState state_{};
};

} // namespace win32mqtt
