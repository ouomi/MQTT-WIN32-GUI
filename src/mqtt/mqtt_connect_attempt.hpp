#pragma once

#include <atomic>
#include <chrono>
#include <memory>

namespace win32mqtt {

struct MqttConnectCancellation {
    std::atomic<bool> cancelled{false};
};

// A request's cancellation flag is never reset or shared with a later request.
class MqttConnectAttempt {
public:
    using Clock = std::chrono::steady_clock;
    using Cancellation = std::shared_ptr<MqttConnectCancellation>;
    enum class Phase { Dns, Tcp, Tls, Connack };
    enum class Status { Pending, Cancelled, TimedOut };

    void Begin(Cancellation cancellation, Clock::time_point now) {
        cancellation_ = std::move(cancellation);
        Enter(Phase::Dns, now);
    }
    void Enter(Phase phase, Clock::time_point now) {
        phase_ = phase;
        deadline_ = now + (phase == Phase::Dns ? std::chrono::seconds(5) : std::chrono::seconds(10));
    }
    Status Check(Clock::time_point now) const {
        if (!cancellation_ || cancellation_->cancelled.load()) return Status::Cancelled;
        return now >= deadline_ ? Status::TimedOut : Status::Pending;
    }
    bool Cancelled() const { return !cancellation_ || cancellation_->cancelled.load(); }
    const char* TimeoutMessage() const {
        switch (phase_) {
        case Phase::Dns: return "DNS lookup timed out";
        case Phase::Tcp: return "TCP connection timed out";
        case Phase::Tls: return "TLS handshake timed out";
        case Phase::Connack: return "MQTT CONNACK timed out";
        }
        return "connection timed out";
    }

private:
    Cancellation cancellation_;
    Phase phase_ = Phase::Dns;
    Clock::time_point deadline_{};
};

} // namespace win32mqtt
