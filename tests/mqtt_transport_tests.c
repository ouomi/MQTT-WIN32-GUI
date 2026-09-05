#include "mqtt.h"
#include <stdio.h>

static int result;
static int retry;
static int calls;
static int requested;
static int writing;

static void check(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

static int transfer(int len, int write_call)
{
    check(++calls == 1, "transport must yield after one attempt");
    check(writing == write_call, "correct operation");
    requested = len;
    return result;
}

#if defined(MQTT_USE_BIO)
static int bio_write(BIO *bio, const char *buf, int len)
{
    (void)buf;
    BIO_clear_retry_flags(bio);
    if (retry == 1) BIO_set_retry_read(bio);
    if (retry == 2) BIO_set_retry_write(bio);
    return transfer(len, 1);
}
static int bio_read(BIO *bio, char *buf, int len)
{
    (void)buf;
    BIO_clear_retry_flags(bio);
    if (retry == 1) BIO_set_retry_read(bio);
    if (retry == 2) BIO_set_retry_write(bio);
    return transfer(len, 0);
}
#else
int mqtt_test_send(int socket, const char *buf, int len, int flags)
{
    (void)socket; (void)buf;
    check(flags == 7, "send flags forwarded");
    return transfer(len, 1);
}
int mqtt_test_recv(int socket, char *buf, int len, int flags)
{
    (void)socket; (void)buf;
    check(flags == 7, "receive flags forwarded");
    return transfer(len, 0);
}
int mqtt_test_socket_error(void) { return retry ? WSAEWOULDBLOCK : 10054; }
#endif

static void run_case(mqtt_pal_socket_handle socket, int write_call, int rv,
                     int retry_flag, size_t size, ssize_t expected)
{
    char buffer[8] = {0};
    ssize_t actual;
    writing = write_call;
    result = rv;
    retry = retry_flag;
    calls = 0;
    requested = 0;
    actual = writing ? mqtt_pal_sendall(socket, buffer, size, 7)
                     : mqtt_pal_recvall(socket, buffer, size, 7);
    check(actual == expected, "transport return value");
    check(calls == (size ? 1 : 0), "zero length does not call transport");
    if (size) check(requested == (size > INT_MAX ? INT_MAX : (int)size), "int length capped");
}

#if defined(MQTT_USE_BIO)
static void test_bio_pair(void)
{
    BIO *writer = NULL;
    BIO *reader = NULL;
    char buffer[8];
    check(BIO_new_bio_pair(&writer, 4, &reader, 4) == 1, "BIO pair allocation");
    check(mqtt_pal_recvall(reader, buffer, sizeof(buffer), 0) == 0, "empty BIO retries");
    check(mqtt_pal_sendall(writer, "abcdefgh", 8, 0) == 4, "real BIO short write");
    check(mqtt_pal_sendall(writer, "efgh", 4, 0) == 0, "full BIO yields");
    check(mqtt_pal_recvall(reader, buffer, sizeof(buffer), 0) == 4 &&
          memcmp(buffer, "abcd", 4) == 0, "real BIO first bytes");
    check(mqtt_pal_sendall(writer, "efgh", 4, 0) == 4, "BIO resumes after draining");
    check(BIO_shutdown_wr(writer) == 1, "close BIO write side");
    check(mqtt_pal_recvall(reader, buffer, sizeof(buffer), 0) == 4 &&
          memcmp(buffer, "efgh", 4) == 0, "deliver remaining bytes before EOF");
    check(mqtt_pal_recvall(reader, buffer, sizeof(buffer), 0) == MQTT_ERROR_SOCKET_ERROR,
          "real BIO EOF is terminal");
    BIO_free(writer);
    BIO_free(reader);
}
#endif

int main(void)
{
    int operation;
#if defined(MQTT_USE_BIO)
    BIO_METHOD *method = BIO_meth_new(BIO_TYPE_SOURCE_SINK, "scripted transport");
    BIO *socket;
    check(method != NULL, "BIO method allocation");
    check(BIO_meth_set_write(method, bio_write) == 1, "BIO write callback");
    check(BIO_meth_set_read(method, bio_read) == 1, "BIO read callback");
    socket = BIO_new(method);
    check(socket != NULL, "BIO allocation");
    BIO_set_init(socket, 1);
#else
    mqtt_pal_socket_handle socket = 1;
#endif
    for (operation = 0; operation <= 1; ++operation) {
        run_case(socket, operation, 8, 0, 8, 8);
        run_case(socket, operation, 3, 0, 8, 3);
        run_case(socket, operation, -1, 1, 8, 0);
        run_case(socket, operation, -1, 2, 8, 0);
        run_case(socket, operation, 0, 0, 8, MQTT_ERROR_SOCKET_ERROR);
        run_case(socket, operation, -1, 0, 8, MQTT_ERROR_SOCKET_ERROR);
        run_case(socket, operation, -1, 0, 0, 0);
        run_case(socket, operation, -1, 1, (size_t)INT_MAX + 1, 0);
#if defined(MQTT_USE_BIO)
        run_case(socket, operation, 0, 1, 8, 0);
        run_case(socket, operation, 0, 2, 8, 0);
#endif
    }
#if defined(MQTT_USE_BIO)
    BIO_free(socket);
    BIO_meth_free(method);
    test_bio_pair();
#endif
    puts("Nonblocking transport tests passed");
    return EXIT_SUCCESS;
}
