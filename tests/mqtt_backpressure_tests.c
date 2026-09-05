#include "mqtt.h"
#include <stdio.h>

mqtt_pal_time_t mqtt_test_time;
static size_t allowance;
static uint8_t output[8192];
static size_t output_size;
static const uint8_t *input;
static size_t input_size;
static int eof;
static unsigned received;
static unsigned reads;

static void check(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(EXIT_FAILURE);
    }
}
/* This target uses mqtt_test_pal.h: the mutex is an integer ownership tracker. */
void mqtt_test_mutex_init(mqtt_pal_mutex_t *mutex) { *mutex = 0; }
void mqtt_test_mutex_lock(mqtt_pal_mutex_t *mutex) { check(*mutex == 0, "lock ownership"); *mutex = 1; }
void mqtt_test_mutex_unlock(mqtt_pal_mutex_t *mutex) { check(*mutex == 1, "unlock ownership"); *mutex = 0; }

ssize_t mqtt_pal_sendall(mqtt_pal_socket_handle fd, const void *buf, size_t len, int flags)
{
    size_t count = len < allowance ? len : allowance;
    (void)fd; (void)flags;
    check(output_size + count <= sizeof(output), "output capacity");
    memcpy(output + output_size, buf, count);
    output_size += count;
    allowance -= count;
    return (ssize_t)count;
}
ssize_t mqtt_pal_recvall(mqtt_pal_socket_handle fd, void *buf, size_t len, int flags)
{
    size_t count = input_size < len ? input_size : len;
    (void)fd; (void)flags;
    ++reads;
    if (count) {
        memcpy(buf, input, count);
        input += count;
        input_size -= count;
        return (ssize_t)count;
    }
    return eof ? MQTT_ERROR_SOCKET_ERROR : 0;
}
static void published(void **state, struct mqtt_response_publish *message)
{
    (void)state;
    check(message->topic_name_size == 1, "received topic");
    ++received;
}

struct fixture {
    struct mqtt_client client;
    union { struct mqtt_queued_message alignment; uint8_t bytes[2048]; } send;
    uint8_t recv[2048];
};
static void setup(struct fixture *f)
{
    memset(f, 0, sizeof(*f));
    mqtt_test_time = 0;
    allowance = 0;
    output_size = 0;
    input = NULL;
    input_size = 0;
    eof = 0;
    received = 0;
    reads = 0;
    mqtt_init_reconnect(&f->client, NULL, NULL, published);
    mqtt_reinit(&f->client, 1, f->send.bytes, sizeof(f->send.bytes), f->recv, sizeof(f->recv));
    f->client.error = MQTT_OK;
    f->client.keep_alive = 600;
}
static void queue_publish(struct fixture *f, const char *topic, uint8_t qos)
{
    check(mqtt_publish(&f->client, topic, "payload", 7, qos) == MQTT_OK, "queue publish");
}
static void send_bytes(struct fixture *f, size_t count)
{
    allowance = count;
    check(win32mqtt_send(&f->client) == MQTT_OK, "send without fatal error");
    check(f->client.mutex == 0, "send releases lock");
}

static void test_resume(size_t first_write)
{
    struct fixture f;
    struct mqtt_queued_message *first;
    struct mqtt_queued_message *second;
    uint8_t expected[128];
    size_t second_size;
    unsigned before_reads;
    setup(&f);
    queue_publish(&f, "first", MQTT_PUBLISH_QOS_1);
    send_bytes(&f, sizeof(output));
    first = mqtt_mq_get(&f.client.mq, 0);
    queue_publish(&f, "second", MQTT_PUBLISH_QOS_0);
    second = mqtt_mq_get(&f.client.mq, 1);
    second_size = second->size;
    memcpy(expected, second->start, second_size);
    output_size = 0;
    send_bytes(&f, first_write);
    check(second->sending && f.client.send_offset == first_write, "remember pending write");
    mqtt_test_time = 31; /* The earlier QoS 1 message is now overdue. */
    before_reads = reads;
    check(mqtt_sync(&f.client) == MQTT_OK, "sync yields on backpressure");
    check(reads == before_reads && second->sending, "pending write precedes reads and timeout retries");
    send_bytes(&f, second_size - first_write);
    check(output_size == second_size && memcmp(output, expected, second_size) == 0,
          "pending message remains contiguous across earlier timeout");
    check(first->state == MQTT_QUEUED_AWAITING_ACK && !second->sending,
          "earlier retry deferred until pending message completes");
}

static void test_retry_and_ack(void)
{
    struct fixture f;
    struct mqtt_queued_message *msg;
    uint8_t expected[128];
    uint8_t ack[4] = {0x40, 2, 0, 0};
    size_t size;
    setup(&f);
    queue_publish(&f, "retry", MQTT_PUBLISH_QOS_1);
    send_bytes(&f, sizeof(output));
    msg = mqtt_mq_get(&f.client.mq, 0);
    size = msg->size;
    memcpy(expected, msg->start, size);
    output_size = 0;
    mqtt_test_time = 31;
    send_bytes(&f, 2);
    check(msg->sending && f.client.number_of_timeouts == 1, "retry started once");
    send_bytes(&f, 1);
    check(f.client.send_offset == 3 && f.client.number_of_timeouts == 1,
          "partial retry does not restart on every send call");
    /* A late ACK for the previous transmission must not remove pending bytes. */
    ack[2] = (uint8_t)(msg->packet_id >> 8);
    ack[3] = (uint8_t)msg->packet_id;
    input = ack;
    input_size = sizeof(ack);
    check(win32mqtt_recv(&f.client) == MQTT_OK && msg->state == MQTT_QUEUED_COMPLETE,
          "late PUBACK accepted");
    mqtt_mq_clean(&f.client.mq);
    check(mqtt_mq_length(&f.client.mq) == 1, "keep acknowledged partial retry");
    send_bytes(&f, size - 3);
    check(output_size == size && memcmp(output, expected, size) == 0, "retry continues from offset");
    check(msg->state == MQTT_QUEUED_COMPLETE && !msg->sending, "ACK remains effective");
    mqtt_mq_clean(&f.client.mq);
    check(mqtt_mq_length(&f.client.mq) == 0, "completed retry can be removed");
}

static void test_compaction_and_reinit(void)
{
    struct fixture f;
    uint8_t expected[128];
    size_t size;
    struct mqtt_queued_message *msg;
    setup(&f);
    queue_publish(&f, "done", MQTT_PUBLISH_QOS_0);
    send_bytes(&f, sizeof(output));
    queue_publish(&f, "moving", MQTT_PUBLISH_QOS_0);
    msg = mqtt_mq_get(&f.client.mq, 1);
    size = msg->size;
    memcpy(expected, msg->start, size);
    output_size = 0;
    send_bytes(&f, 2);
    mqtt_mq_clean(&f.client.mq);
    check(mqtt_mq_length(&f.client.mq) == 1 && mqtt_mq_get(&f.client.mq, 0)->sending,
          "compaction preserves pending write marker");
    send_bytes(&f, size - 2);
    check(output_size == size && memcmp(output, expected, size) == 0, "compacted write bytes preserved");
    queue_publish(&f, "interrupted", MQTT_PUBLISH_QOS_0);
    send_bytes(&f, 3);
    check(f.client.send_offset == 3, "partial write before reinit");
    mqtt_reinit(&f.client, 2, f.send.bytes, sizeof(f.send.bytes), f.recv, sizeof(f.recv));
    check(f.client.send_offset == 0, "reinit resets send offset");
    MQTT_PAL_MUTEX_LOCK(&f.client.mutex);
    check(mqtt_connect(&f.client, "new", NULL, NULL, 0, NULL, NULL,
                       MQTT_CONNECT_CLEAN_SESSION, 60) == MQTT_OK, "new CONNECT queued");
    msg = mqtt_mq_get(&f.client.mq, 0);
    size = msg->size;
    memcpy(expected, msg->start, size);
    output_size = 0;
    send_bytes(&f, size);
    check(output_size == size && memcmp(output, expected, size) == 0, "new CONNECT starts at first byte");
}

static void test_receive(void)
{
    struct fixture f;
    const uint8_t packets[] = {0x30, 3, 0, 1, 'a', 0x30, 3, 0, 1, 'b'};
    setup(&f);
    input = packets;
    input_size = 2;
    check(win32mqtt_recv(&f.client) == MQTT_OK && received == 0, "partial packet buffered");
    input = packets + 2;
    input_size = sizeof(packets) - 2;
    eof = 1;
    check(win32mqtt_recv(&f.client) == MQTT_ERROR_SOCKET_ERROR, "EOF detected");
    check(received == 2, "all buffered messages delivered before EOF");
    check(f.client.mutex == 0, "EOF releases lock");
}

int main(void)
{
    test_resume(0);
    test_resume(2);
    test_retry_and_ack();
    test_compaction_and_reinit();
    test_receive();
    puts("MQTT backpressure tests passed");
    return EXIT_SUCCESS;
}
