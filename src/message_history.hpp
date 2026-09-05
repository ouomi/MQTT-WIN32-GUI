#pragma once

#include "message_log.hpp"

namespace win32mqtt {

// Render each record on arrival so mode changes never rewrite text history.
class MessageHistory {
public:
    bool Hex() const { return hex_; }
    bool Detailed() const { return hex_ || detailed_; }
    void ToggleHex() { hex_ = !hex_; }
    void ToggleDetails() { if (!hex_) detailed_ = !detailed_; }
    void Append(const std::wstring& tag, const std::wstring& body,
                const std::wstring& details, const std::wstring& hex_body = {}) {
        const auto prefix = L"[" + tag + L"] ";
        const auto metadata = details.empty() ? L"" : L"[" + details + L"] ";
        text_.Append(prefix + (Detailed() ? metadata : L"") + body);
        hex_text_.Append(prefix + metadata + (hex_body.empty() ? body : hex_body));
    }
    std::wstring Text() const { return hex_ ? hex_text_.Text() : text_.Text(); }
    void Clear() { text_.Clear(); hex_text_.Clear(); }
private:
    bool hex_ = false;
    bool detailed_ = false;
    MessageLog text_;
    // Preserve full HEX payloads within the existing 4 Mi-character view budget.
    BoundedMessageLog<512, 4 * 1024 * 1024, 4 * 1024 * 1024> hex_text_;
};

} // namespace win32mqtt
