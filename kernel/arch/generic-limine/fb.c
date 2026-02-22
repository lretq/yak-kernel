#include <assert.h>
#include <stddef.h>
#include <limine.h>
#include <string.h>
#include <nanoprintf.h>
#include <flanterm.h>
#include <flanterm_backends/fb.h>
#include <yak/heap.h>
#include <yak/init.h>
#include <yak/spinlock.h>
#include <yak/console.h>
#include <yak/file.h>
#include <yak/fs/devfs.h>
#include <yak/fs/vfs.h>
#include <yak/process.h>
#include <yak/status.h>
#include "request.h"

LIMINE_REQ
volatile struct limine_framebuffer_request fb_request = {
	.id = LIMINE_FRAMEBUFFER_REQUEST_ID,
	.revision = 0,
};

static size_t fb_write(struct console *console, const char *buf, size_t size);

static size_t fb_console_count = 0;
static struct console *fb_consoles = NULL;
struct flanterm_context *kinfo_flanterm_context = NULL;

// scale by 2, builtin font height = 16?, 3 kinfo lines
// 10 margin
#define KINFO_MARGIN 5
#define KINFO_HEIGHT(fontscale) (16 * 3 * fontscale + KINFO_MARGIN * 2)

struct limine_framebuffer limine_fb0_copy;
size_t kinfo_height_start;

static struct console *user_console = NULL;

void console_backend_winsize(struct winsize *ws)
{
	memcpy(ws, &user_console->native_winsize, sizeof(struct winsize));
}

void console_backend_write(const void *buf, size_t length)
{
	fb_write(user_console, buf, length);
}

void console_backend_setup()
{
	assert(fb_console_count > 0);
	struct console *con = &fb_consoles[0];
	sink_remove(con);
	user_console = con;
}

void limine_fb_setup()
{
	assert(!fb_consoles);
	assert(!kinfo_flanterm_context);

	struct limine_framebuffer_response *res = fb_request.response;

	// No framebuffers to setup
	if (0 == res->framebuffer_count)
		return;

	fb_consoles = kcalloc(res->framebuffer_count, sizeof(struct console));

	// for fireworks test
	// after reclaiming memory, this would be gone!
	memcpy(&limine_fb0_copy, res->framebuffers[0],
	       sizeof(struct limine_framebuffer));

	for (size_t i = 0; i < res->framebuffer_count; i++) {
		struct limine_framebuffer *fb = res->framebuffers[i];

		struct console *console = &fb_consoles[i];

		npf_snprintf(console->name, sizeof(console->name) - 1,
			     "limine-fb%ld", i);

		console->write = fb_write;

		size_t font_scale_x = 1, font_scale_y = 1;

		if (fb->width >= (1920 + 1920 / 3) &&
		    fb->height >= (1080 + 1080 / 3)) {
			font_scale_x = 2;
			font_scale_y = 2;
		}
		if (fb->width >= (3840 + 3840 / 3) &&
		    fb->height >= (2160 + 2160 / 3)) {
			font_scale_x = 4;
			font_scale_y = 4;
		}

		size_t console_height = fb->height;

#if CONFIG_KINFO
		if (!kinfo_flanterm_context) {
			size_t kinfo_height = KINFO_HEIGHT(font_scale_y);

			kinfo_height_start = fb->height - kinfo_height;

			console_height = kinfo_height_start;

			console->private = flanterm_fb_init(
				kmalloc, kfree, fb->address, fb->width,
				kinfo_height_start, fb->pitch,
				fb->red_mask_size, fb->red_mask_shift,
				fb->green_mask_size, fb->green_mask_shift,
				fb->blue_mask_size, fb->blue_mask_shift, NULL,
				NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0, 0,
				1, font_scale_x, font_scale_y, 0,
				FLANTERM_FB_ROTATE_0);

			void *footer_address =
				(void *)((uintptr_t)fb->address +
					 (fb->pitch *
					  (fb->height - kinfo_height)));

			uint32_t kinfo_fg = 0xcdd6f4;
			uint32_t kinfo_bg = 0x45475a;
			kinfo_flanterm_context = flanterm_fb_init(
				kmalloc, kfree, footer_address, fb->width,
				kinfo_height, fb->pitch, fb->red_mask_size,
				fb->red_mask_shift, fb->green_mask_size,
				fb->green_mask_shift, fb->blue_mask_size,
				fb->blue_mask_shift, NULL, NULL, NULL,
				&kinfo_bg, &kinfo_fg, NULL, NULL, NULL, 0, 0, 1,
				font_scale_x, font_scale_y, KINFO_MARGIN,
				FLANTERM_FB_ROTATE_0);
		} else {
#endif
			console->private = flanterm_fb_init(
				kmalloc, kfree, fb->address, fb->width,
				fb->height, fb->pitch, fb->red_mask_size,
				fb->red_mask_shift, fb->green_mask_size,
				fb->green_mask_shift, fb->blue_mask_size,
				fb->blue_mask_shift, NULL, NULL, NULL, NULL,
				NULL, NULL, NULL, NULL, 0, 0, 1, 0, 0, 0,
				FLANTERM_FB_ROTATE_0);
#if CONFIG_KINFO
		}
#endif

		struct winsize ws;
		ws.ws_xpixel = fb->width;
		ws.ws_ypixel = console_height;

		size_t ncols;
		size_t nrows;

		flanterm_get_dimensions(console->private, &ncols, &nrows);

		ws.ws_row = nrows;
		ws.ws_col = ncols;

		console->native_winsize = ws;

		console_register(console);
		sink_add(console);
		fb_console_count++;
	}
}

INIT_ENTAILS(fb_setup, bsp_ready);
INIT_DEPS(fb_setup, heap_node);
INIT_NODE(fb_setup, limine_fb_setup);

static size_t fb_write(struct console *console, const char *buf, size_t size)
{
	flanterm_write_crnl(console->private, buf, size);
	return size;
}
