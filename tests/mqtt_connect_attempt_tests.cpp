#include "mqtt/mqtt_connect_attempt.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>

using win32mqtt::MqttConnectAttempt;
using win32mqtt::MqttConnectCancellation;
using Status = MqttConnectAttempt::Status;
using Phase = MqttConnectAttempt::Phase;
static void Check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(EXIT_FAILURE); }
}
int main() {
    const MqttConnectAttempt::Clock::time_point start{};
    const Phase phases[] = {Phase::Dns, Phase::Tcp, Phase::Tls, Phase::Connack};
    const char* messages[] = {"DNS lookup timed out", "TCP connection timed out",
                             "TLS handshake timed out", "MQTT CONNACK timed out"};
    for (unsigned index = 0; index < 4; ++index) {
        MqttConnectAttempt attempt;
        auto token = std::make_shared<MqttConnectCancellation>();
        attempt.Begin(token, start);
        attempt.Enter(phases[index], start);
        const auto duration = std::chrono::seconds(index == 0 ? 5 : 10);
        Check(attempt.Check(start + duration - std::chrono::milliseconds(1)) == Status::Pending,
              "pending before deadline");
        Check(attempt.Check(start + duration) == Status::TimedOut, "timeout at boundary");
        Check(attempt.Check(start + std::chrono::seconds(310)) == Status::TimedOut,
              "silent broker cannot wait indefinitely");
        Check(std::strcmp(attempt.TimeoutMessage(), messages[index]) == 0, "phase-specific error");
        token->cancelled.store(true);
        Check(attempt.Check(start) == Status::Cancelled, "phase cancels immediately");
        Check(attempt.Check(start + duration) == Status::Cancelled, "cancellation wins over timeout");
    }
    auto old_token = std::make_shared<MqttConnectCancellation>();
    old_token->cancelled.store(true);
    MqttConnectAttempt old_attempt;
    old_attempt.Begin(old_token, start);
    Check(old_attempt.Check(start) == Status::Cancelled, "Begin preserves cancellation before start");
    old_attempt.Enter(Phase::Tls, start);
    Check(old_attempt.Check(start) == Status::Cancelled, "phase changes cannot reset cancellation");
    auto new_token = std::make_shared<MqttConnectCancellation>();
    MqttConnectAttempt new_attempt;
    new_attempt.Begin(new_token, start);
    Check(new_attempt.Check(start) == Status::Pending && old_attempt.Check(start) == Status::Cancelled,
          "new request does not reactivate old request");
    old_token->cancelled.store(true);
    Check(new_attempt.Check(start) == Status::Pending, "late old cancellation does not cancel new request");
    new_attempt.Enter(Phase::Connack, start + std::chrono::seconds(20));
    Check(new_attempt.Check(start + std::chrono::seconds(29)) == Status::Pending,
          "new phase gets its own budget");
    Check(new_attempt.Check(start + std::chrono::seconds(30)) == Status::TimedOut,
          "CONNACK phase has a firm deadline");
    std::cout << "Connection deadline and cancellation tests passed\n";
}
