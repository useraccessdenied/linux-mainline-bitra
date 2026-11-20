// SPDX-License-Identifier: GPL-2.0
/*
 * realme GT Neo 2 (bitra) bring-up shim.
 *
 * The OPLUS ABL only accepts an appended DTB that it can merge the vendor dtbo
 * overlay into (matching msm-id/board-id/oplus,dtsi_no, plus the downstream
 * __symbols__ the overlay references). A mainline tree cannot satisfy that, so
 * the boot image carries the stock merged tree for ABL and the kernel swaps in
 * the embedded mainline tree here, keeping only what ABL computed at runtime
 * (/chosen: bootargs, initrd range, rng/kaslr seeds).
 *
 * Bring-up only. It goes away once a U-Boot stage hands Linux a proper DTB.
 */
#include <linux/init.h>
#include <linux/libfdt.h>
#include <linux/string.h>
#include <linux/sizes.h>
#include <linux/printk.h>
#include <linux/sysfb.h>
#include <linux/screen_info.h>
#include <linux/io.h>
#include <linux/sizes.h>

#define BITRA_FB_BASE	0x9c000000UL
#define BITRA_FB_W	1080
#define BITRA_FB_H	2400

/* Paint the top rows of ABL's panel buffer so we can see the kernel was entered. */
static void __init bitra_fb_beacon(u32 argb)
{
	u32 *p;
	int row, rows = 40;
	size_t chunk = BITRA_FB_W * 4 * rows;

	p = early_memremap(BITRA_FB_BASE, chunk);
	if (!p)
		return;
	for (row = 0; row < BITRA_FB_W * rows; row++)
		p[row] = argb;
	early_memunmap(p, chunk);
}

extern const u8 bitra_dtb_start[], bitra_dtb_end[];

static u8 bitra_dtb_buf[SZ_512K] __aligned(8);

void *__init bitra_dtb_shim(void *abl_fdt)
{
	static const char *const props[] = {
		"bootargs", "linux,initrd-start", "linux,initrd-end",
		"rng-seed", "kaslr-seed",
	};
	int src, dst, i, len;
	const void *val;

	bitra_fb_beacon(0xff00ff00);	/* green: kernel entered */

	/* describe ABL's panel buffer to the EFI-framebuffer earlycon (earlycon=efifb) */
	sysfb_primary_display.screen = (struct screen_info) {
		.orig_video_isVGA = VIDEO_TYPE_EFI,
		.lfb_base = BITRA_FB_BASE,
		.lfb_width = BITRA_FB_W,
		.lfb_height = BITRA_FB_H,
		.lfb_depth = 32,
		.lfb_linelength = BITRA_FB_W * 4,
		.lfb_size = BITRA_FB_W * 4 * BITRA_FB_H,
		.red_size = 8, .red_pos = 16, .green_size = 8, .green_pos = 8,
		.blue_size = 8, .blue_pos = 0,
	};

	if (fdt_check_header(bitra_dtb_start) ||
	    fdt_totalsize(bitra_dtb_start) > sizeof(bitra_dtb_buf))
		return abl_fdt;

	memcpy(bitra_dtb_buf, bitra_dtb_start, fdt_totalsize(bitra_dtb_start));
	if (fdt_open_into(bitra_dtb_buf, bitra_dtb_buf, sizeof(bitra_dtb_buf)))
		return abl_fdt;

	src = fdt_path_offset(abl_fdt, "/chosen");
	dst = fdt_path_offset(bitra_dtb_buf, "/chosen");
	if (src < 0 || dst < 0)
		return abl_fdt;

	for (i = 0; i < ARRAY_SIZE(props); i++) {
		val = fdt_getprop(abl_fdt, src, props[i], &len);
		if (val)
			fdt_setprop(bitra_dtb_buf, dst, props[i], val, len);
	}
	return bitra_dtb_buf;
}
