#pragma once

/* =====================================================================
 *  liuyao.h —— 六爻占卜 APP（Flipper Zero 官方 SDK / C 语言）
 *  说明：Flipper 固件默认点阵字体不含汉字，故界面与卦名统一使用拼音，
 *        中文含义在注释中标注。如需显示汉字，可自行打包中文字库后替换。
 * ===================================================================== */

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <gui/input.h>
#include <furi_hal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---------------------- 应用状态机 ---------------------- */
typedef enum {
    AppStateWelcome,  /* 欢迎页：六爻起卦 */
    AppStateTossing,  /* 摇卦中：每次 OK 生成一爻 */
    AppStateResult,   /* 结果页：展示本卦/变卦与每爻类型 */
} AppState;

/* ---------------------- 爻的类型 ----------------------
 * 采用传统铜钱/蓍草记数：6=老阴 7=少阳 8=少阴 9=老阳
 * 老阴(6)、老阳(9) 为“变爻”，向相反属性转化（老阳→阴、老阴→阳）
 */
typedef enum {
    LineOldYin    = 6, /* 老阴：阴爻，变爻，将化为阳 */
    LineYoungYang = 7, /* 少阳：阳爻，不变 */
    LineYoungYin  = 8, /* 少阴：阴爻，不变 */
    LineOldYang   = 9, /* 老阳：阳爻，变爻，将化为阴 */
} YaoType;

/* ---------------------- 应用主结构体 ----------------------
 * 所有运行态集中保存，避免使用全局变量与运行时 malloc 碎片，
 * 固定大小、生命周期清晰，避免内存溢出。
 */
typedef struct {
    Gui* gui;                  /* GUI 记录句柄 */
    ViewPort* view_port;       /* 视图端口（负责绘制与输入回调） */
    FuriMessageQueue* input_queue; /* 输入事件队列（GUI 线程 -> 主循环） */

    AppState state;            /* 当前状态机 */
    YaoType lines[6];          /* 六爻：lines[0]=初爻(最下)，lines[5]=上爻(最上) */
    uint8_t line_count;        /* 已摇出的爻数 0..6 */
    uint8_t cursor;            /* 结果页中高亮选中的爻(0..5)，上/下键移动 */
    bool has_changing;         /* 是否存在变爻 */

    uint8_t ben_index;         /* 本卦在 64 卦表中的索引 0..63 */
    uint8_t bian_index;        /* 变卦在 64 卦表中的索引 0..63 */
    bool running;              /* 主循环是否继续 */
} LiuYaoApp;

/* 入口函数：fbt 通过 application.fam 的 entry_point 调用 */
int32_t liuyao_app(void* p);
