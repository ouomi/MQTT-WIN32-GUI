#include "app_settings.h"
#include "../settings_document.hpp"
#include "../settings_autosave.hpp"

#include <windows.h>
#include <vector>

namespace win32mqtt {
namespace {
constexpr wchar_t kSettingsFileName[] = L"WIN32-MQTT.ini";

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

AppSettingsLoadResult LoadAppSettings(AppLanguage fallback_language, const std::wstring& settings_path) {
    AppSettingsLoadResult result{};
    result.settings.language = fallback_language;
    result.path = settings_path.empty() ? SettingsFilePath() : settings_path;
    if (result.path.empty()) {
        result.error = L"Cannot determine the configuration file path";
        return result;
    }
    HANDLE file = CreateFileW(result.path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND) {
            result.settings.client_id = GenerateDefaultClientId();
        } else {
            result.error = L"Cannot open configuration file (Windows error " + std::to_wstring(error) + L")";
        }
        return result;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
        size.QuadPart > static_cast<LONGLONG>(MaxSettingsFileBytes)) {
        CloseHandle(file);
        result.error = L"Configuration must be nonempty and no larger than 4 MiB";
        return result;
    }
    std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const bool read_ok = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) &&
                         read == bytes.size();
    CloseHandle(file);
    if (!read_ok) {
        result.error = L"Cannot read the complete configuration file";
        return result;
    }
    std::string_view content(bytes);
    if (content.substr(0, 3) == "\xef\xbb\xbf") content.remove_prefix(3);
    const auto text = SettingsFromUtf8(content);
    if (!text) {
        result.error = L"Configuration must use valid UTF-8 text (optional UTF-8 BOM)";
        return result;
    }
    result.error = ParseSettingsDocument(*text, result.settings);
    return result;
}

bool SaveAppSettings(const AppSettings& input, const std::wstring& settings_path) {
    const auto text = SerializeSettingsDocument(PersistentSettings(input));
    const std::wstring path = settings_path.empty() ? SettingsFilePath() : settings_path;
    if (!text || path.empty()) return false;
    // Validate and serialize the complete UTF-8 document before replacing the file.
    const std::wstring temporary = path + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const DWORD bytes = static_cast<DWORD>(text->size());
    DWORD written = 0;
    bool ok = WriteFile(file, text->data(), bytes, &written, nullptr) && written == bytes;
    if (ok) ok = FlushFileBuffers(file) != FALSE;
    if (!CloseHandle(file)) ok = false;
    if (ok) ok = MoveFileExW(temporary.c_str(), path.c_str(),
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!ok) DeleteFileW(temporary.c_str());
    return ok;
}

} // namespace win32mqtt
