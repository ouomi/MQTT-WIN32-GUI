#pragma once

#include <cstddef>
#include <deque>
#include <initializer_list>
#include <utility>

namespace win32mqtt {

inline constexpr std::size_t MqttMaxOutgoingPacket = 4096;
inline constexpr std::size_t MqttReceiveCapacity = 8192;

// Includes the fixed header and its variable-length Remaining Length encoding.
inline bool MqttPacketFits(std::initializer_list<std::size_t> fields) {
    std::size_t remaining = 0;
    for (auto size : fields) {
        if (size > MqttMaxOutgoingPacket - remaining) return false;
        remaining += size;
    }
    std::size_t header = 2;
    for (auto encoded = remaining; encoded >= 128; encoded /= 128) ++header;
    return remaining <= MqttMaxOutgoingPacket - header;
}

// Caller serializes access. One zero-cost reserved entry is available for
// Disconnect, even when application work has exhausted either capacity.
template<class T>
class MqttCommandQueue {
    struct Entry { T value; std::size_t bytes; bool control; };
public:
    static constexpr std::size_t MaxCommands = 256;
    static constexpr std::size_t MaxBytes = 1024 * 1024;
    bool Push(T value, std::size_t bytes, bool control = false) {
        if (control) {
            if (control_pending_ || bytes != 0) return false;
        } else if (count_ == MaxCommands || bytes > MaxBytes - bytes_) return false;
        entries_.push_back({std::move(value), bytes, control});
        if (control) control_pending_ = true;
        else { ++count_; bytes_ += bytes; }
        return true;
    }
    T Pop() {
        auto entry = std::move(entries_.front());
        entries_.pop_front();
        if (entry.control) control_pending_ = false;
        else { --count_; bytes_ -= entry.bytes; }
        return std::move(entry.value);
    }
    bool Empty() const { return entries_.empty(); }
    bool ControlPending() const { return control_pending_; }
private:
    std::deque<Entry> entries_;
    std::size_t count_ = 0;
    std::size_t bytes_ = 0;
    bool control_pending_ = false;
};

} // namespace win32mqtt
