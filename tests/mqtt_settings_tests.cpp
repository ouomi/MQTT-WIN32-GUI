#include "win32/app_settings.h"
#include <windows.h>
#include <cstdlib>
#include <iostream>
using namespace win32mqtt;
static void check(bool v, const char* s) { if (!v) { std::cerr << s << '\n'; std::abort(); } }
int main() {
    wchar_t directory[MAX_PATH], name[MAX_PATH];
    check(GetTempPathW(MAX_PATH, directory) != 0 && GetTempFileNameW(directory, L"mqs", 0, name) != 0, "temporary path");
    const std::wstring path(name);
    AppSettings original{AppLanguage::English, L"mqtt://localhost:1883", L"client", {{L" \"quoted\" 中文 ", true}}, 900, 600, 380};
    check(SaveAppSettings(original, path), "initial atomic save");
    auto loaded = LoadAppSettings(AppLanguage::Chinese, path);
    check(loaded.server_uri == original.server_uri && loaded.subscriptions.size() == 1 &&
        loaded.subscriptions[0].topic == original.subscriptions[0].topic && loaded.subscriptions[0].active,
        "INI exact round trip");
    check(loaded.subscription_panel_width == 380, "splitter width round trip");
    auto changed = original; changed.client_id = L"changed";
    check(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY), "make replacement target read only");
    check(!SaveAppSettings(changed, path), "replacement failure visible");
    check(LoadAppSettings(AppLanguage::English, path).client_id == original.client_id, "old file preserved on replacement failure");
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    changed.subscriptions.resize(257);
    check(!SaveAppSettings(changed, path), "excess subscriptions rejected without truncating file");
    check(LoadAppSettings(AppLanguage::English, path).client_id == original.client_id, "old file preserved on validation failure");
    check(!SaveAppSettings(original, path + L"\\missing\\settings.ini"), "creation failure visible");
    check(SaveAppSettings(original, path), "subsequent save recovers");
    changed = original;
    changed.subscriptions.push_back({L"pending-delete", false, true});
    changed.subscriptions.push_back({L"inactive", false});
    check(SaveAppSettings(changed, path), "save excludes pending deletion");
    loaded = LoadAppSettings(AppLanguage::English, path);
    check(loaded.subscriptions.size() == 2 && loaded.subscriptions[1].topic == L"inactive" &&
        !loaded.subscriptions[1].active, "pending deletion stays deleted after INI reload");
    check(WritePrivateProfileStringW(L"Window", L"SubscriptionPanelWidth", nullptr, path.c_str()),
        "remove splitter key for legacy settings");
    check(LoadAppSettings(AppLanguage::English, path).subscription_panel_width == 0,
        "legacy settings use equal split");
    check(WritePrivateProfileStringW(L"Window", L"SubscriptionPanelWidth", L"-1", path.c_str()),
        "write invalid splitter width");
    check(LoadAppSettings(AppLanguage::English, path).subscription_panel_width == 0,
        "negative splitter width uses default");
    DeleteFileW(path.c_str());
    std::cout << "Windows settings atomic save tests passed\n";
}
