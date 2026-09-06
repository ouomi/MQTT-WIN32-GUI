#include "mqtt_endpoint.hpp"

#include <cstdint>

namespace win32mqtt {
namespace {

MqttEndpointParseResult Failure(MqttEndpointError error) {
    MqttEndpointParseResult result;
    result.error = error;
    return result;
}

} // namespace

bool ValidTlsServerName(std::string_view name) {
    if (name.empty()) return true;
    if (name.size() > 253) return false;
    bool only_digits_and_dots = true;
    std::size_t label_length = 0;
    char previous = 0;
    for (char c : name) {
        const bool digit = c >= '0' && c <= '9';
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        if (c == '.') {
            if (!label_length || previous == '-') return false;
            label_length = 0;
        } else {
            if (!digit && !letter && c != '-') return false;
            if (!label_length && c == '-') return false;
            if (++label_length > 63) return false;
            if (!digit) only_digits_and_dots = false;
        }
        previous = c;
    }
    return label_length && previous != '-' && !only_digits_and_dots;
}

MqttEndpointParseResult ParseMqttEndpoint(std::string_view uri) {
    constexpr std::string_view kMqtt = "mqtt://";
    constexpr std::string_view kMqtts = "mqtts://";
    const bool mqtt = uri.compare(0, kMqtt.size(), kMqtt) == 0;
    const bool mqtts = uri.compare(0, kMqtts.size(), kMqtts) == 0;
    if (!mqtt && !mqtts) {
        return Failure(MqttEndpointError::SchemeRequired);
    }

    const std::string_view authority = uri.substr(mqtts ? kMqtts.size() : kMqtt.size());
    if (authority.empty() || authority.find_first_of("/?#@") != std::string_view::npos) {
        return Failure(MqttEndpointError::AuthorityOnly);
    }

    MqttEndpointParseResult result;
    result.endpoint.secure = mqtts;
    result.endpoint.port = mqtts ? "8883" : "1883";

    if (authority.front() == '[') {
        const std::size_t close = authority.find(']');
        if (close == std::string_view::npos || close == 1) {
            return Failure(MqttEndpointError::InvalidIpv6Host);
        }
        result.endpoint.host.assign(authority.substr(1, close - 1));
        if (close + 1 < authority.size()) {
            if (authority[close + 1] != ':') {
                return Failure(MqttEndpointError::InvalidHostOrPort);
            }
            result.endpoint.port.assign(authority.substr(close + 2));
        }
    } else {
        const std::size_t colon = authority.rfind(':');
        if (colon != std::string_view::npos) {
            if (authority.find(':') != colon) {
                return Failure(MqttEndpointError::Ipv6BracketsRequired);
            }
            result.endpoint.host.assign(authority.substr(0, colon));
            result.endpoint.port.assign(authority.substr(colon + 1));
        } else {
            result.endpoint.host.assign(authority);
        }
    }

    if (result.endpoint.host.empty() || result.endpoint.port.empty()) {
        return Failure(MqttEndpointError::HostAndPortRequired);
    }

    std::uint32_t port = 0;
    for (char value : result.endpoint.port) {
        if (value < '0' || value > '9') {
            return Failure(MqttEndpointError::PortMustBeNumeric);
        }
        port = port * 10 + static_cast<std::uint32_t>(value - '0');
        if (port > 65535) {
            return Failure(MqttEndpointError::PortOutOfRange);
        }
    }
    if (port == 0) {
        return Failure(MqttEndpointError::PortOutOfRange);
    }

    return result;
}

std::string FormatMqttEndpointUri(const MqttEndpoint& endpoint) {
    std::string uri = endpoint.secure ? "mqtts://" : "mqtt://";
    const bool ipv6 = endpoint.host.find(':') != std::string::npos;
    if (ipv6) {
        uri += '[';
    }
    uri += endpoint.host;
    if (ipv6) {
        uri += ']';
    }
    uri += ':';
    uri += endpoint.port;
    return uri;
}

std::string_view MqttEndpointErrorMessage(MqttEndpointError error) {
    switch (error) {
    case MqttEndpointError::None: return {};
    case MqttEndpointError::SchemeRequired: return "URI must begin with mqtt:// or mqtts://";
    case MqttEndpointError::AuthorityOnly: return "URI must contain only a host and optional port";
    case MqttEndpointError::InvalidIpv6Host: return "invalid IPv6 host";
    case MqttEndpointError::InvalidHostOrPort: return "invalid host or port";
    case MqttEndpointError::Ipv6BracketsRequired: return "IPv6 hosts must use brackets";
    case MqttEndpointError::HostAndPortRequired: return "host and port are required";
    case MqttEndpointError::PortMustBeNumeric: return "port must be numeric";
    case MqttEndpointError::PortOutOfRange: return "port must be between 1 and 65535";
    }
    return "invalid MQTT endpoint";
}

} // namespace win32mqtt
