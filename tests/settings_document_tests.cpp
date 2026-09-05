#include "settings_document.hpp"
#include "settings_autosave.hpp"
#include <cstdlib>
#include <iostream>

using namespace win32mqtt;
static void check(bool value, const char* message) {
    if (!value) { std::cerr << message << '\n'; std::abort(); }
}

int main() {
    AppSettings settings{AppLanguage::English, L"mqtt://localhost:1883", L" client=\"中文\"\\id ",
        {{L" \"设备\"/温度;=\\path ", false}, {L"设备/+", true}}, 900, 600, 380};
    const auto bytes = SerializeSettingsDocument(settings);
    check(bytes.has_value(), "serialize valid settings");
    check(bytes->find("ServerUri=mqtt://localhost:1883") != std::string::npos,
          "URI remains readable");
    const auto document = SettingsFromUtf8(*bytes);
    check(document && document->find(L"Topic0= \"设备\"/温度;=\\path \r\n") != std::wstring::npos,
          "quotes, spaces, semicolons, equals signs and backslashes are literal");
    AppSettings parsed{};
    check(ParseSettingsDocument(*document, parsed).empty() && SameSettings(settings, parsed),
          "readable document round trip");
    check(ParseSettingsDocument(L"; comment\n# comment\n\n" + *document, parsed).empty(),
          "blank lines and whole-line comments accepted");
    auto lf = *document;
    for (auto pos = lf.find(L'\r'); pos != lf.npos; pos = lf.find(L'\r')) lf.erase(pos, 1);
    check(ParseSettingsDocument(lf, parsed).empty(), "LF line endings accepted");

    const auto reject = [&](std::wstring text, const char* reason) {
        const auto before = parsed;
        check(!ParseSettingsDocument(text, parsed).empty(), reason);
        check(SameSettings(before, parsed), "invalid document never exposes partial settings");
    };
    const auto replace = [&](std::wstring from, std::wstring to) {
        auto text = *document;
        const auto pos = text.find(from);
        check(pos != text.npos, "fixture replacement found");
        text.replace(pos, from.size(), to);
        return text;
    };
    reject(L"", "empty document rejected");
    reject(L"[Unexpected]\nX=1\n" + *document, "unknown section rejected");
    reject(*document + L"Unknown=1\n", "unknown key rejected");
    reject(*document + L"Count=2\n", "duplicate key rejected");
    reject(*document + L"[Window]\n", "duplicate section rejected");
    reject(replace(L"[Window]", L"[Window"), "malformed section rejected");
    reject(replace(L"Height=600\r\n", L""), "missing required key rejected");
    reject(replace(L"Height=600", L"Height=-1"), "negative dimension rejected");
    reject(replace(L"Height=600", L"Height=999999999999999999999"), "overflow rejected");
    reject(replace(L"Height=600", L"Height=600px"), "numeric suffix rejected");
    reject(replace(L"Active1=1", L"Active1=2"), "nonboolean active flag rejected");
    reject(replace(L"Count=2", L"Count=257"), "excess count rejected");
    reject(replace(L"Count=2", L"Count=1"), "extra indexed keys rejected");
    reject(replace(L"Count=2", L"Count=3"), "missing indexed keys rejected");
    reject(replace(L"Language=English", L"Language=Unknown"), "unknown language rejected");
    reject(replace(L"mqtt://localhost:1883", L"http://localhost"), "invalid URI rejected");
    reject(replace(L"mqtt://localhost:1883", L"mqtt://localhost:99999"), "invalid port rejected");
    reject(replace(L"Topic1=设备/+", L"Topic1=设备/bad+"), "invalid wildcard rejected");
    reject(replace(L"Topic1=设备/+", L"Topic1= \"设备\"/温度;=\\path "), "duplicate topic rejected");
    reject(replace(L"Topic1=设备/+", L"Topic1="), "empty topic rejected");
    reject(*document + std::wstring(1, L'\0'), "embedded NUL rejected");
    reject(replace(L"Topic1=设备/+", L"Topic1=设备/\t"), "control character in value rejected");

    check(!SettingsFromUtf8(std::string("\xc0\xaf", 2)), "overlong UTF-8 rejected");
    check(!SettingsFromUtf8(std::string("\xe4\xb8", 2)), "truncated UTF-8 rejected");
    check(!SettingsFromUtf8(std::string("\xed\xa0\x80", 3)), "UTF-8 encoded surrogate rejected");
    check(!SettingsFromUtf8(std::string("\xf4\x90\x80\x80", 4)), "out-of-range Unicode rejected");
    const std::string emoji = "\xf0\x9f\x98\x80";
    const auto wide_emoji = SettingsFromUtf8(emoji);
    check(wide_emoji && SettingsToUtf8(*wide_emoji) == emoji && ValidSettingsText(*wide_emoji),
          "supplementary Unicode round trips");
    check(!ValidSettingsText(std::wstring(1, static_cast<wchar_t>(0xd800))), "unpaired surrogate rejected");
    check(!ValidSettingsText(std::wstring(4089, L'a')), "oversized value rejected");
    settings.server_uri.clear();
    settings.client_id.clear();
    check(SerializeSettingsDocument(settings).has_value(), "empty connection setup is valid");
    settings.server_uri = L"mqtt://";
    check(!SerializeSettingsDocument(settings), "writer rejects invalid URI");
    settings.server_uri.clear();
    settings.client_id = L"injected\n[Unknown]";
    check(!SerializeSettingsDocument(settings), "writer rejects line injection");
    std::cout << "Settings document tests passed\n";
}
