#pragma once
#include <cstddef>

enum class WifiKeyboardPage { Lower, Upper, Symbols, MoreSymbols };

namespace passport::wifi_keyboard {
// Control tokens are longer than one byte, so every printable ASCII character,
// including '<' and a space, can be entered without colliding with an action.
inline constexpr const char *lower[] = {
    "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l",
    "m", "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x",
    "y", "z", " ", "ABC", "123", "DEL", "GO",
};
inline constexpr const char *upper[] = {
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L",
    "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X",
    "Y", "Z", " ", "abc", "123", "DEL", "GO",
};
inline constexpr const char *symbols[] = {
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "-", "_",
    ".", "@", "#", "$", "%", "&", "*", "!", "+", "=", "?", "/",
    ":", ";", "(", ")", "[", "]", " ", "abc", "#+=", "DEL", "GO",
};
inline constexpr const char *more[] = {
    "`", "~", "\\", "|", "{", "}", "^", "\"", "'", "<", ">", ",",
    " ", "abc", "123", "DEL", "GO",
};
struct Page { const char *const *keys; std::size_t count; };
template <std::size_t N> constexpr Page view(const char *const (&keys)[N]) {
    static_assert(N <= 35, "Wi-Fi keyboard exceeds its fixed widget budget");
    return {keys, N};
}
inline constexpr Page page(WifiKeyboardPage value) {
    switch (value) {
        case WifiKeyboardPage::Lower: return view(lower);
        case WifiKeyboardPage::Upper: return view(upper);
        case WifiKeyboardPage::Symbols: return view(symbols);
        case WifiKeyboardPage::MoreSymbols: return view(more);
    }
    return {nullptr, 0};
}
inline const char *key(WifiKeyboardPage value, std::size_t index) {
    const auto p = page(value);
    return index < p.count ? p.keys[index] : "";
}
} // namespace passport::wifi_keyboard
