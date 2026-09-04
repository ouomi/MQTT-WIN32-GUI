#include "app_settings.h"

#include <windows.h>

#include <algorithm>
#include <cwchar>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace win32mqtt {
namespace {

constexpr wchar_t kSettingsFileName[] = L"WIN32-MQTT.ini";
constexpr wchar_t kDisplaySection[] = L"Display";
constexpr wchar_t kConnectionSection[] = L"Connection";
constexpr wchar_t kSubscriptionsSection[] = L"Subscriptions";
constexpr wchar_t kWindowSection[] = L"Window";
constexpr wchar_t kLanguageKey[] = L"Language";
constexpr wchar_t kServerUriKey[] = L"ServerUri";
constexpr wchar_t kClientIdKey[] = L"ClientId";
constexpr wchar_t kWindowWidthKey[] = L"Width";
constexpr wchar_t kWindowHeightKey[] = L"Height";
constexpr wchar_t kSubscriptionCountKey[] = L"Count";
constexpr wchar_t kChineseLanguageValue[] = L"Chinese";
constexpr wchar_t kEnglishLanguageValue[] = L"English";
constexpr UINT kMaxSavedSubscriptions = 256;
constexpr DWORD kMaximumIniValueLength = 32767;

std::wstring SettingsFilePath() {
    std::vector<wchar_t> executable_path(MAX_PATH);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, executable_path.data(),
                                                static_cast<DWORD>(executable_path.size()));
        if (length == 0) {
            return {};
        }
        if (length < executable_path.size() - 1) {
            std::wstring path(executable_path.data(), length);
            const std::wstring::size_type separator = path.find_last_of(L"\\/");
            return separator == std::wstring::npos ? std::wstring(kSettingsFileName)
                                                   : path.substr(0, separator + 1) +
                                                         kSettingsFileName;
        }
        executable_path.resize(executable_path.size() * 2);
    }
}

void EnsureUnicodeIniFile(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    const BYTE utf16le_bom[] = {0xff, 0xfe};
    DWORD bytes_written{};
    WriteFile(file, utf16le_bom, sizeof(utf16le_bom), &bytes_written, nullptr);
    CloseHandle(file);
}

std::wstring ReadIniValue(const std::wstring& path, const wchar_t* section,
                          const wchar_t* key) {
    std::vector<wchar_t> value(kMaximumIniValueLength);
    const DWORD length = GetPrivateProfileStringW(section, key, L"", value.data(),
                                                  static_cast<DWORD>(value.size()), path.c_str());
    return {value.data(), length};
}

int ReadWindowDimension(const std::wstring& path, const wchar_t* key) {
    const UINT value = GetPrivateProfileIntW(kWindowSection, key, 0, path.c_str());
    return value > static_cast<UINT>(std::numeric_limits<int>::max())
               ? 0
               : static_cast<int>(value);
}

std::wstring SubscriptionTopicKey(UINT index) {
    return L"Topic" + std::to_wstring(index);
}

std::wstring SubscriptionActiveKey(UINT index) {
    return L"Active" + std::to_wstring(index);
}

std::wstring GenerateDefaultClientId() {
    FILETIME current_time{};
    GetSystemTimeAsFileTime(&current_time);
    const ULONGLONG timestamp = (static_cast<ULONGLONG>(current_time.dwHighDateTime) << 32) |
                                current_time.dwLowDateTime;
    constexpr wchar_t kHexDigits[] = L"0123456789abcdef";

    std::wstring result = L"mqttwin-";
    for (int shift = 44; shift >= 0; shift -= 4) {
        result.push_back(kHexDigits[(timestamp >> shift) & 0x0f]);
    }
    const DWORD process_id = GetCurrentProcessId();
    for (int shift = 8; shift >= 0; shift -= 4) {
        result.push_back(kHexDigits[(process_id >> shift) & 0x0f]);
    }
    return result;
}

} // namespace

AppSettings LoadAppSettings(AppLanguage fallback_language) {
    AppSettings settings{fallback_language, {}, {}, {}, 0, 0};
    const std::wstring path = SettingsFilePath();
    if (path.empty()) {
        return settings;
    }

    const std::wstring language = ReadIniValue(path, kDisplaySection, kLanguageKey);
    if (_wcsicmp(language.c_str(), kChineseLanguageValue) == 0) {
        settings.language = AppLanguage::Chinese;
    } else if (_wcsicmp(language.c_str(), kEnglishLanguageValue) == 0) {
        settings.language = AppLanguage::English;
    }
    settings.server_uri = ReadIniValue(path, kConnectionSection, kServerUriKey);
    settings.client_id = ReadIniValue(path, kConnectionSection, kClientIdKey);
    if (settings.client_id.empty()) {
        settings.client_id = GenerateDefaultClientId();
    }
    settings.window_width = ReadWindowDimension(path, kWindowWidthKey);
    settings.window_height = ReadWindowDimension(path, kWindowHeightKey);

    const UINT count = std::min(GetPrivateProfileIntW(kSubscriptionsSection,
                                                       kSubscriptionCountKey, 0, path.c_str()),
                                kMaxSavedSubscriptions);
    SubscriptionCatalog catalog;
    for (UINT index = 0; index < count; ++index) {
        std::wstring topic = ReadIniValue(path, kSubscriptionsSection,
                                          SubscriptionTopicKey(index).c_str());
        if (!catalog.Add(std::move(topic))) {
            continue;
        }
        const bool active = GetPrivateProfileIntW(kSubscriptionsSection,
                                                   SubscriptionActiveKey(index).c_str(), 0,
                                                   path.c_str()) != 0;
        catalog.SetActive(catalog.Size() - 1, active);
    }
    settings.subscriptions = catalog.Snapshot();
    return settings;
}

void SaveAppSettings(const AppSettings& settings) {
    const std::wstring path = SettingsFilePath();
    if (path.empty()) {
        return;
    }
    EnsureUnicodeIniFile(path);

    WritePrivateProfileStringW(kDisplaySection, kLanguageKey,
                               settings.language == AppLanguage::Chinese
                                   ? kChineseLanguageValue
                                   : kEnglishLanguageValue,
                               path.c_str());
    WritePrivateProfileStringW(kConnectionSection, kServerUriKey, settings.server_uri.c_str(),
                               path.c_str());
    WritePrivateProfileStringW(kConnectionSection, kClientIdKey, settings.client_id.c_str(),
                               path.c_str());
    WritePrivateProfileStringW(kWindowSection, kWindowWidthKey,
                               std::to_wstring(settings.window_width).c_str(), path.c_str());
    WritePrivateProfileStringW(kWindowSection, kWindowHeightKey,
                               std::to_wstring(settings.window_height).c_str(), path.c_str());

    const std::size_t subscription_count = std::min(settings.subscriptions.size(),
                                                    static_cast<std::size_t>(kMaxSavedSubscriptions));
    WritePrivateProfileStringW(kSubscriptionsSection, nullptr, nullptr, path.c_str());
    WritePrivateProfileStringW(kSubscriptionsSection, kSubscriptionCountKey,
                               std::to_wstring(subscription_count).c_str(), path.c_str());
    for (std::size_t index = 0; index < subscription_count; ++index) {
        const SubscriptionRecord& subscription = settings.subscriptions[index];
        const UINT ini_index = static_cast<UINT>(index);
        WritePrivateProfileStringW(kSubscriptionsSection, SubscriptionTopicKey(ini_index).c_str(),
                                   subscription.topic.c_str(), path.c_str());
        WritePrivateProfileStringW(kSubscriptionsSection, SubscriptionActiveKey(ini_index).c_str(),
                                   subscription.active ? L"1" : L"0", path.c_str());
    }
}

} // namespace win32mqtt
