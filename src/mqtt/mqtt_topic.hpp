#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

namespace win32mqtt {
namespace topic_detail {

// MQTT strings are well-formed UTF-8, at most 65535 encoded bytes, without U+0000.
// Accept both Windows UTF-16 UI strings and UTF-8 session strings without lossy conversion.
template<class Character>
bool Valid(std::basic_string_view<Character> text, bool filter) {
    if (text.empty()) return false;
    std::size_t bytes = 0;
    for (std::size_t i = 0; i < text.size();) {
        const auto position = i;
        std::uint32_t code = static_cast<std::make_unsigned_t<Character>>(text[i++]);
        if constexpr (sizeof(Character) == 1) {
            unsigned continuation = 0;
            std::uint32_t minimum = 0;
            if (code >= 0xc2 && code <= 0xdf) { continuation = 1; minimum = 0x80; code &= 0x1f; }
            else if (code >= 0xe0 && code <= 0xef) { continuation = 2; minimum = 0x800; code &= 0x0f; }
            else if (code >= 0xf0 && code <= 0xf4) { continuation = 3; minimum = 0x10000; code &= 0x07; }
            else if (code >= 0x80) return false;
            for (unsigned n = 0; n < continuation; ++n) {
                if (i == text.size()) return false;
                const auto next = static_cast<unsigned char>(text[i++]);
                if ((next & 0xc0) != 0x80) return false;
                code = (code << 6) | (next & 0x3f);
            }
            if (code < minimum) return false;
        } else if constexpr (sizeof(Character) == 2) {
            if (code >= 0xd800 && code <= 0xdbff) {
                if (i == text.size()) return false;
                const auto low = static_cast<std::uint32_t>(text[i++]);
                if (low < 0xdc00 || low > 0xdfff) return false;
                code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
            }
        }
        if (code == 0 || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
        bytes += code < 0x80 ? 1 : code < 0x800 ? 2 : code < 0x10000 ? 3 : 4;
        if (bytes > 65535) return false;
        if (code == '+' || code == '#') {
            if (!filter || (position != 0 && text[position - 1] != '/')) return false;
            if (code == '#' && i != text.size()) return false;
            if (code == '+' && i != text.size() && text[i] != '/') return false;
        }
    }
    return true;
}
} // namespace topic_detail

inline bool IsValidPublishTopic(std::string_view topic) { return topic_detail::Valid(topic, false); }
inline bool IsValidPublishTopic(std::wstring_view topic) { return topic_detail::Valid(topic, false); }
inline bool IsValidSubscriptionFilter(std::string_view topic) { return topic_detail::Valid(topic, true); }
inline bool IsValidSubscriptionFilter(std::wstring_view topic) { return topic_detail::Valid(topic, true); }

} // namespace win32mqtt
