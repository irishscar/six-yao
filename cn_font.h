#pragma once
#include <stdint.h>
#define CN_FONT_W 16
#define CN_FONT_H 16
#define CN_FONT_BPR 2
#define CN_FONT_GLYPH_BYTES 32
extern const char* cn_font_index;      /* UTF-8，每字出现一次，顺序=位图行 */
extern const uint16_t cn_font_count;
extern const uint8_t cn_font_bitmap[];  /* count * 32 字节 XBM 位图 */
extern const char* cn_hexagram_names[64]; /* 各卦中文名，下标=六爻二进制值 */
