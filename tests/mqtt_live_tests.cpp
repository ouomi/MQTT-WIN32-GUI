// Opt-in Windows integration test. Requires an isolated anonymous local broker.
#include "mqtt/mqtt_session.h"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>
using namespace win32mqtt;
using namespace std::chrono_literals;
static void check(bool value, const char* text) { if (!value) { std::cerr << text << '\n'; std::exit(1); } }
template<class F> static void wait(F f) {
    const auto end = std::chrono::steady_clock::now() + 15s;
    while (!f()) { check(std::chrono::steady_clock::now() < end, "integration deadline expired"); std::this_thread::sleep_for(10ms); }
}
int main(int argc, char** argv) {
    check(argc >= 2, "usage: mqtt_live_tests URI [--expect-failure | --will-child TOPIC EVENT]");
    const auto parsed = ParseMqttEndpoint(argv[1]);
    check(parsed.Succeeded(), "invalid broker URI");
    const auto endpoint = parsed.endpoint;
    std::atomic<MqttConnectionState> state{MqttConnectionState::Disconnected};
    std::atomic<unsigned> received{0}; std::atomic<bool> will{false};
    const std::string payload("a\0\xffz", 4);
    const std::string topic = argc >= 4 ? argv[3] : "win32mqtt/test/" + std::to_string(GetCurrentProcessId());
    MqttSession session([&](MqttEvent event) {
        if (event.type == MqttEventType::StateChanged) state = event.connection_state;
        if (event.type == MqttEventType::MessageReceived && event.topic == topic) {
            if (event.payload == payload) ++received;
            if (event.payload == "lost") will = true;
        }
    });
    const bool child = argc >= 3 && std::string(argv[2]) == "--will-child";
    session.Connect(endpoint, topic + (child ? "-child" : "-parent"),
                    child ? std::optional<MqttLastWill>{{topic, "lost"}} : std::nullopt);
    if (argc >= 3 && std::string(argv[2]) == "--expect-failure") {
        wait([&] { return state == MqttConnectionState::Failed; }); session.Stop(); return 0;
    }
    wait([&] { return state == MqttConnectionState::Connected; });
    if (child) {
        check(argc == 5, "will child arguments");
        HANDLE ready = OpenEventA(EVENT_MODIFY_STATE, FALSE, argv[4]);
        check(ready != nullptr && SetEvent(ready), "signal child connected"); CloseHandle(ready);
        Sleep(INFINITE); return 1;
    }
    check(session.Subscribe(topic) == MqttAdmission::Accepted, "subscribe admission");
    wait([&] { auto r = session.Subscriptions(); return r.size() == 1 && r[0].actual; });
    for (auto qos : {MqttPublishQos::Qos0, MqttPublishQos::Qos1, MqttPublishQos::Qos2})
        check(session.Publish(topic, payload, qos) == MqttAdmission::Accepted, "publish admission");
    wait([&] { return received == 3; });
    const std::string name = "Local\\mqtt-live-" + std::to_string(GetCurrentProcessId());
    HANDLE ready = CreateEventA(nullptr, TRUE, FALSE, name.c_str()); check(ready != nullptr, "child event");
    char executable[32768]; check(GetModuleFileNameA(nullptr, executable, sizeof(executable)) != 0, "executable path");
    std::string command = std::string("\"") + executable + "\" \"" + argv[1] + "\" --will-child " + topic + " " + name;
    STARTUPINFOA startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION process{};
    check(CreateProcessA(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process), "start will client");
    const DWORD connected = WaitForSingleObject(ready, 15000);
    // This test owns the child; an abrupt exit is the behavior under test.
    TerminateProcess(process.hProcess, 0);
    WaitForSingleObject(process.hProcess, 5000);
    CloseHandle(process.hThread); CloseHandle(process.hProcess); CloseHandle(ready);
    check(connected == WAIT_OBJECT_0, "will child connected");
    wait([&] { return will.load(); });
    session.Disconnect(); wait([&] { return state == MqttConnectionState::Disconnected; }); session.Stop();
    std::cout << "Real broker binary QoS 0/1/2 and Last Will passed\n";
}
