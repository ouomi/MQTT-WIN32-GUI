#include "mqtt/mqtt_tls_identity.hpp"
#include <openssl/err.h>
#include <cstdlib>
#include <cstring>
#include <iostream>

using namespace win32mqtt;
static void Check(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; ERR_print_errors_fp(stderr); std::abort(); }
}

static void Handshake(SSL_CTX* server_context, X509* certificate, const MqttEndpoint& endpoint,
                      const char* expected_sni, long expected_verify, bool trusted = true) {
    auto* context = SSL_CTX_new(TLS_client_method());
    Check(context != nullptr, "client context");
    SSL_CTX_set_verify(context, SSL_VERIFY_PEER, nullptr);
    if (trusted) Check(X509_STORE_add_cert(SSL_CTX_get_cert_store(context), certificate) == 1, "trust fixture");
    auto* client = SSL_new(context);
    auto* server = SSL_new(server_context);
    Check(client && server && ConfigureTlsIdentity(client, endpoint), "configure production TLS identity");
    BIO *client_wire, *server_wire;
    Check(BIO_new_bio_pair(&client_wire, 0, &server_wire, 0) == 1, "TLS wire");
    SSL_set_bio(client, client_wire, client_wire);
    SSL_set_bio(server, server_wire, server_wire);
    SSL_set_connect_state(client);
    SSL_set_accept_state(server);
    bool failed = false;
    for (int i = 0; i < 100 && !failed &&
         (!SSL_is_init_finished(client) || !SSL_is_init_finished(server)); ++i) {
        for (auto* peer : {client, server}) {
            const int result = SSL_do_handshake(peer);
            if (result == 1) continue;
            const int error = SSL_get_error(peer, result);
            if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) {
                failed = true;
                break;
            }
        }
    }
    Check(SSL_get_verify_result(client) == expected_verify, "certificate verification result");
    if (expected_verify == X509_V_OK)
        Check(!failed && SSL_is_init_finished(client) && SSL_is_init_finished(server), "verified handshake");
    else Check(failed && !SSL_is_init_finished(client), "bad identity or untrusted issuer rejected");
    const char* actual_sni = SSL_get_servername(server, TLSEXT_NAMETYPE_host_name);
    Check(expected_sni ? actual_sni && std::strcmp(actual_sni, expected_sni) == 0 : !actual_sni,
          "server receives intended SNI");
    SSL_free(client); SSL_free(server); SSL_CTX_free(context);
    ERR_clear_error();
}

int main() {
    for (const auto* invalid : {"192.0.2.1", "::1", "https://broker.example.com", "broker.example.com:8883",
                               "broker/name", "*.example.com", ".example.com", "a..b", "-a.example", "a-.example",
                               "broker.example.com.", "a b", "中文.example"})
        Check(!ValidTlsServerName(invalid), "invalid SNI rejected");
    Check(!ValidTlsServerName(std::string(64, 'a') + ".example"), "long DNS label rejected");
    Check(!ValidTlsServerName(std::string("broker\0.example", 15)), "embedded NUL rejected");
    Check(ValidTlsServerName("") && ValidTlsServerName("broker.example.com") &&
          ValidTlsServerName("xn--fiqs8s.example"), "optional ASCII and Punycode names");

    auto* key_context = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    EVP_PKEY* key = nullptr;
    auto* certificate = X509_new();
    auto* server_context = SSL_CTX_new(TLS_server_method());
    Check(key_context && certificate && server_context, "fixture allocation");
    Check(EVP_PKEY_keygen_init(key_context) > 0 && EVP_PKEY_CTX_set_rsa_keygen_bits(key_context, 2048) > 0 &&
          EVP_PKEY_keygen(key_context, &key) > 0, "fixture key");
    X509_set_version(certificate, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(certificate), 1);
    X509_gmtime_adj(X509_getm_notBefore(certificate), -60);
    X509_gmtime_adj(X509_getm_notAfter(certificate), 3600);
    X509_set_pubkey(certificate, key);
    X509_NAME_add_entry_by_txt(X509_get_subject_name(certificate), "CN", MBSTRING_ASC,
                              reinterpret_cast<const unsigned char*>("broker.example.com"), -1, -1, 0);
    X509_set_issuer_name(certificate, X509_get_subject_name(certificate));
    auto* san = X509V3_EXT_conf_nid(nullptr, nullptr, NID_subject_alt_name,
        const_cast<char*>("DNS:broker.example.com,IP:192.0.2.1,IP:::1"));
    Check(san && X509_add_ext(certificate, san, -1) == 1, "fixture SANs");
    X509_EXTENSION_free(san);
    Check(X509_sign(certificate, key, EVP_sha256()) > 0 &&
          SSL_CTX_use_certificate(server_context, certificate) == 1 &&
          SSL_CTX_use_PrivateKey(server_context, key) == 1, "fixture certificate");

    // Different destination IP, certificate contains only the override's DNS identity.
    Handshake(server_context, certificate, {"203.0.113.10", "8883", true, "broker.example.com"},
              "broker.example.com", X509_V_OK);
    Handshake(server_context, certificate, {"2001:db8::1", "8883", true, "broker.example.com"},
              "broker.example.com", X509_V_OK);
    Handshake(server_context, certificate, {"broker.example.com", "8883", true},
              "broker.example.com", X509_V_OK);
    Handshake(server_context, certificate, {"192.0.2.1", "8883", true}, nullptr, X509_V_OK);
    Handshake(server_context, certificate, {"::1", "8883", true}, nullptr, X509_V_OK);
    Handshake(server_context, certificate, {"203.0.113.10", "8883", true}, nullptr, X509_V_ERR_IP_ADDRESS_MISMATCH);
    Handshake(server_context, certificate, {"203.0.113.10", "8883", true, "wrong.example.com"},
              "wrong.example.com", X509_V_ERR_HOSTNAME_MISMATCH);
    Handshake(server_context, certificate, {"203.0.113.10", "8883", true, "broker.example.com"},
              "broker.example.com", X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT, false);
    SSL_CTX_free(server_context); X509_free(certificate); EVP_PKEY_free(key); EVP_PKEY_CTX_free(key_context);
    std::cout << "TLS SNI and certificate identity tests passed\n";
}
