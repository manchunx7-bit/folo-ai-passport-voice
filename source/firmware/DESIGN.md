---
name: Passport OS shared UI
description: 已有 240×320 LVGL 共享界面的抽样现状记录
colors:
  travel-bg: "#181B1C"
  travel-surface: "#202526"
  travel-surface-alt: "#303738"
  travel-border: "#59605D"
  travel-text: "#EEE7D6"
  travel-text-dim: "#B3B2A5"
  travel-accent: "#DDB36C"
  travel-danger: "#F09B87"
  travel-on-accent: "#181B1C"
  travel-success: "#9DB89A"
  classic-bg: "#03120B"
  classic-surface: "#091E14"
  classic-surface-alt: "#122E20"
  classic-border: "#37694B"
  classic-text: "#A8EDBB"
  classic-text-dim: "#80AF90"
  classic-accent: "#8CF5A6"
  classic-danger: "#FF9585"
  classic-on-accent: "#03120B"
  classic-success: "#8CF5A6"
typography:
  body:
    fontFamily: "Source Han Sans SC"
    fontSize: "16px"
    lineHeight: "20px"
    letterSpacing: "0px"
  title:
    fontFamily: "Source Han Sans SC"
    fontSize: "20px"
    lineHeight: "24px"
  numeric-label:
    fontFamily: "Montserrat"
    fontSize: "14px"
  numeric-display:
    fontFamily: "Montserrat"
    fontSize: "32px"
rounded:
  pixel: "0px"
spacing:
  panel-inset: "7px"
components:
  travel-top-bar:
    backgroundColor: "{colors.travel-surface}"
    rounded: "{rounded.pixel}"
    width: "240px"
    height: "26px"
  travel-panel:
    backgroundColor: "{colors.travel-surface}"
    rounded: "{rounded.pixel}"
    padding: "{spacing.panel-inset}"
---

# Design System: Passport OS shared UI

## Overview

这是已有系统的 scan-mode 记录，不是新品牌或全机重新设计。范围为 `main/ui/ui_palette.h`、`main/ui/ui_pixel.c`、`main/ui/ui_pixel.h`、共享字体与秒表示例，并核对声音跳一跳的继承情况；未逐页审计全机。没有 PRODUCT.md，未补造品牌隐喻或产品承诺。

共享头文件将界面描述为旅行仪器：深色表面、暖纸色文字、琥珀强调。经典绿是另一套已有主题，两套主题共享布局和字体。运行时仍以 `UI_*` 语义角色和 `ui_theme_color()` 为实现来源；上方数值记录现有 palette，不能用于绕开主题 API。

**Key Characteristics:**
- 固定小屏，文字、状态与实体按键说明优先。
- 方角几何与平面色块；共享构件不依赖阴影表达层级。
- 应用可有局部场景色；它们不自动成为全机配色。

## Colors

### Primary

`travel-accent` / `classic-accent` 对应 `UI_ACCENT`，用于强调状态、标题或动作；其反色文字使用 `UI_ON_ACCENT`。`UI_SUCCESS` 和 `UI_DANGER` 是已有状态角色，经典主题中成功与强调共用色值。

### Neutral

两套主题分别保留背景、表面、选中表面、边界、主文字和次文字角色。默认主题是旅行仪器；经典绿的面板会使用边框。值与顺序取自 `ui_pixel.c::s_palettes`，角色取自 `ui_palette.h`。

## Typography

中文正文使用 `buddy_font_16`，共享标题使用 `ui_font_20`，两者均为 Source Han Sans SC 的 LVGL 字体子集；上方行高来自实际字体结构。它们不是浏览器可直接加载的 webfont。正文默认字距取 `UI_LS_BODY`；共享 title plate 单独使用 `UI_LS_TITLE`（1px），声音跳一跳覆盖面板没有调用 title plate，不继承该字距。

数字标签使用 Montserrat 14px。秒表的大数字使用 32px，小时长度变长后缩至 Montserrat 20px。不要由这些应用用法推导完整的全机字号比例。中文文案变化需核对对应子集字形覆盖。

## Layout

共享构件以 240×320 屏幕定位，顶栏高 26px。共享 title plate 与 footer 内容宽 216px、左右各 12px；秒表正文使用左右各 16px。这是并存的现有用法，未抽象出统一间距网格或响应式断点。

共享 footer 有两行：当前操作与次级导航。声音跳一跳继承该信息顺序，但根据场景高度自行定位；其 189px 游戏区、进度和蓄力条属于应用布局，具体说明见 `docs/sound-jump.md`。

## Elevation & Depth

所检查的共享背景、顶栏、面板和秒表构件通过表面色、细边界与留白分层，没有显式阴影样式。声音跳一跳在场景内用几何层叠表现浮岛厚度，不把它登记为通用 elevation token。

## Shapes

共享 `block()`、背景与面板为零圆角。经典主题的普通面板默认有 1px 边框，旅行主题默认无边框；调用方可显式覆盖。title plate 在经典主题为完整边框，旅行主题只保留下边线。不能把无边框写成全局禁令。

## Components

- **顶栏：** `ui_pixel_top_bar()` 使用表面色与 1px 下边线，容纳应用名及状态。
- **面板：** `ui_pixel_panel_create()` 使用调用方传入的语义表面色与共享内边距；选中且可用时 `ui_pixel_set_selected()` 切换至较亮表面与强调色边界。
- **按键说明：** 秒表使用强调色底的“确定”标签和相邻动作文字；声音跳一跳使用纯文字说明。它们提示实体按键，不是触屏按钮，也没有浏览器 hover 状态。
- **电池与信号：** 共享实现使用几何块。电池填充按电量改变颜色，未知电量显示 `--`；不要用假测量值装饰应用。
- **设置焦点：** 设置页当前行使用强调色底与 `UI_ON_ACCENT` 文字，恢复默认的待确认行使用危险色。确认前写明“再次确定”，上下键可取消。
- **操作动效：** 应用选择使用 140ms、6px 的图标纵向反馈，文字保持静止；设置焦点使用 140ms 背景渐显。连续操作会取消同对象旧动画，不叠加往返或循环动画。
- **切换提示：** 应用切换超过 160ms 时显示目标名称与“正在打开”；提示定时器随过渡屏回收，不人为延长切换。
- **静止页面：** 电池、信号和连接点颜色在值变化时才更新；收音机音量柱保留 20Hz 更新机会，时钟与睡眠计时文案按 1Hz 更新。
- **局部游戏画面：** 声音跳一跳复用背景、顶栏、面板和字体，角色、平台、音符及轨迹由 LVGL 几何绘制。场景中的固定色不随系统主题变更；这是应用边界，不是共享主题的新规则。

## Do's and Don'ts

### Do:
- **Do** 通过现有 UI 语义角色读取主题颜色，并保留两个主题的差异。
- **Do** 在实际 240×320 渲染中核对文字覆盖、截断和按键提示。
- **Do** 将游戏场景的构图、色彩及动画限定在应用文档中。

### Don't:
- **Don't** 将秒表或游戏的局部尺寸提升为全机布局规范。
- **Don't** 将字体子集当作可覆盖任意中文的完整字体。
- **Don't** 将 synthetic fixture 图集描述成实机交互、麦克风体验或显示屏验证。
