#include "mqtt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Unexpected transport use must fail rather than hide a test dependency. */
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

static void check(int condition, const char *name)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", name);
        exit(EXIT_FAILURE);
    }
}

static ssize_t unpack(const uint8_t *bytes, size_t size, struct mqtt_response *response)
{
    /* Exact allocation makes reads beyond the supplied packet visible to ASan. */
    uint8_t *packet = malloc(size ? size : 1);
    ssize_t result;
    check(packet != NULL, "allocate packet");
    memcpy(packet, bytes, size);
    result = mqtt_unpack_response(response, packet, size);
    free(packet);
    return result;
}

static void test_malformed(void)
{
    static const struct {
        const char *name;
        uint8_t bytes[8];
        size_t size;
    } cases[] = {
        {"missing topic length", {0x30, 0}, 2},
        {"partial topic length", {0x30, 1, 0}, 3},
        {"empty topic", {0x30, 2, 0, 0}, 4},
        {"topic exceeds body", {0x30, 3, 0, 2, 'a'}, 5},
        {"oversized QoS 0 topic", {0x30, 4, 0xff, 0xff, 'a', 'b'}, 6},
        {"oversized QoS 1 topic", {0x32, 4, 0xff, 0xff, 'a', 'b'}, 6},
        {"oversized QoS 2 topic", {0x34, 4, 0xff, 0xff, 'a', 'b'}, 6},
        {"missing QoS 1 packet id", {0x32, 3, 0, 1, 'a'}, 5},
        {"partial QoS 1 packet id", {0x32, 4, 0, 1, 'a', 1}, 6},
        {"missing QoS 2 packet id", {0x34, 3, 0, 1, 'a'}, 5},
        {"partial QoS 2 packet id", {0x34, 4, 0, 1, 'a', 1}, 6},
        {"zero packet id", {0x32, 5, 0, 1, 'a', 0, 0}, 7},
        {"reserved QoS", {0x36, 5, 0, 1, 'a', 0, 1}, 7}
    };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        struct mqtt_response response = {0};
        check(unpack(cases[i].bytes, cases[i].size, &response) == MQTT_ERROR_MALFORMED_RESPONSE,
              cases[i].name);
    }
}

static void test_valid(void)
{
    unsigned qos;
    unsigned payload_size;
    for (qos = 0; qos <= 2; ++qos) {
        for (payload_size = 0; payload_size <= 3; ++payload_size) {
            uint8_t packet[12] = {0};
            const uint8_t payload[] = {0, 0xff, 'x'};
            size_t size = 5;
            size_t prefix;
            struct mqtt_response response = {0};
            packet[0] = (uint8_t)(0x31 | (qos << 1) | (qos ? 8 : 0));
            packet[2] = 0;
            packet[3] = 1;
            packet[4] = 'a';
            if (qos) {
                packet[size++] = 0x12;
                packet[size++] = 0x34;
            }
            memcpy(packet + size, payload, payload_size);
            size += payload_size;
            packet[1] = (uint8_t)(size - 2);
            for (prefix = 0; prefix < size; ++prefix) {
                check(unpack(packet, prefix, &response) == 0, "truncated packet waits for data");
            }
            check(mqtt_unpack_response(&response, packet, size) == (ssize_t)size, "valid packet consumed");
            check(response.decoded.publish.topic_name_size == 1 &&
                  memcmp(response.decoded.publish.topic_name, "a", 1) == 0, "topic decoded");
            check(response.decoded.publish.qos_level == qos, "QoS decoded");
            check(response.decoded.publish.retain_flag == 1, "retain decoded");
            check(response.decoded.publish.dup_flag == (qos ? 1 : 0), "dup decoded");
            check(response.decoded.publish.packet_id == (qos ? 0x1234 : 0), "packet id decoded");
            check(response.decoded.publish.application_message_size == payload_size, "payload length");
            check(memcmp(response.decoded.publish.application_message, payload, payload_size) == 0,
                  "binary payload decoded");
            /* A following packet must not become part of this payload. */
            packet[size] = 0xd0;
            packet[size + 1] = 0;
            check(mqtt_unpack_response(&response, packet, size + 2) == (ssize_t)size,
                  "consume only first packet");
            check(response.decoded.publish.application_message_size == payload_size,
                  "payload excludes next packet");
        }
    }
}

int main(void)
{
    test_malformed();
    test_valid();
    puts("PUBLISH parser tests passed");
    return EXIT_SUCCESS;
}
