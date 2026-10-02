// 开机白屏修复:在二级 bootloader 的最早期就把背光脚(GPIO21)拉低并使能输出。
//
// 背景:背光电路在 MCU 引脚悬空时会把背光"点着",而此时 ST7789 面板既没初始化、
// GRAM 也没写入 —— 未驱动的透射式面板在背光下就是一片白。app_main() 里虽然
// 已经把该脚拉低,但那已经是上电 ~560ms 之后了;二级 bootloader 加载/校验整个
// app 镜像的这段时间占了白屏窗口的大头,app 侧代码无论如何都覆盖不到。
//
// 这里用 IDF 的 bootloader 钩子(bootloader_before_init / bootloader_after_init,
// 均为 weak,由本项目强定义覆盖)把"拉低背光"提前到二级 bootloader 的一开始。
// 只用 ROM 函数与寄存器直写,不依赖 driver/任何运行时服务(before_init 阶段
// 连 flash cache 都还没起来)。
//
// before_init 时外设时钟可能还没开,为确保生效,after_init 里再写一次(幂等)。

#include <stdint.h>

#include "esp_rom_gpio.h"
#include "esp_rom_sys.h"
#include "soc/gpio_reg.h"
#include "soc/gpio_sig_map.h"
#include "soc/soc.h"

// 与 components/bsp/include/bsp_pins.h 的 BSP_LCD_BL 保持一致。
// bootloader 不参与 main 的组件依赖,这里直接写死(改背光脚需同步改这里)。
#define BSP_LCD_BL_EARLY 21

// 置 1 打开开机诊断输出(逐阶段打印 GPIO21 的 out/oe/IE + 悬空电平)。
// 日常置 0,只留一行"已拉低"的确认日志。
#define BL_EARLY_DIAG 0

static void backlight_drive_low(void) {
    // 1. 把 pad 接到 GPIO 矩阵
    esp_rom_gpio_pad_select_gpio(BSP_LCD_BL_EARLY);
    // 2. 输出源选 GPIO 寄存器本身(而不是某个外设信号)
    esp_rom_gpio_connect_out_signal(BSP_LCD_BL_EARLY, SIG_GPIO_OUT_IDX, false, false);
    // 3. 先写 0 再开输出:顺序反了会让"开输出"那一瞬输出残留的高电平
    REG_WRITE(GPIO_OUT_W1TC_REG, (1u << BSP_LCD_BL_EARLY));
    REG_WRITE(GPIO_ENABLE_W1TS_REG, (1u << BSP_LCD_BL_EARLY));
}

#if BL_EARLY_DIAG
// 读回 GPIO21 的配置,确认"拉低"真的生效、且没有被后续阶段改回去。
// 只读寄存器,不碰任何 CSR —— ⚠ ESP32-C3 **没有实现标准 cycle CSR(0xC00)**
// (SOC_CPU_HAS_CSR_PC=1,周期计数走私有 CSR 0x7e2)。早期读 0xC00 会直接
// Illegal instruction → bootloader 无限重启。
//   out=输出电平 oe=输出使能 in=引脚读回 IE=输入使能 WPU/WPD=内部上下拉
static void diag_dump(const char *where) {
    uint32_t out = REG_READ(GPIO_OUT_REG);
    uint32_t oe  = REG_READ(GPIO_ENABLE_REG);
    uint32_t in  = REG_READ(GPIO_IN_REG);
    uint32_t pin = REG_READ(GPIO_PIN21_REG);
    esp_rom_printf("[bl_early] %s out=%u oe=%u in=%u IE=%u WPU=%u WPD=%u\n",
                   where,
                   (unsigned)((out >> BSP_LCD_BL_EARLY) & 1u),
                   (unsigned)((oe >> BSP_LCD_BL_EARLY) & 1u),
                   (unsigned)((in >> BSP_LCD_BL_EARLY) & 1u),
                   (unsigned)((pin >> 8) & 1u),
                   (unsigned)((pin >> 7) & 1u),
                   (unsigned)((pin >> 6) & 1u));
}

// 量"悬空时外部电路把该脚拉到什么电平"。
// ⚠ esp_rom_gpio_pad_select_gpio() 只切 MCU_SEL 到 GPIO 功能,**不会**打开
//   输入缓冲(实测调用后 IE 仍为 0,此时读 GPIO_IN 恒为 0,毫无意义)。
//   必须显式置 IE=1,并关掉内部上下拉,读到的才是"外部电路决定"的电平。
// in=1 → 板上(或背光驱动 EN 脚)有上拉,悬空即点亮背光 = 白屏直接成因。
static void diag_float_level(void) {
    esp_rom_gpio_pad_select_gpio(BSP_LCD_BL_EARLY);
    REG_SET_BIT(GPIO_PIN21_REG, 1u << 8);                 // IE=1 打开输入缓冲
    REG_CLR_BIT(GPIO_PIN21_REG, (1u << 7) | (1u << 6));   // WPU=0 WPD=0
    uint32_t pin = REG_READ(GPIO_PIN21_REG);
    uint32_t in  = REG_READ(GPIO_IN_REG);

    // 再挂上内部下拉(~45kΩ)读一次:若仍为高,说明外部上拉很强(或不只是
    // 弱漏电),悬空一定会把背光点亮。
    REG_SET_BIT(GPIO_PIN21_REG, 1u << 6);                 // WPD=1
    uint32_t in_pd = REG_READ(GPIO_IN_REG);
    REG_CLR_BIT(GPIO_PIN21_REG, 1u << 6);                 // 还原

    esp_rom_printf("[bl_early] float IE=%u WPU=%u WPD=%u in=%u in_pd=%u\n",
                   (unsigned)((pin >> 8) & 1u),
                   (unsigned)((pin >> 7) & 1u),
                   (unsigned)((pin >> 6) & 1u),
                   (unsigned)((in >> BSP_LCD_BL_EARLY) & 1u),
                   (unsigned)((in_pd >> BSP_LCD_BL_EARLY) & 1u));
}
#endif

// IDF 的 bootloader 用 `-u bootloader_hooks_include` 强制保留这个符号。
// 原因:钩子在 bootloader_start.c 里是 **weak 引用**,而链接器对"弱未定义"
// 符号**不会**去静态库里提取成员 —— 光定义 bootloader_before_init() 会被
// 直接丢掉(实测反汇编里 call_start_cpu0 的两处钩子调用被编译成
// `li a5,0 / beqz` 而永不执行)。定义这个符号,本目标文件才会被拉进链接。
void bootloader_hooks_include(void);
void bootloader_hooks_include(void) {}

void bootloader_before_init(void) {
#if BL_EARLY_DIAG
    // 1) 悬空时外部电路把它拉到什么电平 —— 判断白屏成因
    diag_float_level();
    diag_dump("pre_hook ");
#endif
    backlight_drive_low();
#if BL_EARLY_DIAG
    diag_dump("post_hook");
#endif
}

void bootloader_after_init(void) {
    backlight_drive_low();
#if BL_EARLY_DIAG
    diag_dump("after_init");
#else
    // 此时 bootloader 控制台已就绪,留一行痕便于日后确认钩子真的生效。
    esp_rom_printf("[bl_early] backlight GPIO%d driven low\n", BSP_LCD_BL_EARLY);
#endif
}
