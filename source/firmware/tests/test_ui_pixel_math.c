#include <assert.h>
#include "ui_pixel_math.h"
int main(void) {
    assert(ui_pixel_blink_frame(1649) == 0);
    assert(ui_pixel_blink_frame(1650) == 1);
    assert(ui_pixel_blink_frame(1799) == 1);
    assert(ui_pixel_blink_frame(1800) == 0);
    assert(ui_pixel_blink_frame(3650) == 1);
    assert(ui_pixel_jump_offset(2) == -5);
    assert(ui_pixel_jump_offset(999) == 0);
    return 0;
}
