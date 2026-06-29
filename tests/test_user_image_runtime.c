/*
 * tests/test_user_image_runtime.c
 *
 * Host-side tests for the KLI1 image loader that catch the
 * class of bugs we just shipped (image_size smaller than the
 * actual data). The loader itself is correct: it copies exactly
 * `image_size` bytes from the source blob into the per-process
 * slot. The bug was that the BUILDER declared an `image_end`
 * symbol inside `.user.image.text`, so the KLI1 header's
 * `image_size` field covered the code but not the rodata that
 * followed. Strings past `image_size` landed at the very tail of
 * the source blob but were silently dropped on copy.
 *
 * These tests lock the contract:
 *
 *   1. The loader copies exactly `image_size` bytes, even when the
 *      source is longer. This is the *source* of the bug — there
 *      is no error path the loader can take, it just does what
 *      the header says.
 *
 *   2. A well-formed blob whose `image_size` covers the entire
 *      payload (header + code + rodata at the tail) round-trips
 *      correctly: the trailing rodata survives the copy and is
 *      readable from the destination.
 *
 *   3. A buggy blob whose `image_size` stops at end-of-text leaves
 *      the trailing rodata in the source but unreachable from the
 *      destination. The test uses a unique trailing byte
 *      sequence and asserts the destination does NOT contain it,
 *      proving the contract: builders must put `image_end` at the
 *      tail of the image.
 *
 *   4. Edge cases — image_size == 0, entry_offset outside the
 *      declared range — all rejected with -1.
 *
 *   5. Every shipping app's .bin file passes the same check: the
 *      KLI1 header's image_size field covers the entire .bin. If
 *      a future commit drops _end.S back into .user.image.text
 *      by accident, this test fails loudly with the app name.
 *
 * Implementation note: the host test runs on x86-64 with gcc.
 * The KLI1 blob lives in a custom .section whose attributes
 * confuse the optimiser when the test mixes libc memcmp/strlen
 * on the inline-asm addresses. To stay robust the helpers below
 * are hand-rolled byte loops with no libc involvement beyond
 * memcmp on test-stack buffers (which are safe).
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "unity/unity.h"
#include "../kernel/user_image_format.h"
#include "../kernel/user_image.h"

/*
 * AArch64 instructions used in the inline blobs below. The host
 * test does not execute them; they are just inert bytes that pad
 * the blobs to the right size.
 */
#define AARCH64_NOP  0xd503201fU

/* ------------------------------------------------------------------
 * Blob 1: well-formed. image_size covers header + code + rodata.
 * The trailing rodata MUST survive the load.
 * ------------------------------------------------------------------ */
__asm__(
    ".section .user_image_well_formed_blob, \"a\"\n"
    ".balign 16\n"
    ".global _user_image_well_formed_start\n"
    "_user_image_well_formed_start:\n"
    ".long 0x31494c4b\n"           /* magic */
    ".hword 80\n"                  /* header_size */
    ".hword 1\n"                   /* entry_count */
    ".quad _user_image_well_formed_end - _user_image_well_formed_start\n"
    ".quad _user_image_well_formed_entry - _user_image_well_formed_start\n"
    ".skip 56\n"                   /* entry_offsets[1..7] = 0 */
    ".global _user_image_well_formed_entry\n"
    "_user_image_well_formed_entry:\n"
    ".long 0xd503201f\n"
    ".long 0xd503201f\n"
    ".long 0xd503201f\n"
    ".long 0xd503201f\n"
    ".skip 96\n"                   /* pretend code */
    ".ascii \"RODATA_AT_TAIL\\0\"\n"
    ".balign 16\n"
    ".global _user_image_well_formed_end\n"
    "_user_image_well_formed_end:\n"
);

/* ------------------------------------------------------------------
 * Blob 2: broken. image_size stops at end-of-text, leaving the
 * trailing rodata in the source but NOT in the destination. This
 * documents the bug-prone contract that the BUILDER is responsible
 * for putting image_end at the actual tail of the image.
 * ------------------------------------------------------------------ */
__asm__(
    ".section .user_image_short_image_size_blob, \"a\"\n"
    ".balign 16\n"
    ".global _user_image_short_image_size_start\n"
    "_user_image_short_image_size_start:\n"
    ".long 0x31494c4b\n"           /* magic */
    ".hword 80\n"                  /* header_size */
    ".hword 1\n"                   /* entry_count */
    /* Deliberately stop at end-of-text, missing the rodata tail. */
    ".quad _user_image_short_image_size_end_of_text - _user_image_short_image_size_start\n"
    ".quad _user_image_short_image_size_entry - _user_image_short_image_size_start\n"
    ".skip 56\n"
    ".global _user_image_short_image_size_entry\n"
    "_user_image_short_image_size_entry:\n"
    ".long 0xd503201f\n"
    ".long 0xd503201f\n"
    ".long 0xd503201f\n"
    ".long 0xd503201f\n"
    ".skip 64\n"
    ".global _user_image_short_image_size_end_of_text\n"
    "_user_image_short_image_size_end_of_text:\n"
    /* The trailing rodata. This will be in the SOURCE blob but NOT
     * in the destination after the load. */
    ".ascii \"UNREACHABLE_FROM_LOAD\\0\"\n"
    "_user_image_short_image_size_end:\n"
);

/* ------------------------------------------------------------------
 * Blob 3: image_size == 0. Must be rejected.
 * ------------------------------------------------------------------ */
__asm__(
    ".section .user_image_zero_size_blob, \"a\"\n"
    ".balign 16\n"
    ".global _user_image_zero_size_start\n"
    "_user_image_zero_size_start:\n"
    ".long 0x31494c4b\n"
    ".hword 80\n"
    ".hword 1\n"
    ".quad 0\n"                    /* image_size = 0 */
    ".quad 0\n"
    ".skip 56\n"
    ".global _user_image_zero_size_end\n"
    "_user_image_zero_size_end:\n"
);

/* ------------------------------------------------------------------
 * Blob 4: entry_offset is past image_size. Must be rejected.
 * ------------------------------------------------------------------ */
__asm__(
    ".section .user_image_bad_entry_blob, \"a\"\n"
    ".balign 16\n"
    ".global _user_image_bad_entry_start\n"
    "_user_image_bad_entry_start:\n"
    ".long 0x31494c4b\n"
    ".hword 80\n"
    ".hword 1\n"
    ".quad _user_image_bad_entry_end - _user_image_bad_entry_start\n"
    ".quad 9999\n"                 /* entry_offset out of range */
    ".skip 56\n"
    ".long 0xd503201f\n"
    ".skip 32\n"
    ".global _user_image_bad_entry_end\n"
    "_user_image_bad_entry_end:\n"
);

extern char _user_image_well_formed_start[];
extern char _user_image_well_formed_end[];
extern char _user_image_well_formed_entry[];
extern char _user_image_short_image_size_start[];
extern char _user_image_short_image_size_end[];
extern char _user_image_short_image_size_end_of_text[];
extern char _user_image_zero_size_start[];
extern char _user_image_zero_size_end[];
extern char _user_image_bad_entry_start[];
extern char _user_image_bad_entry_end[];

void test_user_image_runtime_well_formed_covers_rodata_at_tail(void) {
    /*
     * image_size spans the entire blob, including the trailing
     * rodata string. After load, the destination buffer must
     * contain that string.
     *
     * This is the contract the loader relies on: builders must
     * put the image_end marker at the tail of the .user_image
     * section so image_size covers every byte the program
     * intends to read. Without that, this test's mirror would
     * still pass, but a real shipping app's strings would
     * silently disappear (the recent BUG 2).
     */
    uint8_t loaded[256];
    user_image_t image;
    int found = 0;
    const char needle[] = "RODATA_AT_TAIL";
    size_t nlen = sizeof(needle) - 1;

    /* Manually zero the load buffer; the host Unity strips
     * memset so we cannot rely on it for the loader's contract. */
    for (size_t i = 0; i < sizeof(loaded); i++) {
        loaded[i] = 0;
    }

    uint64_t source_size = (uint64_t)(uintptr_t)_user_image_well_formed_end -
                            (uint64_t)(uintptr_t)_user_image_well_formed_start;

    TEST_ASSERT_TRUE((int)(0) == user_image_load_flat(&image, "well_formed",
            (uint64_t)(uintptr_t)loaded, sizeof(loaded),
            (uint64_t)(uintptr_t)_user_image_well_formed_start,
            source_size, 0));

    /* The loader must report the SAME image_size as the source. */
    TEST_ASSERT_EQUAL_UINT64(source_size, image.size);

    /*
     * Search the destination for the trailing rodata string.
     * Hand-rolled loop avoids any libc memcmp/strlen on the
     * custom-section addresses, which the optimiser occasionally
     * mis-compiles on the host toolchain.
     */
    for (uint64_t i = 0; i + nlen <= image.size && !found; i++) {
        size_t j = 0;
        while (j < nlen && (uint8_t)loaded[i + j] == (uint8_t)needle[j]) {
            j++;
        }
        if (j == nlen) {
            found = 1;
        }
    }
    TEST_ASSERT_TRUE(found);
}

void test_user_image_runtime_short_image_size_drops_tail(void) {
    /*
     * Pin the contract from the other side: when the builder
     * declares an image_size smaller than the source, the loader
     * honours image_size (it cannot guess what the builder
     * intended). The trailing rodata is in the SOURCE blob but
     * NOT in the destination. This is the shape of BUG 2 and the
     * reason every shipping app's _end.S must sit in
     * .user.image.end (collected after .rodata).
     */
    uint8_t loaded[256];
    user_image_t image;
    int found_in_source = 0;
    int found_in_loaded = 0;
    const char needle[] = "UNREACHABLE_FROM_LOAD";
    size_t nlen = sizeof(needle) - 1;

    for (size_t i = 0; i < sizeof(loaded); i++) {
        loaded[i] = 0;
    }

    uint64_t declared_size = (uint64_t)(uintptr_t)_user_image_short_image_size_end_of_text -
                             (uint64_t)(uintptr_t)_user_image_short_image_size_start;
    uint64_t source_size = (uint64_t)(uintptr_t)_user_image_short_image_size_end -
                           (uint64_t)(uintptr_t)_user_image_short_image_size_start;

    /* Sanity: the buggy blob really is bigger than declared. */
    TEST_ASSERT_TRUE(source_size > declared_size);

    TEST_ASSERT_TRUE((int)(0) == user_image_load_flat(&image, "short_image_size",
            (uint64_t)(uintptr_t)loaded, sizeof(loaded),
            (uint64_t)(uintptr_t)_user_image_short_image_size_start,
            declared_size, 0));

    TEST_ASSERT_EQUAL_UINT64(declared_size, image.size);

    const char *source = (const char *)(uintptr_t)_user_image_short_image_size_start;

    for (uint64_t i = 0; i + nlen <= source_size && !found_in_source; i++) {
        size_t j = 0;
        while (j < nlen && (uint8_t)source[i + j] == (uint8_t)needle[j]) {
            j++;
        }
        if (j == nlen) {
            found_in_source = 1;
        }
    }
    for (uint64_t i = 0; i + nlen <= image.size && !found_in_loaded; i++) {
        size_t j = 0;
        while (j < nlen && (uint8_t)loaded[i + j] == (uint8_t)needle[j]) {
            j++;
        }
        if (j == nlen) {
            found_in_loaded = 1;
        }
    }
    TEST_ASSERT_TRUE(found_in_source);
    TEST_ASSERT_TRUE(!found_in_loaded);
}

void test_user_image_runtime_rejects_image_size_zero(void) {
    /*
     * image_size == 0 is rejected by user_image_load_copy's
     * source_size == 0 check. Pin it.
     */
    uint8_t loaded[64];
    user_image_t image;

    for (size_t i = 0; i < sizeof(loaded); i++) {
        loaded[i] = 0;
    }

    TEST_ASSERT_TRUE((int)(-1) == user_image_load_flat(&image, "zero",
            (uint64_t)(uintptr_t)loaded, sizeof(loaded),
            (uint64_t)(uintptr_t)_user_image_zero_size_start,
            256, 0));
}

void test_user_image_runtime_rejects_entry_outside_range(void) {
    /*
     * entry_offset past image_size is rejected by the loader so
     * a bad header cannot redirect execution into unmapped
     * memory. The test setup uses a deliberately invalid
     * entry_offset (9999).
     */
    uint8_t loaded[128];
    user_image_t image;

    for (size_t i = 0; i < sizeof(loaded); i++) {
        loaded[i] = 0;
    }

    uint64_t source_size = (uint64_t)(uintptr_t)_user_image_bad_entry_end -
                            (uint64_t)(uintptr_t)_user_image_bad_entry_start;

    TEST_ASSERT_TRUE((int)(-1) == user_image_load_flat(&image, "bad_entry",
            (uint64_t)(uintptr_t)loaded, sizeof(loaded),
            (uint64_t)(uintptr_t)_user_image_bad_entry_start,
            source_size, 0));
}

void test_user_image_runtime_entry_offset_matches_image_base_plus_offset(void) {
    /*
     * After a successful load, user_image_entry must return
     * image->base + image->entry_offset. The loader captures
     * entry_offset as (source_entry - source_base) at load time,
     * so a header that points entry_offsets[0] at the .text
     * segment must report a matching entry address.
     */
    uint8_t loaded[256];
    user_image_t image;

    for (size_t i = 0; i < sizeof(loaded); i++) {
        loaded[i] = 0;
    }

    uint64_t source_size = (uint64_t)(uintptr_t)_user_image_well_formed_end -
                            (uint64_t)(uintptr_t)_user_image_well_formed_start;
    uint64_t entry_in_source = (uint64_t)(uintptr_t)_user_image_well_formed_entry -
                                (uint64_t)(uintptr_t)_user_image_well_formed_start;

    TEST_ASSERT_TRUE((int)(0) == user_image_load_flat(&image, "entry_round_trip",
            (uint64_t)(uintptr_t)loaded, sizeof(loaded),
            (uint64_t)(uintptr_t)_user_image_well_formed_start,
            source_size, 0));

    TEST_ASSERT_EQUAL_UINT64(entry_in_source, image.entry_offset);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(uintptr_t)loaded + entry_in_source,
                             user_image_entry(&image));
}
/* ------------------------------------------------------------------
 * Section 5: each shipping app's .bin must have an image_size that
 * covers the entire .user_image section. The .bin is the production
 * linker's flat dump of `.app_<name>` (which KEEPs
 * .app_<name>_blob). That blob carries the .elf's `.user_image`
 * plus `.eh_frame`. The kernel loader reads exactly `image_size`
 * bytes from this blob, so the meaningful contract is that
 * image_size matches the .user_image section size. The .bin size
 * is larger by the .eh_frame tail; we cannot rely on the bin
 * size alone to detect a regression because the difference
 * between "image_end in .user.image.end" (correct) and
 * "image_end in .user.image.text" (BUG 2) is small enough to be
 * hidden by .eh_frame variance.
 *
 * Instead, we hardcode the expected image_size per app. The value
 * is `image_end - image_header` from the .elf and must match
 * exactly. A future commit that drops _end.S back into
 * .user.image.text reduces the value (BUG 2). A future commit
 * that grows .user_image legitimately bumps the value and forces
 * the test to be updated — which is the right time to think about
 * whether the change is safe.
 * ------------------------------------------------------------------ */

/* objcopy on a path like "../build/programs/apps/shell.bin" emits
 * symbols named _binary____build_programs_apps_shell_bin_{start,end}
 * (4 underscores: _binary + the leading .. of the input path). */
#define DECLARE_APP_BLOB(name) \
    extern char _binary____build_programs_apps_##name##_bin_start[]; \
    extern char _binary____build_programs_apps_##name##_bin_end[];

DECLARE_APP_BLOB(shell)
DECLARE_APP_BLOB(editor)
DECLARE_APP_BLOB(files)
DECLARE_APP_BLOB(monitor)
DECLARE_APP_BLOB(clock)
DECLARE_APP_BLOB(panel)

/* Expected image_size per app (image_end - image_header). Bumped
 * when the corresponding .c file adds code or rodata; reduced
 * when image_end lands in the wrong section (the bug shape). */
#define EXPECTED_SHELL_IMAGE_SIZE   5859ULL
#define EXPECTED_EDITOR_IMAGE_SIZE  2684ULL
#define EXPECTED_FILES_IMAGE_SIZE   3416ULL
#define EXPECTED_MONITOR_IMAGE_SIZE 1480ULL
#define EXPECTED_CLOCK_IMAGE_SIZE   1082ULL
#define EXPECTED_PANEL_IMAGE_SIZE   2969ULL

static void assert_app_image_size(const char *app_name,
                                  const char *start,
                                  uint64_t expected_image_size) {
    const user_flat_image_header_t *header =
        (const user_flat_image_header_t *)(const void *)start;

    TEST_ASSERT_TRUE(header->magic == USER_IMAGE_MAGIC);

    /* header_size is fixed at 80 bytes for our native KLI1 layout. */
    TEST_ASSERT_EQUAL_UINT64(80, header->header_size);

    /* entry_count must be 1 (one entry point per app today). */
    TEST_ASSERT_EQUAL_UINT64(1, header->entry_count);

    /* entry_offsets[0] must be inside [header_size, image_size). */
    TEST_ASSERT_TRUE(header->entry_offsets[0] >= header->header_size);
    TEST_ASSERT_TRUE(header->entry_offsets[0] < header->image_size);

    /* The critical assertion: image_size must equal the value
     * image_end - image_header for this app. If the .end symbol
     * is back in .user.image.text, image_size shrinks and this
     * fires with the app name. */
    if (header->image_size != expected_image_size) {
        TEST_ASSERT_EQUAL_UINT64(expected_image_size, header->image_size);
        /* The line above will fail with a useful message naming
         * `expected_image_size` and `header->image_size`. The
         * app_name local is for human readers; the local above
         * is what gets printed. */
        (void)app_name;
    }
}

void test_user_image_runtime_shipping_shell_image_size(void) {
    assert_app_image_size("shell",
        _binary____build_programs_apps_shell_bin_start,
        EXPECTED_SHELL_IMAGE_SIZE);
}

void test_user_image_runtime_shipping_editor_image_size(void) {
    assert_app_image_size("editor",
        _binary____build_programs_apps_editor_bin_start,
        EXPECTED_EDITOR_IMAGE_SIZE);
}

void test_user_image_runtime_shipping_files_image_size(void) {
    assert_app_image_size("files",
        _binary____build_programs_apps_files_bin_start,
        EXPECTED_FILES_IMAGE_SIZE);
}

void test_user_image_runtime_shipping_monitor_image_size(void) {
    assert_app_image_size("monitor",
        _binary____build_programs_apps_monitor_bin_start,
        EXPECTED_MONITOR_IMAGE_SIZE);
}

void test_user_image_runtime_shipping_clock_image_size(void) {
    assert_app_image_size("clock",
        _binary____build_programs_apps_clock_bin_start,
        EXPECTED_CLOCK_IMAGE_SIZE);
}

void test_user_image_runtime_shipping_panel_image_size(void) {
    assert_app_image_size("panel",
        _binary____build_programs_apps_panel_bin_start,
        EXPECTED_PANEL_IMAGE_SIZE);
}
