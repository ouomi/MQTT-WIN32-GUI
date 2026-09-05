#pragma once
#include "mqtt_session.h"
#include <deque>

namespace win32mqtt {
class MqttMessageStore {
public:
    static constexpr std::size_t MaxBytes = 1024 * 1024;
    static constexpr std::size_t MaxRecords = 512;
    void Push(MqttEvent event) {
        const auto bytes = event.topic.size() + event.payload.size();
        if (bytes > MaxBytes) return;
        while (!records_.empty() && (records_.size() >= MaxRecords || bytes_ + bytes > MaxBytes)) {
            bytes_ -= records_.front().topic.size() + records_.front().payload.size();
            records_.pop_front();
        }
        bytes_ += bytes;
        records_.push_back(std::move(event));
    }
    void Clear() { records_.clear(); bytes_ = 0; }
    const std::deque<MqttEvent>& Records() const { return records_; }
    static std::wstring Hex(const std::string& bytes) {
        constexpr wchar_t digits[] = L"0123456789ABCDEF";
        std::wstring text;
        for (unsigned char byte : bytes) {
            text += digits[byte >> 4]; text += digits[byte & 15]; text += L' ';
        }
        return text;
    }
private:
    std::deque<MqttEvent> records_;
    std::size_t bytes_ = 0;
};
}
