#include "ui/utf8_tail.h"
#include "launcher/wake_gate.h"
#include <cassert>
#include <string>

int main() {
    wake_gate_t gate{};
    assert(!wake_gate_consume(&gate, true));
    wake_gate_arm(&gate);
    // Both short-click and long-hold tails must be swallowed, even after release.
    for (int event = 0; event < 5; ++event) assert(wake_gate_consume(&gate, false));
    assert(!wake_gate_consume(&gate, true));
    assert(!wake_gate_consume(&gate, false));
    wake_gate_arm(&gate);
    assert(wake_gate_consume(&gate, false));
    assert(!wake_gate_consume(&gate, true));

    assert(passport::utf8_tail("中文A", 4) == "文A");
    assert(passport::utf8_tail("中文A", 3) == "A");
    assert(passport::utf8_tail("中文A", 0).empty());
    assert(passport::utf8_tail("A😀B", 5) == "😀B");
    assert(passport::utf8_tail("A😀B", 4) == "B");
    std::string reply = "中文";
    passport::append_utf8_tail(reply, "你好", 9);
    assert(reply == "文你好");
    passport::append_utf8_tail(reply, std::string(100000, 'x') + "完", 12);
    assert(reply == std::string(9, 'x') + "完");
    for (int i = 0; i < 2000; ++i) {
        passport::append_utf8_tail(reply, "中😀A", 1200);
        assert(reply.size() <= 1200);
        assert((static_cast<unsigned char>(reply[0]) & 0xC0) != 0x80);
    }
}
