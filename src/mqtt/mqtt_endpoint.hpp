#pragma once

#include <string>
#include <string_view>

namespace win32mqtt {

struct MqttEndpoint {
    std::string host;
    std::string port;
    bool secure{false};
};

enum class MqttEndpointError {
    None,
    SchemeRequired,
    AuthorityOnly,
    InvalidIpv6Host,
    InvalidHostOrPort,
    Ipv6BracketsRequired,
    HostAndPortRequired,
    PortMustBeNumeric,
    PortOutOfRange,
};

struct MqttEndpointParseResult {
    MqttEndpoint endpoint;
    MqttEndpointError error;

    MqttEndpointParseResult() noexcept : error(MqttEndpointError::None) {}

    bool Succeeded() const noexcept {
        return error == MqttEndpointError::None;
    }
};

MqttEndpointParseResult ParseMqttEndpoint(std::string_view uri);
std::string FormatMqttEndpointUri(const MqttEndpoint& endpoint);
std::string_view MqttEndpointErrorMessage(MqttEndpointError error);

} // namespace win32mqtt
