#include "connection_panel.h"

#include <commctrl.h>

#include "control_helpers.h"
#include "ui_ids.h"

namespace win32mqtt {
namespace {

constexpr int kControlGap = 8;
constexpr int kLabelWidth = 96;
constexpr int kButtonWidth = 108;
constexpr int kRowHeight = 23;
#if WIN32MQTT_ENABLE_TLS
constexpr wchar_t kDefaultServerUri[] = L"mqtts://broker.emqx.io:8883";
#else
constexpr wchar_t kDefaultServerUri[] = L"mqtt://broker.emqx.io:1883";
#endif

} // namespace

void ConnectionPanel::Create(HWND parent, AppLanguage language, MqttConnectionState state,
                             const std::wstring& server_uri, const std::wstring& client_id,
                             const std::wstring& tls_server_name) {
    server_uri_label_ = AddText(parent, 60001, L"Server URI:");
    server_uri_ = AddEdit(parent, IDC_SERVER_URI,
                          server_uri.empty() ? kDefaultServerUri : server_uri.c_str());
    client_id_label_ = AddText(parent, 60002, L"Client ID:");
    client_id_ = AddEdit(parent, IDC_CLIENT_ID, client_id.c_str());
    tls_server_name_label_ = AddText(parent, 60003, L"TLS SNI:");
    tls_server_name_ = AddEdit(parent, IDC_TLS_SERVER_NAME, tls_server_name.c_str());
    SendMessageW(tls_server_name_, EM_SETLIMITTEXT, 253, 0);
    connect_ = AddButton(parent, IDC_CONNECT, L"");
    UpdateText(language, state);
}

void ConnectionPanel::Layout(const RECT& bounds) const {
    const int width = bounds.right - bounds.left;
    const int field_x = bounds.left + kLabelWidth + kControlGap;
    const int server_uri_width = width - kLabelWidth - kControlGap - kButtonWidth - kControlGap;
    const int second_row_y = bounds.top + kRowHeight + kControlGap;

    PositionControl(server_uri_label_, bounds.left, bounds.top + 4, kLabelWidth, 18);
    PositionControl(server_uri_, field_x, bounds.top, server_uri_width, kRowHeight);
    PositionControl(connect_, bounds.right - kButtonWidth, bounds.top, kButtonWidth, kRowHeight);
    PositionControl(client_id_label_, bounds.left, second_row_y + 4, kLabelWidth, 18);
    PositionControl(client_id_, field_x, second_row_y, width - kLabelWidth - kControlGap, kRowHeight);
    const int third_row_y = second_row_y + kRowHeight + kControlGap;
    PositionControl(tls_server_name_label_, bounds.left, third_row_y + 4, kLabelWidth, 18);
    PositionControl(tls_server_name_, field_x, third_row_y, width - kLabelWidth - kControlGap, kRowHeight);
}

void ConnectionPanel::UpdateText(AppLanguage language, MqttConnectionState state) const {
    SetWindowTextW(server_uri_label_, Text(language, UiText::ServerUri).data());
    SetWindowTextW(client_id_label_, Text(language, UiText::ClientId).data());
    SetWindowTextW(tls_server_name_label_, Text(language, UiText::TlsServerName).data());
    SendMessageW(tls_server_name_, EM_SETCUEBANNER, FALSE,
                 reinterpret_cast<LPARAM>(Text(language, WIN32MQTT_ENABLE_TLS ?
                     UiText::TlsServerNameHint : UiText::TlsBuildRequired).data()));
    const bool can_disconnect = state == MqttConnectionState::Connecting ||
                                state == MqttConnectionState::Connected ||
                                state == MqttConnectionState::Disconnecting;
    EnableWindow(tls_server_name_, WIN32MQTT_ENABLE_TLS && !can_disconnect);
    SetWindowTextW(connect_, Text(language, can_disconnect ? UiText::Disconnect : UiText::Connect).data());
}

bool ConnectionPanel::HandleCommand(WORD id, WORD notification, MqttConnectionState state,
                                    ConnectionPanelRequest& request) const {
    if (id != IDC_CONNECT || notification != BN_CLICKED) {
        return false;
    }

    if (state == MqttConnectionState::Disconnected || state == MqttConnectionState::Failed) {
        request.type = ConnectionPanelRequest::Type::Connect;
        request.server_uri = ControlText(server_uri_);
        request.client_id = ControlText(client_id_);
        request.tls_server_name = ControlText(tls_server_name_);
    } else {
        request.type = ConnectionPanelRequest::Type::Disconnect;
    }
    return true;
}

std::wstring ConnectionPanel::ServerUri() const {
    return ControlText(server_uri_);
}

std::wstring ConnectionPanel::ClientId() const {
    return ControlText(client_id_);
}

std::wstring ConnectionPanel::TlsServerName() const {
    return ControlText(tls_server_name_);
}

} // namespace win32mqtt
