#pragma once

#include <windows.h>

#include <string>

#include "../mqtt/mqtt_session.h"
#include "localization.h"

namespace win32mqtt {

struct ConnectionPanelRequest {
    enum class Type { None, Connect, Disconnect };

    Type type{Type::None};
    std::wstring server_uri;
    std::wstring client_id;
    std::wstring tls_server_name;
};

class ConnectionPanel {
public:
    void Create(HWND parent, AppLanguage language, MqttConnectionState state,
                const std::wstring& server_uri, const std::wstring& client_id,
                const std::wstring& tls_server_name);
    void Layout(const RECT& bounds) const;
    void UpdateText(AppLanguage language, MqttConnectionState state) const;
    bool HandleCommand(WORD id, WORD notification, MqttConnectionState state,
                       ConnectionPanelRequest& request) const;
    std::wstring ServerUri() const;
    std::wstring ClientId() const;
    std::wstring TlsServerName() const;

private:
    HWND server_uri_label_{};
    HWND server_uri_{};
    HWND client_id_label_{};
    HWND client_id_{};
    HWND tls_server_name_label_{};
    HWND tls_server_name_{};
    HWND connect_{};
};

} // namespace win32mqtt
