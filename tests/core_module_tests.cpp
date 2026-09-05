#include <iostream>
#include <string>
#include <vector>

#include "mqtt/mqtt_endpoint.hpp"
#include "mqtt/mqtt_topic.hpp"
#include "subscription_catalog.hpp"
#include "settings_codec.hpp"

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

void TestEndpointParsing() {
    win32mqtt::MqttEndpointParseResult result = win32mqtt::ParseMqttEndpoint("mqtt://broker.example");
    Check(result.Succeeded(), "plain MQTT endpoint parses");
    Check(result.endpoint.host == "broker.example", "plain MQTT host is retained");
    Check(result.endpoint.port == "1883", "plain MQTT default port is applied");
    Check(!result.endpoint.secure, "plain MQTT endpoint is not secure");

    result = win32mqtt::ParseMqttEndpoint("mqtts://[2001:db8::1]:9443");
    Check(result.Succeeded(), "bracketed IPv6 endpoint parses");
    Check(result.endpoint.host == "2001:db8::1", "IPv6 brackets are removed from the host");
    Check(result.endpoint.port == "9443", "explicit port is retained");
    Check(result.endpoint.secure, "mqtts endpoint is secure");
    Check(win32mqtt::FormatMqttEndpointUri(result.endpoint) == "mqtts://[2001:db8::1]:9443",
          "parsed endpoint formats to a canonical URI");

    Check(win32mqtt::ParseMqttEndpoint("http://broker.example").error ==
              win32mqtt::MqttEndpointError::SchemeRequired,
          "non-MQTT scheme is rejected");
    Check(win32mqtt::ParseMqttEndpoint("mqtt://broker.example/path").error ==
              win32mqtt::MqttEndpointError::AuthorityOnly,
          "URI paths are rejected");
    Check(win32mqtt::ParseMqttEndpoint("mqtt://user@broker.example").error ==
              win32mqtt::MqttEndpointError::AuthorityOnly,
          "URI credentials are rejected");
    Check(win32mqtt::ParseMqttEndpoint("mqtt://2001:db8::1").error ==
              win32mqtt::MqttEndpointError::Ipv6BracketsRequired,
          "unbracketed IPv6 is rejected");
    Check(win32mqtt::ParseMqttEndpoint("mqtt://broker.example:nope").error ==
              win32mqtt::MqttEndpointError::PortMustBeNumeric,
          "non-numeric port is rejected");
    Check(win32mqtt::ParseMqttEndpoint("mqtt://broker.example:65536").error ==
              win32mqtt::MqttEndpointError::PortOutOfRange,
          "out-of-range port is rejected");
}

void TestSubscriptionCatalog() {
    win32mqtt::SubscriptionCatalog catalog;
    Check(catalog.Add(L"sensors/temperature"), "first topic is added");
    Check(catalog.Add(L"sensors/humidity"), "second topic is added");
    Check(!catalog.Add(L"sensors/temperature"), "duplicate topic is rejected");
    Check(catalog.Size() == 2, "duplicate does not change catalog size");
    Check(catalog.Find(L"sensors/humidity") == 1, "topic lookup returns stable order");

    Check(catalog.SetActive(1, true), "topic can be activated");
    Check(catalog.SetActive(0, true), "another topic can be activated");
    Check(catalog.ActiveTopics() ==
              std::vector<std::wstring>{L"sensors/temperature", L"sensors/humidity"},
          "active snapshot follows catalog order");

    Check(catalog.SetActive(0, false), "topic can be deactivated");
    Check(catalog.ActiveTopics() == std::vector<std::wstring>{L"sensors/humidity"},
          "deactivated topic leaves the active snapshot");
    Check(catalog.Remove(0), "topic can be removed");
    Check(catalog.Find(L"sensors/humidity") == 0, "remaining topic shifts with catalog order");
    Check(!catalog.Remove(4), "invalid removal is rejected");

    catalog.Replace({{L"restored/temperature", false}, {L"restored/humidity", true},
                     {L"restored/temperature", true}, {L"", true}});
    Check(catalog.Size() == 2, "restore ignores empty and duplicate topics");
    Check(catalog.At(1)->active, "restore retains the active state");
    Check(catalog.Snapshot().at(0).topic == L"restored/temperature",
          "restore retains topic order");
}

void TestTopicValidation() {
    using win32mqtt::IsValidPublishTopic;
    using win32mqtt::IsValidSubscriptionFilter;
    for (const auto* topic : {"a", "/", "a//b", " a ", "$SYS/status", "\xe4\xb8\xad\xe6\x96\x87"}) {
        Check(IsValidPublishTopic(topic) && IsValidSubscriptionFilter(topic), "concrete topics accepted");
    }
    for (const auto* filter : {"#", "+", "/+", "a/+/#", "+/+/", "a//#"}) {
        Check(IsValidSubscriptionFilter(filter), "whole-level wildcard filters accepted");
        Check(!IsValidPublishTopic(filter), "wildcard filters cannot be published");
    }
    for (const auto* filter : {"", "a+", "a/#/b", "a/b#", "a/+b", "##", "a/++"}) {
        Check(!IsValidSubscriptionFilter(filter), "invalid wildcard placement rejected");
    }
    Check(!IsValidPublishTopic(std::string("a\0b", 3)), "embedded null rejected");
    for (const std::string& topic : {std::string("\xc0\x80"), std::string("\xed\xa0\x80"),
                                    std::string("\xf4\x90\x80\x80"), std::string("\xe4\xb8"),
                                    std::string("\x80"), std::string("\xc2x")}) {
        Check(!IsValidPublishTopic(topic) && !IsValidSubscriptionFilter(topic), "malformed UTF-8 rejected");
    }
    Check(IsValidPublishTopic(std::string(65535, 'a')), "maximum topic byte length accepted");
    Check(!IsValidPublishTopic(std::string(65536, 'a')), "oversize topic rejected");
    Check(IsValidPublishTopic(std::wstring(21845, L'\u4e2d')), "UTF-8 length at 65535 bytes accepted");
    Check(!IsValidPublishTopic(std::wstring(21846, L'\u4e2d')), "wide topic checked by UTF-8 bytes");
    Check(IsValidPublishTopic(L"\U0001f600"), "supplementary Unicode topic accepted");
    Check(!IsValidPublishTopic(std::wstring(1, static_cast<wchar_t>(0xd800))), "lone surrogate rejected");
    Check(!IsValidPublishTopic(std::wstring(L"a\0b", 3)), "wide embedded null rejected");
    Check(win32mqtt::topic_detail::Valid(std::u16string_view(u"\U0001f600/+"), true),
          "UTF-16 surrogate pair decoded on native platform");
    Check(!win32mqtt::topic_detail::Valid(std::u16string_view(u"\U0001f600/+"), false),
          "UTF-16 wildcard rejected for publish");
    win32mqtt::SubscriptionCatalog catalog;
    Check(catalog.Add(L"sensors/+") && !catalog.Add(L"sensors/bad+"), "catalog validates filters");
    catalog.Replace({{L"sensors/#", true}, {L"sensors/#/bad", true}, {L"sensors/+", false}});
    Check(catalog.Size() == 2 && catalog.ActiveTopics() == std::vector<std::wstring>{L"sensors/#"},
          "restored invalid filters excluded");
}

} // namespace

int main() {
    win32mqtt::SubscriptionCatalog bounded;
    for (int i = 0; i < 256; ++i) Check(bounded.Add(std::to_wstring(i)), "catalog accepts capacity");
    Check(!bounded.Add(L"overflow") && bounded.Size() == 256, "catalog rejects 257th entry");
    const std::wstring sensitive = L" \"quoted\"\t\r\n;= 中文 ";
    Check(win32mqtt::DecodeSetting(win32mqtt::EncodeSetting(sensitive)) == sensitive, "INI values round trip losslessly");
    Check(!win32mqtt::DecodeSetting(L"0000000z"), "malformed encoding rejected");
    TestEndpointParsing();
    TestSubscriptionCatalog();
    TestTopicValidation();
    if (failures == 0) {
        std::cout << "All core module tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
