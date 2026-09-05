#include "subscription_catalog.hpp"
#include "mqtt/mqtt_topic.hpp"

#include <algorithm>
#include <utility>

namespace win32mqtt {

std::size_t SubscriptionCatalog::Size() const noexcept {
    return records_.size();
}

const SubscriptionRecord* SubscriptionCatalog::At(std::size_t index) const noexcept {
    return index < records_.size() ? &records_[index] : nullptr;
}

std::size_t SubscriptionCatalog::Find(const std::wstring& topic) const noexcept {
    const auto found = std::find_if(records_.begin(), records_.end(), [&topic](const SubscriptionRecord& record) {
        return record.topic == topic;
    });
    return found == records_.end() ? npos : static_cast<std::size_t>(found - records_.begin());
}

bool SubscriptionCatalog::Add(std::wstring topic) {
    if (!IsValidSubscriptionFilter(topic) || Find(topic) != npos) {
        return false;
    }
    records_.push_back({std::move(topic), false});
    return true;
}

bool SubscriptionCatalog::Remove(std::size_t index) {
    if (index >= records_.size()) {
        return false;
    }
    records_.erase(records_.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

bool SubscriptionCatalog::SetActive(std::size_t index, bool active) {
    if (index >= records_.size()) {
        return false;
    }
    records_[index].active = active;
    return true;
}

void SubscriptionCatalog::Replace(std::vector<SubscriptionRecord> records) {
    records_.clear();
    for (SubscriptionRecord& record : records) {
        if (IsValidSubscriptionFilter(record.topic) && Find(record.topic) == npos) {
            records_.push_back(std::move(record));
        }
    }
}

std::vector<SubscriptionRecord> SubscriptionCatalog::Snapshot() const {
    return records_;
}

std::vector<std::wstring> SubscriptionCatalog::ActiveTopics() const {
    std::vector<std::wstring> topics;
    topics.reserve(records_.size());
    for (const SubscriptionRecord& record : records_) {
        if (record.active) {
            topics.push_back(record.topic);
        }
    }
    return topics;
}

} // namespace win32mqtt
