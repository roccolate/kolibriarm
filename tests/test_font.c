#include <stdint.h>

#include "unity/unity.h"
#include "../kernel/font.h"

void test_font_draw_char_renders_8x8_glyph(void) {
    uint32_t pixels[64] = { 0 };
    fb_t fb;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 8, 8, 8));

    /*
     * 'A' = { 0x38, 0x6C, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6, 0x00 }
     * MSB (0x80) is the leftmost column.
     * Row 0: 0x38 = 0b00111000 -> cols 2,3,4 set (the apex).
     * Row 4: 0xFE = 0b11111110 -> cols 0..6 set, col 7 clear (the bar).
     * Row 7: 0x00 -> all clear (the bottom row of the glyph is empty).
     */
    font_draw_char(&fb, 0, 0, 'A', 0xffabcdefU);

    /* Row 0: only the apex (cols 2..4) is set. */
    TEST_ASSERT_EQUAL_UINT64(0, pixels[0 * 8 + 0]);
    TEST_ASSERT_EQUAL_UINT64(0, pixels[0 * 8 + 1]);
    TEST_ASSERT_EQUAL_UINT64(0xffabcdefU, pixels[0 * 8 + 2]);
    TEST_ASSERT_EQUAL_UINT64(0xffabcdefU, pixels[0 * 8 + 3]);
    TEST_ASSERT_EQUAL_UINT64(0xffabcdefU, pixels[0 * 8 + 4]);
    TEST_ASSERT_EQUAL_UINT64(0, pixels[0 * 8 + 5]);
    TEST_ASSERT_EQUAL_UINT64(0, pixels[0 * 8 + 6]);
    TEST_ASSERT_EQUAL_UINT64(0, pixels[0 * 8 + 7]);

    /* Row 4: full bar minus the rightmost pixel. */
    TEST_ASSERT_EQUAL_UINT64(0xffabcdefU, pixels[4 * 8 + 0]);
    TEST_ASSERT_EQUAL_UINT64(0xffabcdefU, pixels[4 * 8 + 6]);
    TEST_ASSERT_EQUAL_UINT64(0, pixels[4 * 8 + 7]);

    /* Row 7: empty (the bottom row of the glyph is intentionally blank). */
    for (uint32_t col = 0; col < 8; col++) {
        TEST_ASSERT_EQUAL_UINT64(0, pixels[7 * 8 + col]);
    }
}

void test_font_draw_text_advances_and_handles_lowercase(void) {
    uint32_t pixels[128] = { 0 };
    fb_t fb;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 16, 8, 16));

    /*
     * "a1" -- 'a' is at x=0, '1' starts at x=8.
     *   'a' = { 0x00, 0x00, 0x7C, 0x06, 0x7E, 0xC6, 0x7E, 0x00 }
     *          0x7C row at index 2 = 0b01111100 -> cols 1..6 set, 0 and 7 clear.
     *   '1' = { 0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00 }
     *          0x7E row at index 6 = 0b01111110 -> cols 0..6 set, col 7 clear.
     *     '1' is offset by 8 columns, so the leftmost column lands at x=8.
     */
    font_draw_text(&fb, 0, 0, "a1", 0xff123456U);

    /* 'a' row 2. */
    TEST_ASSERT_EQUAL_UINT64(0xff123456U, pixels[2 * 16 + 1]);
    TEST_ASSERT_EQUAL_UINT64(0xff123456U, pixels[2 * 16 + 5]);
    TEST_ASSERT_EQUAL_UINT64(0, pixels[2 * 16 + 0]);
    TEST_ASSERT_EQUAL_UINT64(0, pixels[2 * 16 + 6]);
    TEST_ASSERT_EQUAL_UINT64(0, pixels[2 * 16 + 7]);

    /* '1' row 6 (leftmost pixel lands at x=9, since glyph col 0 is empty). */
    TEST_ASSERT_EQUAL_UINT64(0, pixels[6 * 16 + 8]);
    TEST_ASSERT_EQUAL_UINT64(0xff123456U, pixels[6 * 16 + 9]);
    TEST_ASSERT_EQUAL_UINT64(0xff123456U, pixels[6 * 16 + 14]);
    TEST_ASSERT_EQUAL_UINT64(0, pixels[6 * 16 + 15]);
}

void test_font_draw_text_renders_punctuation(void) {
    uint32_t pixels[64] = { 0 };
    fb_t fb;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 8, 8, 8));

    /*
     * '!' = { 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x00 }
     * 0x18 = 0b00011000 -> cols 3,4 set.
     * With the old 36-glyph table '!' would have been skipped
     * (glyph_index returned -1); the 8x8 port renders it.
     */
    font_draw_char(&fb, 0, 0, '!', 0xff00ff00U);

    TEST_ASSERT_EQUAL_UINT64(0xff00ff00U, pixels[0 * 8 + 3]);
    TEST_ASSERT_EQUAL_UINT64(0xff00ff00U, pixels[0 * 8 + 4]);
    /* Row 5 is empty (the gap in '!'). */
    TEST_ASSERT_EQUAL_UINT64(0, pixels[5 * 8 + 3]);
    /* Row 6 is the dot at the bottom. */
    TEST_ASSERT_EQUAL_UINT64(0xff00ff00U, pixels[6 * 8 + 3]);
    TEST_ASSERT_EQUAL_UINT64(0xff00ff00U, pixels[6 * 8 + 4]);
}

void test_font_draw_char_ignores_non_ascii_bytes(void) {
    uint32_t pixels[64] = { 0 };
    fb_t fb;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 8, 8, 8));

    font_draw_char(&fb, 0, 0, (char)0xc1U, 0xff00ff00U);

    for (uint32_t i = 0; i < 64U; i++) {
        TEST_ASSERT_EQUAL_UINT64(0, pixels[i]);
    }
}

void test_font_draw_text_clips_at_framebuffer_edge(void) {
    uint32_t pixels[16] = { 0 };
    fb_t fb;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 4, 4, 4));

    /*
     * 'O' = { 0x7C, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00 }
     * Draw at x=2 so only cols 2 and 3 are inside the 4-wide framebuffer.
     * Row 0 (0x7C = 01111100): cols 2 and 3 of the glyph are set, mapping
     * to pixels 2 and 3 of the framebuffer.
     */
    font_draw_char(&fb, 2, 0, 'O', 0xff00ff00U);

    /* Only framebuffer col 3 is set; cols 2 and 4+ are outside the 'O' top row. */
    TEST_ASSERT_EQUAL_UINT64(0, pixels[0 * 4 + 0]);
    TEST_ASSERT_EQUAL_UINT64(0, pixels[0 * 4 + 1]);
    TEST_ASSERT_EQUAL_UINT64(0, pixels[0 * 4 + 2]);
    TEST_ASSERT_EQUAL_UINT64(0xff00ff00U, pixels[0 * 4 + 3]);
    /* Row 1 has the side bars of 'O' (0xC6 = 11000110), which puts glyph
     * cols 0 and 1 at framebuffer cols 2 and 3. */
    TEST_ASSERT_EQUAL_UINT64(0xff00ff00U, pixels[1 * 4 + 2]);
    TEST_ASSERT_EQUAL_UINT64(0xff00ff00U, pixels[1 * 4 + 3]);
}

void test_font_draw_text_handles_newline(void) {
    uint32_t pixels[128] = { 0 };
    fb_t fb;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 16, 16, 16));

    /*
     * "A\nB" should put A at (0,0) and B at (0,10) (FONT_LINE_HEIGHT=10).
     * 'B' = { 0xFC, 0xC6, 0xC6, 0xFC, 0xC6, 0xC6, 0xFC, 0x00 }
     * 0xFC = 11111100 -> cols 0..5 set, cols 6 and 7 clear.
     */
    font_draw_text(&fb, 0, 0, "A\nB", 0xffeeee00U);

    /* 'A' row 0 col 2..4 set (apex). */
    TEST_ASSERT_EQUAL_UINT64(0xffeeee00U, pixels[0 * 16 + 2]);
    TEST_ASSERT_EQUAL_UINT64(0xffeeee00U, pixels[0 * 16 + 4]);

    /* 'B' row 10 col 0 set (0xFC top row, leftmost). */
    TEST_ASSERT_EQUAL_UINT64(0xffeeee00U, pixels[10 * 16 + 0]);
    TEST_ASSERT_EQUAL_UINT64(0xffeeee00U, pixels[10 * 16 + 5]);
    TEST_ASSERT_EQUAL_UINT64(0, pixels[10 * 16 + 6]);
}

void test_font_draw_text_clipped_stops_at_max_h(void) {
    uint32_t pixels[64] = { 0 };
    fb_t fb;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 8, 8, 8));

    /*
     * 'A' = { 0x38, 0x6C, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6, 0x00 }
     * With max_h = 4, only the first 4 rows of the glyph should be drawn.
     * Row 4 (0xFE = 11111110) would otherwise paint col 0; it must stay 0.
     */
    font_draw_text_clipped(&fb, 0, 0, 4U, "A", 0xffabcdefU);

    /* Rows 0..3: glyph pixels present.
     *   Row 0 (0x38 = 00111000) -> cols 2, 3, 4 set.
     *   Row 1 (0x6C = 01101100) -> cols 1, 2, 4, 5 set.
     *   Row 2 (0xC6 = 11000110) -> cols 0, 1, 5, 6 set.
     *   Row 3 (0xC6)              -> cols 0, 1, 5, 6 set.
     * Row 4 (0xFE) must NOT be touched. */
    TEST_ASSERT_EQUAL_UINT64(0xffabcdefU, pixels[0 * 8 + 2]);
    TEST_ASSERT_EQUAL_UINT64(0xffabcdefU, pixels[1 * 8 + 5]);
    TEST_ASSERT_EQUAL_UINT64(0xffabcdefU, pixels[2 * 8 + 0]);
    TEST_ASSERT_EQUAL_UINT64(0xffabcdefU, pixels[3 * 8 + 6]);
    /* Row 4 must NOT be touched: col 0 of row 4 would otherwise be 0xFE. */
    TEST_ASSERT_EQUAL_UINT64(0, pixels[4 * 8 + 0]);
    TEST_ASSERT_EQUAL_UINT64(0, pixels[4 * 8 + 6]);
    TEST_ASSERT_EQUAL_UINT64(0, pixels[7 * 8 + 0]);
}

void test_font_draw_text_clipped_handles_zero_max_h(void) {
    uint32_t pixels[8] = { 0 };
    fb_t fb;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 8, 1, 8));

    font_draw_text_clipped(&fb, 0, 0, 0U, "A", 0xff112233U);

    for (uint32_t col = 0; col < 8; col++) {
        TEST_ASSERT_EQUAL_UINT64(0, pixels[col]);
    }
}
