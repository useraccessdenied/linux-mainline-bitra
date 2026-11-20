/* SPDX-License-Identifier: GPL-2.0 */
	.section .rodata, "a"
	.balign 8
	.globl bitra_dtb_start
bitra_dtb_start:
	.incbin "arch/arm64/boot/dts/qcom/sm8250-realme-bitra.dtb"
	.globl bitra_dtb_end
bitra_dtb_end:
