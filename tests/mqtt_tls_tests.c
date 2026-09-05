/* Real TLS handshake and write-retry regression, using a bounded BIO pair. */
#include "mqtt.h"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <stdio.h>
static void check(int value, const char *message) {
    if (!value) { fprintf(stderr, "%s\n", message); ERR_print_errors_fp(stderr); abort(); }
}
void mqtt_test_mutex_init(mqtt_pal_mutex_t *p) { *p = 0; }
void mqtt_test_mutex_lock(mqtt_pal_mutex_t *p) { check(!*p, "lock"); *p = 1; }
void mqtt_test_mutex_unlock(mqtt_pal_mutex_t *p) { check(*p, "unlock"); *p = 0; }
static int received;
static void published(void **state, struct mqtt_response_publish *p) {
    (void)state; check(p->application_message_size == 1, "TLS payload"); ++received;
}
static void test_plain_bio_duplex(void) {
    BIO *local, *peer;
    struct mqtt_client client;
    union { struct mqtt_queued_message align; uint8_t bytes[2048]; } send;
    uint8_t recv[2048];
    const uint8_t inbound[] = {0x30, 4, 0, 1, 't', 'x'};
    check(BIO_new_bio_pair(&local, 1, &peer, 128) == 1, "plain BIO pair");
    memset(&client, 0, sizeof(client));
    mqtt_init_reconnect(&client, NULL, NULL, published);
    mqtt_reinit(&client, local, send.bytes, sizeof(send.bytes), recv, sizeof(recv));
    client.error = MQTT_OK; client.keep_alive = 600;
    check(mqtt_publish(&client, "t", "body", 4, MQTT_PUBLISH_QOS_0) == MQTT_OK, "plain BIO publish");
    check(win32mqtt_send(&client) == MQTT_OK && win32mqtt_send(&client) == MQTT_OK && BIO_should_write(local),
          "plain BIO write blocked");
    check(BIO_write(peer, inbound, sizeof(inbound)) == sizeof(inbound), "plain peer input");
    check(mqtt_sync(&client) == MQTT_OK && received == 1, "plain BIO reads during write congestion");
    received = 0;
    BIO_free(local); BIO_free(peer);
}
int main(void) {
    EVP_PKEY_CTX *key_context = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    EVP_PKEY *key = NULL;
    X509 *certificate = X509_new();
    SSL_CTX *server_context = SSL_CTX_new(TLS_server_method());
    SSL_CTX *client_context = SSL_CTX_new(TLS_client_method());
    SSL *server, *client_ssl;
    BIO *client_wire, *server_wire, *client_bio;
    struct mqtt_client client;
    union { struct mqtt_queued_message align; uint8_t bytes[16384]; } send;
    uint8_t recv[8192], payload[4000], decrypted[20000];
    const uint8_t inbound[] = {0x30, 4, 0, 1, 't', 'x'};
    size_t total = 0, expected = 0;
    int i, pending = 0;
    test_plain_bio_duplex();
    check(key_context && certificate && server_context && client_context, "TLS allocation");
    check(EVP_PKEY_keygen_init(key_context) > 0 && EVP_PKEY_CTX_set_rsa_keygen_bits(key_context, 2048) > 0 &&
          EVP_PKEY_keygen(key_context, &key) > 0, "test key");
    X509_set_version(certificate, 2); ASN1_INTEGER_set(X509_get_serialNumber(certificate), 1);
    X509_gmtime_adj(X509_getm_notBefore(certificate), -60); X509_gmtime_adj(X509_getm_notAfter(certificate), 3600);
    X509_set_pubkey(certificate, key);
    X509_NAME_add_entry_by_txt(X509_get_subject_name(certificate), "CN", MBSTRING_ASC,
                              (const unsigned char *)"localhost", -1, -1, 0);
    X509_set_issuer_name(certificate, X509_get_subject_name(certificate));
    check(X509_sign(certificate, key, EVP_sha256()) > 0 &&
          SSL_CTX_use_certificate(server_context, certificate) == 1 && SSL_CTX_use_PrivateKey(server_context, key) == 1,
          "test certificate");
    SSL_CTX_set_verify(client_context, SSL_VERIFY_PEER, NULL);
    check(X509_STORE_add_cert(SSL_CTX_get_cert_store(client_context), certificate) == 1, "trust test certificate");
    client_ssl = SSL_new(client_context); server = SSL_new(server_context);
    SSL_set1_host(client_ssl, "localhost");
    SSL_set_mode(client_ssl, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    check(BIO_new_bio_pair(&client_wire, 8192, &server_wire, 8192) == 1, "bounded wire");
    SSL_set_bio(client_ssl, client_wire, client_wire); SSL_set_connect_state(client_ssl);
    SSL_set_bio(server, server_wire, server_wire); SSL_set_accept_state(server);
    client_bio = BIO_new(BIO_f_ssl()); BIO_set_ssl(client_bio, client_ssl, BIO_CLOSE);
    memset(&client, 0, sizeof(client)); memset(payload, 'p', sizeof(payload));
    mqtt_init_reconnect(&client, NULL, NULL, published);
    mqtt_reinit(&client, client_bio, send.bytes, sizeof(send.bytes), recv, sizeof(recv));
    client.error = MQTT_OK; client.keep_alive = 600;
    check(mqtt_publish(&client, "t", payload, sizeof(payload), MQTT_PUBLISH_QOS_0) == MQTT_OK, "queue during handshake");
    check(win32mqtt_send(&client) == MQTT_OK && BIO_should_read(client_bio), "TLS write requests read progress");
    for (i = 0; i < 100 && (!SSL_is_init_finished(server) || !SSL_is_init_finished(client_ssl)); ++i) {
        int n = SSL_do_handshake(server);
        if (n != 1) { int e = SSL_get_error(server, n); check(e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE, "server handshake"); }
        check(mqtt_sync(&client) == MQTT_OK, "TLS WANT_READ handshake progresses through sync");
    }
    check(SSL_is_init_finished(server) && SSL_is_init_finished(client_ssl) && SSL_get_verify_result(client_ssl) == X509_V_OK,
          "real verified TLS handshake completed");
    /* Drain the initial request before saturating the bounded encrypted wire. */
    for (;;) {
        int n = SSL_read(server, decrypted, sizeof(decrypted));
        if (n <= 0) { check(SSL_get_error(server, n) == SSL_ERROR_WANT_READ, "drain handshake publish"); break; }
    }
    mqtt_mq_clean(&client.mq);
    for (i = 0; i < 3; ++i) {
        check(mqtt_publish(&client, "t", payload, sizeof(payload), MQTT_PUBLISH_QOS_0) == MQTT_OK, "queue TLS burst");
        expected += mqtt_mq_get(&client.mq, i)->size;
    }
    check(win32mqtt_send(&client) == MQTT_OK && BIO_should_write(client_bio), "TLS write backpressure");
    for (i = 0; i < mqtt_mq_length(&client.mq); ++i) pending += mqtt_mq_get(&client.mq, i)->sending;
    check(pending == 1, "one retained TLS write");
    check(SSL_write(server, inbound, sizeof(inbound)) == sizeof(inbound), "peer publishes during blocked write");
    check(mqtt_sync(&client) == MQTT_OK && received == 0, "WANT_WRITE preserves required operation");
    mqtt_mq_clean(&client.mq); /* pending write moves; identical bytes and length */
    for (i = 0; i < 100 && (total < expected || !received); ++i) {
        int n = SSL_read(server, decrypted + total, (int)(sizeof(decrypted) - total));
        if (n > 0) total += n;
        else check(SSL_get_error(server, n) == SSL_ERROR_WANT_READ, "peer read during retry");
        check(mqtt_sync(&client) == MQTT_OK, "TLS retry after compaction");
    }
    check(total == expected && received == 1, "both directions complete without duplicated bytes");
    BIO_free_all(client_bio); SSL_free(server); SSL_CTX_free(client_context); SSL_CTX_free(server_context);
    X509_free(certificate); EVP_PKEY_free(key); EVP_PKEY_CTX_free(key_context);
    puts("Real TLS handshake and retry tests passed"); return 0;
}
