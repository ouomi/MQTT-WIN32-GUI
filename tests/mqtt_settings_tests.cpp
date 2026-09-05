#include "win32/app_settings.h"
#include <windows.h>
#include <cstdlib>
#include <iostream>
using namespace win32mqtt;
static void check(bool v, const char* s) { if (!v) { std::cerr << s << '\n'; std::abort(); } }
static AppSettings load(AppLanguage language, const std::wstring& path) {
    const auto result = LoadAppSettings(language, path);
    check(result.Succeeded(), "valid file loads");
    return result.settings;
}
static std::string read_bytes(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    check(file != INVALID_HANDLE_VALUE, "open file for inspection");
    std::string bytes(GetFileSize(file, nullptr), '\0');
    DWORD read = 0;
    check(ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) &&
          read == bytes.size(), "read entire file");
    CloseHandle(file);
    return bytes;
}
static void write_bytes(const std::wstring& path, const std::string& bytes) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    check(file != INVALID_HANDLE_VALUE, "open test fixture");
    DWORD written = 0;
    check(WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
          written == bytes.size(), "write test fixture");
    CloseHandle(file);
}
int main() {
    wchar_t directory[MAX_PATH], name[MAX_PATH];
    check(GetTempPathW(MAX_PATH, directory) != 0 && GetTempFileNameW(directory, L"mqs", 0, name) != 0, "temporary path");
    const std::wstring path(name);
    AppSettings original{AppLanguage::English, L"mqtt://localhost:1883", L"client", {{L" \"quoted\" 中文 ", true}}, 900, 600, 380};
    check(SaveAppSettings(original, path), "initial atomic save");
    auto loaded = load(AppLanguage::Chinese, path);
    check(loaded.server_uri == original.server_uri && loaded.subscriptions.size() == 1 &&
        loaded.subscriptions[0].topic == original.subscriptions[0].topic && loaded.subscriptions[0].active,
        "INI exact round trip");
    check(loaded.subscription_panel_width == 380, "splitter width round trip");
    auto changed = original; changed.client_id = L"changed";
    check(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY), "make replacement target read only");
    check(!SaveAppSettings(changed, path), "replacement failure visible");
    check(load(AppLanguage::English, path).client_id == original.client_id, "old file preserved on replacement failure");
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    changed.subscriptions.resize(257);
    check(!SaveAppSettings(changed, path), "excess subscriptions rejected without truncating file");
    check(load(AppLanguage::English, path).client_id == original.client_id, "old file preserved on validation failure");
    check(!SaveAppSettings(original, path + L"\\missing\\settings.ini"), "creation failure visible");
    check(SaveAppSettings(original, path), "subsequent save recovers");
    changed = original;
    changed.subscriptions.push_back({L"pending-delete", false, true});
    changed.subscriptions.push_back({L"inactive", false});
    check(SaveAppSettings(changed, path), "save excludes pending deletion");
    loaded = load(AppLanguage::English, path);
    check(loaded.subscriptions.size() == 2 && loaded.subscriptions[1].topic == L"inactive" &&
        !loaded.subscriptions[1].active, "pending deletion stays deleted after INI reload");
    const auto readable = read_bytes(path);
    check(readable.find("ServerUri=mqtt://localhost:1883") != std::string::npos &&
          readable.find("ClientId=client") != std::string::npos, "saved bytes are readable UTF-8");
    write_bytes(path, "\xef\xbb\xbf" + readable);
    check(load(AppLanguage::Chinese, path).client_id == original.client_id, "UTF-8 BOM accepted");
    write_bytes(path, readable + "Unexpected=1\n");
    const auto invalid = LoadAppSettings(AppLanguage::English, path);
    check(!invalid.Succeeded() && invalid.path == path && !invalid.error.empty(), "invalid config exposes path and reason");
    check(read_bytes(path) == readable + "Unexpected=1\n", "invalid file left untouched");
    write_bytes(path, "\xff\xfe");
    check(!LoadAppSettings(AppLanguage::English, path).Succeeded(), "non-UTF-8 file rejected");
    write_bytes(path, "");
    check(!LoadAppSettings(AppLanguage::English, path).Succeeded(), "empty existing file rejected");
    write_bytes(path, std::string(4 * 1024 * 1024 + 1, 'x'));
    check(!LoadAppSettings(AppLanguage::English, path).Succeeded(), "oversized file rejected");
    check(SaveAppSettings(original, path), "restore valid settings fixture");
    HANDLE locked = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    check(locked != INVALID_HANDLE_VALUE, "lock configuration");
    check(!LoadAppSettings(AppLanguage::English, path).Succeeded(), "unreadable file rejected");
    CloseHandle(locked);
    DeleteFileW(path.c_str());
    const auto missing = LoadAppSettings(AppLanguage::English, path);
    check(missing.Succeeded() && !missing.settings.client_id.empty(), "missing file uses first-run defaults");
    check(GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES, "loading defaults does not write file");
    std::cout << "Windows settings atomic save tests passed\n";
}
