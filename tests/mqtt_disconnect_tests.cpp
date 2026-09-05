#include "mqtt/mqtt_disconnect.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

using win32mqtt::MqttDisconnect;
using Result = MqttDisconnect::Result;
static std::size_t allowance;
static unsigned writes;
static bool fail_write;
static std::vector<unsigned char> output;

static void Check(bool condition, const char* detail) {
    if (!condition) {
        std::cerr << "FAIL: " << detail << '\n';
        std::exit(EXIT_FAILURE);
    }
}
extern "C" {
void mqtt_test_mutex_init(mqtt_pal_mutex_t* mutex) { *mutex = 0; }
void mqtt_test_mutex_lock(mqtt_pal_mutex_t* mutex) { Check(*mutex == 0, "lock ownership"); *mutex = 1; }
void mqtt_test_mutex_unlock(mqtt_pal_mutex_t* mutex) { Check(*mutex == 1, "unlock ownership"); *mutex = 0; }
ssize_t mqtt_pal_sendall(mqtt_pal_socket_handle, const void* buf, size_t len, int) {
    ++writes;
    if (fail_write) return MQTT_ERROR_SOCKET_ERROR;
    const auto count = std::min(len, allowance);
    const auto* bytes = static_cast<const unsigned char*>(buf);
    output.insert(output.end(), bytes, bytes + count);
    allowance -= count;
    return static_cast<ssize_t>(count);
}
ssize_t mqtt_pal_recvall(mqtt_pal_socket_handle, void*, size_t, int) {
    Check(false, "disconnect must not wait for a read or ACK");
    return 0;
}
}

struct Fixture {
    mqtt_client client{};
    alignas(mqtt_queued_message) unsigned char send[1024]{};
    unsigned char recv[1024]{};
    MqttDisconnect closing;
    MqttDisconnect::Clock::time_point now{};
    Fixture() {
        allowance = 0; writes = 0; fail_write = false; output.clear();
        mqtt_init_reconnect(&client, nullptr, nullptr, nullptr);
        mqtt_reinit(&client, 1, send, sizeof(send), recv, sizeof(recv));
        client.error = MQTT_OK;
        client.keep_alive = 60;
        closing.Begin(now);
    }
    Result Poll(std::size_t available) {
        allowance = available;
        const auto before = writes;
        const auto result = closing.Poll(client, now);
        Check(writes - before <= 1, "one write attempt per worker tick");
        return result;
    }
    void Publish(const char* topic, uint8_t qos = MQTT_PUBLISH_QOS_0) {
        Check(mqtt_publish(&client, topic, "data", 4, qos) == MQTT_OK, "queue publish");
    }
};
static const std::vector<unsigned char> disconnect_packet{0xe0, 0};

static void TestShortWrite() {
    Fixture f;
    Check(f.Poll(0) == Result::Pending && output.empty(), "blocked disconnect yields");
    Check(f.Poll(1) == Result::Pending && output.size() == 1, "partial DISCONNECT");
    Check(f.Poll(0) == Result::Pending && output.size() == 1, "partial disconnect blocks");
    Check(f.Poll(1) == Result::Sent && output == disconnect_packet, "complete DISCONNECT wire bytes");
    const auto before = writes;
    Check(f.Poll(10) == Result::Sent && writes == before, "completion is terminal");
}
static void TestPendingWrite(std::size_t first_write) {
    Fixture f;
    f.Publish("first", MQTT_PUBLISH_QOS_1);
    auto* first = mqtt_mq_get(&f.client.mq, 0);
    std::vector<unsigned char> expected(first->start, first->start + first->size);
    allowance = first_write;
    Check(win32mqtt_send(&f.client) == MQTT_OK && first->sending, "start partial application write");
    f.Publish("abandoned");
    Check(f.Poll(0) == Result::Pending && output.size() == first_write, "pending write remains blocked");
    Check(f.Poll(1024) == Result::Pending && output == expected, "finish only the already started packet");
    Check(f.Poll(2) == Result::Sent, "send DISCONNECT without waiting for PUBACK");
    expected.insert(expected.end(), disconnect_packet.begin(), disconnect_packet.end());
    Check(output == expected, "wire contains pending packet then DISCONNECT, no queued work");
}
static void TestFullQueue() {
    Fixture f;
    while (mqtt_publish(&f.client, "queued", "data", 4, MQTT_PUBLISH_QOS_0) == MQTT_OK) {}
    Check(f.client.error == MQTT_ERROR_SEND_BUFFER_IS_FULL, "queue filled");
    Check(f.Poll(2) == Result::Sent && output == disconnect_packet, "full queue cannot block DISCONNECT");
}
static void TestAwaitingAck() {
    Fixture f;
    f.Publish("awaiting", MQTT_PUBLISH_QOS_2);
    allowance = 1024;
    Check(win32mqtt_send(&f.client) == MQTT_OK, "send QoS 2 publish");
    output.clear();
    Check(f.Poll(2) == Result::Sent && output == disconnect_packet, "no PUBREC wait during disconnect");
}
static void TestTimeout(bool pending_application) {
    Fixture f;
    if (pending_application) {
        f.Publish("pending");
        allowance = 1;
        Check(win32mqtt_send(&f.client) == MQTT_OK, "partial packet before timeout");
    }
    Check(f.Poll(0) == Result::Pending, "initial backpressure");
    f.now += MqttDisconnect::Timeout - std::chrono::milliseconds(1);
    Check(f.Poll(0) == Result::Pending, "deadline not reached");
    f.closing.Begin(f.now); // Repeated Disconnect or Stop must not extend the deadline.
    f.now += std::chrono::milliseconds(1);
    const auto before = writes;
    Check(f.Poll(1024) == Result::TimedOut && writes == before, "deadline prevents further writes");
    Check(f.Poll(1024) == Result::TimedOut && writes == before, "timeout is terminal");
    f.closing.Begin(f.now);
    Check(f.Poll(0) == Result::Pending, "new disconnect attempt has a fresh deadline");
}
static void TestError(bool pending_application) {
    Fixture f;
    if (pending_application) {
        f.Publish("pending");
        Check(win32mqtt_send(&f.client) == MQTT_OK, "pending write before failure");
    }
    fail_write = true;
    Check(f.Poll(1) == Result::Failed, "transport failure closes promptly");
    const auto before = writes;
    Check(f.Poll(1) == Result::Failed && writes == before, "failure is terminal");
}
int main() {
    TestShortWrite();
    TestPendingWrite(0);
    TestPendingWrite(3);
    TestFullQueue();
    TestAwaitingAck();
    TestTimeout(false);
    TestTimeout(true);
    TestError(false);
    TestError(true);
    std::cout << "Graceful disconnect tests passed\n";
}
