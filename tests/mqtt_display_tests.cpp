#include "mqtt/mqtt_event_queue.hpp"
#include "message_log.hpp"
#include "win32/mqtt_window_bridge.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>

using namespace win32mqtt;
static void Check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(EXIT_FAILURE); }
}
static MqttEvent Message(std::string payload) {
    return {MqttEventType::MessageReceived, MqttConnectionState::Connected, {}, "topic",
            std::move(payload), MqttPublishQos::Qos0};
}
static MqttEvent State(MqttConnectionState state) {
    return {MqttEventType::StateChanged, state, {}, {}, {}, MqttPublishQos::Qos0};
}
static void TestCountAndState() {
    MqttEventQueue queue;
    queue.Push(State(MqttConnectionState::Connecting));
    for (unsigned i = 0; i < 600; ++i) queue.Push(Message(std::to_string(i)));
    queue.Push(State(MqttConnectionState::Connected));
    auto batch = queue.Take();
    Check(batch.dropped == 88 && batch.events.size() == 64, "bounded queue and batch");
    Check(batch.events.front().connection_state == MqttConnectionState::Connected &&
          batch.events.front().type == MqttEventType::StateChanged, "latest state survives and is prioritized");
    unsigned expected = 88;
    for (std::size_t i = 1; i < batch.events.size(); ++i)
        Check(batch.events[i].payload == std::to_string(expected++), "retained ordinary events remain FIFO");
    while (!(batch = queue.Take()).events.empty()) {
        Check(batch.dropped == 0, "drop counter reported once");
        for (const auto& event : batch.events) Check(event.payload == std::to_string(expected++), "remaining FIFO");
    }
    Check(expected == 600, "all retained events delivered");
}
static void TestBytesAndClose() {
    MqttEventQueue queue;
    for (unsigned i = 0; i < 40; ++i) queue.Push(Message(std::string(32768, 'x')));
    queue.Push(Message(std::string(32769, 'x')));
    auto batch = queue.Take();
    Check(batch.events.size() == 31 && batch.dropped == 10, "byte budget and oversized event rejection");
    queue.Push(Message("new"));
    Check(queue.Take().events.size() == 1, "draining restores capacity");
    queue.Push(State(MqttConnectionState::Failed));
    queue.Push(Message("pending"));
    queue.Close();
    queue.Push(Message("late"));
    queue.Push(State(MqttConnectionState::Connected));
    batch = queue.Take();
    Check(batch.events.empty() && batch.dropped == 0, "close clears storage and ignores late producers");
}
static void TestConcurrent() {
    MqttEventQueue queue;
    std::atomic<bool> done{false};
    std::thread producer([&] {
        for (unsigned i = 0; i < 10000; ++i) queue.Push(Message(std::to_string(i)));
        queue.Push(State(MqttConnectionState::Disconnected));
        done.store(true);
    });
    std::size_t delivered = 0, dropped = 0;
    bool state_seen = false;
    for (;;) {
        const bool finished = done.load();
        auto batch = queue.Take();
        dropped += batch.dropped;
        Check(batch.events.size() <= 64, "concurrent batch limit");
        for (const auto& event : batch.events) {
            if (event.type == MqttEventType::StateChanged) state_seen = true;
            else ++delivered;
        }
        if (finished && batch.events.empty()) break;
        std::this_thread::yield();
    }
    producer.join();
    Check(delivered + dropped == 10000 && state_seen, "concurrent accounting and final state");
    auto bridge = std::make_unique<MqttWindowBridge>();
    auto handler = bridge->Handler();
    bridge->Close();
    bridge.reset();
    std::thread late([&] { handler(Message("after window teardown")); });
    late.join(); // ASan checks callback lifetime after the window owner disappears.
}
static void TestLog() {
    MessageLog log;
    for (unsigned i = 0; i < 1100; ++i) log.Append(std::to_wstring(i));
    Check(log.Size() == 1000 && log.Text().find(L"100\r\n") == 0, "oldest records evicted");
    log.Clear();
    Check(log.Size() == 0 && log.Characters() == 0 && log.Text().empty(), "clear releases retained content");
    for (unsigned i = 0; i < 40; ++i) log.Append(std::wstring(9000, L'x'));
    Check(log.Size() == 32 && log.Characters() == MessageLog::MaxCharacters, "character budget enforced");
    Check(log.Text().find(L"\u2026\r\n") != std::wstring::npos, "truncation is visible");
    log.Clear();
    log.Append(std::wstring(L"a\0b", 3));
    Check(log.Text() == L"a\u2400b\r\n", "embedded NUL does not hide subsequent content");
    log.Clear();
    std::wstring surrogate(8188, L'x');
    surrogate += static_cast<wchar_t>(0xd83d);
    surrogate += static_cast<wchar_t>(0xde00);
    surrogate += L"trailing";
    log.Append(surrogate);
    Check(log.Text() == std::wstring(8188, L'x') + L"\u2026\r\n", "truncate without splitting UTF-16 pair");
}
int main() {
    TestCountAndState(); TestBytesAndClose(); TestConcurrent(); TestLog();
    std::cout << "Bounded event delivery and log tests passed\n";
}
