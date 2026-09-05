#include "mqtt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned initializations;
static unsigned locks;
static unsigned unlocks;

static void check(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

void mqtt_test_mutex_init(mqtt_pal_mutex_t *mutex)
{
    ++initializations;
    *mutex = 0;
}

void mqtt_test_mutex_lock(mqtt_pal_mutex_t *mutex)
{
    check(*mutex == 0, "unexpected recursive lock");
    *mutex = 1;
    ++locks;
}

void mqtt_test_mutex_unlock(mqtt_pal_mutex_t *mutex)
{
    check(*mutex == 1, "unlock without ownership");
    *mutex = 0;
    ++unlocks;
}

ssize_t mqtt_pal_sendall(mqtt_pal_socket_handle fd, const void *buf, size_t len, int flags)
{
    (void)fd; (void)buf; (void)len; (void)flags;
    abort();
}

ssize_t mqtt_pal_recvall(mqtt_pal_socket_handle fd, void *buf, size_t len, int flags)
{
    (void)fd; (void)buf; (void)len; (void)flags;
    abort();
}

static void published(void **state, struct mqtt_response_publish *message)
{
    (void)state; (void)message;
    abort();
}

static enum MQTTErrors connect_client(struct mqtt_client *client, const char *id)
{
    return mqtt_connect(client, id, NULL, NULL, 0, NULL, NULL,
                        MQTT_CONNECT_CLEAN_SESSION, 60);
}

int main(int argc, char **argv)
{
    struct mqtt_client client = {0};
    struct mqtt_client ordinary_client = {0};
    /* Queue metadata requires alignment at the end of the send buffer. */
    union {
        struct mqtt_queued_message alignment;
        uint8_t bytes[1024];
    } send_buffer;
    uint8_t recv_buffer[1024];
    char oversized_id[2048];
    unsigned iteration;
    (void)argv;

    mqtt_init_reconnect(&client, NULL, NULL, published);
    check(initializations == 1 && locks == 0 && unlocks == 0,
          "reconnect initialization creates an unlocked mutex");
    check(client.publish_response_callback == published, "callback installed");

    /* An optional negative run reproduces the original session call sequence. */
    if (argc > 1) {
        mqtt_reinit(&client, 1, send_buffer.bytes, sizeof(send_buffer.bytes),
                    recv_buffer, sizeof(recv_buffer));
        (void)connect_client(&client, "unlocked");
        check(0, "lock tracker should reject the original sequence");
    }

    memset(oversized_id, 'a', sizeof(oversized_id) - 1);
    oversized_id[sizeof(oversized_id) - 1] = '\0';
    for (iteration = 0; iteration < 3; ++iteration) {
        enum MQTTErrors result;
        mqtt_reinit(&client, 1, send_buffer.bytes, sizeof(send_buffer.bytes),
                    recv_buffer, sizeof(recv_buffer));
        check(client.mutex == 0 && initializations == 1, "reinit does not reinitialize or lock mutex");

        /* Cancellation before CONNECT leaves no acquired lock to release. */
        check(locks == unlocks, "balanced before connecting");
        MQTT_PAL_MUTEX_LOCK(&client.mutex);
        if (iteration == 0) {
            result = mqtt_connect(&client, NULL, NULL, NULL, 0, NULL, NULL, 0, 60);
            check(result == MQTT_ERROR_CLEAN_SESSION_IS_REQUIRED, "invalid CONNECT fails");
        } else if (iteration == 1) {
            result = connect_client(&client, oversized_id);
            check(result == MQTT_ERROR_SEND_BUFFER_IS_FULL, "full buffer fails");
        } else {
            result = connect_client(&client, "valid");
            check(result == MQTT_OK, "CONNECT succeeds after failures");
            check(mqtt_mq_find(&client.mq, MQTT_CONTROL_CONNECT, NULL) != NULL,
                  "CONNECT queued");
        }
        check(client.mutex == 0 && locks == unlocks, "CONNECT releases lock on every path");
    }

    /* Protect the legacy mqtt_init -> mqtt_connect contract as well. */
    check(mqtt_init(&ordinary_client, 1, send_buffer.bytes, sizeof(send_buffer.bytes),
                    recv_buffer, sizeof(recv_buffer), published) == MQTT_OK,
          "ordinary initialization succeeds");
    check(ordinary_client.mutex == 1, "ordinary initialization owns the lock");
    check(connect_client(&ordinary_client, "ordinary") == MQTT_OK, "ordinary CONNECT succeeds");
    check(ordinary_client.mutex == 0 && locks == unlocks, "ordinary path balances locks");
    puts("MQTT initialization lock tests passed");
    return EXIT_SUCCESS;
}
