#include "mqtt/mqtt_session.h"
extern "C" {
#include "mqtt.h"
ssize_t mqtt_pal_sendall(mqtt_pal_socket_handle, const void*, size_t, int) { return MQTT_ERROR_SOCKET_ERROR; }
ssize_t mqtt_pal_recvall(mqtt_pal_socket_handle, void*, size_t, int) { return MQTT_ERROR_SOCKET_ERROR; }
}
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>
using namespace win32mqtt;
using namespace std::chrono_literals;
static void check(bool value, const char* detail) { if (!value) { std::cerr << detail << '\n'; std::abort(); } }
template<class F> void wait(F f) {
    const auto end = std::chrono::steady_clock::now() + 8s;
    while (!f()) { check(std::chrono::steady_clock::now() < end, "worker wait timed out"); std::this_thread::sleep_for(2ms); }
}
class Broker : public MqttSessionBackend {
public:
    MqttEndpoint opened_endpoint; // Read only after Stop joins the worker.
    bool Open(const MqttEndpoint& endpoint, const std::function<bool()>& cancelled, std::string&) override {
        opened_endpoint = endpoint;
        ++opens;
        while (block_open && !cancelled()) std::this_thread::sleep_for(1ms);
        return !cancelled();
    }
    std::ptrdiff_t Send(const void* bytes, std::size_t n) override {
        if (blocked) { ++blocked_calls; return 0; }
        std::lock_guard<std::mutex> lock(mutex);
        const auto* p = static_cast<const uint8_t*>(bytes);
        mqtt_response response{};
        const auto header = mqtt_unpack_fixed_header(&response, p, n);
        check(header > 0, "complete worker packets");
        const auto type = p[0] >> 4;
        if (type == 1) {
            connect_has_will = (p[header + 7] & 4) != 0;
            input.insert(input.end(), {0x20, 2, 0, 0});
        }
        if (type == 8 || type == 10) {
            ++subscriptions;
            input.insert(input.end(), {static_cast<uint8_t>(type == 8 ? 0x90 : 0xb0),
                static_cast<uint8_t>(type == 8 ? 3 : 2), p[header], p[header + 1]});
            if (type == 8) input.push_back(reject ? 0x80 : 0);
        }
        if (type == 3) {
            ++publishes;
            if (((p[0] >> 1) & 3) == 1 && delay_publish_ack) {
                mqtt_response publish{};
                check(mqtt_unpack_response(&publish, p, n) > 0, "decode outgoing publication");
                delayed_id = publish.decoded.publish.packet_id;
                if (++qos1_sends == 2)
                    input.insert(input.end(), {0x40, 2, static_cast<uint8_t>(delayed_id >> 8),
                                               static_cast<uint8_t>(delayed_id)});
            }
        }
        if (type == 5) ++pubrecs;
        if (type == 7) ++pubcomps;
        if (type == 12) ++pings; // deliberately no PINGRESP
        if (type == 14) ++disconnects;
        return n;
    }
    std::ptrdiff_t Receive(void* bytes, std::size_t size) override {
        std::lock_guard<std::mutex> lock(mutex);
        auto* p = static_cast<uint8_t*>(bytes); std::size_t n = 0;
        while (n < size && !input.empty()) { p[n++] = input.front(); input.pop_front(); }
        return n;
    }
    void Close() override { std::lock_guard<std::mutex> lock(mutex); input.clear(); ++closes; }
    std::chrono::steady_clock::time_point Now() const override {
        return std::chrono::steady_clock::time_point(std::chrono::seconds(seconds.load()));
    }
    std::atomic<bool> connect_has_will{false};
    std::atomic<int> closes{0};
    std::atomic<int> opens{0}, subscriptions{0}, disconnects{0}, pings{0}, seconds{0}, blocked_calls{0};
    std::atomic<int> publishes{0}, qos1_sends{0}, pubrecs{0}, pubcomps{0};
    bool delay_publish_ack = false; // Set before the session worker is created.
    uint16_t delayed_id = 0; // Protected by mutex.
    std::atomic<bool> blocked{false}, reject{false}, block_open{false};
    std::mutex mutex; std::deque<uint8_t> input;
};
static void test_abnormal_disconnect() {
    auto broker = std::make_shared<Broker>();
    std::atomic<MqttConnectionState> state{MqttConnectionState::Disconnected};
    std::atomic<int> simulated{0};
    MqttSession session([&](MqttEvent event) {
        if (event.type == MqttEventType::StateChanged) {
            if (event.simulated_disconnect) {
                check(event.connection_state == MqttConnectionState::Disconnected, "simulation reports disconnected");
                ++simulated;
            }
            state = event.connection_state;
        }
    }, broker);
    check(session.Connect({"localhost", "1883", false}, "will-test", MqttLastWill{"last/will", "offline"})
          == MqttAdmission::Accepted, "will connection accepted");
    wait([&] { return state == MqttConnectionState::Connected; });
    check(broker->connect_has_will, "CONNECT registers Last Will");
    // Closing must work even when pending application bytes cannot be sent.
    broker->blocked = true;
    session.Publish("busy", "pending", MqttPublishQos::Qos1);
    wait([&] { return broker->blocked_calls > 0; });
    const auto closes = broker->closes.load();
    check(session.SimulateAbnormalDisconnect() == MqttAdmission::Accepted, "simulation accepted");
    wait([&] { return state == MqttConnectionState::Disconnected; });
    check(broker->closes > closes && simulated == 1, "transport closed before simulation completion");
    check(broker->disconnects == 0, "simulation does not send MQTT DISCONNECT");
    check(broker->opens == 1, "simulation does not reconnect");
    // The same session remains usable, and ordinary disconnect stays graceful.
    broker->blocked = false;
    session.Connect({"localhost", "1883", false}, "will-test", MqttLastWill{"last/will", "offline"});
    wait([&] { return state == MqttConnectionState::Connected; });
    session.Disconnect();
    wait([&] { return state == MqttConnectionState::Disconnected; });
    check(broker->disconnects == 1 && simulated == 1, "normal disconnect is not a simulation");
    session.Stop();
}

static void test_protocol_recovery_in_worker() {
    auto broker = std::make_shared<Broker>();
    broker->delay_publish_ack = true;
    std::atomic<MqttConnectionState> state{MqttConnectionState::Disconnected};
    std::atomic<int> messages{0};
    MqttSession session([&](MqttEvent event) {
        if (event.type == MqttEventType::StateChanged) state = event.connection_state;
        if (event.type == MqttEventType::MessageReceived) ++messages;
    }, broker);
    check(session.Connect({"localhost", "1883", false}, "recovery") == MqttAdmission::Accepted, "recovery connect");
    wait([&] { return state == MqttConnectionState::Connected; });
    check(session.Publish("t", "x", MqttPublishQos::Qos1) == MqttAdmission::Accepted, "QoS1 worker publication");
    wait([&] { return broker->qos1_sends == 1; });
    broker->seconds = 31;
    wait([&] { return broker->qos1_sends == 2; });
    // Small batches let the worker send every request and reclaim the old ACK.
    for (int batch = 0; batch < 25; ++batch) {
        for (int i = 0; i < 8; ++i)
            check(session.Publish("t", "x", MqttPublishQos::Qos0) == MqttAdmission::Accepted, "worker churn admission");
        wait([&] { return broker->publishes >= 2 + 8 * (batch + 1); });
        session.TakePublishResults();
    }
    {
        std::lock_guard<std::mutex> lock(broker->mutex);
        broker->input.insert(broker->input.end(), {0x40, 2,
            static_cast<uint8_t>(broker->delayed_id >> 8), static_cast<uint8_t>(broker->delayed_id)});
        // Broker-injected QoS2 burst exercises production receiving regardless
        // of the UI's current QoS0 subscription preference.
        for (int id = 1; id <= 200; ++id)
            broker->input.insert(broker->input.end(), {0x34, 5, 0, 1, 't',
                static_cast<uint8_t>(id >> 8), static_cast<uint8_t>(id)});
    }
    wait([&] { return messages == 200 && broker->pubrecs == 200; });
    {
        std::lock_guard<std::mutex> lock(broker->mutex);
        for (int id = 1; id <= 200; ++id)
            broker->input.insert(broker->input.end(), {0x62, 2,
                static_cast<uint8_t>(id >> 8), static_cast<uint8_t>(id)});
        broker->input.insert(broker->input.end(), {0x62, 2, 0, 1});
    }
    wait([&] { return broker->pubcomps == 201; });
    check(state == MqttConnectionState::Connected && messages == 200,
          "late ACK and QoS2 burst keep production session connected without duplicate delivery");
    session.Stop();
}

int main() {
    test_abnormal_disconnect();
    test_protocol_recovery_in_worker();
    MqttSubscriptions model;
    check(model.SetDesired({"topic"}), "initial model intent");
    model.Advance(1, [](const auto&, bool) { return 10; });
    model.Complete("topic", 10, true);
    check(model.SetDesired({}), "reverse subscription intent");
    model.Advance(1, [](const auto&, bool) { return 11; });
    model.Complete("topic", 10, true);
    check(model.Snapshot()[0].pending && model.Snapshot()[0].actual, "late prior ACK cannot complete new operation");
    model.Complete("topic", 11, true);
    check(!model.Snapshot()[0].pending && !model.Snapshot()[0].actual, "matching ACK completes unsubscribe");
    auto broker = std::make_shared<Broker>();
    std::atomic<MqttConnectionState> state{MqttConnectionState::Disconnected};
    std::atomic<int> callbacks{0};
    MqttSession session([&](MqttEvent event) { ++callbacks; if (event.type == MqttEventType::StateChanged) state = event.connection_state; }, broker);
    check(session.Connect({"localhost", "1883", false}, "test") == MqttAdmission::Accepted, "connect accepted");
    wait([&] { return state == MqttConnectionState::Connected; });
    std::vector<std::string> topics;
    for (int i = 0; i < 100; ++i) topics.push_back(std::to_string(i) + std::string(150, 't'));
    check(session.SetSubscriptions(topics) == MqttAdmission::Accepted, "desired set accepted");
    wait([&] { auto records = session.Subscriptions(); return records.size() == topics.size() && std::all_of(records.begin(), records.end(), [](const auto& r) { return r.actual && !r.pending; }); });
    check(session.SetSubscriptions({}) == MqttAdmission::Accepted, "unsubscribe intent");
    wait([&] { auto records = session.Subscriptions(); return std::none_of(records.begin(), records.end(), [](const auto& r) { return r.actual || r.pending; }); });
    broker->reject = true;
    check(session.SetSubscriptions({"rejected"}) == MqttAdmission::Accepted, "rejected topic accepted locally");
    wait([&] { auto r = session.Subscriptions(); return r.size() == 1 && !r[0].error.empty() && !r[0].actual; });
    session.Publish("topic", "payload", MqttPublishQos::Qos0);
    wait([&] { auto results = session.TakePublishResults(); return !results.empty() && results[0].type == MqttEventType::PublishQueued && results[0].operation != 0; });
    // Result storage is independent of the lossy UI event queue and applies
    // admission pressure until the UI has consumed every accepted result.
    for (int i = 0; i < 256; ++i)
        check(session.Publish("topic", "payload", MqttPublishQos::Qos0) == MqttAdmission::Accepted, "result capacity admission");
    check(session.Publish("topic", "overflow", MqttPublishQos::Qos0) == MqttAdmission::QueueFull, "unconsumed results bounded");
    std::size_t results_seen = 0;
    wait([&] { results_seen += session.TakePublishResults().size(); return results_seen == 256; });
    broker->reject = false; broker->blocked = true;
    check(session.Publish("topic", std::string(3980, 'x'), MqttPublishQos::Qos0) == MqttAdmission::Accepted, "block large publish");
    wait([&] { return broker->blocked_calls >= 2; });
    check(session.SetSubscriptions({std::string(4000, 't')}) == MqttAdmission::Accepted, "retain desired state during send congestion");
    const int attempts = broker->blocked_calls;
    wait([&] { return broker->blocked_calls > attempts + 2; });
    { auto r = session.Subscriptions(); check(r.size() == 1 && r[0].desired && !r[0].pending && r[0].error.empty(), "capacity waits without losing intent"); }
    broker->blocked = false;
    wait([&] { auto r = session.Subscriptions(); return r.size() == 1 && r[0].actual; });
    broker->seconds = 61;
    wait([&] { return broker->pings > 0; });
    broker->seconds = 91;
    wait([&] { return state == MqttConnectionState::Failed; });
    broker->seconds = 100;
    session.Connect({"localhost", "1883", false}, "replacement");
    wait([&] { return state == MqttConnectionState::Connected; });
    session.Connect({"localhost", "1883", false}, "replacement2");
    wait([&] { return broker->opens >= 3 && state == MqttConnectionState::Connected; });
    check(broker->disconnects >= 1, "replacement sends DISCONNECT");
    session.Stop();
    const int stopped_callbacks = callbacks;
    std::this_thread::sleep_for(30ms);
    check(callbacks == stopped_callbacks, "no callbacks after Stop joins");
    auto pending = std::make_shared<Broker>(); pending->block_open = true;
    MqttSession cancel([](MqttEvent) {}, pending);
    cancel.Connect({"203.0.113.10", "8883", true, "broker.example.com"}, "cancel");
    wait([&] { return pending->opens > 0; });
    cancel.Disconnect(); cancel.Stop();
    check(pending->opened_endpoint.host == "203.0.113.10" &&
          pending->opened_endpoint.tls_server_name == "broker.example.com",
          "connection queue preserves destination IP separately from TLS identity");
    std::cout << "Production session worker tests passed\n";
}
