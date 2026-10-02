#include "launcher/wifi_keyboard.h"
#include <cassert>
#include <initializer_list>
#include <cstring>
#include <cstdio>

int main() {
    bool available[127]{};
    for (auto p : {WifiKeyboardPage::Lower, WifiKeyboardPage::Upper,
                   WifiKeyboardPage::Symbols, WifiKeyboardPage::MoreSymbols}) {
        const auto page = passport::wifi_keyboard::page(p);
        bool space=false, backspace=false, connect=false;
        assert(page.count <= 35);
        for (std::size_t i=0;i<page.count;i++) {
            const char *key=passport::wifi_keyboard::key(p,i);
            if (std::strlen(key)==1) available[(unsigned char)key[0]]=true;
            space |= std::strcmp(key," ")==0;
            backspace |= std::strcmp(key,"DEL")==0;
            connect |= std::strcmp(key,"GO")==0;
        }
        assert(space && backspace && connect);
        assert(!*passport::wifi_keyboard::key(p,page.count));
    }
    for (int c=32;c<=126;c++) assert(available[c]);
    puts("Wi-Fi keyboard: all 95 printable ASCII characters and separate controls PASS");
}
