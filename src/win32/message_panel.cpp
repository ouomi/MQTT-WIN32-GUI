#include "message_panel.h"

#include "control_helpers.h"
#include "ui_ids.h"

#include <algorithm>
#include <cwchar>

namespace win32mqtt {
namespace {

constexpr int kButtonWidth = 108;
constexpr int kPanelContentMargin = 8;
constexpr int kControlGap = 8;
constexpr int kHeadingHeight = 18;
constexpr int kHeadingOffset = 24;
constexpr int kRowHeight = 23;
constexpr int kHeadingButtonOffset = -3;

std::wstring EventDetails(const MqttEvent& event) {
    return L"generation=" + std::to_wstring(event.generation) +
        L" operation=" + std::to_wstring(event.operation) +
        L" monotonic-ms=" + std::to_wstring(event.received_ms) +
        L" QoS=" + std::to_wstring(static_cast<int>(event.publish_qos)) +
        L" retain=" + std::to_wstring(event.retain) + L" dup=" + std::to_wstring(event.dup) +
        L" packet=" + std::to_wstring(event.packet_id) +
        L" bytes=" + std::to_wstring(event.payload.size());
}

std::wstring HexBody(const MqttEvent& event) {
    return L"Topic HEX: " + MqttMessageStore::Hex(event.topic) +
        L"\r\nPayload HEX: " + MqttMessageStore::Hex(event.payload);
}

} // namespace

void MessagePanel::Create(HWND parent, AppLanguage language) {
    title_ = AddText(parent, 60005, L"");
    output_ = AddEdit(parent, IDC_MESSAGES, L"",
                      ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_HSCROLL | WS_VSCROLL);
    SendMessageW(output_, EM_SETLIMITTEXT, MessageLog::MaxCharacters, 0);
    hex_button_ = AddButton(parent, IDC_RAW_MESSAGES, L"Text / HEX");
    mode_button_ = AddButton(parent, IDC_MESSAGE_DETAILS, L"");
    clear_button_ = AddButton(parent, IDC_CLEAR_MESSAGES, L"");
    UpdateText(language);
}

void MessagePanel::UpdateText(AppLanguage language) const {
    language_ = language;
    UpdateModeButton();
    SetWindowTextW(title_, Text(language, UiText::SubscriptionMessages).data());
    SetWindowTextW(clear_button_, Text(language, UiText::ClearMessages).data());
}

void MessagePanel::Layout(const RECT& bounds) const {
    const int content_left = bounds.left + kPanelContentMargin;
    const int content_top = bounds.top + kPanelContentMargin;
    const int content_width = bounds.right - bounds.left - 2 * kPanelContentMargin;
    const int title_width = language_ == AppLanguage::Chinese ? 144 : 280;
    const int button_width = std::min(kButtonWidth, (content_width - 2 * kControlGap) / 3);
    const int buttons_width = 3 * button_width + 2 * kControlGap;
    const bool stacked = content_width < title_width + kControlGap + buttons_width;
    const int button_y = content_top + (stacked ? kHeadingOffset : 0) + kHeadingButtonOffset;
    const int output_y = content_top + kHeadingOffset * (stacked ? 2 : 1);
    const int output_bottom = bounds.bottom - kPanelContentMargin;
    const int buttons_left = content_left + content_width - buttons_width;

    PositionControl(title_, content_left, content_top,
                    stacked ? content_width : content_width - buttons_width - kControlGap,
                    kHeadingHeight);
    PositionControl(mode_button_, buttons_left, button_y, button_width, kRowHeight);
    PositionControl(hex_button_, buttons_left + button_width + kControlGap,
                    button_y, button_width, kRowHeight);
    PositionControl(clear_button_, buttons_left + 2 * (button_width + kControlGap),
                    button_y, button_width, kRowHeight);
    PositionControl(output_, content_left, output_y, content_width, output_bottom - output_y);
}

bool MessagePanel::HandleCommand(WORD id, WORD notification) const {
    if (id == IDC_RAW_MESSAGES && notification == BN_CLICKED) {
        history_.ToggleHex();
        UpdateModeButton();
        dirty_ = true;
        Refresh();
        return true;
    }
    if (id == IDC_MESSAGE_DETAILS && notification == BN_CLICKED) {
        history_.ToggleDetails();
        UpdateModeButton();
        return true;
    }
    if (id != IDC_CLEAR_MESSAGES || notification != BN_CLICKED) {
        return false;
    }
    history_.Clear();
    dirty_ = true;
    Refresh();
    return true;
}

void MessagePanel::UpdateModeButton() const {
    const bool chinese = language_ == AppLanguage::Chinese;
    SetWindowTextW(mode_button_, history_.Detailed()
        ? (chinese ? L"详细模式" : L"Detailed")
        : (chinese ? L"简洁模式" : L"Compact"));
    EnableWindow(mode_button_, !history_.Hex());
}

void MessagePanel::AppendRecord(const std::wstring& tag, const std::wstring& body,
                                const std::wstring& details, const std::wstring& hex_body) const {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t timestamp[32]{};
    std::swprintf(timestamp, 32, L"%02u:%02u:%02u.%03u",
                  static_cast<unsigned>(now.wHour), static_cast<unsigned>(now.wMinute),
                  static_cast<unsigned>(now.wSecond), static_cast<unsigned>(now.wMilliseconds));
    history_.Append(tag, body, std::wstring(timestamp) + (details.empty() ? L"" : L" " + details), hex_body);
    dirty_ = true;
    if (!batching_) Refresh();
}

void MessagePanel::Append(const std::wstring& message, const std::wstring& details) const {
    AppendRecord(language_ == AppLanguage::Chinese ? L"系统" : L"System", message, details);
}

void MessagePanel::EndBatch() const {
    batching_ = false;
    if (dirty_) Refresh();
}

void MessagePanel::Refresh() const {
    const auto text = history_.Text();
    SendMessageW(output_, EM_SETLIMITTEXT, 4 * MqttMessageStore::MaxBytes, 0);
    SendMessageW(output_, WM_SETREDRAW, FALSE, 0);
    SetWindowTextW(output_, text.c_str());
    SendMessageW(output_, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
    SendMessageW(output_, EM_SCROLLCARET, 0, 0);
    SendMessageW(output_, WM_SETREDRAW, TRUE, 0);
    // Restore both client and nonclient painting after the batched update,
    // including the background and scrollbars.
    RedrawWindow(output_, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
    dirty_ = false;
}

} // namespace win32mqtt

namespace win32mqtt {
void MessagePanel::Receive(const MqttEvent& event, const std::wstring& topic, const std::wstring& text) const {
    AppendRecord(language_ == AppLanguage::Chinese ? L"收" : L"RX",
                 L"[" + topic + L"] " + text, EventDetails(event), HexBody(event));
}

void MessagePanel::Publish(const MqttEvent& event, const std::wstring& topic, const std::wstring& text) const {
    const bool chinese = language_ == AppLanguage::Chinese;
    if (event.type == MqttEventType::PublishRejected) {
        AppendRecord(chinese ? L"发" : L"TX", L"[" + topic + L"] " +
                     (chinese ? L"未发布：" : L"Not published: ") + text, EventDetails(event));
    } else {
        const std::wstring queued = chinese ? L"已入队 " : L"Queued ";
        AppendRecord(chinese ? L"发" : L"TX", (history_.Detailed() ? queued : L"") + L"[" + topic + L"] " + text,
                     EventDetails(event), queued + HexBody(event));
    }
}
}
