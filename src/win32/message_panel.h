#pragma once

#include <windows.h>

#include <string>

#include "localization.h"
#include "../message_log.hpp"
#include "../mqtt/mqtt_message_store.hpp"

namespace win32mqtt {

class MessagePanel {
public:
    void Create(HWND parent, AppLanguage language);
    void Layout(const RECT& bounds) const;
    void UpdateText(AppLanguage language) const;
    bool HandleCommand(WORD id, WORD notification) const;
    void Append(const std::wstring& message) const;
    void Receive(const MqttEvent& event, const std::wstring& topic, const std::wstring& text) const;
    void BeginBatch() const { batching_ = true; }
    void EndBatch() const;

private:
    void Refresh() const;
    mutable MessageLog log_;
    mutable MqttMessageStore raw_;
    mutable bool hex_ = false;
    HWND hex_button_{};
    mutable bool batching_ = false;
    mutable bool dirty_ = false;
    HWND title_{};
    HWND output_{};
    HWND clear_button_{};
};

} // namespace win32mqtt
