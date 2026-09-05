#include "settings_document.hpp"
#include "mqtt/mqtt_endpoint.hpp"
#include "mqtt/mqtt_topic.hpp"

#include <codecvt>
#include <locale>
#include <map>
#include <set>
#include <type_traits>

namespace win32mqtt {
namespace {
using Utf8Codec = std::conditional_t<sizeof(wchar_t) == 2,
    std::codecvt_utf8_utf16<wchar_t>, std::codecvt_utf8<wchar_t>>;

std::wstring_view Trim(std::wstring_view text) {
    const auto first = text.find_first_not_of(L" \t");
    if (first == text.npos) return {};
    return text.substr(first, text.find_last_not_of(L" \t") - first + 1);
}

bool Number(std::wstring_view text, int maximum, int& value) {
    text = Trim(text);
    if (text.empty()) return false;
    value = 0;
    for (wchar_t c : text) {
        if (c < L'0' || c > L'9' || value > (maximum - (c - L'0')) / 10) return false;
        value = value * 10 + c - L'0';
        if (value > maximum) return false;
    }
    return true;
}
} // namespace

std::optional<std::wstring> SettingsFromUtf8(std::string_view value) {
    if (value.empty()) return std::wstring{};
    try {
        std::wstring_convert<Utf8Codec> converter;
        auto result = converter.from_bytes(value.data(), value.data() + value.size());
        if (converter.converted() != value.size()) return std::nullopt;
        for (std::size_t i = 0; i < result.size(); ++i) {
            auto code = static_cast<std::uint32_t>(result[i]);
            if constexpr (sizeof(wchar_t) == 2) {
                if (code >= 0xd800 && code <= 0xdbff) {
                    if (++i == result.size()) return std::nullopt;
                    const auto low = static_cast<std::uint32_t>(result[i]);
                    if (low < 0xdc00 || low > 0xdfff) return std::nullopt;
                    continue;
                }
            }
            if ((code >= 0xd800 && code <= 0xdfff) || code > 0x10ffff) return std::nullopt;
        }
        // Round-trip also rejects noncanonical UTF-8.
        if (converter.to_bytes(result) != value) return std::nullopt;
        return result;
    } catch (const std::range_error&) { return std::nullopt; }
}

std::optional<std::string> SettingsToUtf8(std::wstring_view value) {
    if (value.empty()) return std::string{};
    try {
        std::wstring_convert<Utf8Codec> converter;
        auto result = converter.to_bytes(value.data(), value.data() + value.size());
        if (converter.converted() != value.size()) return std::nullopt;
        return result;
    } catch (const std::range_error&) { return std::nullopt; }
}

bool ValidSettingsText(std::wstring_view value) {
    for (std::size_t i = 0; i < value.size(); ++i) {
        auto code = static_cast<std::uint32_t>(value[i]);
        if constexpr (sizeof(wchar_t) == 2) {
            if (code >= 0xd800 && code <= 0xdbff) {
                if (++i == value.size()) return false;
                auto low = static_cast<std::uint32_t>(value[i]);
                if (low < 0xdc00 || low > 0xdfff) return false;
                code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
            }
        }
        if (code < 0x20 || (code >= 0x7f && code <= 0x9f) || code > 0x10ffff ||
            (code >= 0xd800 && code <= 0xdfff) || code == 0xfeff ||
            code == 0x2028 || code == 0x2029) return false;
    }
    const auto utf8 = SettingsToUtf8(value);
    return utf8 && utf8->size() <= 4088;
}

bool ValidSettingsServerUri(std::wstring_view value) {
    if (!ValidSettingsText(value)) return false;
    if (value.empty()) return true;
    if (value.find(L' ') != value.npos) return false;
    const auto utf8 = SettingsToUtf8(value);
    return utf8 && ParseMqttEndpoint(*utf8).Succeeded();
}

std::wstring ValidateSettings(const AppSettings& settings) {
    if (settings.language != AppLanguage::Chinese && settings.language != AppLanguage::English)
        return L"[Display] Language: expected Chinese or English";
    if (!ValidSettingsServerUri(settings.server_uri))
        return L"[Connection] ServerUri: expected an empty value or mqtt://host[:port] / mqtts://host[:port]";
    if (!ValidSettingsText(settings.client_id))
        return L"[Connection] ClientId: invalid Unicode, control characters, or more than 4088 UTF-8 bytes";
    for (const auto& entry : {std::pair{L"Width", settings.window_width},
                             std::pair{L"Height", settings.window_height},
                             std::pair{L"SubscriptionPanelWidth", settings.subscription_panel_width}}) {
        if (entry.second < 0 || entry.second > 32767)
            return std::wstring(L"[Window] ") + entry.first + L": expected 0..32767";
    }
    if (settings.subscriptions.size() > SubscriptionCatalog::MaxSubscriptions)
        return L"[Subscriptions] Count: expected 0..256";
    std::set<std::wstring> topics;
    for (std::size_t i = 0; i < settings.subscriptions.size(); ++i) {
        const auto& topic = settings.subscriptions[i].topic;
        const auto key = L"[Subscriptions] Topic" + std::to_wstring(i);
        if (!ValidSettingsText(topic) || !IsValidSubscriptionFilter(topic))
            return key + L": invalid subscription filter, control characters, or more than 4088 UTF-8 bytes";
        if (!topics.insert(topic).second) return key + L": duplicate topic";
    }
    return {};
}

std::wstring ParseSettingsDocument(std::wstring_view text, AppSettings& settings) {
    // A fixed schema catches misspellings, duplicates, and truncated documents.
    std::map<std::wstring, std::wstring> values;
    std::set<std::wstring> sections;
    std::wstring section;
    std::size_t line_number = 0;
    while (!text.empty()) {
        ++line_number;
        const auto end = text.find(L'\n');
        auto line = text.substr(0, end);
        text = end == text.npos ? std::wstring_view{} : text.substr(end + 1);
        if (!line.empty() && line.back() == L'\r') line.remove_suffix(1);
        const auto error = [&](const std::wstring& reason) {
            return L"Line " + std::to_wstring(line_number) + L": " + reason;
        };
        for (wchar_t c : line) {
            if ((c < 0x20 && c != L'\t') || c == 0x7f || c == 0xfeff)
                return error(L"unexpected control character or BOM");
        }
        const auto trimmed = Trim(line);
        if (trimmed.empty() || trimmed.front() == L';' || trimmed.front() == L'#') continue;
        if (trimmed.front() == L'[') {
            if (trimmed.back() != L']') return error(L"malformed section header");
            section = Trim(trimmed.substr(1, trimmed.size() - 2));
            if (section != L"Display" && section != L"Connection" &&
                section != L"Window" && section != L"Subscriptions")
                return error(L"unknown section [" + section + L"]");
            if (!sections.insert(section).second) return error(L"duplicate section [" + section + L"]");
            continue;
        }
        const auto equals = line.find(L'=');
        if (section.empty() || equals == line.npos) return error(L"expected key=value inside a section");
        const std::wstring key(Trim(line.substr(0, equals)));
        if (key.empty()) return error(L"empty key");
        // Everything after the first '=' is literal, including quotes and spaces.
        if (!values.emplace(section + L"/" + key, line.substr(equals + 1)).second)
            return error(L"duplicate key [" + section + L"] " + key);
    }
    AppSettings parsed{};
    std::wstring failure;
    const auto take = [&](const std::wstring& key) {
        auto found = values.find(key);
        if (found == values.end()) {
            if (failure.empty()) failure = L"Missing required key: " + key;
            return std::wstring{};
        }
        auto value = std::move(found->second);
        values.erase(found);
        return value;
    };
    const auto number = [&](const std::wstring& key, int maximum, int& result) {
        const auto value = take(key);
        if (!Number(value, maximum, result) && failure.empty())
            failure = key + L": expected an integer in 0.." + std::to_wstring(maximum);
    };
    const auto language = take(L"Display/Language");
    if (language == L"Chinese") parsed.language = AppLanguage::Chinese;
    else if (language == L"English") parsed.language = AppLanguage::English;
    else if (failure.empty()) failure = L"Display/Language: expected Chinese or English";
    parsed.server_uri = take(L"Connection/ServerUri");
    parsed.client_id = take(L"Connection/ClientId");
    number(L"Window/Width", 32767, parsed.window_width);
    number(L"Window/Height", 32767, parsed.window_height);
    number(L"Window/SubscriptionPanelWidth", 32767, parsed.subscription_panel_width);
    int count = 0;
    number(L"Subscriptions/Count", 256, count);
    if (!failure.empty()) return failure;
    for (int i = 0; i < count; ++i) {
        auto topic = take(L"Subscriptions/Topic" + std::to_wstring(i));
        int active = 0;
        number(L"Subscriptions/Active" + std::to_wstring(i), 1, active);
        parsed.subscriptions.push_back({std::move(topic), active == 1});
    }
    if (!failure.empty()) return failure;
    if (!values.empty()) return L"Unknown or out-of-range key: " + values.begin()->first;
    failure = ValidateSettings(parsed);
    if (!failure.empty()) return failure;
    settings = std::move(parsed);
    return {};
}

std::optional<std::string> SerializeSettingsDocument(const AppSettings& settings) {
    if (!ValidateSettings(settings).empty()) return std::nullopt;
    std::wstring text = L"[Display]\r\nLanguage=";
    text += settings.language == AppLanguage::Chinese ? L"Chinese" : L"English";
    text += L"\r\n[Connection]\r\nServerUri=" + settings.server_uri + L"\r\nClientId=" + settings.client_id;
    text += L"\r\n[Window]\r\nWidth=" + std::to_wstring(settings.window_width) +
            L"\r\nHeight=" + std::to_wstring(settings.window_height) +
            L"\r\nSubscriptionPanelWidth=" + std::to_wstring(settings.subscription_panel_width);
    text += L"\r\n[Subscriptions]\r\nCount=" + std::to_wstring(settings.subscriptions.size());
    for (std::size_t i = 0; i < settings.subscriptions.size(); ++i) {
        text += L"\r\nTopic" + std::to_wstring(i) + L"=" + settings.subscriptions[i].topic +
                L"\r\nActive" + std::to_wstring(i) + (settings.subscriptions[i].active ? L"=1" : L"=0");
    }
    text += L"\r\n";
    auto bytes = SettingsToUtf8(text);
    if (!bytes || bytes->size() > MaxSettingsFileBytes) return std::nullopt;
    return bytes;
}
} // namespace win32mqtt
