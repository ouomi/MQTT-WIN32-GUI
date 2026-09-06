#pragma once

#include "mqtt_endpoint.hpp"
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

namespace win32mqtt {

// The network destination remains endpoint.host. An override selects both the
// virtual TLS server and the identity its trusted certificate must prove.
inline bool ConfigureTlsIdentity(SSL* ssl, const MqttEndpoint& endpoint) {
    if (!ssl || !ValidTlsServerName(endpoint.tls_server_name)) return false;
    const auto& identity = endpoint.tls_server_name.empty() ? endpoint.host : endpoint.tls_server_name;
    if (identity.empty()) return false;
    if (endpoint.tls_server_name.empty() &&
        X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(ssl), identity.c_str()) == 1) {
        // IP literals are verified against IP SANs and are not sent as DNS SNI.
        return true;
    }
    return SSL_set1_host(ssl, identity.c_str()) == 1 &&
           SSL_set_tlsext_host_name(ssl, identity.c_str()) == 1;
}

} // namespace win32mqtt
