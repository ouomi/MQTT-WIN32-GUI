#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace win32mqtt {
struct MqttSubscriptionStatus {
    std::string topic;
    bool desired = false;
    bool actual = false;
    bool pending = false;
    bool requested = false;
    std::uint64_t generation = 0;
    std::uint64_t operation = 0;
    std::uint16_t packet_id = 0;
    std::string error;
};
// Worker-owned reconciliation; the session serializes access with its mutex.
// Only one exchange is in flight, so restoration never floods the packet queue.
class MqttSubscriptions {
public:
    bool SetDesired(const std::vector<std::string>& topics) {
        if (topics.size() > 256) return false;
        auto next = records_;
        for (auto& record : next) {
            const bool desired = std::find(topics.begin(), topics.end(), record.topic) != topics.end();
            if (record.desired != desired) record.error.clear();
            record.desired = desired;
        }
        for (const auto& topic : topics) {
            if (std::none_of(next.begin(), next.end(), [&](const auto& r) { return r.topic == topic; })) {
                MqttSubscriptionStatus record; record.topic = topic; record.desired = true;
                next.push_back(std::move(record));
            }
        }
        next.erase(std::remove_if(next.begin(), next.end(), [](const auto& r) {
            return !r.desired && !r.actual && !r.pending;
        }), next.end());
        if (next.size() > 256) return false;
        records_ = std::move(next);
        return true;
    }
    void Reset(std::uint64_t generation) {
        for (auto& r : records_) {
            r.actual = r.pending = false; r.generation = generation; r.error.clear();
        }
    }
    template<class Send> void Advance(std::uint64_t generation, Send send) {
        for (const auto& r : records_) if (r.pending) return;
        for (auto& r : records_) {
            if (r.desired == r.actual || !r.error.empty()) continue;
            const int result = send(r.topic, r.desired);
            if (result == 0) return; // capacity: retry on next worker tick
            r.generation = generation;
            r.operation = ++operation_;
            if (result < 0) { r.error = "subscription request failed"; return; }
            r.pending = true; r.requested = r.desired;
            r.packet_id = static_cast<std::uint16_t>(result);
            return;
        }
    }
    void Complete(const std::string& topic, std::uint16_t packet_id, bool accepted) {
        for (auto& r : records_) if (r.topic == topic && r.pending && r.packet_id == packet_id) {
            r.pending = false;
            if (accepted) r.actual = r.requested;
            else r.error = "Broker rejected subscription";
            return;
        }
    }
    const std::vector<MqttSubscriptionStatus>& Snapshot() const { return records_; }
private:
    std::vector<MqttSubscriptionStatus> records_;
    std::uint64_t operation_ = 0;
};
}
