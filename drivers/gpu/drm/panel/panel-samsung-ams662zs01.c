// SPDX-License-Identifier: GPL-2.0-only
// Samsung AMS662ZS01 1080x2400 DSC command-mode OLED panel (realme GT Neo 2, bitra).
// Init sequence and timings taken from the vendor device tree (qcom,mdss_dsi_oppo21619samsung_ams662zs01_1080_2400_cmd_dvt);
// structure derived from panel-samsung-amb655x.c (the sibling OnePlus 8T panel).

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/display/drm_dsc.h>
#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_crtc.h>

struct ams662zs01 {
	struct drm_panel panel;
	struct drm_connector *connector;
	struct mipi_dsi_device *dsi;
	struct drm_dsc_config dsc;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *vout_gpio;
	bool hz120;
};

static const struct regulator_bulk_data ams662zs01_supplies[] = {
	{ .supply = "vdd" },	/* VCI, 3.0 V */
	{ .supply = "vddio" },	/* 1.8 V */
	{ .supply = "vddr" },	/* 1.2 V logic rail named vout in the vendor tree */
};

static inline struct ams662zs01 *to_ams662zs01(struct drm_panel *panel)
{
	return container_of_const(panel, struct ams662zs01, panel);
}

static void ams662zs01_reset(struct ams662zs01 *ctx)
{
	/* vendor reset sequence: 1 (10 ms), 0 (10 ms), 1 (10 ms); the DT gpio is active-low (asserted = in reset) */
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int ams662zs01_on(struct ams662zs01 *ctx, bool hz120)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 128);

	/* DSC parameter set via DCS 0x9e (as the vendor does), then enable compression */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9e,
				     0x11, 0x00, 0x00, 0x89, 0x30, 0x80, 0x09,
				     0x60, 0x04, 0x38, 0x00, 0x1e, 0x02, 0x1c,
				     0x02, 0x1c, 0x02, 0x00, 0x02, 0x0e, 0x00,
				     0x20, 0x02, 0xe3, 0x00, 0x07, 0x00, 0x0c,
				     0x03, 0x50, 0x03, 0x64, 0x18, 0x00, 0x10,
				     0xf0, 0x03, 0x0c, 0x20, 0x00, 0x06, 0x0b,
				     0x0b, 0x33, 0x0e, 0x1c, 0x2a, 0x38, 0x46,
				     0x54, 0x62, 0x69, 0x70, 0x77, 0x79, 0x7b,
				     0x7d, 0x7e, 0x01, 0x02, 0x01, 0x00, 0x09,
				     0x40, 0x09, 0xbe, 0x19, 0xfc, 0x19, 0xfa,
				     0x19, 0xf8, 0x1a, 0x38, 0x1a, 0x78, 0x1a,
				     0xb6, 0x2a, 0xf6, 0x2b, 0x34, 0x2b, 0x74,
				     0x3b, 0x74, 0x6b, 0xf4, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc2, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9d, 0x01);

	/* TE on (vblank) */
	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	mipi_dsi_dcs_set_column_address_multi(&dsi_ctx, 0, 1080 - 1);
	mipi_dsi_dcs_set_page_address_multi(&dsi_ctx, 0, 2400 - 1);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x27, 0xf2);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf2, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);

	/* refresh rate: 0x60 0x08 = 120 Hz, 0x60 0x00 = 60 Hz */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	if (hz120)
		mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x60, 0x08, 0x00);
	else
		mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x60, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf7, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_POWER_SAVE, 0x00);

	return dsi_ctx.accum_err;
}

static int ams662zs01_enable(struct drm_panel *panel)
{
	struct ams662zs01 *ctx = to_ams662zs01(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	return dsi_ctx.accum_err;
}

static int ams662zs01_off(struct ams662zs01 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x91, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 11);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 128);

	return dsi_ctx.accum_err;
}

static int ams662zs01_prepare(struct drm_panel *panel)
{
	struct ams662zs01 *ctx = to_ams662zs01(panel);
	struct device *dev = &ctx->dsi->dev;
	struct drm_dsc_picture_parameter_set pps;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ams662zs01_supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	ams662zs01_reset(ctx);

	if (ctx->connector && ctx->connector->state && ctx->connector->state->crtc &&
	    ctx->connector->state->crtc->state)
		ctx->hz120 = drm_mode_vrefresh(&ctx->connector->state->crtc->state->adjusted_mode) > 90;
	else
		ctx->hz120 = true;

	ret = ams662zs01_on(ctx, ctx->hz120);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(ams662zs01_supplies), ctx->supplies);
		return ret;
	}

	drm_dsc_pps_payload_pack(&pps, &ctx->dsc);

	ret = mipi_dsi_picture_parameter_set(ctx->dsi, &pps);
	if (ret < 0) {
		dev_err(panel->dev, "failed to transmit PPS: %d\n", ret);
		return ret;
	}

	return 0;
}

static int ams662zs01_unprepare(struct drm_panel *panel)
{
	struct ams662zs01 *ctx = to_ams662zs01(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = ams662zs01_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ams662zs01_supplies), ctx->supplies);

	return 0;
}

/*
 * In command mode the DSI clock is derived from the mode's pixel clock, not from a real blanking period.
 * The vendor tree runs the link at 800-830 MHz; that leaves ~1.3 ms of slack per 120 Hz frame, so the 120 Hz
 * mode carries 1216 extra "porch" lines (like the OnePlus 8T panel) to raise the link to ~1.2 Gbit/s per lane.
 */
#define AMS662ZS01_MODE(_hz, _hfp, _vfp)						\
{										\
	.clock = (1080 + (_hfp) + 10 + 40) * (2400 + (_vfp) + 2 + 9) * (_hz) / 1000,	\
	.hdisplay = 1080,							\
	.hsync_start = 1080 + (_hfp),						\
	.hsync_end = 1080 + (_hfp) + 10,					\
	.htotal = 1080 + (_hfp) + 10 + 40,					\
	.vdisplay = 2400,							\
	.vsync_start = 2400 + (_vfp),						\
	.vsync_end = 2400 + (_vfp) + 2,						\
	.vtotal = 2400 + (_vfp) + 2 + 9,					\
	.width_mm = 70,								\
	.height_mm = 153,							\
	.type = DRM_MODE_TYPE_DRIVER,						\
}

static const struct drm_display_mode ams662zs01_120_mode = AMS662ZS01_MODE(120, 50, 21 + 1216);
static const struct drm_display_mode ams662zs01_60_mode = AMS662ZS01_MODE(60, 540, 21);

static inline struct ams662zs01 *ctx_from_panel(struct drm_panel *panel)
{
	return container_of_const(panel, struct ams662zs01, panel);
}

static int ams662zs01_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	int count = 0;

	ctx_from_panel(panel)->connector = connector;

	/* 120 Hz first (preferred); the mode switch itself is a one-register write, see ams662zs01_on() */
	count += drm_connector_helper_get_modes_fixed(connector, &ams662zs01_120_mode);
	count += drm_connector_helper_get_modes_fixed(connector, &ams662zs01_60_mode);

	return count;
}

static const struct drm_panel_funcs ams662zs01_panel_funcs = {
	.prepare = ams662zs01_prepare,
	.enable = ams662zs01_enable,
	.unprepare = ams662zs01_unprepare,
	.get_modes = ams662zs01_get_modes,
};

static int ams662zs01_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

static int ams662zs01_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_get_display_brightness_large(dsi, &brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return brightness;
}

static const struct backlight_ops ams662zs01_bl_ops = {
	.update_status = ams662zs01_bl_update_status,
	.get_brightness = ams662zs01_bl_get_brightness,
};

static struct backlight_device *
ams662zs01_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 1174,
		.max_brightness = 2047,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &ams662zs01_bl_ops, &props);
}

static int ams662zs01_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct ams662zs01 *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct ams662zs01, panel,
				   &ams662zs01_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(ams662zs01_supplies),
					    ams662zs01_supplies,
					    &ctx->supplies);
	if (ret < 0)
		return ret;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	/* panel AVDD enable (vendor: qcom,platform-panel-vout-gpio); optional, pinctrl may already drive it */
	ctx->vout_gpio = devm_gpiod_get_optional(dev, "vout", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->vout_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->vout_gpio), "Failed to get vout-gpios\n");

	ctx->hz120 = true;
	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_NO_EOT_PACKET |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM;

	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = ams662zs01_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	/* This panel only supports DSC; unconditionally enable it */
	dsi->dsc = &ctx->dsc;

	ctx->dsc.dsc_version_major = 1;
	ctx->dsc.dsc_version_minor = 1;

	/* TODO: Pass slice_per_pkt = 2 */
	ctx->dsc.slice_height = 30;
	ctx->dsc.slice_width = 540;
	/*
	 * TODO: hdisplay should be read from the selected mode once
	 * it is passed back to drm_panel (in prepare?)
	 */
	WARN_ON(1080 % ctx->dsc.slice_width);
	ctx->dsc.slice_count = 1080 / ctx->dsc.slice_width;
	ctx->dsc.bits_per_component = 8;
	ctx->dsc.bits_per_pixel = 8 << 4; /* 4 fractional bits */
	ctx->dsc.block_pred_enable = true;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return dev_err_probe(dev, ret, "Failed to attach to DSI host\n");
	}

	return 0;
}

static void ams662zs01_remove(struct mipi_dsi_device *dsi)
{
	struct ams662zs01 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id ams662zs01_of_match[] = {
	{ .compatible = "samsung,ams662zs01" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ams662zs01_of_match);

static struct mipi_dsi_driver ams662zs01_driver = {
	.probe = ams662zs01_probe,
	.remove = ams662zs01_remove,
	.driver = {
		.name = "panel-ams662zs01",
		.of_match_table = ams662zs01_of_match,
	},
};
module_mipi_dsi_driver(ams662zs01_driver);

MODULE_AUTHOR("bitra mainline port (AI-assisted); based on work by Robert Oscilowski <drgitx@gmail.com>");
MODULE_DESCRIPTION("DRM driver for Samsung AMS662ZS01 FHD cmd mode DSC DSI panel (realme GT Neo 2)");
MODULE_LICENSE("GPL");
