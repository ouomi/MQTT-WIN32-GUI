#pragma once

#include <string>
#include <vector>

#include "../subscription_catalog.hpp"
#include "localization.h"

namespace win32mqtt {

struct AppSettings {
    AppLanguage language;
    std::wstring server_uri;
    std::wstring client_id;
    std::vector<SubscriptionRecord> subscriptions;
    int window_width{};
    int window_height{};
    int subscription_panel_width{}; // Zero uses the default equal split.
    std::wstring tls_server_name{};
};

struct AppSettingsLoadResult {
    AppSettings settings{};
    std::wstring path;
    std::wstring error;
    bool Succeeded() const { return error.empty(); }
};

AppSettingsLoadResult LoadAppSettings(AppLanguage fallback_language, const std::wstring& path = {});
bool SaveAppSettings(const AppSettings& settings, const std::wstring& path = {});

} // namespace win32mqtt
