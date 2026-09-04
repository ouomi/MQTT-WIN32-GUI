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
};

AppSettings LoadAppSettings(AppLanguage fallback_language);
void SaveAppSettings(const AppSettings& settings);

} // namespace win32mqtt
