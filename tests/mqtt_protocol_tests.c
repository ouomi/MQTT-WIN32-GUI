/* Protocol queue, backpressure and subscription acknowledgement regressions. */
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
    union { struct mqtt_queued_message alignment; uint8_t bytes[8192]; } send;
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
    check(reads > before_reads && second->sending, "pending write allows reads and precedes timeout retries");
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

static void test_receive_budget(void)
{
    struct fixture f;
    uint8_t packets[500];
    const uint8_t packet[] = {0x30, 3, 0, 1, 'a'};
    unsigned i;
    setup(&f);
    for (i = 0; i < 100; ++i) memcpy(packets + i * sizeof(packet), packet, sizeof(packet));
    input = packets;
    input_size = sizeof(packets);
    for (i = 1; i <= 3; ++i) {
        check(win32mqtt_recv(&f.client) == MQTT_OK && received == i * 32,
              "receive yields after bounded packet batch");
        check(f.client.mutex == 0, "receive budget releases mutex");
    }
    check(win32mqtt_recv(&f.client) == MQTT_OK && received == 100,
          "remaining buffered packets survive yielding");
}

static void test_empty_publish(void)
{
    unsigned qos;
    for (qos = 0; qos <= 2; ++qos) {
        struct fixture f;
        struct mqtt_response response = {0};
        setup(&f);
        check(mqtt_publish(&f.client, "a", "", 0, (uint8_t)(qos << 1)) == MQTT_OK,
              "empty payload queued without a subscription");
        send_bytes(&f, sizeof(output));
        check(output_size == (qos ? 7u : 5u), "empty PUBLISH wire length");
        check(mqtt_unpack_response(&response, output, output_size) == (ssize_t)output_size,
              "empty PUBLISH wire packet parses");
        check(response.decoded.publish.application_message_size == 0 &&
              response.decoded.publish.qos_level == qos, "empty payload and QoS preserved");
    }
}

static unsigned subscription_results;
static uint16_t result_ids[4];
static uint8_t result_codes[4];
static void subscribed(void *state, const struct mqtt_queued_message *request, uint8_t code)
{
    struct fixture *f = state;
    check(f->client.mutex == 1, "SUBACK callback holds mutex");
    check(request->control_type == MQTT_CONTROL_SUBSCRIBE, "callback identifies SUBSCRIBE");
    check(subscription_results < 4, "bounded result count");
    result_ids[subscription_results] = request->packet_id;
    result_codes[subscription_results++] = code;
}

static void test_subscription_results(void)
{
    struct fixture f;
    uint16_t first, second;
    uint8_t packets[] = {0x90, 3, 0, 0, 0x80, 0x90, 3, 0, 0, 0,
                         0x30, 3, 0, 1, 'a'};
    setup(&f);
    check(f.client.subscribe_response_callback == NULL &&
          f.client.subscribe_response_callback_state == NULL, "optional callback initialized");
    f.client.subscribe_response_callback = subscribed;
    f.client.subscribe_response_callback_state = &f;
    subscription_results = 0;
    check(mqtt_subscribe(&f.client, "first/#", 0) == MQTT_OK, "first subscription queued");
    first = mqtt_mq_get(&f.client.mq, 0)->packet_id;
    check(mqtt_subscribe(&f.client, "second/+", 0) == MQTT_OK, "second subscription queued");
    second = mqtt_mq_get(&f.client.mq, 1)->packet_id;
    send_bytes(&f, sizeof(output));
    /* Out-of-order results must still identify the original request. */
    packets[2] = (uint8_t)(second >> 8); packets[3] = (uint8_t)second;
    packets[7] = (uint8_t)(first >> 8); packets[8] = (uint8_t)first;
    input = packets; input_size = sizeof(packets);
    check(mqtt_sync(&f.client) == MQTT_OK && f.client.error == MQTT_OK,
          "broker rejection leaves connection healthy");
    check(subscription_results == 2 && result_ids[0] == second && result_codes[0] == 0x80 &&
          result_ids[1] == first && result_codes[1] == 0, "results correlated to requests");
    check(received == 1, "PUBLISH after rejected SUBACK is delivered");
    mqtt_test_time = 31;
    output_size = 0;
    send_bytes(&f, sizeof(output));
    check(output_size == 0, "completed subscriptions are not retransmitted");
    check(mqtt_subscribe(&f.client, "retry", 0) == MQTT_OK, "can subscribe after rejection");
    first = mqtt_mq_get(&f.client.mq, 0)->packet_id;
    send_bytes(&f, sizeof(output));
    f.client.subscribe_response_callback = NULL;
    packets[2] = (uint8_t)(first >> 8); packets[3] = (uint8_t)first;
    input = packets; input_size = 5;
    check(mqtt_sync(&f.client) == MQTT_OK && f.client.error == MQTT_OK,
          "rejection without optional callback is nonfatal");
    first = first == 65535 ? 1 : (uint16_t)(first + 1);
    packets[2] = (uint8_t)(first >> 8); packets[3] = (uint8_t)first;
    input = packets; input_size = 5;
    check(win32mqtt_recv(&f.client) == MQTT_ERROR_ACK_OF_UNKNOWN,
          "unknown SUBACK remains a protocol error");
}

static void test_suback_validation(void)
{
    unsigned code;
    for (code = 0; code <= 255; ++code) {
        struct mqtt_response response;
        uint8_t packet[] = {0x90, 3, 0, 1, (uint8_t)code};
        ssize_t result = mqtt_unpack_response(&response, packet, sizeof(packet));
        check((code <= 2 || code == 128) ? result == 5 : result == MQTT_ERROR_MALFORMED_RESPONSE,
              "only protocol-defined SUBACK return codes accepted");
    }
    {
        struct fixture f;
        uint8_t packet[] = {0x90, 4, 0, 0, 0, 0};
        uint16_t id;
        setup(&f);
        check(mqtt_subscribe(&f.client, "a", 0) == MQTT_OK, "queue single-filter request");
        id = mqtt_mq_get(&f.client.mq, 0)->packet_id;
        send_bytes(&f, sizeof(output));
        packet[2] = (uint8_t)(id >> 8); packet[3] = (uint8_t)id;
        input = packet; input_size = sizeof(packet);
        check(win32mqtt_recv(&f.client) == MQTT_ERROR_MALFORMED_RESPONSE,
              "excess return codes are a protocol error");
    }
    {
        struct mqtt_response response;
        const uint8_t zero_id[] = {0x90, 3, 0, 0, 0};
        const uint8_t no_code[] = {0x90, 2, 0, 1};
        check(mqtt_unpack_response(&response, zero_id, sizeof(zero_id)) == MQTT_ERROR_MALFORMED_RESPONSE,
              "zero SUBACK packet ID rejected");
        check(mqtt_unpack_response(&response, no_code, sizeof(no_code)) == MQTT_ERROR_MALFORMED_RESPONSE,
              "missing SUBACK return code rejected");
    }
}

static unsigned unsubscription_results;
static uint16_t unsubscribe_ids[2];
static char unsubscribe_topics[2][32];
static void unsubscribed(void *state, const struct mqtt_queued_message *request)
{
    struct fixture *f = state;
    struct mqtt_response header;
    ssize_t header_size = mqtt_unpack_fixed_header(&header, request->start, request->size);
    const uint8_t *body;
    size_t length;
    check(f->client.mutex == 1, "UNSUBACK callback holds mutex");
    check(request->control_type == MQTT_CONTROL_UNSUBSCRIBE &&
          request->state == MQTT_QUEUED_COMPLETE, "callback identifies completed UNSUBSCRIBE");
    check(header_size > 0 && unsubscription_results < 2, "valid callback request");
    body = request->start + header_size;
    length = ((size_t)body[2] << 8) | body[3];
    check(length < sizeof(unsubscribe_topics[0]), "topic fits test capture");
    memcpy(unsubscribe_topics[unsubscription_results], body + 4, length);
    unsubscribe_topics[unsubscription_results][length] = 0;
    unsubscribe_ids[unsubscription_results++] = request->packet_id;
}

static void test_unsubscription_results(void)
{
    struct fixture f;
    uint16_t first, second;
    uint8_t packets[] = {0xb0, 2, 0, 0, 0xb0, 2, 0, 0, 0x30, 3, 0, 1, 'a'};
    setup(&f);
    check(f.client.unsubscribe_response_callback == NULL &&
          f.client.unsubscribe_response_callback_state == NULL, "reconnect init clears UNSUBACK callback");
    f.client.unsubscribe_response_callback = unsubscribed;
    f.client.unsubscribe_response_callback_state = &f;
    unsubscription_results = 0;
    check(mqtt_unsubscribe(&f.client, "first/#") == MQTT_OK, "first unsubscription queued");
    first = mqtt_mq_get(&f.client.mq, 0)->packet_id;
    check(mqtt_unsubscribe(&f.client, "second/+") == MQTT_OK, "second unsubscription queued");
    second = mqtt_mq_get(&f.client.mq, 1)->packet_id;
    send_bytes(&f, sizeof(output));
    packets[2] = (uint8_t)(second >> 8); packets[3] = (uint8_t)second;
    packets[6] = (uint8_t)(first >> 8); packets[7] = (uint8_t)first;
    input = packets; input_size = 3;
    check(win32mqtt_recv(&f.client) == MQTT_OK && unsubscription_results == 0,
          "fragmented UNSUBACK waits for packet ID");
    input = packets + 3; input_size = sizeof(packets) - 3;
    check(mqtt_sync(&f.client) == MQTT_OK && f.client.error == MQTT_OK,
          "UNSUBACK leaves connection healthy");
    check(unsubscription_results == 2 && unsubscribe_ids[0] == second && unsubscribe_ids[1] == first &&
          strcmp(unsubscribe_topics[0], "second/+") == 0 && strcmp(unsubscribe_topics[1], "first/#") == 0,
          "out-of-order UNSUBACK identifies original filter and packet ID");
    check(received == 1, "in-flight PUBLISH after UNSUBACK is delivered");
    input = packets; input_size = 4;
    check(win32mqtt_recv(&f.client) == MQTT_OK && unsubscription_results == 2,
          "duplicate UNSUBACK before compaction does not notify twice");
    mqtt_test_time = 31;
    output_size = 0;
    send_bytes(&f, sizeof(output));
    check(output_size == 0, "acknowledged unsubscriptions are not retransmitted");
    mqtt_reinit(&f.client, 2, f.send.bytes, sizeof(f.send.bytes), f.recv, sizeof(f.recv));
    check(f.client.unsubscribe_response_callback == unsubscribed &&
          f.client.unsubscribe_response_callback_state == &f, "reinit retains UNSUBACK callback");
    /* Traditional init must clear a previously installed callback as well. */
    check(mqtt_init(&f.client, 1, f.send.bytes, sizeof(f.send.bytes), f.recv, sizeof(f.recv), published) == MQTT_OK,
          "traditional init succeeds");
    check(f.client.unsubscribe_response_callback == NULL &&
          f.client.unsubscribe_response_callback_state == NULL, "traditional init clears UNSUBACK callback");
    /* mqtt_init leaves the mutex held for the initial mqtt_connect call. */
    MQTT_PAL_MUTEX_UNLOCK(&f.client.mutex);
    f.client.error = MQTT_OK;
    f.client.keep_alive = 600;
    check(mqtt_unsubscribe(&f.client, "again") == MQTT_OK, "unsubscribe after reinit");
    first = mqtt_mq_get(&f.client.mq, 0)->packet_id;
    send_bytes(&f, sizeof(output));
    packets[2] = (uint8_t)(first >> 8); packets[3] = (uint8_t)first;
    input = packets; input_size = 4;
    check(mqtt_sync(&f.client) == MQTT_OK && unsubscription_results == 2,
          "UNSUBACK without callback completes normally");
    mqtt_mq_clean(&f.client.mq);
    input = packets; input_size = 4;
    check(win32mqtt_recv(&f.client) == MQTT_OK && unsubscription_results == 2,
          "late UNSUBACK after compaction is harmless and never notifies twice");
}

static void test_unsuback_validation(void)
{
    struct mqtt_response response;
    const uint8_t zero_id[] = {0xb0, 2, 0, 0};
    const uint8_t short_id[] = {0xb0, 1, 1};
    const uint8_t extra_byte[] = {0xb0, 3, 0, 1, 0};
    const uint8_t bad_flags[] = {0xb1, 2, 0, 1};
    const uint8_t valid[] = {0xb0, 2, 0xff, 0xff};
    struct fixture f;
    check(mqtt_unpack_response(&response, zero_id, sizeof(zero_id)) == MQTT_ERROR_MALFORMED_RESPONSE,
          "zero UNSUBACK packet ID rejected");
    check(mqtt_unpack_response(&response, short_id, sizeof(short_id)) == MQTT_ERROR_MALFORMED_RESPONSE,
          "short UNSUBACK packet ID rejected");
    check(mqtt_unpack_response(&response, extra_byte, sizeof(extra_byte)) == MQTT_ERROR_MALFORMED_RESPONSE,
          "extra UNSUBACK bytes rejected");
    check(mqtt_unpack_response(&response, bad_flags, sizeof(bad_flags)) < 0,
          "reserved UNSUBACK flags rejected");
    check(mqtt_unpack_response(&response, valid, sizeof(valid)) == 4 &&
          response.decoded.unsuback.packet_id == 65535, "maximum UNSUBACK packet ID accepted");
    setup(&f);
    f.client.unsubscribe_response_callback = unsubscribed;
    f.client.unsubscribe_response_callback_state = &f;
    unsubscription_results = 0;
    input = valid; input_size = sizeof(valid);
    check(win32mqtt_recv(&f.client) == MQTT_ERROR_ACK_OF_UNKNOWN && unsubscription_results == 0,
          "unknown UNSUBACK never notifies success");
    setup(&f);
    f.client.unsubscribe_response_callback = unsubscribed;
    f.client.unsubscribe_response_callback_state = &f;
    input = zero_id; input_size = sizeof(zero_id);
    check(win32mqtt_recv(&f.client) == MQTT_ERROR_MALFORMED_RESPONSE && unsubscription_results == 0,
          "malformed UNSUBACK never notifies success");
}

static void test_liveness_and_duplex(void)
{
    struct fixture f;
    const uint8_t publish[] = {0x30, 4, 0, 1, 't', 'x'};
    setup(&f);
    queue_publish(&f, "blocked", MQTT_PUBLISH_QOS_0);
    send_bytes(&f, 0);
    input = publish; input_size = sizeof(publish);
    check(mqtt_sync(&f.client) == MQTT_OK && received == 1, "read while write has no progress");
    mqtt_test_time = 60;
    check(mqtt_sync(&f.client) == MQTT_ERROR_RESPONSE_TIMEOUT, "stalled write terminates");
    setup(&f);
    f.client.keep_alive = 60;
    mqtt_test_time = 61;
    send_bytes(&f, sizeof(output));
    send_bytes(&f, sizeof(output));
    mqtt_test_time = 91;
    check(mqtt_sync(&f.client) == MQTT_ERROR_RESPONSE_TIMEOUT, "missing PINGRESP terminates");
    setup(&f);
    queue_publish(&f, "qos2", MQTT_PUBLISH_QOS_2);
    send_bytes(&f, sizeof(output));
    check(output[0] == 0x34, "QoS2 initial DUP clear");
    output_size = 0; mqtt_test_time = 31;
    send_bytes(&f, 1);
    send_bytes(&f, sizeof(output) - 1);
    check(output[0] == 0x3c, "QoS2 partial retry sets DUP");
    mqtt_test_time = 120;
    check(mqtt_sync(&f.client) == MQTT_ERROR_RESPONSE_TIMEOUT, "unacknowledged request has terminal deadline");
}
static void test_control_reserve(void)
{
    struct fixture f;
    uint8_t ack[4] = {0x50, 2, 0, 0};
    enum MQTTErrors result;
    size_t before;
    setup(&f);
    check(mqtt_subscribe(&f.client, "head", 0) == MQTT_OK, "blocked queue head");
    queue_publish(&f, "qos2", MQTT_PUBLISH_QOS_2);
    ack[2] = (uint8_t)(mqtt_mq_get(&f.client.mq, 1)->packet_id >> 8);
    ack[3] = (uint8_t)mqtt_mq_get(&f.client.mq, 1)->packet_id;
    do { result = mqtt_publish(&f.client, "t", "x", 1, MQTT_PUBLISH_QOS_0); } while (result == MQTT_OK);
    check(result == MQTT_ERROR_SEND_BUFFER_IS_FULL, "business capacity bounded");
    f.client.error = MQTT_OK;
    send_bytes(&f, sizeof(output));
    before = (size_t)mqtt_mq_length(&f.client.mq);
    input = ack; input_size = sizeof(ack);
    check(mqtt_sync(&f.client) == MQTT_OK, "full business queue can process PUBREC");
    mqtt_mq_clean(&f.client.mq);
    check((size_t)mqtt_mq_length(&f.client.mq) < before, "completed middle messages reclaimed");
    check(mqtt_mq_find(&f.client.mq, MQTT_CONTROL_PUBREL, NULL) != NULL, "PUBREL remains queued");
}

static void receive_ack(struct fixture *f, uint8_t type, uint16_t id)
{
    uint8_t packet[] = {type, 2, (uint8_t)(id >> 8), (uint8_t)id};
    input = packet; input_size = sizeof(packet);
    check(win32mqtt_recv(&f->client) == MQTT_OK, "valid or repeated acknowledgement");
}

static void test_suback_history(void)
{
    struct fixture f;
    uint16_t id;
    uint8_t packet[] = {0x90, 3, 0, 0, 0};
    setup(&f);
    subscription_results = 0;
    f.client.subscribe_response_callback = subscribed;
    f.client.subscribe_response_callback_state = &f;
    check(mqtt_subscribe(&f.client, "topic", 0) == MQTT_OK, "history subscription queued");
    id = mqtt_mq_get(&f.client.mq, 0)->packet_id;
    packet[2] = (uint8_t)(id >> 8); packet[3] = (uint8_t)id;
    send_bytes(&f, sizeof(output));
    for (unsigned i = 0; i < 3; ++i) {
        input = packet; input_size = sizeof(packet);
        check(win32mqtt_recv(&f.client) == MQTT_OK && subscription_results == 1,
              "duplicate SUBACK never calls observer twice, including after compaction");
        if (i == 1) mqtt_mq_clean(&f.client.mq);
    }
}

static void test_ack_history(void)
{
    struct fixture f;
    uint16_t id;
    setup(&f);
    queue_publish(&f, "t", MQTT_PUBLISH_QOS_1);
    id = mqtt_mq_get(&f.client.mq, 0)->packet_id;
    send_bytes(&f, sizeof(output));
    mqtt_test_time = 31; output_size = 0;
    send_bytes(&f, sizeof(output));
    receive_ack(&f, 0x40, id);
    /* Exercise normal capacity-triggered compaction, not a mocked queue. */
    for (unsigned i = 0; i < 200; ++i) {
        queue_publish(&f, "next", MQTT_PUBLISH_QOS_0);
        output_size = 0; send_bytes(&f, sizeof(output));
    }
    check(mqtt_mq_find(&f.client.mq, MQTT_CONTROL_PUBLISH, &id) == NULL,
          "original request was reclaimed");
    receive_ack(&f, 0x40, id);
    check(f.client.error == MQTT_OK, "late PUBACK does not disconnect");
    mqtt_reinit(&f.client, 1, f.send.bytes, sizeof(f.send.bytes), f.recv, sizeof(f.recv));
    f.client.error = MQTT_OK;
    {
        uint8_t packet[] = {0x40, 2, (uint8_t)(id >> 8), (uint8_t)id};
        input = packet; input_size = sizeof(packet);
        check(win32mqtt_recv(&f.client) == MQTT_ERROR_ACK_OF_UNKNOWN,
              "new session does not accept previous session ACK history");
    }
}

static void test_outbound_qos2_duplicates(void)
{
    struct fixture f;
    uint16_t id;
    setup(&f);
    queue_publish(&f, "t", MQTT_PUBLISH_QOS_2);
    id = mqtt_mq_get(&f.client.mq, 0)->packet_id;
    send_bytes(&f, sizeof(output));
    receive_ack(&f, 0x50, id);
    output_size = 0; send_bytes(&f, sizeof(output));
    check(output_size == 4 && output[0] == 0x62, "PUBREC produces PUBREL");
    mqtt_mq_clean(&f.client.mq);
    receive_ack(&f, 0x50, id);
    output_size = 0; send_bytes(&f, 1);
    receive_ack(&f, 0x50, id);
    receive_ack(&f, 0x70, id);
    mqtt_mq_clean(&f.client.mq);
    check(mqtt_mq_length(&f.client.mq) == 1 && mqtt_mq_get(&f.client.mq, 0)->sending,
          "PUBCOMP preserves partial PUBREL retry");
    send_bytes(&f, 3);
    check(output_size == 4 && output[0] == 0x62 && output[2] == (uint8_t)(id >> 8) &&
          output[3] == (uint8_t)id, "partial PUBREL retry retains exact bytes");
    mqtt_mq_clean(&f.client.mq);
    receive_ack(&f, 0x70, id);
    receive_ack(&f, 0x50, id);
    check(mqtt_mq_length(&f.client.mq) == 0, "late QoS2 ACKs do not resurrect completed exchange");
}

static void test_ack_reuse_and_validation(void)
{
    struct fixture f;
    uint16_t id;
    setup(&f);
    f.client.pid_lfsr = 163;
    queue_publish(&f, "old", MQTT_PUBLISH_QOS_1);
    id = mqtt_mq_get(&f.client.mq, 0)->packet_id;
    send_bytes(&f, sizeof(output)); receive_ack(&f, 0x40, id);
    mqtt_mq_clean(&f.client.mq);
    f.client.pid_lfsr = 163;
    queue_publish(&f, "new", MQTT_PUBLISH_QOS_2);
    check(mqtt_mq_get(&f.client.mq, 0)->packet_id == id, "exercise packet ID reuse");
    send_bytes(&f, sizeof(output));
    {
        uint8_t wrong[] = {0x40, 2, (uint8_t)(id >> 8), (uint8_t)id};
        input = wrong; input_size = sizeof(wrong);
        check(win32mqtt_recv(&f.client) == MQTT_ERROR_MALFORMED_RESPONSE &&
              mqtt_mq_get(&f.client.mq, 0)->state == MQTT_QUEUED_AWAITING_ACK,
              "old PUBACK must not complete a new QoS2 request with the same ID");
    }
    setup(&f);
    queue_publish(&f, "unsent", MQTT_PUBLISH_QOS_1);
    id = mqtt_mq_get(&f.client.mq, 0)->packet_id;
    {
        uint8_t early[] = {0x40, 2, (uint8_t)(id >> 8), (uint8_t)id};
        input = early; input_size = sizeof(early);
        check(win32mqtt_recv(&f.client) == MQTT_ERROR_MALFORMED_RESPONSE,
              "ACK cannot complete an unsent request");
    }
    {
        struct mqtt_response response;
        const uint8_t types[] = {0x40, 0x50, 0x62, 0x70};
        for (unsigned i = 0; i < sizeof(types); ++i) {
            uint8_t packet[] = {types[i], 2, 0, 0};
            check(mqtt_unpack_response(&response, packet, sizeof(packet)) == MQTT_ERROR_MALFORMED_RESPONSE,
                  "zero packet ID rejected for every publication ACK");
        }
        const uint8_t bad_ping[] = {0xd0, 2, 0xd0, 0};
        check(mqtt_unpack_response(&response, bad_ping, sizeof(bad_ping)) == MQTT_ERROR_MALFORMED_RESPONSE,
              "PINGRESP cannot carry a body that becomes a second packet");
    }
}

static void test_inbound_qos2_duplicates(void)
{
    struct fixture f;
    const uint8_t publish[] = {0x34, 5, 0, 1, 't', 0xff, 0xff};
    setup(&f);
    for (unsigned i = 0; i < 2; ++i) {
        input = publish; input_size = sizeof(publish);
        allowance = sizeof(output); output_size = 0;
        check(mqtt_sync(&f.client) == MQTT_OK && received == 1,
              "duplicate QoS2 PUBLISH delivered once");
        check(output_size == 4 && output[0] == 0x50, "each duplicate PUBLISH gets PUBREC");
        mqtt_mq_clean(&f.client.mq);
        check(mqtt_mq_length(&f.client.mq) == 0, "QoS2 ownership does not pin the send queue");
    }
    for (unsigned i = 0; i < 2; ++i) {
        receive_ack(&f, 0x62, 65535);
        output_size = 0; send_bytes(&f, sizeof(output));
        check(output_size == 4 && output[0] == 0x70 && received == 1,
              "repeated PUBREL gets PUBCOMP without duplicate delivery");
        mqtt_mq_clean(&f.client.mq);
    }
    input = publish; input_size = sizeof(publish); allowance = sizeof(output);
    check(mqtt_sync(&f.client) == MQTT_OK && received == 2, "inbound ID reusable after PUBREL");
    mqtt_reinit(&f.client, 1, f.send.bytes, sizeof(f.send.bytes), f.recv, sizeof(f.recv));
    f.client.error = MQTT_OK;
    input = publish; input_size = sizeof(publish); allowance = sizeof(output);
    check(mqtt_sync(&f.client) == MQTT_OK && received == 3, "new session clears inbound ownership");
}

static void test_inbound_qos2_burst(int blocked_first)
{
    struct fixture f;
    uint8_t packets[200 * 7];
    setup(&f);
    for (unsigned i = 0; i < 200; ++i) {
        const uint16_t id = (uint16_t)(i + 1);
        const uint8_t packet[] = {0x34, 5, 0, 1, 't', (uint8_t)(id >> 8), (uint8_t)id};
        memcpy(packets + i * 7, packet, 7);
    }
    input = packets; input_size = sizeof(packets);
    if (blocked_first) {
        for (unsigned i = 0; i < 10; ++i) {
            allowance = 0;
            check(mqtt_sync(&f.client) == MQTT_OK, "blocked control writes apply bounded backpressure");
        }
        check(received > 0 && received < 200 && output_size == 0,
              "input pauses while actual transport cannot write");
    }
    for (unsigned i = 0; i < 20; ++i) {
        allowance = sizeof(output) - output_size;
        check(mqtt_sync(&f.client) == MQTT_OK, "large QoS2 burst makes progress");
    }
    check(received == 200 && output_size == 800, "entire burst acknowledged before any PUBREL");
    for (unsigned i = 0; i < 200; ++i)
        check(output[4*i] == 0x50 && output[4*i+3] == i+1, "PUBREC identifiers match burst");
    for (unsigned i = 1; i <= 200; ++i) {
        receive_ack(&f, 0x62, (uint16_t)i);
        output_size = 0; send_bytes(&f, sizeof(output));
        check(output_size == 4 && output[0] == 0x70 && output[3] == i,
              "PUBREL frees ownership and gets matching PUBCOMP");
    }
    mqtt_mq_clean(&f.client.mq);
    check(mqtt_mq_length(&f.client.mq) == 0, "burst leaves no waiting control packets");
    mqtt_test_time = 121;
    check(mqtt_sync(&f.client) == MQTT_OK, "completed burst does not trigger request timeout");
}

/* All legal inbound IDs can be pending, independently of outbound capacity. */
static void test_inbound_qos2_id_space(void)
{
    struct fixture f;
    setup(&f);
    for (unsigned id = 1; id <= 65535; ++id) {
        const uint8_t packet[] = {0x34, 5, 0, 1, 't', (uint8_t)(id >> 8), (uint8_t)id};
        input = packet; input_size = sizeof(packet); output_size = 0; allowance = 4;
        check(mqtt_sync(&f.client) == MQTT_OK && output_size == 4, "full inbound ID space makes progress");
    }
    check(received == 65535, "every legal ID delivered exactly once");
    for (unsigned id = 1; id <= 65535; ++id) {
        receive_ack(&f, 0x62, (uint16_t)id); output_size = 0; send_bytes(&f, 4);
        check(output_size == 4 && output[0] == 0x70, "full inbound ID space can complete");
    }
}

int main(void)
{
    test_suback_history();
    test_ack_history();
    test_outbound_qos2_duplicates();
    test_ack_reuse_and_validation();
    test_inbound_qos2_duplicates();
    test_inbound_qos2_burst(0);
    test_inbound_qos2_burst(1);
    test_inbound_qos2_id_space();
    test_liveness_and_duplex();
    test_control_reserve();
    test_resume(0);
    test_resume(2);
    test_retry_and_ack();
    test_compaction_and_reinit();
    test_receive();
    test_receive_budget();
    test_empty_publish();
    test_subscription_results();
    test_suback_validation();
    test_unsubscription_results();
    test_unsuback_validation();
    puts("MQTT backpressure tests passed");
    return EXIT_SUCCESS;
}
