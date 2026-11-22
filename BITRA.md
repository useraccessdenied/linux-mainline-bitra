# realme GT Neo 2 (bitra) — what this fork is

Branch `bitra` of this tree is the Linux 7.2.0 kernel for the **realme GT Neo 2** (RMX3370, SM8250-AC), based on
[omnipha/linux-qualcomm-sm8250](https://github.com/omnipha/linux-qualcomm-sm8250) (branch `omnipha`, base `b56ee7a86`).
Tooling, docs and the hardware status tracker are in <https://github.com/useraccessdenied/bitra-mainline>
(start with `docs/27-hardware-status.md`); the same commits are exported there as `patches/series/`.

Everything is experimental and AI-assisted (see `AI_DISCLOSURE.md`, `ATTRIBUTION.md`).

## What the branch adds on top of the base
- `arch/arm64/boot/dts/qcom/sm8250-realme-bitra.dts`: board tree (display, touch, UFS, Wi-Fi/BT, fuel gauge, PON reboot modes), plus an ABL DTB shim
  (`CONFIG_BITRA_DTB_SHIM`): the OPLUS bootloader only accepts the stock base DTB, the kernel swaps in this tree.
- `drivers/gpu/drm/panel/panel-samsung-ams662zs01.c`: the Samsung AMS662ZS01 DSC command-mode panel.
- Synaptics TouchComm I2C touchscreen (`synaptics_tcm_i2c`, posted upstream by its author, unmerged) with hardening.
- Fixes found by fault injection / KASAN (upstream candidates, listed in bitra-mainline `docs/24-upstream-candidates.md`):
  typec altmode descriptor, qcom-wdt armed-at-probe, pinconf-generic NULL check, and a drm/msm teardown/error-path lifetime series
  (self-refresh work, kms workqueue/workers, dpu global object, irq_requested, rpm_enabled).

## Build
See bitra-mainline `README.md` (config: `configs/bitra_defconfig`; `make ... Image.gz modules dtbs`).
