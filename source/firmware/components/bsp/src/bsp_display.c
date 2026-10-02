// components/bsp/src/bsp_display.c
// 移植自 trae_card/components/platform/platform_esp32/src/disp_st7789.c
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_pins.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "bsp_disp";

static esp_lcd_panel_handle_t    s_panel;
static esp_lcd_panel_io_handle_t s_io;
static bool                      s_bl_ready;
static uint8_t                   s_requested_brightness = 100;
static uint8_t                   s_master_brightness = 80;

// ---------------------------------------------------------------------------
// ST7789P3 厂商专属初始化序列(porch / power / gamma)。
// 这些是【面板厂给的参考例程 TFT_init() 里的值】,不是 ST7789 通用默认值 ——
// 换面板必须找对应厂商要新的一份,照抄这份大概率显示异常。
//
// 以下四条由 esp_lcd 内置驱动完成,故此处不重复:
//   0x11 SLPOUT / 0x3A COLMOD → esp_lcd_panel_init()
//   0x21 INVON                → esp_lcd_panel_invert_color()
//   0x29 DISPON               → esp_lcd_panel_disp_on_off()
//   0x36 MADCTL               → esp_lcd_panel_mirror()(⚠ 别再手动写 0x36,会被它覆盖)
// ---------------------------------------------------------------------------
typedef struct {
    uint8_t  cmd;
    uint8_t  data[16];
    uint8_t  len;
    uint16_t delay_ms;
} st_init_cmd_t;

static const st_init_cmd_t ST7789P3_CMDS[] = {
    {0xB2, {0x05, 0x05, 0x00, 0x33, 0x33}, 5, 0},   // PORCTRL 帧率 porch
    {0xB7, {0x35}, 1, 0},                            // GCTRL 栅极
    {0xBB, {0x21}, 1, 0},                            // VCOMS
    {0xC0, {0x2C}, 1, 0},                            // LCMCTRL
    {0xC2, {0x01}, 1, 0},                            // VDVVRHEN
    {0xC3, {0x0B}, 1, 0},                            // VRHS
    {0xC4, {0x20}, 1, 0},                            // VDVSET
    {0xC6, {0x0F}, 1, 0},                            // FRCTRL2 60Hz 点反转
    {0xD0, {0xA7, 0xA1}, 2, 0},                      // PWCTRL1
    {0xD0, {0xA4, 0xA1}, 2, 0},                      // PWCTRL1(参考例程重发,覆盖上一条)
    {0xD6, {0xA1}, 1, 0},
    {0xE0, {0xD0, 0x04, 0x08, 0x0A, 0x09, 0x05, 0x2D, 0x43,
            0x49, 0x09, 0x16, 0x15, 0x26, 0x2B}, 14, 0},   // PVGAMCTRL 正伽马
    {0xE1, {0xD0, 0x03, 0x09, 0x0A, 0x0A, 0x06, 0x2E, 0x44,
            0x40, 0x3A, 0x15, 0x15, 0x26, 0x2A}, 14, 10},  // NVGAMCTRL 负伽马
};

static void backlight_init(void) {
    if (BSP_LCD_BL < 0) { ESP_LOGW(TAG, "背光引脚未接 MCU,亮度不可调"); return; }
    ledc_timer_config_t t = {
        .speed_mode      = BSP_BL_LEDC_MODE,
        .timer_num       = BSP_BL_LEDC_TIMER,
        .duty_resolution = BSP_BL_LEDC_RES,
        .freq_hz         = BSP_BL_LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t e = ledc_timer_config(&t);
    if (e != ESP_OK) { ESP_LOGE(TAG, "ledc_timer_config 失败: %s", esp_err_to_name(e)); return; }

    ledc_channel_config_t ch = {
        .gpio_num   = BSP_LCD_BL,
        .speed_mode = BSP_BL_LEDC_MODE,
        .channel    = BSP_BL_LEDC_CHANNEL,
        .timer_sel  = BSP_BL_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    e = ledc_channel_config(&ch);
    if (e != ESP_OK) { ESP_LOGE(TAG, "ledc_channel_config 失败: %s", esp_err_to_name(e)); return; }

    s_bl_ready = true;
    ESP_LOGI(TAG, "背光 LEDC 就绪 gpio=%d", BSP_LCD_BL);
}

esp_err_t bsp_display_init(void) {
    if (s_panel) return ESP_OK;

    spi_bus_config_t bus = {
        .mosi_io_num = BSP_LCD_MOSI,
        .sclk_io_num = BSP_LCD_SCLK,
        .miso_io_num = -1, .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = BSP_LCD_W * 80 * 2,
    };
    esp_err_t e = spi_bus_initialize(BSP_LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "SPI 总线初始化失败 (%s) —— 检查 MOSI=GPIO%d / SCLK=GPIO%d 是否冲突",
                 esp_err_to_name(e), BSP_LCD_MOSI, BSP_LCD_SCLK);
        return e;
    }

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = BSP_LCD_CS,
        .dc_gpio_num = BSP_LCD_DC,
        .pclk_hz = BSP_LCD_PCLK_HZ,
        .spi_mode = BSP_LCD_SPI_MODE,
        .lcd_cmd_bits = 8, .lcd_param_bits = 8,
        .trans_queue_depth = 10,
    };
    e = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BSP_LCD_SPI_HOST, &io_cfg, &s_io);
    if (e != ESP_OK) { ESP_LOGE(TAG, "panel_io 创建失败: %s", esp_err_to_name(e)); return e; }

    esp_lcd_panel_dev_config_t dev = {
        .reset_gpio_num = BSP_LCD_RST,          // -1 → SWRESET 软复位
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    e = esp_lcd_new_panel_st7789(s_io, &dev, &s_panel);
    if (e != ESP_OK) { ESP_LOGE(TAG, "面板创建失败: %s", esp_err_to_name(e)); return e; }

    esp_lcd_panel_reset(s_panel);   // rst=-1 时走 SWRESET
    esp_lcd_panel_init(s_panel);    // SLPOUT / COLMOD / RAMCTRL

    for (size_t i = 0; i < sizeof(ST7789P3_CMDS) / sizeof(ST7789P3_CMDS[0]); i++) {
        const st_init_cmd_t *c = &ST7789P3_CMDS[i];
        esp_err_t r = esp_lcd_panel_io_tx_param(s_io, c->cmd, c->data, c->len);
        if (r != ESP_OK) ESP_LOGE(TAG, "厂商初始化命令 0x%02X 失败: %s", c->cmd, esp_err_to_name(r));
        if (c->delay_ms) vTaskDelay(pdMS_TO_TICKS(c->delay_ms));
    }

    esp_lcd_panel_invert_color(s_panel, BSP_LCD_INVERT_COLOR);   // 0x21 / 0x20
    esp_lcd_panel_mirror(s_panel, false, false);                 // 0x36 MADCTL:本板不需镜像(XY 双镜像 = 画面 180°)
    esp_lcd_panel_set_gap(s_panel, 0, 0);
    esp_lcd_panel_disp_on_off(s_panel, true);                    // 0x29 DISPON

    // DISPON 之后、点背光之前,先把整屏 GRAM 铺成黑。
    // ST7789 上电时 GRAM 未初始化,显示出来是一片白;只要有任何时刻背光比
    // "首帧刷完"早一点,露出的就是白闪。这里先钉成黑,把这类白闪的源头去掉。
    {
        esp_err_t fr = bsp_display_fill(0x0000);
        if (fr != ESP_OK) ESP_LOGW(TAG, "清屏失败: %s", esp_err_to_name(fr));
    }

    backlight_init();
    ESP_LOGI(TAG, "显示就绪 %dx%d", BSP_LCD_W, BSP_LCD_H);
    return ESP_OK;
}

esp_lcd_panel_handle_t bsp_display_panel(void) { return s_panel; }

esp_lcd_panel_io_handle_t bsp_display_io(void) { return s_io; }

// 不经 LVGL,直接把整屏 GRAM 同步铺成一个纯色。
//
// 为什么需要它:LVGL 这边是 **12 行单缓冲**(bsp_display_lvgl.c),整屏 320 行要
// 分 27 块推,且 flush 是异步的。所以 lv_refr_now() 未必能把首帧推完 —— 点亮
// 背光时没推到的区域露出来的就是"上电未初始化的白色 GRAM / LVGL 默认白底屏",
// 这就是开机点亮背光那一瞬的白闪。
//
// 这里用 esp_lcd_panel_draw_bitmap 逐块同步写(无回调 → polling 发送,写完才返回),
// 保证返回时整屏已经是一个确定颜色。放在点背光之前调用,白闪就没有了。
//
// color 用 LVGL 的 RGB565 表示(与 lv_color_to_u16 同序);面板方向是 swap_bytes,
// 这里自己交换高低字节,效果与 esp_lvgl_port 的 .swap_bytes = true 一致。
esp_err_t bsp_display_fill(uint16_t color) {
    if (!s_panel) return ESP_ERR_INVALID_STATE;

    const int    chunk = 16;                       // 16 行/块:16*240*2 = 7.5KB
    const size_t n     = (size_t)BSP_LCD_W * chunk;
    uint16_t    *buf   = (uint16_t *)heap_caps_malloc(n * sizeof(uint16_t),
                                                      MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!buf) return ESP_ERR_NO_MEM;

    const uint16_t be = (uint16_t)((color >> 8) | (color << 8));   // → 大端
    for (size_t i = 0; i < n; ++i) buf[i] = be;

    esp_err_t ret = ESP_OK;
    for (int y = 0; y < BSP_LCD_H; y += chunk) {
        const int h = (y + chunk <= BSP_LCD_H) ? chunk : (BSP_LCD_H - y);
        esp_err_t r = esp_lcd_panel_draw_bitmap(s_panel, 0, y, BSP_LCD_W, y + h, buf);
        if (r != ESP_OK) { ret = r; break; }
    }
    // 等待所有已排队的 SPI DMA 传输完全落地,防止释放内存后被 DMA 读取导致数据损坏
    if (s_io) {
        esp_lcd_panel_io_tx_param(s_io, 0x00, NULL, 0);
    }
    heap_caps_free(buf);
    return ret;
}

void bsp_display_backlight(uint8_t percent) {
    if (percent > 100) percent = 100;
    s_requested_brightness = percent;
    percent = (uint8_t)(((uint16_t)percent * s_master_brightness) / 100U);
    if (!s_bl_ready) return;
    uint32_t max_duty = (1u << BSP_BL_LEDC_RES) - 1u;
    uint32_t duty = (max_duty * percent) / 100u;
    ledc_set_duty(BSP_BL_LEDC_MODE, BSP_BL_LEDC_CHANNEL, duty);
    ledc_update_duty(BSP_BL_LEDC_MODE, BSP_BL_LEDC_CHANNEL);
}

void bsp_display_set_master_brightness(uint8_t percent) {
    if (percent > 100) percent = 100;
    s_master_brightness = percent;
    bsp_display_backlight(s_requested_brightness);
}

uint8_t bsp_display_get_master_brightness(void) {
    return s_master_brightness;
}

void bsp_display_power(bool on) {
    if (!s_panel || !s_io) return;      // 未初始化 no-op(初始化序列已 DISPON)
    if (on) {
        // 面板上电 SPI 活动与 ADC1_CH0 耦合,会腐蚀按键读数(假 UP 按下 → 假 PTT)。
        // 窗口覆盖 SLPOUT+120ms+DISPON+LVGL 全屏刷新;期间按键判定返回"未按下"。
        bsp_button_suppress_panel_glitch();
        esp_lcd_panel_io_tx_param(s_io, 0x11, NULL, 0);    // SLPOUT 唤醒
        vTaskDelay(pdMS_TO_TICKS(120));                     // 等内部时钟稳定(SLPIN 后必需)
        esp_lcd_panel_disp_on_off(s_panel, true);           // DISPON 恢复显示
    } else {
        // 顺序很关键:先 DISPOFF(0x28) 停像素驱动,再 SLPIN(0x10) 停内部振荡器。
        // P3 面板实测只发 SLPIN 不灭屏——液晶仍被扫描驱动,留有内容残影。
        esp_lcd_panel_disp_on_off(s_panel, false);          // DISPOFF 像素全灭
        esp_lcd_panel_io_tx_param(s_io, 0x10, NULL, 0);     // SLPIN 睡眠
    }
}
