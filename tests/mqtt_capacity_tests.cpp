#include "mqtt/mqtt_capacity.hpp"
#include "mqtt/mqtt_request.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

static void Check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(EXIT_FAILURE); }
}
static std::vector<unsigned char> sent;
static std::vector<unsigned char> incoming;
static std::size_t read_offset;
static unsigned messages_received;
extern "C" {
void mqtt_test_mutex_init(mqtt_pal_mutex_t* mutex) { *mutex = 0; }
void mqtt_test_mutex_lock(mqtt_pal_mutex_t* mutex) { Check(*mutex == 0, "lock ownership"); *mutex = 1; }
void mqtt_test_mutex_unlock(mqtt_pal_mutex_t* mutex) { Check(*mutex == 1, "unlock ownership"); *mutex = 0; }
ssize_t mqtt_pal_sendall(mqtt_pal_socket_handle, const void* data, size_t size, int) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    sent.insert(sent.end(), bytes, bytes + size);
    return static_cast<ssize_t>(size);
}
ssize_t mqtt_pal_recvall(mqtt_pal_socket_handle, void* data, size_t size, int) {
    const auto count = std::min(size, incoming.size() - read_offset);
    if (count) std::memcpy(data, incoming.data() + read_offset, count);
    read_offset += count;
    return static_cast<ssize_t>(count);
}
}

static void TestQueue() {
    using Queue = win32mqtt::MqttCommandQueue<unsigned>;
    Queue queue;
    Check(queue.Empty(), "empty queue");
    for (unsigned i = 0; i < Queue::MaxCommands; ++i) Check(queue.Push(i, 1), "fill command slots");
    Check(!queue.Push(999, 1), "count limit rejects new work");
    Check(queue.Push(1000, 0, true), "disconnect reserved slot survives full count");
    Check(!queue.Push(1001, 0, true), "only one disconnect slot");
    for (unsigned i = 0; i < Queue::MaxCommands; ++i) Check(queue.Pop() == i, "rejection preserves FIFO");
    Check(queue.ControlPending() && queue.Pop() == 1000 && queue.Empty(), "reserved slot delivered");
    Check(!queue.ControlPending(), "disconnect reservation released");
    Check(queue.Push(1, Queue::MaxBytes), "exact byte limit");
    Check(!queue.Push(2, 1), "byte limit independently enforced");
    Check(!queue.Push(2, std::numeric_limits<std::size_t>::max()), "huge byte count cannot overflow");
    Check(queue.Push(3, 0, true), "disconnect survives full byte budget");
    Check(queue.Pop() == 1 && queue.Pop() == 3, "full-byte FIFO");
    Check(queue.Push(4, Queue::MaxBytes), "pop returns byte capacity");
    Check(queue.Pop() == 4 && queue.Empty(), "queue reusable");
}

static void TestPacketSize() {
    using win32mqtt::MqttPacketFits;
    Check(MqttPacketFits({4093}), "4096-byte packet fits");
    Check(!MqttPacketFits({4094}), "4097-byte packet rejected");
    Check(!MqttPacketFits({std::numeric_limits<std::size_t>::max()}), "size_t overflow rejected");
    Check(!MqttPacketFits({4096, std::numeric_limits<std::size_t>::max()}), "sum overflow rejected");
    unsigned char packet[8192];
    const std::string topic = "topic";
    for (unsigned qos = 0; qos <= 2; ++qos) {
        for (std::size_t payload_size : {0u, 120u, 121u, 4084u, 4085u, 4086u, 4087u}) {
            const std::string payload(payload_size, 'x');
            const auto size = mqtt_pack_publish_request(packet, sizeof(packet), topic.c_str(), 1,
                payload.data(), payload.size(), static_cast<uint8_t>(qos << 1));
            Check(size > 0, "reference packet encoded");
            Check(MqttPacketFits({2, topic.size(), payload_size, qos ? 2u : 0u}) ==
                  (static_cast<std::size_t>(size) <= win32mqtt::MqttMaxOutgoingPacket),
                  "size admission matches actual PUBLISH encoding");
        }
    }
    const std::string will(4000, 'w');
    const auto size = mqtt_pack_connection_request(packet, sizeof(packet), "id", "will", will.data(),
        will.size(), nullptr, nullptr, MQTT_CONNECT_CLEAN_SESSION, 60);
    Check(size > 0 && MqttPacketFits({10, 2, 2, 4, 4, will.size()}) ==
          (static_cast<std::size_t>(size) <= win32mqtt::MqttMaxOutgoingPacket), "CONNECT sizing includes will");
}

static void TestQueueFullRecovery() {
    mqtt_client client{};
    alignas(mqtt_queued_message) unsigned char send[8192]{};
    unsigned char receive[8192]{};
    mqtt_init_reconnect(&client, nullptr, nullptr, nullptr);
    mqtt_reinit(&client, 1, send, sizeof(send), receive, sizeof(receive));
    client.error = MQTT_OK;
    client.keep_alive = 60;
    const std::string payload(1000, 'x');
    unsigned accepted = 0;
    while (win32mqtt::MqttUserRequest(client, [&] {
        return mqtt_publish(&client, "a", payload.data(), payload.size(), MQTT_PUBLISH_QOS_0);
    }) == MQTT_OK) ++accepted;
    Check(accepted > 0 && client.error == MQTT_OK, "local queue full does not poison connection");
    const auto count = mqtt_mq_length(&client.mq);
    const std::string long_topic(4000, 'a');
    Check(win32mqtt::MqttUserRequest(client, [&] { return mqtt_subscribe(&client, long_topic.c_str(), 0); }) ==
          MQTT_ERROR_SEND_BUFFER_IS_FULL && client.error == MQTT_OK, "subscribe full rejects without disconnection");
    Check(win32mqtt::MqttUserRequest(client, [&] { return mqtt_unsubscribe(&client, long_topic.c_str()); }) ==
          MQTT_ERROR_SEND_BUFFER_IS_FULL && client.error == MQTT_OK, "unsubscribe full rejects without disconnection");
    Check(mqtt_mq_length(&client.mq) == count, "rejected operations do not enter queue");
    std::vector<unsigned char> expected;
    for (ssize_t i = 0; i < count; ++i) {
        const auto* message = mqtt_mq_get(&client.mq, i);
        expected.insert(expected.end(), message->start, message->start + message->size);
    }
    Check(mqtt_sync(&client) == MQTT_OK && sent == expected, "accepted packets survive rejected operation");
    Check(win32mqtt::MqttUserRequest(client, [&] {
        return mqtt_publish(&client, "retry", "", 0, MQTT_PUBLISH_QOS_0);
    }) == MQTT_OK, "retry accepted after completed messages reclaimed");
    client.error = MQTT_ERROR_SOCKET_ERROR;
    Check(win32mqtt::MqttUserRequest(client, [&] {
        return mqtt_publish(&client, "retry", "", 0, MQTT_PUBLISH_QOS_0);
    }) == MQTT_ERROR_SOCKET_ERROR && client.error == MQTT_ERROR_SOCKET_ERROR,
          "real connection failures are never cleared");
}
static void TestReceiveLimit() {
    for (bool oversized : {false, true}) {
        mqtt_client client{};
        alignas(mqtt_queued_message) unsigned char send[8192]{};
        unsigned char receive[win32mqtt::MqttReceiveCapacity]{};
        mqtt_init_reconnect(&client, nullptr, nullptr, [](void**, mqtt_response_publish*) { ++messages_received; });
        mqtt_reinit(&client, 1, send, sizeof(send), receive, sizeof(receive));
        client.error = MQTT_OK;
        incoming.assign(win32mqtt::MqttReceiveCapacity + (oversized ? 1 : 0), 'x');
        incoming[0] = 0x30;
        incoming[1] = oversized ? 0xfe : 0xfd;
        incoming[2] = 0x3f;
        incoming[3] = 0;
        incoming[4] = 1;
        incoming[5] = 'a';
        read_offset = 0;
        messages_received = 0;
        Check(win32mqtt_recv(&client) == (oversized ? MQTT_ERROR_RECV_BUFFER_TOO_SMALL : MQTT_OK),
              "exact receive capacity accepted, one extra byte rejected");
        Check(messages_received == (oversized ? 0u : 1u), "no partial oversized message delivered");
    }
}

int main() {
    TestQueue();
    TestPacketSize();
    TestQueueFullRecovery();
    TestReceiveLimit();
    std::cout << "Capacity and local backpressure tests passed\n";
}
