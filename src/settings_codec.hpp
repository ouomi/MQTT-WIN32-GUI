#pragma once
#include <optional>
#include <climits>
#include <string>
#include <string_view>
namespace win32mqtt {
// Hex code units preserve INI-sensitive whitespace, quotes and line breaks.
inline std::wstring EncodeSetting(std::wstring_view value) {
    constexpr wchar_t digits[] = L"0123456789abcdef";
    std::wstring encoded;
    for (wchar_t c : value) {
        const auto code = static_cast<unsigned long>(c);
        for (int shift = 28; shift >= 0; shift -= 4) encoded += digits[(code >> shift) & 15];
    }
    return encoded;
}
inline std::optional<std::wstring> DecodeSetting(std::wstring_view value) {
    if (value.size() % 8) return std::nullopt;
    std::wstring decoded;
    for (std::size_t i = 0; i < value.size(); i += 8) {
        unsigned long code = 0;
        for (std::size_t j = 0; j < 8; ++j) {
            const auto c = value[i + j];
            if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'))) return std::nullopt;
            code = (code << 4) | (c <= L'9' ? c - L'0' : c - L'a' + 10);
        }
        if (code > static_cast<unsigned long>(WCHAR_MAX)) return std::nullopt;
        decoded += static_cast<wchar_t>(code);
    }
    return decoded;
}
}
