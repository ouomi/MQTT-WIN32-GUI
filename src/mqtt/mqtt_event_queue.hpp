#pragma once

#include "mqtt_session.h"
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>
#include <utility>

namespace win32mqtt {

// The UI polls this mailbox. No per-event window message or raw owning pointer
// crosses threads. The latest connection state is retained independently of load.
class MqttEventQueue {
    struct Entry { MqttEvent event; std::size_t bytes; };
public:
    static constexpr std::size_t MaxEvents = 512;
    static constexpr std::size_t MaxBytes = 1024 * 1024;
    static constexpr std::size_t MaxFieldBytes = 32768;
    static constexpr std::size_t BatchSize = 64;
    struct Batch { std::vector<MqttEvent> events; std::size_t dropped = 0; };

    void Push(MqttEvent event) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return;
        if (event.generation < generation_) return;
        if (event.generation > generation_) {
            generation_ = event.generation;
            dropped_ += events_.size(); events_.clear(); bytes_ = 0; latest_state_.reset();
        }
        const bool state = event.type == MqttEventType::StateChanged;
        if (state) {
            // State payload is not message data. Bound even a malformed producer.
            auto detail = event.detail.substr(0, MaxFieldBytes);
            event.detail = std::move(detail); event.topic.clear(); event.payload.clear();
        } else if (event.detail.size() > MaxFieldBytes || event.topic.size() > MaxFieldBytes ||
                   event.payload.size() > MaxFieldBytes) {
            ++dropped_;
            return;
        }
        const auto bytes = event.detail.size() + event.topic.size() + event.payload.size();
        Entry entry{std::move(event), bytes};
        if (state) { latest_state_ = std::move(entry); return; }
        while (events_.size() >= MaxEvents || bytes > MaxBytes - bytes_) {
            bytes_ -= events_.front().bytes;
            events_.pop_front();
            ++dropped_;
        }
        bytes_ += bytes;
        events_.push_back(std::move(entry));
    }
    Batch Take() {
        std::lock_guard<std::mutex> lock(mutex_);
        Batch batch;
        batch.dropped = dropped_; dropped_ = 0;
        batch.events.reserve(BatchSize);
        // State precedes ordinary history, so sustained traffic cannot delay it.
        if (latest_state_) {
            batch.events.push_back(std::move(latest_state_->event));
            latest_state_.reset();
        }
        while (batch.events.size() < BatchSize && !events_.empty()) {
            bytes_ -= events_.front().bytes;
            batch.events.push_back(std::move(events_.front().event));
            events_.pop_front();
        }
        return batch;
    }
    void Close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        events_.clear(); latest_state_.reset(); bytes_ = 0; dropped_ = 0;
    }
private:
    std::mutex mutex_;
    std::deque<Entry> events_;
    std::optional<Entry> latest_state_;
    std::size_t bytes_ = 0;
    std::size_t dropped_ = 0;
    bool closed_ = false;
    std::uint64_t generation_ = 0;
};

} // namespace win32mqtt
