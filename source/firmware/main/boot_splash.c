// main/boot_splash.c —— 开机动画:黑底磷光绿。
//   "PASSPORT OS" 打字机式出现 + 尾部光标闪烁,下方 14 格绿点进度条逐格点亮。
//   总时长 ~0.9s,与开机音(chime)同时发生,声画同步。
#include "boot_splash.h"
#include "ui_palette.h"

#include "bsp_display.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <stdbool.h>

static const char *TAG = "boot_splash";

// 与首页同源的磷光配色
#define C_BG      UI_BG
#define C_PHOS    UI_ACCENT
#define C_PHOSDIM UI_TEXT_DIM
#define C_BORDER  UI_BORDER

#define BAR_N     14
#define STEP_MS   62          // 14 格 × 62ms ≈ 0.87s

static lv_obj_t *s_splash;

static lv_obj_t *blk(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    return o;
}

void boot_splash_run(void) {
    bsp_lvgl_lock(-1);

    // 确保此前任何在飞的 SPI DMA 事务完全落地
    esp_lcd_panel_io_handle_t io = bsp_display_io();
    if (io) {
        esp_lcd_panel_io_tx_param(io, 0x00, NULL, 0);
    }

    s_splash = lv_obj_create(NULL);
    lv_obj_remove_flag(s_splash, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_splash, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(s_splash, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_splash, 0, 0);
    lv_obj_set_style_border_width(s_splash, 0, 0);
    lv_obj_set_style_outline_width(s_splash, 0, 0);
    lv_obj_set_style_shadow_width(s_splash, 0, 0);
    lv_obj_set_style_pad_all(s_splash, 0, 0);

    // 主标题:PASSPORT OS(打字机由"光标块"配合实现 —— 文本先整段放好,
    // 用一块与底色同色的遮罩盖住,随节拍右移露出,等效打字且不重排文本)
    lv_obj_t *title = lv_label_create(s_splash);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(C_PHOS), 0);
    lv_obj_set_style_text_letter_space(title, 3, 0);
    lv_label_set_text(title, "PASSPORT OS");
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -26);

    // 副标题
    lv_obj_t *sub = lv_label_create(s_splash);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(C_PHOSDIM), 0);
    lv_obj_set_style_text_letter_space(sub, 1, 0);
    lv_label_set_text(sub, "TRAVEL INSTRUMENT");
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 4);

    // Measure the actual title rather than assuming a 150 px width. A mask
    // reveals the existing text, so its alignment never jumps between frames.
    lv_obj_update_layout(title);
    const int title_x = lv_obj_get_x(title);
    const int title_y = lv_obj_get_y(title);
    const int title_w = lv_obj_get_width(title);
    const int title_h = lv_obj_get_height(title);
    lv_obj_t *mask = blk(s_splash, title_x, title_y, title_w, title_h, C_BG);
    lv_obj_t *cursor = blk(s_splash, title_x, title_y, 2, title_h, C_PHOS);

    // 进度条:14 格 10×10,间隙 3px,总宽 179,水平居中
    (void)blk(s_splash, (240 - (BAR_N * 13 - 3)) / 2, 186,
              BAR_N * 13 - 3, 10, UI_SURFACE_ALT);
    lv_obj_t *cells[BAR_N];
    for (int i = 0; i < BAR_N; ++i) {
        cells[i] = blk(s_splash, (240 - (BAR_N * 13 - 3)) / 2 + i * 13, 186,
                       10, 10, C_PHOS);
        lv_obj_add_flag(cells[i], LV_OBJ_FLAG_HIDDEN);
    }

    lv_screen_load(s_splash);

    // 在全程持锁保护下，同步将 320 行（27 块）完整首帧推到 ST7789
    // 不经中途解锁，彻底杜绝 taskLVGL 与直写 DMA 冲突导致的顶部红线/错位
    const int64_t t0 = esp_timer_get_time();
    lv_refr_now(lv_display_get_default());
    if (io) {
        esp_lcd_panel_io_tx_param(io, 0x00, NULL, 0); // 阻塞等待最后一块 DMA 落地
    }
    const int64_t t1 = esp_timer_get_time();
    ESP_LOGI(TAG, "first frame synced: refr=%lldms", (long long)((t1 - t0) / 1000));
    bsp_lvgl_unlock();

    // 首帧完全落地后再渐亮背光（140ms，CRT 上电质感，无白闪亦无红线）
    for (int b = 0; b <= 100; b += 10) {
        bsp_display_backlight(b);
        vTaskDelay(pdMS_TO_TICKS(14));
    }

    // 节拍:光标在标题宽度(约 150px)内随进度右移,格点同步点亮
    for (int i = 0; i < BAR_N; ++i) {
        vTaskDelay(pdMS_TO_TICKS(STEP_MS));
        if (!bsp_lvgl_lock(200)) continue;
        lv_obj_clear_flag(cells[i], LV_OBJ_FLAG_HIDDEN);
        const int revealed = title_w * (i + 1) / BAR_N;
        lv_obj_set_x(mask, title_x + revealed);
        lv_obj_set_width(mask, title_w - revealed);
        lv_obj_set_x(cursor, title_x + revealed);
        if (i + 1 == BAR_N) {
            lv_obj_add_flag(mask, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(cursor, LV_OBJ_FLAG_HIDDEN);
        }
        // 光标闪烁:偶数格半透明
        lv_obj_set_style_opa(cursor, (i % 2) ? LV_OPA_40 : LV_OPA_COVER, 0);
        bsp_lvgl_unlock();
    }
    vTaskDelay(pdMS_TO_TICKS(140));
    ESP_LOGI(TAG, "splash done");
}

void boot_splash_end(void) {
    if (!s_splash) return;
    if (!bsp_lvgl_lock(500)) return;
    lv_obj_delete(s_splash);
    s_splash = NULL;
    bsp_lvgl_unlock();
}
