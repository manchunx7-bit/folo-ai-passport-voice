#pragma once
#include <string>
#include <string_view>

namespace passport {
// Bound retained server text before allocating a label copy. Inputs are UTF-8
// JSON strings; skip continuation bytes when removing the oldest characters.
inline std::string_view utf8_tail(std::string_view text, size_t limit) {
    if (text.size() <= limit) return text;
    size_t start = text.size() - limit;
    while (start < text.size() &&
           (static_cast<unsigned char>(text[start]) & 0xC0) == 0x80) ++start;
    return text.substr(start);
}
inline void append_utf8_tail(std::string &buffer, std::string_view text, size_t limit) {
    if (text.size() >= limit) {
        const auto tail = utf8_tail(text, limit);
        buffer.assign(tail.data(), tail.size());
        return;
    }
    const auto tail = utf8_tail(buffer, limit - text.size());
    buffer.erase(0, buffer.size() - tail.size());
    buffer.append(text.data(), text.size());
}
}
