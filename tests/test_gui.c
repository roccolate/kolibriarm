#include <stdint.h>

#include "unity/unity.h"
#include "../kernel/gui.h"
#include "../kernel/process.h"

static uint32_t g_test_gui_pixels[640U * 480U];

void test_gui_init_for_framebuffer_starts_with_empty_desktop(void) {
    fb_t fb;
    gui_desktop_t *desktop;

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)fb_init(&fb, g_test_gui_pixels,
                                               640, 480, 640));
    gui_init_for_framebuffer(&fb, 0);
    desktop = gui_desktop();
    TEST_ASSERT_NOT_NULL(desktop);
    TEST_ASSERT_EQUAL_UINT64(GUI_NO_WINDOW, desktop->focused_window_id);

    for (uint32_t i = 0; i < GUI_MAX_WINDOWS; i++) {
        TEST_ASSERT_EQUAL_UINT64(0, desktop->windows[i].used);
    }
}

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
    gui_event_t ev;
    int32_t data1 = 0;

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

    /* The kernel's actual key flow is gui_window_push_event with
     * GUI_EVENT_KEY_PRESS. The historical gui_send_key helper that
     * bumped key_count/last_key was removed; cover the event path
     * here instead. */
    input_event_t input = { .type = INPUT_EVENT_KEY_PRESS,
                            .data.key.key = 'k' };
    (void)input;
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_window_push_event(
                                 &desktop.windows[second],
                                 GUI_EVENT_KEY_PRESS, 'k', 0));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_window_pop_event(
                                 &desktop.windows[second], &ev));
    TEST_ASSERT_EQUAL_UINT64(GUI_EVENT_KEY_PRESS, ev.type);
    data1 = ev.data1;
    TEST_ASSERT_EQUAL_UINT64('k', (uint32_t)data1);
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

void test_gui_draw_renders_16x16_cursor_over_desktop(void) {
    uint32_t pixels[400] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;

    TEST_ASSERT_EQUAL_UINT64(16, (uint64_t)GUI_CURSOR_W);
    TEST_ASSERT_EQUAL_UINT64(16, (uint64_t)GUI_CURSOR_H);
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 20, 20, 20));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_init(&desktop, &fb, 0xff606060U));

    gui_set_cursor(&desktop, 1, 1);
    gui_draw(&desktop);

    TEST_ASSERT_EQUAL_UINT64(0xff101010U, pixels[1 * 20 + 1]);
    TEST_ASSERT_EQUAL_UINT64(0xffe0e0e0U, pixels[3 * 20 + 2]);
    TEST_ASSERT_TRUE(pixels[19 * 20 + 19] != 0);
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
     * MOUSE_CLICK event is pushed by gui_handle_input, not by the
     * low-level gui_cursor_button, so this test only checks focus. */
    gui_set_cursor(&desktop, 5, 5);
    gui_cursor_button(&desktop, 0, 1);
    TEST_ASSERT_EQUAL_UINT64(second, desktop.focused_window_id);

    /* Click inside the first window: focus returns to it. */
    gui_set_cursor(&desktop, 1, 1);
    gui_cursor_button(&desktop, 0, 1);
    TEST_ASSERT_EQUAL_UINT64(first, desktop.focused_window_id);
}

void test_gui_left_click_on_background_clears_focus(void) {
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

    /* Cursor over empty desktop: left click must clear focus and
     * must not deliver a click event to the existing window. */
    gui_set_cursor(&desktop, 7, 7);
    gui_cursor_button(&desktop, 0, 1);
    TEST_ASSERT_EQUAL_UINT64(GUI_NO_WINDOW, desktop.focused_window_id);
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
    uint32_t final_x;
    uint32_t final_y;

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
    final_x = desktop.windows[id].x;
    final_y = desktop.windows[id].y;
    gui_drag_update(&desktop, 0, 0);
    TEST_ASSERT_EQUAL_UINT64(final_x, desktop.windows[id].x);
    TEST_ASSERT_EQUAL_UINT64(final_y, desktop.windows[id].y);
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

void test_gui_drag_stays_active_until_left_release(void) {
    fb_t fb;
    gui_desktop_t *desktop;
    uint32_t window_id;
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
                             (uint64_t)fb_init(&fb, g_test_gui_pixels,
                                               640, 480, 640));
    gui_init_for_framebuffer(&fb, 0);
    desktop = gui_desktop();
    TEST_ASSERT_NOT_NULL(desktop);

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 desktop, 220, 150, 340, 230, 0xff38a169U,
                                 0xfffff4c2U, &window_id));

    gui_set_cursor(desktop, 250, 160);
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_handle_input(&press));
    TEST_ASSERT_EQUAL_UINT64(1, (uint64_t)gui_drag_active(desktop));

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_handle_input(&move));
    TEST_ASSERT_EQUAL_UINT64(1, (uint64_t)gui_drag_active(desktop));
    TEST_ASSERT_EQUAL_UINT64(240, desktop->windows[window_id].x);
    TEST_ASSERT_EQUAL_UINT64(160, desktop->windows[window_id].y);

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_handle_input(&release));
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

void test_gui_title_bar_shifts_owner_draw_below_bar(void) {
    uint32_t pixels[64] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t window_id;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 8, 8, 8));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_init(&desktop, &fb, 0xff101010U));
    desktop.cursor.visible = 0;
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 0, 0, 8, 8, 0xff0000aaU,
                                 0xffffffffU, &window_id));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_set_window_title_bar(
                                 &desktop, window_id, 3U));

    /* Owner draws a red rect at logical (0, 0, 4, 1). Without the shift
     * the kernel would paint the bg_color into y=0 instead of the red.
     * With the title_h=3 shift the red must appear at y=3. */
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_window_draw_rect(
                                 &desktop, window_id, 0, 0, 4, 1, 0xffff0000U));

    gui_draw(&desktop);

    /* y=0..2 inside the window is occupied by the title bar, not the
     * owner's red rect. */
    for (uint32_t x = 1; x < 7; x++) {
        TEST_ASSERT_TRUE(pixels[0 * 8 + x] != 0xffff0000U);
        TEST_ASSERT_TRUE(pixels[1 * 8 + x] != 0xffff0000U);
        TEST_ASSERT_TRUE(pixels[2 * 8 + x] != 0xffff0000U);
    }
    /* The owner's red rect must show up at y=3, x=1..3 (x=0 is owned by
     * the kernel's left border which is painted after owner draws). */
    TEST_ASSERT_EQUAL_UINT64(0xffff0000U, pixels[3 * 8 + 1]);
    TEST_ASSERT_EQUAL_UINT64(0xffff0000U, pixels[3 * 8 + 2]);
    TEST_ASSERT_EQUAL_UINT64(0xffff0000U, pixels[3 * 8 + 3]);
    /* The kernel's left border remains visible at x=0 (brighter because
     * the window is focused by default). */
    TEST_ASSERT_TRUE(pixels[3 * 8 + 0] != 0xffff0000U);
    /* y=4 must not contain red — the rect is only one row tall. */
    TEST_ASSERT_TRUE(pixels[4 * 8 + 1] != 0xffff0000U);
}

void test_gui_title_bar_paints_bar_and_text(void) {
    uint32_t pixels[64] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t window_id;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 8, 8, 8));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_init(&desktop, &fb, 0xff101010U));
    desktop.cursor.visible = 0;
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window_for_pid(
                                 &desktop, 7U, 0, 0, 8, 8, 0xff0000aaU,
                                 0xff808080U, "abcd", &window_id));
    /* Use a smaller title_h so the test window (8 px tall) still has
     * content underneath the bar. */
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_set_window_title_bar(
                                 &desktop, window_id, 3U));

    gui_draw(&desktop);

    /* The bar background must dominate the top 3 rows inside the
     * window. Anything inside the bar is not the window bg_color
     * 0xff0000aa because the kernel overpaints it with a darker shade
     * of the border color. */
    for (uint32_t y = 0; y < 3; y++) {
        for (uint32_t x = 1; x < 7; x++) {
            TEST_ASSERT_TRUE(pixels[y * 8 + x] != 0xff0000aaU);
        }
    }
    /* Below the title bar the bg_color (0xff0000aa) shows because no
     * owner draw has happened. The 4th row (index 3) is the separator
     * line; the bg_color shows starting at y=4. The title text starts
     * at x=2, so x=3 is a safe pixel that is neither text nor border. */
    TEST_ASSERT_EQUAL_UINT64(0xff0000aaU, pixels[4 * 8 + 3]);
    TEST_ASSERT_EQUAL_UINT64(0xff0000aaU, pixels[5 * 8 + 3]);
}

void test_gui_cursor_shape_changes_over_clickable_title_decoration(void) {
    uint32_t pixels[20000] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t titled;
    uint32_t cover;

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)fb_init(&fb, pixels, 200, 100, 200));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_init(&desktop, &fb, 0xff101010U));
    desktop.cursor.visible = 0;

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window_for_pid(
                                 &desktop, 7U, 20, 10, 100, 60, 0xff0000aaU,
                                 0xff808080U, "demo", &titled));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_set_window_title_bar(
                                 &desktop, titled, 12U));

    gui_set_cursor(&desktop, 30, 30);
    TEST_ASSERT_EQUAL_UINT64(GUI_CURSOR_ARROW,
                             (uint64_t)desktop.cursor.shape);

    gui_set_cursor(&desktop, 30, 15);
    TEST_ASSERT_EQUAL_UINT64(GUI_CURSOR_HAND,
                             (uint64_t)desktop.cursor.shape);

    gui_set_cursor(&desktop, 180, 80);
    TEST_ASSERT_EQUAL_UINT64(GUI_CURSOR_ARROW,
                             (uint64_t)desktop.cursor.shape);

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 25, 12, 20, 20, 0xff00aa00U,
                                 0xff404040U, &cover));
    (void)cover;

    gui_set_cursor(&desktop, 30, 15);
    TEST_ASSERT_EQUAL_UINT64(GUI_CURSOR_ARROW,
                             (uint64_t)desktop.cursor.shape);
}

void test_gui_set_cursor_shape_rejects_unknown_shapes(void) {
    uint32_t pixels[16] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 4, 4, 4));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_init(&desktop, &fb, 0xff101010U));

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_set_cursor_shape(
                                 &desktop, GUI_CURSOR_HAND));
    TEST_ASSERT_EQUAL_UINT64(GUI_CURSOR_HAND,
                             (uint64_t)desktop.cursor.shape);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)gui_set_cursor_shape(&desktop, 99U));
    TEST_ASSERT_EQUAL_UINT64(GUI_CURSOR_HAND,
                             (uint64_t)desktop.cursor.shape);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)gui_set_cursor_shape(0,
                                                            GUI_CURSOR_ARROW));
}

void test_gui_cursor_register_region_installs_shape_inside_window(void) {
    /*
     * A registered HAND region inside the window's content area
     * must flip the cursor shape even when no title bar is
     * present. Without the per-region path, the kernel would fall
     * through to the ARROW default for both positions.
     */
    uint32_t pixels[20000] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t window_id;

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)fb_init(&fb, pixels, 200, 100, 200));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_init(&desktop, &fb, 0xff101010U));
    desktop.cursor.visible = 0;

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 20, 10, 100, 60, 0xff0000aaU,
                                 0xff808080U, &window_id));

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_register_cursor_region(
                                 &desktop, window_id, 0U, 4, 4, 20, 20,
                                 GUI_CURSOR_HAND));

    gui_set_cursor(&desktop, 10, 20);
    TEST_ASSERT_EQUAL_UINT64(GUI_CURSOR_ARROW,
                             (uint64_t)desktop.cursor.shape);

    gui_set_cursor(&desktop, 30, 20);
    TEST_ASSERT_EQUAL_UINT64(GUI_CURSOR_HAND,
                             (uint64_t)desktop.cursor.shape);

    gui_set_cursor(&desktop, 25, 15);
    TEST_ASSERT_EQUAL_UINT64(GUI_CURSOR_HAND,
                             (uint64_t)desktop.cursor.shape);

    gui_set_cursor(&desktop, 50, 30);
    TEST_ASSERT_EQUAL_UINT64(GUI_CURSOR_ARROW,
                             (uint64_t)desktop.cursor.shape);
}

void test_gui_cursor_register_region_overrides_title_bar_default(void) {
    /*
     * A title-bar hit normally switches to HAND. If a slot-0
     * ARROW region covers that exact point, the region must win:
     * the panel's launcher buttons live below the title bar but a
     * docked app could legitimately want a region that overlaps
     * the bar to override the implicit HAND.
     */
    uint32_t pixels[20000] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t window_id;

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)fb_init(&fb, pixels, 200, 100, 200));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_init(&desktop, &fb, 0xff101010U));
    desktop.cursor.visible = 0;

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window_for_pid(
                                 &desktop, 9U, 20, 10, 100, 60, 0xff0000aaU,
                                 0xff808080U, "titled", &window_id));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_set_window_title_bar(
                                 &desktop, window_id, 12U));

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_register_cursor_region(
                                 &desktop, window_id, 0U, 0, -10, 100, 30,
                                 GUI_CURSOR_ARROW));

    gui_set_cursor(&desktop, 30, 15);
    TEST_ASSERT_EQUAL_UINT64(GUI_CURSOR_ARROW,
                             (uint64_t)desktop.cursor.shape);
}

void test_gui_cursor_register_region_first_matching_slot_wins(void) {
    /*
     * When two slots overlap, the lower slot index must win
     * because the kernel walks in ascending order. This keeps
     * "background" regions predictable: the app's coarse regions
     * land in the early slots and fine-grained overrides land
     * later without the order depending on registration time.
     */
    uint32_t pixels[20000] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t window_id;

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)fb_init(&fb, pixels, 200, 100, 200));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_init(&desktop, &fb, 0xff101010U));
    desktop.cursor.visible = 0;

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 20, 10, 100, 60, 0xff0000aaU,
                                 0xff808080U, &window_id));

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_register_cursor_region(
                                 &desktop, window_id, 0U, 0, 0, 100, 60,
                                 GUI_CURSOR_HAND));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_register_cursor_region(
                                 &desktop, window_id, 1U, 0, 0, 100, 60,
                                 GUI_CURSOR_ARROW));

    gui_set_cursor(&desktop, 30, 30);
    TEST_ASSERT_EQUAL_UINT64(GUI_CURSOR_HAND,
                             (uint64_t)desktop.cursor.shape);
}

void test_gui_cursor_register_region_delete_clears_slot(void) {
    /*
     * Passing the DELETE sentinel must clear the slot without
     * installing a replacement. A subsequent move over the same
     * coords must fall through to the title-bar / ARROW default
     * so the app can release its hover affordance without
     * leaving a stale region behind.
     */
    uint32_t pixels[20000] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t window_id;

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)fb_init(&fb, pixels, 200, 100, 200));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_init(&desktop, &fb, 0xff101010U));
    desktop.cursor.visible = 0;

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 20, 10, 100, 60, 0xff0000aaU,
                                 0xff808080U, &window_id));

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_register_cursor_region(
                                 &desktop, window_id, 3U, 0, 0, 100, 60,
                                 GUI_CURSOR_HAND));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_register_cursor_region(
                                 &desktop, window_id, 3U, 0, 0, 100, 60,
                                 GUI_CURSOR_REGION_DELETE));

    gui_set_cursor(&desktop, 30, 30);
    TEST_ASSERT_EQUAL_UINT64(GUI_CURSOR_ARROW,
                             (uint64_t)desktop.cursor.shape);
}

void test_gui_cursor_register_region_rejects_invalid_inputs(void) {
    uint32_t pixels[20000] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t window_id;

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)fb_init(&fb, pixels, 200, 100, 200));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_init(&desktop, &fb, 0xff101010U));
    desktop.cursor.visible = 0;

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 20, 10, 100, 60, 0xff0000aaU,
                                 0xff808080U, &window_id));

    /* unknown shape */
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)gui_register_cursor_region(
                                 &desktop, window_id, 0U, 0, 0, 10, 10, 99U));

    /* out-of-range window */
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)gui_register_cursor_region(
                                 &desktop, GUI_MAX_WINDOWS, 0U, 0, 0, 10, 10,
                                 GUI_CURSOR_HAND));

    /* out-of-range slot */
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)gui_register_cursor_region(
                                 &desktop, window_id, GUI_MAX_CURSOR_REGIONS,
                                 0, 0, 10, 10, GUI_CURSOR_HAND));

    /* null desktop */
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)gui_register_cursor_region(
                                 0, window_id, 0U, 0, 0, 10, 10,
                                 GUI_CURSOR_HAND));

    /* unknown window id (slot is empty) */
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)gui_register_cursor_region(
                                 &desktop, 5U, 0U, 0, 0, 10, 10,
                                 GUI_CURSOR_HAND));
}

void test_gui_cursor_register_region_clears_on_window_destroy(void) {
    /*
     * gui_destroy_window must wipe the cursor_regions array so a
     * reused slot does not inherit leftover HAND shapes from the
     * previous occupant. Without this, a fresh window that
     * happens to land on the same pool index would show HAND
     * everywhere before the new owner sets up its own regions.
     */
    uint32_t pixels[20000] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t first;
    uint32_t reused;

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)fb_init(&fb, pixels, 200, 100, 200));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_init(&desktop, &fb, 0xff101010U));
    desktop.cursor.visible = 0;

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 20, 10, 100, 60, 0xff0000aaU,
                                 0xff808080U, &first));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_register_cursor_region(
                                 &desktop, first, 0U, 0, 0, 100, 60,
                                 GUI_CURSOR_HAND));

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_destroy_window(&desktop, first));

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 20, 10, 100, 60, 0xff00aa00U,
                                 0xff404040U, &reused));
    TEST_ASSERT_EQUAL_UINT64(first, reused);

    gui_set_cursor(&desktop, 30, 30);
    TEST_ASSERT_EQUAL_UINT64(GUI_CURSOR_ARROW,
                             (uint64_t)desktop.cursor.shape);
}

void test_gui_cursor_register_region_constant_is_sane(void) {
    /*
     * Lock down the cap so a future change that bumps the
     * constant (or accidentally drops it to 0) is caught here
     * instead of in a runtime-only test.
     */
    TEST_ASSERT_TRUE(GUI_MAX_CURSOR_REGIONS >= 4U);
    TEST_ASSERT_TRUE(GUI_MAX_CURSOR_REGIONS <= 16U);
}

void test_gui_set_title_bar_rejects_height_larger_than_window(void) {
    uint32_t pixels[16] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t window_id;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 4, 4, 4));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_init(&desktop, &fb, 0xff101010U));
    desktop.cursor.visible = 0;
    /* 2x2 window; title_h must leave at least one content row. */
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 0, 0, 2, 2, 0xff0000aaU,
                                 0xffffffffU, &window_id));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                            (uint64_t)gui_set_window_title_bar(
                                &desktop, window_id, 2U));
    TEST_ASSERT_EQUAL_UINT64(0,
                            (uint64_t)gui_set_window_title_bar(
                                &desktop, window_id, 1U));
}

void test_gui_set_title_bar_zero_disables_bar(void) {
    uint32_t pixels[64] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t window_id;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 8, 8, 8));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_init(&desktop, &fb, 0xff101010U));
    desktop.cursor.visible = 0;
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window_for_pid(
                                 &desktop, 7U, 0, 0, 8, 8, 0xff0000aaU,
                                 0xffffffffU, "abc", &window_id));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_set_window_title_bar(
                                 &desktop, window_id, 0U));

    gui_draw(&desktop);

    /* With title_h=0 the bg_color (0xff0000aa) shows at the interior
     * (1, 1) and there is no kernel title bar overlay. */
    TEST_ASSERT_EQUAL_UINT64(0xff0000aaU, pixels[1 * 8 + 1]);
}

void test_gui_close_button_pushed_event_on_title_bar_click(void) {
    fb_t fb;
    gui_desktop_t *desktop;
    uint32_t window_id;
    input_event_t press;
    gui_event_t event;

    process_table_init();
    TEST_ASSERT_NOT_NULL(process_alloc(7U, "demo"));

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)fb_init(&fb, g_test_gui_pixels,
                                               640, 480, 640));
    gui_init_for_framebuffer(&fb, 0);
    desktop = gui_desktop();
    TEST_ASSERT_NOT_NULL(desktop);

    /* Add a titled window that owns the close button. Startup no longer
     * creates any windows. */
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window_for_pid(
                                 desktop, 7U, 0, 0, 80, 80, 0xff0000aaU,
                                 0xff808080U, "demo", &window_id));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_set_window_title_bar(
                                 desktop, window_id, 12U));

    /* Cursor inside the close box: cb_x = 80 - 10 - 2 = 68, cb_y = 2,
     * cb_w = 10, cb_h = 8. Pick (72, 6). */
    gui_set_cursor(desktop, 72, 6);
    press = (input_event_t){
        .type = INPUT_EVENT_MOUSE_BUTTON,
        .timestamp = 0,
        .data.mouse_button = { .button = 0U, .pressed = 1U },
    };
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_handle_input(&press));

    /* Expect exactly one GUI_EVENT_CLOSE on the window queue. */
    TEST_ASSERT_EQUAL_UINT64(1U, desktop->windows[window_id].event_count);
    TEST_ASSERT_EQUAL_UINT64(
        0, (uint64_t)gui_window_pop_event(
               &desktop->windows[window_id], &event));
    TEST_ASSERT_EQUAL_UINT64(GUI_EVENT_CLOSE, event.type);
}

void test_gui_close_button_destroys_window_for_dead_owner(void) {
    fb_t fb;
    gui_desktop_t *desktop;
    uint32_t window_id;
    input_event_t press;
    process_t *owner;

    process_table_init();
    owner = process_alloc(7U, "demo");
    TEST_ASSERT_NOT_NULL(owner);
    process_mark_exited(owner, 0x33U);

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)fb_init(&fb, g_test_gui_pixels,
                                               640, 480, 640));
    gui_init_for_framebuffer(&fb, 0);
    desktop = gui_desktop();
    TEST_ASSERT_NOT_NULL(desktop);

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window_for_pid(
                                 desktop, 7U, 0, 0, 80, 80, 0xff0000aaU,
                                 0xff808080U, "demo", &window_id));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_set_window_title_bar(
                                 desktop, window_id, 12U));

    gui_set_cursor(desktop, 72, 6);
    press = (input_event_t){
        .type = INPUT_EVENT_MOUSE_BUTTON,
        .timestamp = 0,
        .data.mouse_button = { .button = 0U, .pressed = 1U },
    };
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_handle_input(&press));

    TEST_ASSERT_EQUAL_UINT64(0U, desktop->windows[window_id].used);
    TEST_ASSERT_EQUAL_UINT64(0U, desktop->windows[window_id].event_count);
}

void test_gui_close_button_destroys_window_for_reclaimed_owner(void) {
    fb_t fb;
    gui_desktop_t *desktop;
    uint32_t window_id;
    input_event_t press;

    process_table_init();

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)fb_init(&fb, g_test_gui_pixels,
                                               640, 480, 640));
    gui_init_for_framebuffer(&fb, 0);
    desktop = gui_desktop();
    TEST_ASSERT_NOT_NULL(desktop);

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window_for_pid(
                                 desktop, 7U, 0, 0, 80, 80, 0xff0000aaU,
                                 0xff808080U, "demo", &window_id));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_set_window_title_bar(
                                 desktop, window_id, 12U));

    gui_set_cursor(desktop, 72, 6);
    press = (input_event_t){
        .type = INPUT_EVENT_MOUSE_BUTTON,
        .timestamp = 0,
        .data.mouse_button = { .button = 0U, .pressed = 1U },
    };
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_handle_input(&press));

    TEST_ASSERT_EQUAL_UINT64(0U, desktop->windows[window_id].used);
    TEST_ASSERT_EQUAL_UINT64(0U, desktop->windows[window_id].event_count);
}

void test_gui_close_button_click_outside_box_starts_drag(void) {
    fb_t fb;
    gui_desktop_t *desktop;
    uint32_t window_id;
    input_event_t press;
    gui_event_t event;

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)fb_init(&fb, g_test_gui_pixels,
                                               640, 480, 640));
    gui_init_for_framebuffer(&fb, 0);
    desktop = gui_desktop();
    TEST_ASSERT_NOT_NULL(desktop);

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window_for_pid(
                                 desktop, 7U, 0, 0, 80, 80, 0xff0000aaU,
                                 0xff808080U, "demo", &window_id));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_set_window_title_bar(
                                 desktop, window_id, 12U));

    /* Click on the title bar but to the left of the close box: should
     * produce GUI_EVENT_MOUSE_CLICK and start a drag, NOT a close. */
    gui_set_cursor(desktop, 10, 4);
    press = (input_event_t){
        .type = INPUT_EVENT_MOUSE_BUTTON,
        .timestamp = 0,
        .data.mouse_button = { .button = 0U, .pressed = 1U },
    };
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_handle_input(&press));

    TEST_ASSERT_EQUAL_UINT64(1, (uint64_t)gui_drag_active(desktop));
    TEST_ASSERT_EQUAL_UINT64(1U, desktop->windows[window_id].event_count);
    TEST_ASSERT_EQUAL_UINT64(
        0, (uint64_t)gui_window_pop_event(
               &desktop->windows[window_id], &event));
    TEST_ASSERT_EQUAL_UINT64(GUI_EVENT_MOUSE_CLICK, event.type);
    /* And the event is not a close. */
    TEST_ASSERT_TRUE(event.type != GUI_EVENT_CLOSE);
}

void test_gui_focus_window_switches_to_target(void) {
    uint32_t pixels[80] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t first;
    uint32_t second;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 8, 10, 8));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_init(&desktop, &fb, 0xff101010U));
    desktop.cursor.visible = 0;
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window_for_pid(
                                 &desktop, 11U, 0, 0, 8, 10, 0xff0000aaU,
                                 0xff101010U, "first", &first));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window_for_pid(
                                 &desktop, 22U, 0, 0, 8, 10, 0xff00aa00U,
                                 0xff202020U, "second", &second));
    /* First window starts focused. */
    TEST_ASSERT_EQUAL_UINT64(first, desktop.focused_window_id);

    /* gui_focus_window must be callable from outside the owner pid; the
     * panel taskbar will do this. */
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_focus_window(&desktop, second));
    TEST_ASSERT_EQUAL_UINT64(second, desktop.focused_window_id);

    /* Re-focusing an unknown window must fail without changing state. */
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)gui_focus_window(
                                 &desktop, GUI_MAX_WINDOWS));
    TEST_ASSERT_EQUAL_UINT64(second, desktop.focused_window_id);
}

void test_gui_focus_window_raises_target_above_overlap(void) {
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
                                 0xff3030ffU, &first));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 2, 2, 5, 5, 0xff00aa00U,
                                 0xff30ff30U, &second));

    gui_draw(&desktop);
    TEST_ASSERT_EQUAL_UINT64(0xff00aa00U, pixels[3 * 8 + 3]);
    TEST_ASSERT_EQUAL_UINT64(second, (uint32_t)gui_hit_test(&desktop, 3, 3));

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_focus_window(&desktop, first));
    gui_draw(&desktop);
    TEST_ASSERT_EQUAL_UINT64(0xff0000aaU, pixels[3 * 8 + 3]);
    TEST_ASSERT_EQUAL_UINT64(first, (uint32_t)gui_hit_test(&desktop, 3, 3));
}

void test_gui_window_for_pid_returns_owner_windows(void) {
    uint32_t pixels[64] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t first;
    uint32_t second;
    uint32_t third;
    uint32_t orphan;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 8, 8, 8));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_init(&desktop, &fb, 0xff101010U));
    desktop.cursor.visible = 0;
    /* pid 42 owns first and second; pid 99 owns third; the ownerless window
     * (orphan) has no owner. */
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window_for_pid(
                                 &desktop, 42U, 0, 0, 4, 4, 0xff0000aaU,
                                 0xff101010U, "a", &first));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window_for_pid(
                                 &desktop, 42U, 4, 0, 4, 4, 0xff0000aaU,
                                 0xff101010U, "b", &second));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window_for_pid(
                                 &desktop, 99U, 0, 4, 4, 4, 0xff0000aaU,
                                 0xff101010U, "c", &third));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 4, 4, 4, 4, 0xff0000aaU,
                                 0xff101010U, &orphan));

    /* pid 42 enumerates two windows, in creation order. */
    TEST_ASSERT_EQUAL_UINT64(first,
                             (uint64_t)gui_window_for_pid(
                                 &desktop, 42U, 0U));
    TEST_ASSERT_EQUAL_UINT64(second,
                             (uint64_t)gui_window_for_pid(
                                 &desktop, 42U, 1U));
    /* Past the end returns NO_WINDOW. */
    TEST_ASSERT_EQUAL_UINT64(GUI_NO_WINDOW,
                             (uint64_t)gui_window_for_pid(
                                 &desktop, 42U, 2U));
    /* pid 99 owns exactly one window. */
    TEST_ASSERT_EQUAL_UINT64(third,
                             (uint64_t)gui_window_for_pid(
                                 &desktop, 99U, 0U));
    TEST_ASSERT_EQUAL_UINT64(GUI_NO_WINDOW,
                             (uint64_t)gui_window_for_pid(
                                 &desktop, 99U, 1U));
    /* The ownerless window does not appear under any pid. */
    TEST_ASSERT_EQUAL_UINT64(GUI_NO_WINDOW,
                             (uint64_t)gui_window_for_pid(
                                 &desktop, GUI_NO_OWNER, 0U));
    /* A pid that owns nothing also returns NO_WINDOW. */
    TEST_ASSERT_EQUAL_UINT64(GUI_NO_WINDOW,
                             (uint64_t)gui_window_for_pid(
                                 &desktop, 12345U, 0U));
}

/*
 * Backing buffer tests: each owner-drawn window carries a per-window
 * BGRA buffer. App draws land in the backing, and the compositor
 * blits that backing onto the framebuffer during redraw. This keeps
 * the content glued to the window across drags / focus changes /
 * z-order shuffles, which is the concrete partial-update bug the
 * pre-backing compositor could not survive.
 */

void test_gui_backing_buffer_lazy_allocates_on_first_draw(void) {
    uint32_t pixels[64] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t window_id;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 8, 8, 8));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0xff101010U));
    desktop.cursor.visible = 0;

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 0, 0, 8, 8, 0xff0000aaU,
                                 0xffffffffU, &window_id));
    /* No draw yet: no backing, no owner_drawn. */
    gui_window_t *window = &desktop.windows[window_id];
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)window->owner_drawn);
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)(uintptr_t)window->backing);

    /* First draw triggers backing allocation. */
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_window_draw_rect(
                                 &desktop, window_id, 1, 1, 2, 1, 0xffff0000U));
    TEST_ASSERT_TRUE(window->owner_drawn != 0);
    TEST_ASSERT_TRUE(window->backing != 0);
}

void test_gui_backing_buffer_init_to_bg_color(void) {
    uint32_t pixels[64] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t window_id;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 8, 8, 8));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0xff101010U));
    desktop.cursor.visible = 0;

    /*
     * The default backing is filled with the window's bg_color so the
     * first blit does not flash through arbitrary heap bytes.
     */
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 0, 0, 8, 8, 0xff334455U,
                                 0xffffffffU, &window_id));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_window_draw_rect(
                                 &desktop, window_id, 0, 0, 1, 1, 0xff000000U));

    gui_window_t *window = &desktop.windows[window_id];
    /* The owner's draw replaced pixel 0 with black; surrounding pixels
     * stay at the bg_color that the kernel seeded into the backing. */
    TEST_ASSERT_EQUAL_UINT64(0xff000000U, window->backing[0]);
    TEST_ASSERT_EQUAL_UINT64(0xff334455U, window->backing[1]);
    TEST_ASSERT_EQUAL_UINT64(0xff334455U, window->backing[8]);
}

void test_gui_backing_buffer_survives_compositor_redraw(void) {
    uint32_t pixels[64] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t window_id;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 8, 8, 8));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0xff101010U));
    desktop.cursor.visible = 0;

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 0, 0, 8, 8, 0xff000000U,
                                 0xffffffffU, &window_id));

    /* Owner paints a horizontal red bar at y=2. */
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_window_draw_rect(
                                 &desktop, window_id, 1, 2, 6, 1, 0xffff0000U));

    /* First draw: blit the backing, paint border. Red bar visible. */
    gui_draw(&desktop);
    for (uint32_t x = 1; x < 7; x++) {
        TEST_ASSERT_EQUAL_UINT64(0xffff0000U, pixels[2 * 8 + x]);
    }

    /* Trigger another redraw without any new app draw. The backing is
     * the source of truth, so the bar must still be there. */
    gui_draw(&desktop);
    for (uint32_t x = 1; x < 7; x++) {
        TEST_ASSERT_EQUAL_UINT64(0xffff0000U, pixels[2 * 8 + x]);
    }
}

void test_gui_backing_buffer_follows_window_on_drag(void) {
    uint32_t pixels[64] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t window_id;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 8, 8, 8));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0xff101010U));
    desktop.cursor.visible = 0;

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 0, 0, 4, 4, 0xff000000U,
                                 0xffffffffU, &window_id));

    /*
     * Paint a 2x2 red square at content-local (0,0). On the FB this
     * becomes (window.x, window.y) initially; the 1px border is
     * painted on top of the blit so the top row / leftmost column
     * of the square get overwritten by the border colour. The
     * red rect only covers content columns 0..1 / rows 0..1, so
     * its single surviving FB pixel is (1, 1).
     */
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_window_draw_rect(
                                 &desktop, window_id, 0, 0, 2, 2, 0xffff0000U));
    gui_draw(&desktop);
    /* Row 0 is entirely the top border. */
    TEST_ASSERT_TRUE(pixels[0 * 8 + 1] != 0xffff0000U);
    /* (1, 1) is the only inner red pixel; the rest of row 1 is
     * the right border (col 3) or untouched bg_color (col 2). */
    TEST_ASSERT_EQUAL_UINT64(0xffff0000U, pixels[1 * 8 + 1]);
    /* Row 2 is entirely the backing's untouched bg_color (rows 2 and 3
     * of the 4x4 backing were never drawn). */
    TEST_ASSERT_EQUAL_UINT64(0xff000000U, pixels[2 * 8 + 2]);
    /* Row 3 is the bottom border; cols 1..2 are interior cells. */
    TEST_ASSERT_EQUAL_UINT64(0xff000000U, pixels[2 * 8 + 1]);

    /* Drag the window by (3, 2). The compositor redraws; the backing
     * blit follows the window, so the red square is now at (3, 2). */
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_move_window(
                                 &desktop, window_id, 3, 2));
    gui_draw(&desktop);

    /* Old position should be empty (bg / gradient) again, since the
     * backing no longer lives there. */
    TEST_ASSERT_TRUE(pixels[0 * 8 + 0] != 0xffff0000U);
    TEST_ASSERT_TRUE(pixels[1 * 8 + 1] != 0xffff0000U);

    /* New position carries the red square. The 2x2 backing rect at
     * content (0..1, 0..1) now lives at FB (3..4, 2..3). Borders
     * overdraw the top row and the leftmost column, so the single
     * surviving red pixel is FB (4, 3). */
    TEST_ASSERT_TRUE(pixels[2 * 8 + 3] != 0xffff0000U);
    TEST_ASSERT_TRUE(pixels[2 * 8 + 4] != 0xffff0000U);
    /* FB (3, 3) is the new left border, not red. */
    TEST_ASSERT_TRUE(pixels[3 * 8 + 3] != 0xffff0000U);
    /* FB (4, 3) is the inner red pixel. */
    TEST_ASSERT_EQUAL_UINT64(0xffff0000U, pixels[3 * 8 + 4]);
}

void test_gui_backing_buffer_freed_on_destroy(void) {
    uint32_t pixels[64] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t window_id;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 8, 8, 8));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0xff101010U));
    desktop.cursor.visible = 0;

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 0, 0, 4, 4, 0xff000000U,
                                 0xffffffffU, &window_id));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_window_draw_rect(
                                 &desktop, window_id, 0, 0, 2, 2, 0xffff0000U));

    gui_window_t *window = &desktop.windows[window_id];
    TEST_ASSERT_TRUE(window->backing != 0);

    /* Destroying the window must release the backing so the kernel heap
     * can reuse the storage for the next window. */
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_destroy_window(
                                 &desktop, window_id));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)(uintptr_t)window->backing);
    TEST_ASSERT_EQUAL_UINT64(0, window->backing_capacity);
    TEST_ASSERT_EQUAL_UINT64(0, window->owner_drawn);
}

void test_gui_backing_buffer_reallocates_when_window_reused(void) {
    uint32_t pixels[64] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t first_id;
    uint32_t second_id;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 8, 8, 8));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0xff101010U));
    desktop.cursor.visible = 0;

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 0, 0, 4, 4, 0xff000000U,
                                 0xffffffffU, &first_id));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_window_draw_rect(
                                 &desktop, first_id, 0, 0, 1, 1, 0xffff0000U));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_destroy_window(
                                 &desktop, first_id));

    /*
     * The freed slot can host a new window of a different size; the
     * new backing must be allocated fresh and zeroed. This guards
     * against accidental reuse of the previous window's buffer.
     * Backing is lazy so we trigger the first draw before asserting.
     */
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 0, 0, 6, 6, 0xff112233U,
                                 0xffffffffU, &second_id));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_window_draw_rect(
                                 &desktop, second_id, 0, 0, 1, 1, 0xffff0000U));
    gui_window_t *window = &desktop.windows[second_id];
    TEST_ASSERT_TRUE(window->backing != 0);
    /* Backing is initialised to bg_color before any app draw; the
     * owner draw at (0, 0, 1, 1) replaced that pixel with red. */
    TEST_ASSERT_EQUAL_UINT64(0xffff0000U, window->backing[0]);
    TEST_ASSERT_EQUAL_UINT64(0xff112233U, window->backing[1]);
    TEST_ASSERT_EQUAL_UINT64(0xff112233U, window->backing[6]);
}

/*
 * Damage-tracking tests. These exercise the per-rect path that the
 * partial-redraw branch of gui_draw uses, so a regression in coalescing,
 * clipping, or the full-sentinel fallback is caught by the host suite
 * before it reaches the QEMU run.
 */

void test_gui_damage_init_queues_full_sentinel(void) {
    uint32_t pixels[32 * 24];
    fb_t fb;
    gui_desktop_t desktop;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 32, 24, 32));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0xff101010U));
    /* gui_init marks the desktop as fully damaged so the first draw
     * paints the gradient even before any window exists. */
    TEST_ASSERT_EQUAL_UINT64(1U, desktop.damage_full);
    TEST_ASSERT_EQUAL_UINT64(0U, desktop.damage_count);
}

void test_gui_damage_add_clips_to_framebuffer(void) {
    uint32_t pixels[32 * 24];
    fb_t fb;
    gui_desktop_t desktop;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 32, 24, 32));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0xff101010U));
    /* Drop the initial full sentinel so we exercise the partial path. */
    gui_damage_clear(&desktop);

    gui_damage_add(&desktop, 0, 0, 4, 4);
    TEST_ASSERT_EQUAL_UINT64(0U, desktop.damage_full);
    TEST_ASSERT_EQUAL_UINT64(1U, desktop.damage_count);
    /* Fully outside the framebuffer: dropped. */
    gui_damage_add(&desktop, 100, 100, 4, 4);
    TEST_ASSERT_EQUAL_UINT64(1U, desktop.damage_count);
    /* Partly outside: clipped to the framebuffer edge. */
    gui_damage_add(&desktop, 30, 20, 10, 10);
    TEST_ASSERT_EQUAL_UINT64(2U, desktop.damage_count);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uint32_t)30,
                             (uint64_t)(uint32_t)desktop.damage_rects[1].x);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uint32_t)20,
                             (uint64_t)(uint32_t)desktop.damage_rects[1].y);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uint32_t)2,
                             (uint64_t)(uint32_t)desktop.damage_rects[1].w);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uint32_t)4,
                             (uint64_t)(uint32_t)desktop.damage_rects[1].h);
    /* Zero-area rects are dropped. */
    gui_damage_add(&desktop, 0, 0, 0, 4);
    gui_damage_add(&desktop, 0, 0, 4, 0);
    TEST_ASSERT_EQUAL_UINT64(2U, desktop.damage_count);
}

void test_gui_damage_add_merges_overlapping_rects(void) {
    uint32_t pixels[32 * 24];
    fb_t fb;
    gui_desktop_t desktop;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 32, 24, 32));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0xff101010U));
    gui_damage_clear(&desktop);

    gui_damage_add(&desktop, 0, 0, 8, 8);
    gui_damage_add(&desktop, 4, 4, 8, 8);
    TEST_ASSERT_EQUAL_UINT64(1U, desktop.damage_count);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uint32_t)0,
                             (uint64_t)(uint32_t)desktop.damage_rects[0].x);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uint32_t)0,
                             (uint64_t)(uint32_t)desktop.damage_rects[0].y);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uint32_t)12,
                             (uint64_t)(uint32_t)desktop.damage_rects[0].w);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uint32_t)12,
                             (uint64_t)(uint32_t)desktop.damage_rects[0].h);

    /* Adjacent (touching) rects also merge so we do not pay for two
     * full-row fillrects on the same band. */
    gui_damage_add(&desktop, 12, 0, 4, 4);
    TEST_ASSERT_EQUAL_UINT64(1U, desktop.damage_count);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uint32_t)16,
                             (uint64_t)(uint32_t)desktop.damage_rects[0].w);
}

void test_gui_damage_add_full_collapses_to_sentinel(void) {
    uint32_t pixels[32 * 24];
    fb_t fb;
    gui_desktop_t desktop;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 32, 24, 32));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0xff101010U));
    gui_damage_clear(&desktop);

    /* A rect that covers the full framebuffer collapses to the sentinel
     * and short-circuits any further adds until the next clear. */
    gui_damage_add(&desktop, 0, 0, 32, 24);
    TEST_ASSERT_EQUAL_UINT64(1U, desktop.damage_full);
    TEST_ASSERT_EQUAL_UINT64(0U, desktop.damage_count);

    gui_damage_add(&desktop, 0, 0, 4, 4);
    TEST_ASSERT_EQUAL_UINT64(1U, desktop.damage_full);
    TEST_ASSERT_EQUAL_UINT64(0U, desktop.damage_count);
}

void test_gui_damage_overflow_collapses_to_sentinel(void) {
    uint32_t pixels[64 * 64];
    fb_t fb;
    gui_desktop_t desktop;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 64, 64, 64));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0xff101010U));
    gui_damage_clear(&desktop);

    /* Push more non-overlapping rects than the list can hold. The list
     * collapses to "full" so the next draw degrades to a full repaint
     * instead of overflowing. */
    for (uint32_t i = 0; i < GUI_DAMAGE_MAX + 4U; i++) {
        uint32_t x = (i * 7U) % 60U;
        uint32_t y = (i * 5U) % 60U;
        gui_damage_add(&desktop, (int32_t)x, (int32_t)y, 2, 2);
    }
    TEST_ASSERT_EQUAL_UINT64(1U, desktop.damage_full);
    TEST_ASSERT_EQUAL_UINT64(0U, desktop.damage_count);
}

void test_gui_damage_draw_text_pushes_tight_rect(void) {
    uint32_t pixels[64] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t window_id;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 8, 8, 8));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0xff101010U));
    desktop.cursor.visible = 0;
    /* Drop the initial full sentinel so we can inspect the rect added
     * by a single draw call. */
    gui_damage_clear(&desktop);

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 1, 1, 5, 5, 0xff000000U,
                                 0xffffffffU, &window_id));
    /* The window create itself adds a rect; clear it so we only see the
     * one the upcoming draw_text will queue. */
    gui_damage_clear(&desktop);

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_window_draw_text(
                                 &desktop, window_id, 0, 0, "ab",
                                 0xffffffffU));
    TEST_ASSERT_EQUAL_UINT64(1U, desktop.damage_count);
    /* Tight text rect: window (1,1) + title_h 0 + content (0,0). The
     * text is 16 px wide and 8 px tall, but the window is 5x5, so the
     * rect is clipped to the window's right and bottom edges. */
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uint32_t)1,
                             (uint64_t)(uint32_t)desktop.damage_rects[0].x);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uint32_t)1,
                             (uint64_t)(uint32_t)desktop.damage_rects[0].y);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uint32_t)5,
                             (uint64_t)(uint32_t)desktop.damage_rects[0].w);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uint32_t)5,
                             (uint64_t)(uint32_t)desktop.damage_rects[0].h);
}

void test_gui_damage_draw_rect_pushes_content_rect(void) {
    uint32_t pixels[64] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t window_id;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 8, 8, 8));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0xff101010U));
    desktop.cursor.visible = 0;
    gui_damage_clear(&desktop);

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 2, 2, 4, 4, 0xff000000U,
                                 0xffffffffU, &window_id));
    gui_damage_clear(&desktop);

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_window_draw_rect(
                                 &desktop, window_id, 1, 1, 2, 2, 0xffff0000U));
    TEST_ASSERT_EQUAL_UINT64(1U, desktop.damage_count);
    /* Content (1,1,2,2) inside window (2,2,4,4): framebuffer (3,3,2,2). */
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uint32_t)3,
                             (uint64_t)(uint32_t)desktop.damage_rects[0].x);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uint32_t)3,
                             (uint64_t)(uint32_t)desktop.damage_rects[0].y);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uint32_t)2,
                             (uint64_t)(uint32_t)desktop.damage_rects[0].w);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uint32_t)2,
                             (uint64_t)(uint32_t)desktop.damage_rects[0].h);
}

void test_gui_damage_title_bar_offset_in_draw_text(void) {
    uint32_t pixels[64] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t window_id;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 8, 8, 8));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0xff101010U));
    desktop.cursor.visible = 0;
    gui_damage_clear(&desktop);

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 1, 1, 5, 5, 0xff000000U,
                                 0xffffffffU, &window_id));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_set_window_title_bar(
                                 &desktop, window_id, 2U));
    gui_damage_clear(&desktop);

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_window_draw_text(
                                 &desktop, window_id, 0, 0, "x",
                                 0xffffffffU));
    /* Content (0,0) below a 2-px title bar at window y=1: framebuffer y=3. */
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uint32_t)1,
                             (uint64_t)(uint32_t)desktop.damage_rects[0].x);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uint32_t)3,
                             (uint64_t)(uint32_t)desktop.damage_rects[0].y);
}

void test_gui_damage_move_pushes_union_rect(void) {
    uint32_t pixels[64] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t window_id;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 16, 16, 16));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0xff101010U));
    desktop.cursor.visible = 0;
    gui_damage_clear(&desktop);

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 0, 0, 4, 4, 0xff000000U,
                                 0xffffffffU, &window_id));
    gui_damage_clear(&desktop);

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_move_window(&desktop, window_id,
                                                       4, 0));
    /* Old rect (0,0,4,4) and new rect (4,0,4,4): union (0,0,8,4). */
    TEST_ASSERT_EQUAL_UINT64(1U, desktop.damage_count);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uint32_t)0,
                             (uint64_t)(uint32_t)desktop.damage_rects[0].x);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uint32_t)0,
                             (uint64_t)(uint32_t)desktop.damage_rects[0].y);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uint32_t)8,
                             (uint64_t)(uint32_t)desktop.damage_rects[0].w);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uint32_t)4,
                             (uint64_t)(uint32_t)desktop.damage_rects[0].h);
}

void test_gui_damage_partial_repaint_leaves_pixels_outside_rect(void) {
    /*
     * With a small damage rect and no windows, gui_draw must only
     * touch the gradient pixels in that rect. The compositor walks the
     * window list in z-order, so a regression that always repaints
     * the entire framebuffer (or that swallows the damage list and
     * does nothing) fails here.
     */
    uint32_t pixels[16 * 16];
    fb_t fb;
    gui_desktop_t desktop;

    for (uint32_t i = 0; i < 16 * 16; i++) {
        pixels[i] = 0xff777777U;
    }
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 16, 16, 16));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0xff101010U));
    desktop.cursor.visible = 0;
    /* Drop the initial full sentinel so the upcoming draw only repaints
     * what we explicitly damage. */
    gui_damage_clear(&desktop);

    /* Mark a tiny rect (0,0)-(1,1) and run the partial draw. */
    gui_damage_add(&desktop, 0, 0, 1, 1);
    gui_draw(&desktop);

    /* Pixel inside the dirty rect was repainted by the gradient. */
    TEST_ASSERT_TRUE(pixels[0] != 0xff777777U);
    /* Pixels outside the dirty rect keep the pre-draw value, including
     * the immediately adjacent cells (right and below) and the
     * opposite corner. */
    TEST_ASSERT_EQUAL_UINT64(0xff777777U, pixels[1]);
    TEST_ASSERT_EQUAL_UINT64(0xff777777U, pixels[16]);
    TEST_ASSERT_EQUAL_UINT64(0xff777777U, pixels[15 * 16 + 15]);
}

void test_gui_damage_partial_repaint_paints_every_pixel_in_rect(void) {
    /* Stricter variant of test_gui_damage_partial_repaint_leaves_pixels_outside_rect.
     * A 4x4 rect at (4,4) inside a 16x16 desktop: every pixel inside the
     * rect must be repainted (gradient touched it), every pixel outside
     * the rect must keep its prior value. Catches a regression where
     * the partial path under-fills the rect (e.g. off-by-one in the
     * row loop) or accidentally writes one column past the right edge. */
    uint32_t pixels[16 * 16];
    fb_t fb;
    gui_desktop_t desktop;

    for (uint32_t i = 0; i < 16 * 16; i++) {
        pixels[i] = 0xff777777U;
    }
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 16, 16, 16));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0xff101010U));
    desktop.cursor.visible = 0;
    gui_damage_clear(&desktop);

    /* 4x4 rect at (4,4) covers pixels 4..7 in rows 4..7. */
    gui_damage_add(&desktop, 4, 4, 4, 4);
    gui_draw(&desktop);

    /* Every pixel inside the rect was repainted. */
    for (uint32_t row = 4; row < 8; row++) {
        for (uint32_t col = 4; col < 8; col++) {
            uint32_t idx = row * 16U + col;
            TEST_ASSERT_TRUE(pixels[idx] != 0xff777777U);
        }
    }

    /* Every pixel outside the rect keeps its prior value. We check the
     * four borders (left, right, top, bottom of the rect) and the four
     * outer corners so a regression that overruns by one pixel on any
     * side fails. */
    const uint32_t prior = 0xff777777U;
    /* Top border (row 3). */
    for (uint32_t col = 0; col < 16; col++) {
        TEST_ASSERT_EQUAL_UINT64((uint64_t)prior,
                                 (uint64_t)pixels[3 * 16U + col]);
    }
    /* Bottom border (row 8). */
    for (uint32_t col = 0; col < 16; col++) {
        TEST_ASSERT_EQUAL_UINT64((uint64_t)prior,
                                 (uint64_t)pixels[8 * 16U + col]);
    }
    /* Left border of the rect rows (cols 0..3). */
    for (uint32_t row = 4; row < 8; row++) {
        for (uint32_t col = 0; col < 4; col++) {
            TEST_ASSERT_EQUAL_UINT64((uint64_t)prior,
                                     (uint64_t)pixels[row * 16U + col]);
        }
    }
    /* Right border of the rect rows (cols 8..15). */
    for (uint32_t row = 4; row < 8; row++) {
        for (uint32_t col = 8; col < 16; col++) {
            TEST_ASSERT_EQUAL_UINT64((uint64_t)prior,
                                     (uint64_t)pixels[row * 16U + col]);
        }
    }
    /* Outer corners. */
    TEST_ASSERT_EQUAL_UINT64((uint64_t)prior, (uint64_t)pixels[0]);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)prior, (uint64_t)pixels[15]);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)prior, (uint64_t)pixels[15 * 16U]);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)prior, (uint64_t)pixels[15 * 16U + 15U]);
}

void test_gui_damage_partial_repaint_skips_when_rect_outside_fb(void) {
    /* A damage rect entirely outside the framebuffer must be a no-op.
     * gui_damage_add clips; the resulting damage list stays empty so
     * gui_draw has nothing to repaint and every pixel keeps its prior
     * value. */
    uint32_t pixels[16 * 16];
    fb_t fb;
    gui_desktop_t desktop;

    for (uint32_t i = 0; i < 16 * 16; i++) {
        pixels[i] = 0xff777777U;
    }
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 16, 16, 16));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0xff101010U));
    desktop.cursor.visible = 0;
    gui_damage_clear(&desktop);

    /* Rect completely above the framebuffer. gui_damage_add drops it. */
    gui_damage_add(&desktop, 0, -10, 4, 4);
    /* Rect completely to the right of the framebuffer. */
    gui_damage_add(&desktop, 100, 0, 4, 4);
    /* Rect at negative coords. */
    gui_damage_add(&desktop, -5, -5, 4, 4);

    gui_draw(&desktop);

    /* Every pixel still has its prior value: nothing was repainted. */
    for (uint32_t i = 0; i < 16 * 16; i++) {
        TEST_ASSERT_EQUAL_UINT64((uint64_t)0xff777777U, (uint64_t)pixels[i]);
    }
    TEST_ASSERT_EQUAL_UINT64(0U, desktop.damage_count);
    TEST_ASSERT_EQUAL_UINT64(0U, desktop.damage_full);
}

void test_gui_damage_partial_repaint_with_multiple_disjoint_rects(void) {
    /* Two non-overlapping damage rects in the same draw must both be
     * repainted, with their respective surrounding pixels kept
     * untouched. */
    uint32_t pixels[32 * 16];
    fb_t fb;
    gui_desktop_t desktop;

    for (uint32_t i = 0; i < 32 * 16; i++) {
        pixels[i] = 0xff777777U;
    }
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 32, 16, 32));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0xff101010U));
    desktop.cursor.visible = 0;
    gui_damage_clear(&desktop);

    gui_damage_add(&desktop, 0, 0, 4, 4);
    gui_damage_add(&desktop, 16, 8, 4, 4);
    TEST_ASSERT_EQUAL_UINT64(2U, desktop.damage_count);

    gui_draw(&desktop);

    /* First rect: pixels (0..3, 0..3). */
    for (uint32_t row = 0; row < 4; row++) {
        for (uint32_t col = 0; col < 4; col++) {
            uint32_t idx = row * 32U + col;
            TEST_ASSERT_TRUE(pixels[idx] != 0xff777777U);
        }
    }
    /* Second rect: pixels (16..19, 8..11). */
    for (uint32_t row = 8; row < 12; row++) {
        for (uint32_t col = 16; col < 20; col++) {
            uint32_t idx = row * 32U + col;
            TEST_ASSERT_TRUE(pixels[idx] != 0xff777777U);
        }
    }
    /* A pixel between the two rects must be untouched. */
    TEST_ASSERT_EQUAL_UINT64(0xff777777U, pixels[5 * 32U + 10U]);
    /* Pixels just outside the first rect must be untouched. */
    TEST_ASSERT_EQUAL_UINT64(0xff777777U, pixels[4]);
    TEST_ASSERT_EQUAL_UINT64(0xff777777U, pixels[4 * 32U + 0U]);
}

void test_gui_damage_full_sentinel_re_paints_everything(void) {
    uint32_t pixels[16 * 16];
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t window_id;

    for (uint32_t i = 0; i < 16 * 16; i++) {
        pixels[i] = 0xff777777U;
    }
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 16, 16, 16));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0xff101010U));
    desktop.cursor.visible = 0;
    gui_damage_clear(&desktop);

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window(
                                 &desktop, 0, 0, 4, 4, 0xff000000U,
                                 0xffffffffU, &window_id));
    gui_damage_clear(&desktop);

    /* Collapse to full: next gui_draw must repaint the whole fb. */
    gui_damage_add_full(&desktop);
    gui_draw(&desktop);

    /* The corner pixel that the partial-path test leaves untouched
     * here is repainted by the gradient. */
    TEST_ASSERT_TRUE(pixels[0] != 0xff777777U);
}

/* ------------------------------------------------------------------
 * Tests for gui_destroy_windows_for_pid. The helper walks the
 * window pool back-to-front destroying every window whose
 * owner_pid matches. We verify the loop, the multi-window case,
 * and the GUI_NO_OWNER / non-matching paths.
 * ------------------------------------------------------------------ */

void test_gui_destroy_windows_for_pid_destroys_owner_windows(void) {
    uint32_t pixels[64] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t w_a;
    uint32_t w_b;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 8, 8, 8));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0xff000000U));
    desktop.cursor.visible = 0;

    /* Owner 7 owns two windows; owner 8 owns one. The framebuffer
     * is 8x8 so we keep all windows inside [0, 4] x [0, 4] range. */
    TEST_ASSERT_EQUAL_UINT64(0,
        (uint64_t)gui_create_window_for_pid(&desktop, 7U, 0, 0, 2, 2,
            0xff111111U, 0xff000000U, "a", &w_a));
    TEST_ASSERT_EQUAL_UINT64(0,
        (uint64_t)gui_create_window_for_pid(&desktop, 7U, 2, 0, 2, 2,
            0xff222222U, 0xff000000U, "b", &w_b));
    TEST_ASSERT_EQUAL_UINT64(0,
        (uint64_t)gui_create_window_for_pid(&desktop, 8U, 4, 0, 2, 2,
            0xff333333U, 0xff000000U, "c", NULL));

    gui_destroy_windows_for_pid(&desktop, 7U);

    /* Both of pid 7's windows must be UNUSED; pid 8's window must
     * still be alive because the destroy call only matches 7. */
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)desktop.windows[w_a].used);
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)desktop.windows[w_b].used);
    /* Find the still-alive window (its id is whichever is still
     * used == 1) and assert it is owner 8. */
    for (uint32_t i = 0; i < GUI_MAX_WINDOWS; i++) {
        if (desktop.windows[i].used != 0) {
            TEST_ASSERT_EQUAL_UINT64(8U, desktop.windows[i].owner_pid);
        }
    }
}

void test_gui_destroy_windows_for_pid_is_safe_on_no_match(void) {
    uint32_t pixels[64] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t window_id;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 8, 8, 8));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0xff000000U));
    desktop.cursor.visible = 0;

    TEST_ASSERT_EQUAL_UINT64(0,
        (uint64_t)gui_create_window_for_pid(&desktop, 7U, 0, 0, 4, 4,
            0xff111111U, 0xff000000U, "a", &window_id));

    /* No window owned by pid 99 — must be a no-op. */
    gui_destroy_windows_for_pid(&desktop, 99U);

    TEST_ASSERT_EQUAL_UINT64(1, (uint64_t)desktop.windows[window_id].used);
    TEST_ASSERT_EQUAL_UINT64(7U, desktop.windows[window_id].owner_pid);
}

void test_gui_destroy_windows_for_pid_handles_null_and_no_owner_sentinel(void) {
    /*
     * GUI_NO_OWNER is 0xffffffff. Passing it must not match any
     * window (none have that pid). Passing a NULL desktop must
     * not crash.
     */
    uint32_t pixels[64] = { 0 };
    fb_t fb;
    gui_desktop_t desktop;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)fb_init(&fb, pixels, 8, 8, 8));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0xff000000U));
    desktop.cursor.visible = 0;

    gui_destroy_windows_for_pid(0, 7U);
    gui_destroy_windows_for_pid(&desktop, GUI_NO_OWNER);
    /* No assertion — the run is the test. */
    TEST_ASSERT_TRUE(1);
}
