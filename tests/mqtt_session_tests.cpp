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
    bool Open(const MqttEndpoint&, const std::function<bool()>& cancelled, std::string&) override {
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
        if (type == 1) input.insert(input.end(), {0x20, 2, 0, 0});
        if (type == 8 || type == 10) {
            ++subscriptions;
            input.insert(input.end(), {static_cast<uint8_t>(type == 8 ? 0x90 : 0xb0),
                static_cast<uint8_t>(type == 8 ? 3 : 2), p[header], p[header + 1]});
            if (type == 8) input.push_back(reject ? 0x80 : 0);
        }
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
    void Close() override { std::lock_guard<std::mutex> lock(mutex); input.clear(); }
    std::chrono::steady_clock::time_point Now() const override {
        return std::chrono::steady_clock::time_point(std::chrono::seconds(seconds.load()));
    }
    std::atomic<int> opens{0}, subscriptions{0}, disconnects{0}, pings{0}, seconds{0}, blocked_calls{0};
    std::atomic<bool> blocked{false}, reject{false}, block_open{false};
    std::mutex mutex; std::deque<uint8_t> input;
};
int main() {
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
    cancel.Connect({"localhost", "1883", false}, "cancel");
    wait([&] { return pending->opens > 0; });
    cancel.Disconnect(); cancel.Stop();
    std::cout << "Production session worker tests passed\n";
}
