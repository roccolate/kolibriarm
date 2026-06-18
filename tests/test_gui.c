#include <stdint.h>
#include <string.h>

#include "unity/unity.h"
#include "../kernel/gui.h"

static uint32_t g_test_gui_demo_pixels[640U * 480U];

void test_gui_create_draws_windows_in_creation_order(void) {
    uint32_t pixels[64] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t first;
    uint32_t second;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 8, 8, 8));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_init(&desktop, &fb, 0xff101010U));
    desktop.cursor.visible = 0;
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 1, 1, 5, 5, 0xff0000aaU,
                                 0xffffffffU, &first));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 3, 3, 4, 4, 0xff00aa00U,
                                 0xffeeee00U, &second));

    gui_draw(&desktop);

    TEST_ASSERT_EQUAL_UINT64(0xff101010U, pixels[0]);
    /* First window is focused by default -> brighter border 0xffe0e8f0. */
    TEST_ASSERT_EQUAL_UINT64(0xffe0e8f0U, pixels[1 * 8 + 1]);
    TEST_ASSERT_EQUAL_UINT64(0xff0000aaU, pixels[2 * 8 + 2]);
    /* Second window is not focused -> keeps its original border. */
    TEST_ASSERT_EQUAL_UINT64(0xffeeee00U, pixels[3 * 8 + 3]);
    TEST_ASSERT_EQUAL_UINT64(0xff00aa00U, pixels[4 * 8 + 4]);
    TEST_ASSERT_EQUAL_UINT64(0, first);
    TEST_ASSERT_EQUAL_UINT64(1, second);
}

void test_gui_move_window_redraws_at_new_position(void) {
    uint32_t pixels[36] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t window_id;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 6, 6, 6));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_init(&desktop, &fb, 0xff111111U));
    desktop.cursor.visible = 0;
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 1, 1, 3, 3, 0xff222222U,
                                 0xff333333U, &window_id));

    gui_draw(&desktop);
    /* Newly created window is auto-focused, so its border uses the
     * focus highlight color, not the user-supplied border color. */
    TEST_ASSERT_EQUAL_UINT64(0xffe0e8f0U, pixels[1 * 6 + 1]);

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_move_window(&desktop, window_id,
                                                       2, 2));
    gui_draw(&desktop);
    /* The window's old position (1,1) is now covered by the gradient
     * background. The border color is gone from there. */
    TEST_ASSERT_TRUE(pixels[1 * 6 + 1] != 0xffe0e8f0U);
    TEST_ASSERT_EQUAL_UINT64(0xffe0e8f0U, pixels[2 * 6 + 2]);
    TEST_ASSERT_EQUAL_UINT64(0xff222222U, pixels[3 * 6 + 3]);
}

void test_gui_rejects_invalid_inputs_and_window_limit(void) {
    uint32_t pixels[16] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t id;

    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)gui_init(0, &fb, 0));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 4, 4, 4));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0));

    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)gui_create_window(
                                 &desktop, 0, 0, 1, 3, 0, 0, &id));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)gui_move_window(&desktop, 0, 1, 1));

    for (uint32_t i = 0; i < GUI_MAX_WINDOWS; i++) {
        TEST_ASSERT_EQUAL_UINT64(0,
                                 (uint64_t)gui_create_window(
                                     &desktop, 0, 0, 2, 2, 0, 0, &id));
    }

    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)gui_create_window(
                                 &desktop, 0, 0, 2, 2, 0, 0, &id));
}

void test_gui_delivers_key_to_focused_window(void) {
    uint32_t pixels[16] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t first;
    uint32_t second;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 4, 4, 4));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 0, 0, 2, 2, 0, 0, &first));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 1, 1, 2, 2, 0, 0, &second));

    TEST_ASSERT_EQUAL_UINT64(first, desktop.focused_window_id);
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_focus_window(&desktop, second));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_send_key(&desktop, 'k'));

    TEST_ASSERT_EQUAL_UINT64(0, desktop.windows[first].key_count);
    TEST_ASSERT_EQUAL_UINT64(1, desktop.windows[second].key_count);
    TEST_ASSERT_EQUAL_UINT64('k', desktop.windows[second].last_key);
}

void test_gui_cursor_move_clamps_to_bounds(void) {
    uint32_t pixels[16] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t id;
    int32_t x = 0;
    int32_t y = 0;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 4, 4, 4));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0));
    desktop.cursor.visible = 0;
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 0, 0, 2, 2, 0, 0, &id));

    gui_set_cursor(&desktop, 2, 2);
    gui_get_cursor(&desktop, &x, &y);
    TEST_ASSERT_EQUAL_UINT64(2, (uint64_t)x);
    TEST_ASSERT_EQUAL_UINT64(2, (uint64_t)y);

    /* Move far past the right edge: must clamp to width - 1. */
    gui_cursor_move(&desktop, 100, 0);
    gui_get_cursor(&desktop, &x, &y);
    TEST_ASSERT_EQUAL_UINT64(3, (uint64_t)x);
    TEST_ASSERT_EQUAL_UINT64(2, (uint64_t)y);

    /* Move far past the left edge: must clamp to 0. */
    gui_cursor_move(&desktop, -100, 0);
    gui_get_cursor(&desktop, &x, &y);
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)x);

    /* Move far past the bottom edge: must clamp to height - 1. */
    gui_cursor_move(&desktop, 0, 100);
    gui_get_cursor(&desktop, &x, &y);
    TEST_ASSERT_EQUAL_UINT64(3, (uint64_t)y);
}

void test_gui_left_click_raises_window_under_cursor(void) {
    uint32_t pixels[64] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t first;
    uint32_t second;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 8, 8, 8));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0));
    desktop.cursor.visible = 0;
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 0, 0, 4, 4, 0, 0, &first));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 4, 4, 4, 4, 0, 0, &second));
    TEST_ASSERT_EQUAL_UINT64(first, desktop.focused_window_id);

    /* Click inside the second window: it should become focused. The
     * MOUSE_CLICK event is pushed by gui_demo_handle_input, not by the
     * low-level gui_cursor_button, so this test only checks focus. */
    gui_set_cursor(&desktop, 5, 5);
    gui_cursor_button(&desktop, 0, 1);
    TEST_ASSERT_EQUAL_UINT64(second, desktop.focused_window_id);

    /* Click inside the first window: focus returns to it. */
    gui_set_cursor(&desktop, 1, 1);
    gui_cursor_button(&desktop, 0, 1);
    TEST_ASSERT_EQUAL_UINT64(first, desktop.focused_window_id);
}

void test_gui_left_click_on_background_keeps_focus(void) {
    uint32_t pixels[64] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t only;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 8, 8, 8));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0));
    desktop.cursor.visible = 0;
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 0, 0, 2, 2, 0, 0, &only));
    TEST_ASSERT_EQUAL_UINT64(only, desktop.focused_window_id);

    /* Cursor over empty desktop: left click must not change focus and
     * must not deliver a click event to the existing window. */
    gui_set_cursor(&desktop, 7, 7);
    gui_cursor_button(&desktop, 0, 1);
    TEST_ASSERT_EQUAL_UINT64(only, desktop.focused_window_id);
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)desktop.windows[only].event_count);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)GUI_BUTTON_LEFT,
                             (uint64_t)desktop.cursor.buttons_mask);

    /* Release clears the bit. */
    gui_cursor_button(&desktop, 0, 0);
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)(desktop.cursor.buttons_mask &
                                        (uint8_t)GUI_BUTTON_LEFT));
}

void test_gui_right_and_middle_buttons_set_mask_without_focus_change(void) {
    uint32_t pixels[64] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t first;
    uint32_t second;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 8, 8, 8));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0));
    desktop.cursor.visible = 0;
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 0, 0, 4, 4, 0, 0, &first));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 4, 4, 4, 4, 0, 0, &second));
    gui_focus_window(&desktop, first);
    TEST_ASSERT_EQUAL_UINT64(first, desktop.focused_window_id);

    /* Right click on second window: mask bit set, focus unchanged. */
    gui_set_cursor(&desktop, 5, 5);
    gui_cursor_button(&desktop, 1, 1);
    TEST_ASSERT_EQUAL_UINT64(first, desktop.focused_window_id);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)GUI_BUTTON_RIGHT,
                             (uint64_t)(desktop.cursor.buttons_mask &
                                        (uint8_t)GUI_BUTTON_RIGHT));

    /* Middle click adds its own bit; left does not affect the right bit. */
    gui_cursor_button(&desktop, 2, 1);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)GUI_BUTTON_MIDDLE,
                             (uint64_t)(desktop.cursor.buttons_mask &
                                        (uint8_t)GUI_BUTTON_MIDDLE));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)GUI_BUTTON_RIGHT,
                             (uint64_t)(desktop.cursor.buttons_mask &
                                        (uint8_t)GUI_BUTTON_RIGHT));

    /* Releasing one button must not clear the other. */
    gui_cursor_button(&desktop, 1, 0);
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)(desktop.cursor.buttons_mask &
                                        (uint8_t)GUI_BUTTON_RIGHT));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)GUI_BUTTON_MIDDLE,
                             (uint64_t)(desktop.cursor.buttons_mask &
                                        (uint8_t)GUI_BUTTON_MIDDLE));
}

void test_gui_focused_window_gets_brighter_border(void) {
    uint32_t pixels[64] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t first;
    uint32_t second;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 8, 8, 8));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0xff101010U));
    desktop.cursor.visible = 0;
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 0, 0, 4, 4, 0, 0, &first));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 4, 4, 4, 4, 0, 0, &second));

    /* First is focused by default. Its border corner pixel should match
     * the focus highlight color, second should keep its default (0). */
    gui_draw(&desktop);
    TEST_ASSERT_EQUAL_UINT64(0xffe0e8f0U, pixels[0 * 8 + 0]);
    TEST_ASSERT_EQUAL_UINT64(0U, pixels[4 * 8 + 4]);

    /* Switch focus: now second should get the highlight. */
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_focus_window(&desktop, second));
    gui_draw(&desktop);
    TEST_ASSERT_EQUAL_UINT64(0U, pixels[0 * 8 + 0]);
    TEST_ASSERT_EQUAL_UINT64(0xffe0e8f0U, pixels[4 * 8 + 4]);
}

void test_gui_drag_moves_window_following_cursor(void) {
    uint32_t pixels[100] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t id;
    uint32_t pixels_backup[100];

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 10, 10, 10));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0xff202020U));
    desktop.cursor.visible = 0;
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 2, 2, 4, 4, 0, 0, &id));
    TEST_ASSERT_EQUAL_UINT64(2, desktop.windows[id].x);
    TEST_ASSERT_EQUAL_UINT64(2, desktop.windows[id].y);

    /* Start drag at offset (1,1) within the window. */
    gui_set_cursor(&desktop, 3, 3);
    gui_drag_start(&desktop, id, 1, 1);
    TEST_ASSERT_EQUAL_UINT64(1, (uint64_t)gui_drag_active(&desktop));

    /* Move cursor by (+2, +1): window should follow by the same delta. */
    gui_drag_update(&desktop, 5, 4);
    TEST_ASSERT_EQUAL_UINT64(4, desktop.windows[id].x);
    TEST_ASSERT_EQUAL_UINT64(3, desktop.windows[id].y);

    /* Move to negative (off the left/top): drag clamps to (0, 0). */
    gui_drag_update(&desktop, -5, -5);
    TEST_ASSERT_EQUAL_UINT64(0, desktop.windows[id].x);
    TEST_ASSERT_EQUAL_UINT64(0, desktop.windows[id].y);

    /* Move past right/bottom: window is allowed to run off-screen
     * (the desktop redraw clips it). */
    gui_drag_update(&desktop, 100, 100);
    TEST_ASSERT_EQUAL_UINT64(99, desktop.windows[id].x);
    TEST_ASSERT_EQUAL_UINT64(99, desktop.windows[id].y);

    /* End drag: state clears, further updates are no-ops. */
    gui_drag_end(&desktop);
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_drag_active(&desktop));
    memcpy(pixels_backup, pixels, sizeof(pixels));
    gui_drag_update(&desktop, 0, 0);
    TEST_ASSERT_EQUAL_UINT64(0, memcmp(pixels_backup, pixels, sizeof(pixels)));
}

void test_gui_drag_rejects_unknown_window(void) {
    uint32_t pixels[16] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 4, 4, 4));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0));

    /* Unknown window: start should be a no-op. */
    gui_drag_start(&desktop, 99, 0, 0);
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_drag_active(&desktop));
}

void test_gui_demo_drag_stays_active_until_left_release(void) {
    fb_t fb;
    gui_desktop_t *desktop;
    input_event_t press = {
        .type = INPUT_EVENT_MOUSE_BUTTON,
        .timestamp = 0,
        .data.mouse_button = { .button = 0, .pressed = 1 },
    };
    input_event_t move = {
        .type = INPUT_EVENT_MOUSE_MOVE,
        .timestamp = 0,
        .data.mouse_move = { .dx = 20, .dy = 10 },
    };
    input_event_t release = {
        .type = INPUT_EVENT_MOUSE_BUTTON,
        .timestamp = 0,
        .data.mouse_button = { .button = 0, .pressed = 0 },
    };

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)fb_init(&fb, g_test_gui_demo_pixels,
                                               640, 480, 640));
    gui_draw_demo(&fb, 0);
    desktop = gui_demo_desktop();
    TEST_ASSERT_NOT_NULL(desktop);

    gui_set_cursor(desktop, 250, 160);
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_demo_handle_input(&press));
    TEST_ASSERT_EQUAL_UINT64(1, (uint64_t)gui_drag_active(desktop));

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_demo_handle_input(&move));
    TEST_ASSERT_EQUAL_UINT64(1, (uint64_t)gui_drag_active(desktop));
    TEST_ASSERT_EQUAL_UINT64(240, desktop->windows[1].x);
    TEST_ASSERT_EQUAL_UINT64(160, desktop->windows[1].y);

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_demo_handle_input(&release));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_drag_active(desktop));
}

void test_gui_draw_fills_vertical_gradient(void) {
    uint32_t pixels[40] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 4, 10, 4));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_init(&desktop, &fb, 0xff808080U));
    desktop.cursor.visible = 0;

    gui_draw(&desktop);

    /* Top row should be (close to) the requested background color. */
    TEST_ASSERT_EQUAL_UINT64(0xff808080U, pixels[0 * 4]);
    /* Bottom row should be a noticeably darker shade. */
    TEST_ASSERT_TRUE(pixels[9 * 4] < 0xff404040U);
    /* The gradient must be strictly monotonic darker from top to bottom. */
    for (uint32_t row = 1; row < 10; row++) {
        TEST_ASSERT_TRUE(pixels[row * 4] <= pixels[(row - 1) * 4]);
    }
}
