#pragma once

#include <chrono>
#include <cstddef>

extern "C" {
#include "mqtt_c/mqtt.h"
}

namespace win32mqtt {

// Owned exclusively by the session worker. While active, the caller must stop
// normal MQTT operations and keep the client, transport and queue alive.
class MqttDisconnect {
public:
    using Clock = std::chrono::steady_clock;
    enum class Result { Pending, Sent, TimedOut, Failed };
    static constexpr auto Timeout = std::chrono::seconds(1);

    void Begin(Clock::time_point now) {
        if (result_ == Result::Pending) return;
        deadline_ = now + Timeout;
        offset_ = 0;
        result_ = Result::Pending;
    }

    // At most one nonblocking write per call. No ACK or receive is required for
    // MQTT 3.1.1 DISCONNECT. Pending SSL writes retain their content and length.
    Result Poll(mqtt_client& client, Clock::time_point now) {
        if (result_ != Result::Pending) return result_;
        if (now >= deadline_) return result_ = Result::TimedOut;
        for (ssize_t i = 0; i < mqtt_mq_length(&client.mq); ++i) {
            auto* message = mqtt_mq_get(&client.mq, i);
            if (!message->sending) continue;
            const auto written = Send(client,
                message->start + client.send_offset, message->size - client.send_offset, 0);
            if (written < 0) return result_ = Result::Failed;
            client.send_offset += static_cast<std::size_t>(written);
            if (client.send_offset == message->size) {
                message->sending = 0;
                message->state = MQTT_QUEUED_COMPLETE;
                client.send_offset = 0;
            }
            return Result::Pending;
        }
        // Send outside the normal queue: a full application queue must not stop
        // disconnect, and unsent work is abandoned when the transport closes.
        const auto written = Send(client, packet_ + offset_, sizeof(packet_) - offset_, 0);
        if (written < 0) return result_ = Result::Failed;
        offset_ += static_cast<std::size_t>(written);
        if (offset_ == sizeof(packet_)) result_ = Result::Sent;
        return result_;
    }

private:
    static ssize_t Send(mqtt_client& client, const void* bytes, size_t size, int flags) {
        return client.send_callback ? client.send_callback(client.io_state, bytes, size) :
            mqtt_pal_sendall(client.socketfd, bytes, size, flags);
    }
    Clock::time_point deadline_{};
    std::size_t offset_ = 0;
    Result result_ = Result::Sent;
    const unsigned char packet_[2] = {0xe0, 0x00};
};

} // namespace win32mqtt
