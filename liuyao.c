/* =====================================================================
 *  liuyao.c —— 六爻占卜 APP 主程序（Flipper Zero 官方 SDK / C 语言）
 *
 *  新增能力（相对初版）：
 *    A) 自打包中文字库（cn_font.c/.h，由 gen_cn_font.py 生成），
 *       通过 canvas_draw_xbm() 绘制 16x16 点阵汉字，无需 Flipper 自带中文字库。
 *    B) 长按 OK 连摇：摇卦页长按 OK 一次性摇完六爻并直接进结果页；
 *       短按 OK 仍逐爻手摇。
 *
 *  随机算法：使用 furi_hal_random() 硬件随机数，模拟“三枚铜钱”。
 *   阳面数 0/1/2/3 -> 老阴6/少阳7/少阴8/老阳9
 *   老阴、老阳各 1/8(12.5%)，少阳、少阴各 3/8(37.5%)，符合传统概率。
 *
 *  卦象索引：初爻(bit0)..上爻(bit5)，1=阳 0=阴，
 *    index = 下卦值 + 8*上卦值，八卦值：乾7 兑6 离5 震3 巽2 坎1 艮4 坤0
 *  卦名由 cn_hexagram_names[]（中文，下标同索引）提供。
 * ===================================================================== */

#include "liuyao.h"
#include "cn_font.h"

/* ===================== 工具函数 ===================== */

/* 判断某一爻当前是阴还是阳。
 * changed=true 时计算“变卦”：老阳化阴、老阴化阳，少阳/少阴不变。 */
static bool yao_is_yang(YaoType t, bool changed) {
    if (changed) {
        if (t == LineOldYang) return false; /* 老阳 -> 阴 */
        if (t == LineOldYin)  return true;  /* 老阴 -> 阳 */
        return (t == LineYoungYang);        /* 少阳不变 */
    }
    /* 本卦：阳爻 = 少阳 或 老阳 */
    return (t == LineYoungYang || t == LineOldYang);
}

/* 由六爻计算卦象索引（初爻=bit0 ... 上爻=bit5） */
static uint8_t compute_hexagram_index(const YaoType lines[6], bool changed) {
    uint8_t idx = 0;
    for (int i = 0; i < 6; i++) {
        if (yao_is_yang(lines[i], changed)) idx |= (1u << i);
    }
    return idx;
}

/* 模拟摇卦：三枚铜钱，统计阳面(heads)数量
 *   0 阳 -> 老阴(6)    1 阳 -> 少阳(7)
 *   2 阳 -> 少阴(8)    3 阳 -> 老阳(9)
 * 概率：老阴/老阳各 1/8，少阳/少阴各 3/8，符合传统设定。 */
static YaoType toss_yao(void) {
    uint8_t heads = 0;
    for (int i = 0; i < 3; i++) {
        if (furi_hal_random() & 1u) heads++;
    }
    switch (heads) {
        case 0: return LineOldYin;
        case 1: return LineYoungYang;
        case 2: return LineYoungYin;
        default: return LineOldYang; /* case 3 */
    }
}

/* 爻类型 -> 中文（少阳/少阴/老阳/老阴） */
static const char* yao_type_cn(YaoType t) {
    switch (t) {
        case LineYoungYang: return "少阳";
        case LineYoungYin:  return "少阴";
        case LineOldYang:   return "老阳";
        case LineOldYin:    return "老阴";
        default:            return "?";
    }
}

/* 开始一次新的起卦（重置运行态） */
static void start_toss(LiuYaoApp* app) {
    app->state = AppStateTossing;
    app->line_count = 0;
    app->cursor = 0;
    app->has_changing = false;
    memset(app->lines, 0, sizeof(app->lines));
}

/* 摇出下一爻；若已六爻则结算本卦/变卦并切到结果页 */
static void toss_next(LiuYaoApp* app) {
    if (app->line_count >= 6) return;

    YaoType y = toss_yao();
    app->lines[app->line_count] = y;
    app->line_count++;

    if (y == LineOldYang || y == LineOldYin) app->has_changing = true;

    if (app->line_count >= 6) {
        app->ben_index  = compute_hexagram_index(app->lines, false);
        app->bian_index = compute_hexagram_index(app->lines, true);
        app->state = AppStateResult;
        app->cursor = 0;
    }
}

/* ===================== 中文字库绘制 ===================== */

/* 解析一个 UTF-8 字符，返回码点与该字符字节长度（支持 1/2/3 字节） */
static uint32_t utf8_decode(const uint8_t* p, int* len) {
    uint8_t c = p[0];
    if (c < 0x80) { *len = 1; return c; }
    if ((c & 0xE0) == 0xC0) {
        *len = 2;
        return ((uint32_t)(c & 0x1F) << 6) | (p[1] & 0x3F);
    }
    if ((c & 0xF0) == 0xE0) {
        *len = 3;
        return ((uint32_t)(c & 0x0F) << 12) |
               ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    }
    *len = 1;
    return 0; /* 不支持，跳过 */
}

/* 在字库中按码点查找字形，返回 32 字节位图指针（未收录返回 NULL） */
static const uint8_t* cn_font_find(uint32_t cp) {
    const uint8_t* p = (const uint8_t*)cn_font_index;
    int idx = 0;
    while (*p) {
        int len;
        uint32_t c = utf8_decode(p, &len);
        if (c == cp) return &cn_font_bitmap[idx * CN_FONT_GLYPH_BYTES];
        idx++;
        p += len;
    }
    return NULL;
}

/* 在 (x, y) 以 16x16 绘制一个中文字形（XBM 位图，1=黑点） */
static void draw_cn_glyph(Canvas* canvas, int x, int y, const uint8_t* data) {
    canvas_draw_xbm(canvas, x, y, CN_FONT_W, CN_FONT_H, data);
}

/* 绘制中文字符串（等宽 16px，自动跳过未收录字符） */
static void draw_cn_str(Canvas* canvas, int x, int y, const char* str) {
    int px = x;
    const uint8_t* p = (const uint8_t*)str;
    while (*p) {
        int len;
        uint32_t cp = utf8_decode(p, &len);
        const uint8_t* g = cn_font_find(cp);
        if (g) draw_cn_glyph(canvas, px, y, g);
        px += CN_FONT_W;
        p += len;
    }
}

/* ===================== 绘制函数 ===================== */

/* 画一行爻：
 *   - 阳爻画整条实线，阴爻画中间断开的两段（x:16..86 共 70px）
 *   - 变爻（老阳/老阴）在右侧画 o / x 传统记号（未选中时）
 *   - 当前选中爻：左侧画 “>” 光标，右侧画该爻中文类型（少阳/少阴/老阳/老阴）
 *   爻位顺序：屏幕自上而下 = 上爻(6)→初爻(1)。
 */
static void draw_yao_row(Canvas* canvas, LiuYaoApp* app, int row_top, int line_idx) {
    YaoType t = app->lines[line_idx];
    bool yang = yao_is_yang(t, false);
    bool is_old = (t == LineOldYang || t == LineOldYin);
    bool selected = (app->cursor == line_idx);
    int baseline = row_top + 7; /* 英文记号基线，与 2px 爻线居中对齐 */

    /* 光标 */
    if (selected) {
        canvas_draw_str(canvas, 0, baseline, ">");
    }

    /* 爻线 */
    if (yang) {
        canvas_draw_box(canvas, 16, row_top, 70, 2);
    } else {
        canvas_draw_box(canvas, 16, row_top, 33, 2);
        canvas_draw_box(canvas, 53, row_top, 33, 2);
    }

    /* 变爻标记 / 选中爻类型 */
    if (selected) {
        draw_cn_str(canvas, 90, row_top, yao_type_cn(t));
    } else if (is_old) {
        canvas_draw_str(canvas, 120, baseline, (t == LineOldYang) ? "o" : "x");
    }
}

/* 欢迎页 */
static void draw_welcome(Canvas* canvas, LiuYaoApp* app) {
    (void)app;
    canvas_set_font(canvas, FontPrimary);
    draw_cn_str(canvas, 32, 8, "六爻起卦");   /* 4 字 * 16 = 64px，居中 */

    /* 装饰：画一个示意卦象（三爻） */
    canvas_draw_box(canvas, 40, 30, 48, 2);
    canvas_draw_box(canvas, 40, 36, 22, 2);
    canvas_draw_box(canvas, 66, 36, 22, 2);
    canvas_draw_box(canvas, 40, 42, 48, 2);

    draw_cn_str(canvas, 32, 46, "确认开始");   /* 提示：按 OK 开始 */
}

/* 摇卦页（支持长按 OK 连摇） */
static void draw_tossing(Canvas* canvas, LiuYaoApp* app) {
    canvas_set_font(canvas, FontPrimary);
    draw_cn_str(canvas, 40, 4, "摇卦中");
    draw_cn_str(canvas, 32, 24, "长按连摇");   /* 提示长按 OK 一次摇满 */

    /* 进度：第 N 爻（N 用 ASCII 数字，中文“第/爻”用字库） */
    draw_cn_str(canvas, 30, 44, "第");
    char dbuf[2];
    snprintf(dbuf, sizeof(dbuf), "%d", app->line_count);
    canvas_draw_str(canvas, 46, 48, dbuf);
    draw_cn_str(canvas, 54, 44, "爻");
}

/* 结果页：本卦、变卦、六爻及选中爻类型 */
static void draw_result(Canvas* canvas, LiuYaoApp* app) {
    canvas_set_font(canvas, FontPrimary);

    draw_cn_str(canvas, 2, 2, "本卦");
    draw_cn_str(canvas, 34, 2, cn_hexagram_names[app->ben_index]);

    if (app->has_changing) {
        draw_cn_str(canvas, 2, 20, "变卦");
        draw_cn_str(canvas, 34, 20, cn_hexagram_names[app->bian_index]);
    } else {
        draw_cn_str(canvas, 2, 20, "无变爻");
    }

    /* 六爻：上爻(line5)在顶端、初爻(line0)在底端 */
    for (int k = 0; k < 6; k++) {
        int line_idx = 5 - k;
        int row_top = 37 + k * 4;
        draw_yao_row(canvas, app, row_top, line_idx);
    }
}

/* ===================== 回调与主循环 ===================== */

/* 绘制回调：GUI 线程调用，根据状态机分发绘制 */
static void liuyao_draw_callback(Canvas* canvas, void* ctx) {
    LiuYaoApp* app = ctx;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    switch (app->state) {
        case AppStateWelcome:
            draw_welcome(canvas, app);
            break;
        case AppStateTossing:
            draw_tossing(canvas, app);
            break;
        case AppStateResult:
            draw_result(canvas, app);
            break;
        default:
            break;
    }
}

/* 输入回调：GUI 线程调用，把事件送入队列由主循环统一处理 */
static void liuyao_input_callback(InputEvent* event, void* ctx) {
    LiuYaoApp* app = ctx;
    furi_message_queue_put(app->input_queue, event, FuriWaitForever);
}

/* 应用入口 */
int32_t liuyao_app(void* p) {
    (void)p;

    /* 分配主结构体（固定大小，无运行时碎片） */
    LiuYaoApp* app = malloc(sizeof(LiuYaoApp));
    if (!app) return -1;
    memset(app, 0, sizeof(LiuYaoApp));

    /* 打开 GUI 记录，分配视图端口与输入队列 */
    app->gui = furi_record_open(RECORD_GUI);
    app->view_port = view_port_alloc();
    app->input_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->state = AppStateWelcome;
    app->running = true;

    /* 注册绘制与输入回调，挂载到全屏图层 */
    view_port_draw_callback_set(app->view_port, liuyao_draw_callback, app);
    view_port_input_callback_set(app->view_port, liuyao_input_callback, app);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    /* 主循环：从队列取输入事件并派发 */
    InputEvent event;
    while (app->running) {
        if (furi_message_queue_get(app->input_queue, &event, FuriWaitForever)
            != FuriStatusCodeOk) {
            continue;
        }

        /* 短按 OK：各状态的基础响应 */
        if (event.type == InputTypePress) {
            switch (app->state) {
                case AppStateWelcome:
                    if (event.key == InputKeyOk) {
                        start_toss(app);
                    } else if (event.key == InputKeyBack) {
                        app->running = false;
                    }
                    break;

                case AppStateTossing:
                    if (event.key == InputKeyOk) {
                        toss_next(app);       /* 摇出下一爻（短按逐爻） */
                    } else if (event.key == InputKeyBack) {
                        app->state = AppStateWelcome;
                    }
                    break;

                case AppStateResult:
                    if (event.key == InputKeyUp) {
                        if (app->cursor > 0) app->cursor--;
                    } else if (event.key == InputKeyDown) {
                        if (app->cursor < 5) app->cursor++;
                    } else if (event.key == InputKeyOk) {
                        start_toss(app);      /* 重新起卦 */
                    } else if (event.key == InputKeyBack) {
                        app->state = AppStateWelcome;
                    }
                    break;

                default:
                    break;
            }
        }
        /* 长按 OK（仅摇卦页）：一次性摇完剩余爻并进入结果页 */
        else if (event.type == InputTypeLong &&
                 event.key == InputKeyOk &&
                 app->state == AppStateTossing) {
            while (app->line_count < 6) {
                toss_next(app);
            }
        }

        view_port_update(app->view_port); /* 状态变化后触发重绘 */
    }

    /* 清理资源（无泄漏） */
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_message_queue_free(app->input_queue);
    furi_record_close(RECORD_GUI);
    free(app);
    return 0;
}

/* =====================================================================
 *  部署 / 编译 / 烧录步骤
 * =====================================================================
 *
 *  〇、准备中文字库（只需在改动界面文字后重做）
 *    本工程中文由 gen_cn_font.py 生成 cn_font.c / cn_font.h（16x16 点阵）。
 *    在本机（Windows 已自带微软雅黑）执行：
 *        python gen_cn_font.py
 *    Linux/Mac 传入本机中文字体路径：
 *        python gen_cn_font.py /usr/share/fonts/.../wqy-microhei.ttc
 *    生成的 cn_font.c 已内含 64 卦中文名（cn_hexagram_names[]）。
 *
 *  一、准备固件源码（含官方 SDK 与 fbt 构建工具）
 *    git clone https://github.com/flipperdevices/flipperzero-firmware.git
 *    cd flipperzero-firmware
 *
 *  二、放置本项目
 *    将整个 liuyao/ 文件夹复制到：  applications_user/liuyao/
 *    目录结构应为：
 *      applications_user/liuyao/
 *        ├── application.fam   (工程描述)
 *        ├── liuyao.h          (类型与入口声明)
 *        ├── liuyao.c          (主程序)
 *        ├── cn_font.h         (字库头，gen 生成)
 *        ├── cn_font.c         (字库数据，gen 生成)
 *        └── gen_cn_font.py    (字库生成脚本)
 *
 *  三、编译（任选其一）
 *    1) 仅编译本 APP 为 .fap 包：
 *         ./fbt fap_liuyao_zhanbu
 *       产物：build/f7-firmware-D/.extapps/liuyao_zhanbu.fap
 *    2) 编译完整固件（含本 APP）：
 *         ./fbt
 *
 *  四、运行 / 烧录
 *    A. 仅安装 APP（不刷固件）：
 *       - 把 liuyao_zhanbu.fap 拷到 SD 卡 /ext/apps/ 任意子目录，机上 Apps 打开
 *    B. 整体烧录（固件 + APP 一并写入）：
 *         ./fbt flash
 *       或 qFlipper 选择 build/f7-firmware-D/flipper-z-f7-update-*.tgz
 *
 *  五、交互说明
 *    欢迎页：按 OK 开始；Back 退出
 *    摇卦页：短按 OK 摇一爻（共六爻）；长按 OK 一次摇满六爻
 *    结果页：上/下键移动光标查看每爻类型；OK 重新起卦；Back 返回
 * ===================================================================== */
