#include "app_settings.h"
#include "../settings_codec.hpp"
#include "../settings_autosave.hpp"

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
constexpr UINT kMaxSavedSubscriptions = SubscriptionCatalog::MaxSubscriptions;
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

std::wstring ReadIniValue(const std::wstring& path, const wchar_t* section,
                          const wchar_t* key) {
    std::vector<wchar_t> value(kMaximumIniValueLength);
    const DWORD length = GetPrivateProfileStringW(section, key, L"", value.data(),
                                                  static_cast<DWORD>(value.size()), path.c_str());
    std::wstring result(value.data(), length);
    if ((section == kConnectionSection || (section == kSubscriptionsSection && key[0] == L'T')) &&
        GetPrivateProfileIntW(L"Format", L"HexValues", 0, path.c_str()) == 1) {
        const auto decoded = DecodeSetting(result);
        return decoded.value_or(L"");
    }
    return result;
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

AppSettings LoadAppSettings(AppLanguage fallback_language, const std::wstring& settings_path) {
    AppSettings settings{fallback_language, {}, {}, {}, 0, 0};
    const std::wstring path = settings_path.empty() ? SettingsFilePath() : settings_path;
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

bool SaveAppSettings(const AppSettings& input, const std::wstring& settings_path) {
    const AppSettings settings = PersistentSettings(input);
    const std::wstring path = settings_path.empty() ? SettingsFilePath() : settings_path;
    if (path.empty() || settings.subscriptions.size() > kMaxSavedSubscriptions) return false;
    // Serialize the entire UTF-16 file before touching the existing settings.
    // Encode INI-sensitive values without losing quotes or whitespace.
    const auto valid = [](const std::wstring& value) {
        return value.find(L'\0') == std::wstring::npos && value.size() * 8 < kMaximumIniValueLength - 1;
    };
    if (!valid(settings.server_uri) || !valid(settings.client_id)) return false;
    std::wstring text = L"\ufeff[Format]\r\nHexValues=1\r\n[Display]\r\nLanguage=";
    text += settings.language == AppLanguage::Chinese ? kChineseLanguageValue : kEnglishLanguageValue;
    text += L"\r\n[Connection]\r\nServerUri=" + EncodeSetting(settings.server_uri) + L"\r\nClientId=" + EncodeSetting(settings.client_id);
    text += L"\r\n[Window]\r\nWidth=" + std::to_wstring(settings.window_width) +
            L"\r\nHeight=" + std::to_wstring(settings.window_height);
    text += L"\r\n[Subscriptions]\r\nCount=" + std::to_wstring(settings.subscriptions.size());
    for (std::size_t i = 0; i < settings.subscriptions.size(); ++i) {
        const auto& r = settings.subscriptions[i];
        if (!valid(r.topic)) return false;
        text += L"\r\nTopic" + std::to_wstring(i) + L"=" + EncodeSetting(r.topic) +
                L"\r\nActive" + std::to_wstring(i) + (r.active ? L"=1" : L"=0");
    }
    text += L"\r\n";
    const std::wstring temporary = path + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const DWORD bytes = static_cast<DWORD>(text.size() * sizeof(wchar_t));
    DWORD written = 0;
    bool ok = WriteFile(file, text.data(), bytes, &written, nullptr) && written == bytes;
    if (ok) ok = FlushFileBuffers(file) != FALSE;
    if (!CloseHandle(file)) ok = false;
    if (ok) ok = MoveFileExW(temporary.c_str(), path.c_str(),
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!ok) DeleteFileW(temporary.c_str());
    return ok;
}

} // namespace win32mqtt
