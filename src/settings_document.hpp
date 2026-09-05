#pragma once

#include "win32/app_settings.h"
#include <optional>
#include <string_view>

namespace win32mqtt {

constexpr std::size_t MaxSettingsFileBytes = 4 * 1024 * 1024;
// Empty strings are allowed for connection fields during first-time setup.
bool ValidSettingsText(std::wstring_view value);
bool ValidSettingsServerUri(std::wstring_view value);
std::optional<std::wstring> SettingsFromUtf8(std::string_view value);
std::optional<std::string> SettingsToUtf8(std::wstring_view value);
std::wstring ValidateSettings(const AppSettings& settings);
std::wstring ParseSettingsDocument(std::wstring_view text, AppSettings& settings);
std::optional<std::string> SerializeSettingsDocument(const AppSettings& settings);

} // namespace win32mqtt
