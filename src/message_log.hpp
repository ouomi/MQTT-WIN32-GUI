#pragma once

#include <deque>
#include <string>

namespace win32mqtt {

// Limits are wchar_t units (UTF-16 units on Windows), including line endings.
class MessageLog {
public:
    static constexpr std::size_t MaxRecords = 1000;
    static constexpr std::size_t MaxCharacters = 256 * 1024;
    static constexpr std::size_t MaxRecordCharacters = 8192;
    void Append(const std::wstring& message) {
        auto line = message.substr(0, MaxRecordCharacters - 2);
        if (message.size() > line.size()) {
            line.resize(MaxRecordCharacters - 3); // Reserve ellipsis and CRLF.
            // Do not retain a high surrogate whose low surrogate was truncated.
            if (!line.empty() && line.back() >= 0xd800 && line.back() <= 0xdbff) line.pop_back();
            line += L'\u2026';
        }
        for (auto& character : line) if (character == L'\0') character = L'\u2400';
        line += L"\r\n";
        while (records_.size() >= MaxRecords || line.size() > MaxCharacters - characters_) {
            characters_ -= records_.front().size();
            records_.pop_front();
        }
        characters_ += line.size();
        records_.push_back(std::move(line));
    }
    void Clear() { records_.clear(); characters_ = 0; }
    std::wstring Text() const {
        std::wstring text;
        text.reserve(characters_);
        for (const auto& record : records_) text += record;
        return text;
    }
    std::size_t Size() const { return records_.size(); }
    std::size_t Characters() const { return characters_; }
private:
    std::deque<std::wstring> records_;
    std::size_t characters_ = 0;
};

} // namespace win32mqtt
