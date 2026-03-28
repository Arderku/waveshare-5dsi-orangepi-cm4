/*
 * ws_dsi_panel.c - Waveshare 5-DSI-TOUCH-A panel driver for Rockchip RK3566
 *
 * Drives the Waveshare 5" DSI display (720x1280, HX8394 panel IC) via
 * the onboard MCU at I2C address 0x45 and a 2-lane MIPI DSI link.
 *
 * MCU register map (reverse-engineered from Waveshare ESP32-P4 driver):
 *   0x95  Init/unlock — write 0x11, then 0x17 to enable backlight control
 *   0x96  Backlight brightness — 0x00 = off, 0xFF = full
 *
 * HX8394 init sequence taken verbatim from Waveshare's official component:
 *   https://components.espressif.com/components/waveshare/esp_lcd_hx8394
 *
 * IMPORTANT: The drm_panel enable() callback must NOT issue any DSI
 * commands.  The Synopsys DW MIPI DSI core's dw_mipi_message_config()
 * clears PHY_TXREQUESTCLKHS in LPCLK_CTRL for every LPM transfer,
 * killing the HS clock if called after dw_mipi_dsi_enable().
 *
 * Touch: GT911 at I2C 0x14/0x5D — handled by the existing Goodix driver.
 *
 * License: GPL-2.0+
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/of_graph.h>
#include <linux/backlight.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_mipi_dsi.h>
#include <video/mipi_display.h>

struct ws_panel {
	struct drm_panel         panel;
	struct i2c_client       *client;
	struct mipi_dsi_device  *dsi;
};

static inline struct ws_panel *panel_to_ws(struct drm_panel *p)
{
	return container_of(p, struct ws_panel, panel);
}

/* ------------------------------------------------------------------ */
/*  MCU helpers (I2C to onboard STM8 at 0x45)                        */
/* ------------------------------------------------------------------ */

static int mcu_write(struct ws_panel *ws, u8 reg, u8 val)
{
	int ret = i2c_smbus_write_byte_data(ws->client, reg, val);
	if (ret < 0)
		dev_warn(&ws->client->dev,
			 "MCU write 0x%02x=0x%02x fail: %d\n", reg, val, ret);
	return ret;
}

/* ------------------------------------------------------------------ */
/*  HX8394 DSI initialisation (from Waveshare ESP32-P4 component)     */
/* ------------------------------------------------------------------ */

static int hx8394_dsi_write(struct mipi_dsi_device *dsi,
			    const void *data, size_t len)
{
	ssize_t ret = mipi_dsi_generic_write(dsi, data, len);
	return (ret < 0) ? ret : 0;
}

static int hx8394_init_sequence(struct ws_panel *ws)
{
	struct mipi_dsi_device *dsi = ws->dsi;
	int ret;

	static const u8 cmd_b9[] = { 0xB9, 0xFF, 0x83, 0x94 };
	static const u8 cmd_b1[] = {
		0xB1, 0x48, 0x0A, 0x6A, 0x09, 0x33,
		0x54, 0x71, 0x71, 0x2E, 0x45
	};
	static const u8 cmd_ba[] = {
		0xBA, 0x61, 0x03, 0x68, 0x6B, 0xB2, 0xC0
	};
	static const u8 cmd_b2[] = {
		0xB2, 0x00, 0x80, 0x64, 0x0C, 0x06, 0x2F
	};
	static const u8 cmd_b4[] = {
		0xB4, 0x1C, 0x78, 0x1C, 0x78, 0x1C, 0x78, 0x01,
		0x0C, 0x86, 0x75, 0x00, 0x3F, 0x1C, 0x78, 0x1C,
		0x78, 0x1C, 0x78, 0x01, 0x0C, 0x86
	};
	static const u8 cmd_d3[] = {
		0xD3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,
		0x08, 0x32, 0x10, 0x05, 0x00, 0x05, 0x32, 0x13,
		0xC1, 0x00, 0x01, 0x32, 0x10, 0x08, 0x00, 0x00,
		0x37, 0x03, 0x07, 0x07, 0x37, 0x05, 0x05, 0x37,
		0x0C, 0x40
	};
	static const u8 cmd_d5[] = {
		0xD5, 0x18, 0x18, 0x18, 0x18, 0x22, 0x23, 0x20,
		0x21, 0x04, 0x05, 0x06, 0x07, 0x00, 0x01, 0x02,
		0x03, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
		0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
		0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
		0x18, 0x19, 0x19, 0x19, 0x19
	};
	static const u8 cmd_d6[] = {
		0xD6, 0x18, 0x18, 0x19, 0x19, 0x21, 0x20, 0x23,
		0x22, 0x03, 0x02, 0x01, 0x00, 0x07, 0x06, 0x05,
		0x04, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
		0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
		0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
		0x18, 0x19, 0x19, 0x18, 0x18
	};
	static const u8 cmd_e0[] = {
		0xE0, 0x07, 0x08, 0x09, 0x0D, 0x10, 0x14, 0x16,
		0x13, 0x24, 0x36, 0x48, 0x4A, 0x58, 0x6F, 0x76,
		0x80, 0x97, 0xA5, 0xA8, 0xB5, 0xC6, 0x62, 0x63,
		0x68, 0x6F, 0x72, 0x78, 0x7F, 0x7F,
		0x00, 0x02, 0x08, 0x0D, 0x0C, 0x0E, 0x0F, 0x10,
		0x24, 0x36, 0x48, 0x4A, 0x58, 0x6F, 0x78, 0x82,
		0x99, 0xA4, 0xA0, 0xB1, 0xC0, 0x5E, 0x5E, 0x64,
		0x6B, 0x6C, 0x73, 0x7F, 0x7F
	};
	static const u8 cmd_cc[]  = { 0xCC, 0x0B };
	static const u8 cmd_c0[]  = { 0xC0, 0x1F, 0x73 };
	static const u8 cmd_b6[]  = { 0xB6, 0x6B, 0x6B };
	static const u8 cmd_d4[]  = { 0xD4, 0x02 };
	static const u8 cmd_bd1[] = { 0xBD, 0x01 };
	static const u8 cmd_b1x[] = { 0xB1, 0x00 };
	static const u8 cmd_bd0[] = { 0xBD, 0x00 };
	static const u8 cmd_bf[]  = {
		0xBF, 0x40, 0x81, 0x50, 0x00, 0x1A, 0xFC, 0x01
	};
	static const u8 cmd_3a[]  = { 0x3A, 0x77 };   /* RGB888 */
	static const u8 cmd_b2x[] = {
		0xB2, 0x00, 0x80, 0x64, 0x0C, 0x06, 0x2F,
		0x00, 0x00, 0x00, 0x00, 0xC0, 0x18
	};

	ret = hx8394_dsi_write(dsi, cmd_b9, sizeof(cmd_b9));
	if (ret) return ret;
	usleep_range(1000, 2000);

	hx8394_dsi_write(dsi, cmd_b1,  sizeof(cmd_b1));
	hx8394_dsi_write(dsi, cmd_ba,  sizeof(cmd_ba));
	hx8394_dsi_write(dsi, cmd_b2,  sizeof(cmd_b2));
	hx8394_dsi_write(dsi, cmd_b4,  sizeof(cmd_b4));
	hx8394_dsi_write(dsi, cmd_d3,  sizeof(cmd_d3));
	hx8394_dsi_write(dsi, cmd_d5,  sizeof(cmd_d5));
	hx8394_dsi_write(dsi, cmd_d6,  sizeof(cmd_d6));
	hx8394_dsi_write(dsi, cmd_e0,  sizeof(cmd_e0));
	hx8394_dsi_write(dsi, cmd_cc,  sizeof(cmd_cc));
	hx8394_dsi_write(dsi, cmd_c0,  sizeof(cmd_c0));
	hx8394_dsi_write(dsi, cmd_b6,  sizeof(cmd_b6));
	hx8394_dsi_write(dsi, cmd_d4,  sizeof(cmd_d4));
	hx8394_dsi_write(dsi, cmd_bd1, sizeof(cmd_bd1));
	hx8394_dsi_write(dsi, cmd_b1x, sizeof(cmd_b1x));
	hx8394_dsi_write(dsi, cmd_bd0, sizeof(cmd_bd0));
	hx8394_dsi_write(dsi, cmd_bf,  sizeof(cmd_bf));
	hx8394_dsi_write(dsi, cmd_3a,  sizeof(cmd_3a));

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) return ret;
	msleep(200);

	hx8394_dsi_write(dsi, cmd_b2x, sizeof(cmd_b2x));

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) return ret;
	msleep(80);

	return 0;
}

/* ------------------------------------------------------------------ */
/*  DRM panel callbacks                                               */
/* ------------------------------------------------------------------ */

static int ws_prepare(struct drm_panel *panel)
{
	struct ws_panel *ws = panel_to_ws(panel);
	return hx8394_init_sequence(ws);
}

static int ws_unprepare(struct drm_panel *panel)
{
	struct ws_panel *ws = panel_to_ws(panel);

	mipi_dsi_dcs_write(ws->dsi, MIPI_DCS_SET_DISPLAY_OFF, NULL, 0);
	msleep(20);
	mipi_dsi_dcs_write(ws->dsi, MIPI_DCS_ENTER_SLEEP_MODE, NULL, 0);
	msleep(120);
	return 0;
}

static int ws_enable(struct drm_panel *panel)
{
	struct ws_panel *ws = panel_to_ws(panel);

	/*
	 * Only I2C here — NO DSI commands!
	 * Any LPM DSI transfer after dw_mipi_dsi_enable() clears
	 * PHY_TXREQUESTCLKHS, killing the HS video clock permanently.
	 */
	mcu_write(ws, 0x96, 0xff);
	return 0;
}

static int ws_disable(struct drm_panel *panel)
{
	struct ws_panel *ws = panel_to_ws(panel);

	mcu_write(ws, 0x96, 0x00);
	return 0;
}

/*
 * 720x1280 @ 60 Hz   (htotal 800, vtotal 1318)
 * pixel clock = 800 * 1318 * 60 = 63264 kHz
 */
static const struct drm_display_mode ws_mode = {
	.clock       = 63264,
	.hdisplay    = 720,
	.hsync_start = 720 + 40,
	.hsync_end   = 720 + 40 + 20,
	.htotal      = 720 + 40 + 20 + 20,
	.vdisplay    = 1280,
	.vsync_start = 1280 + 24,
	.vsync_end   = 1280 + 24 + 4,
	.vtotal      = 1280 + 24 + 4 + 10,
	.width_mm    = 62,
	.height_mm   = 110,
	.type        = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static int ws_get_modes(struct drm_panel *panel,
			struct drm_connector *connector)
{
	static const u32 bus_fmt = 0x100a; /* MEDIA_BUS_FMT_RGB888_1X24 */
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &ws_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	connector->display_info.bpc       = 8;
	connector->display_info.width_mm  = ws_mode.width_mm;
	connector->display_info.height_mm = ws_mode.height_mm;
	drm_display_info_set_bus_formats(&connector->display_info, &bus_fmt, 1);
	return 1;
}

static const struct drm_panel_funcs ws_panel_funcs = {
	.prepare   = ws_prepare,
	.unprepare = ws_unprepare,
	.enable    = ws_enable,
	.disable   = ws_disable,
	.get_modes = ws_get_modes,
};

/* ------------------------------------------------------------------ */
/*  Backlight via Linux backlight subsystem                           */
/* ------------------------------------------------------------------ */

static int ws_bl_update(struct backlight_device *bl)
{
	struct ws_panel *ws = bl_get_data(bl);

	mcu_write(ws, 0x96, backlight_get_brightness(bl));
	return 0;
}

static const struct backlight_ops ws_bl_ops = {
	.update_status = ws_bl_update,
};

/* ------------------------------------------------------------------ */
/*  I2C probe / remove                                                */
/* ------------------------------------------------------------------ */

static int ws_i2c_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct ws_panel *ws;
	struct device *dev = &client->dev;
	struct device_node *endpoint, *dsi_host_node;
	struct mipi_dsi_host *host;
	struct mipi_dsi_device_info dsi_info = { .type = "ws-panel", .channel = 0 };
	struct backlight_device *bl;
	const struct backlight_properties bl_props = {
		.type = BACKLIGHT_RAW, .brightness = 255, .max_brightness = 255,
	};
	int ret;

	ws = devm_kzalloc(dev, sizeof(*ws), GFP_KERNEL);
	if (!ws)
		return -ENOMEM;

	ws->client = client;
	i2c_set_clientdata(client, ws);

	/* MCU unlock sequence (required before backlight register works) */
	mcu_write(ws, 0x95, 0x11);
	mcu_write(ws, 0x95, 0x17);
	mcu_write(ws, 0x96, 0x00);
	msleep(100);
	mcu_write(ws, 0x96, 0xff);

	/* Find DSI host via device-tree graph */
	endpoint = of_graph_get_next_endpoint(dev->of_node, NULL);
	if (!endpoint)
		return -ENODEV;

	dsi_host_node = of_graph_get_remote_port_parent(endpoint);
	if (!dsi_host_node) {
		of_node_put(endpoint);
		return -ENODEV;
	}

	host = of_find_mipi_dsi_host_by_node(dsi_host_node);
	of_node_put(dsi_host_node);
	if (!host) {
		of_node_put(endpoint);
		return -EPROBE_DEFER;
	}

	dsi_info.node = of_graph_get_remote_port(endpoint);
	of_node_put(endpoint);

	ws->dsi = mipi_dsi_device_register_full(host, &dsi_info);
	if (IS_ERR(ws->dsi))
		return PTR_ERR(ws->dsi);

	drm_panel_init(&ws->panel, dev, &ws_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	bl = devm_backlight_device_register(dev, dev_name(dev), dev, ws,
					    &ws_bl_ops, &bl_props);
	if (IS_ERR(bl)) {
		mipi_dsi_device_unregister(ws->dsi);
		return PTR_ERR(bl);
	}
	ws->panel.backlight = bl;

	drm_panel_add(&ws->panel);

	ws->dsi->lanes      = 2;
	ws->dsi->format     = MIPI_DSI_FMT_RGB888;
	ws->dsi->mode_flags = MIPI_DSI_MODE_VIDEO |
			      MIPI_DSI_MODE_VIDEO_BURST |
			      MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_attach(ws->dsi);
	if (ret) {
		drm_panel_remove(&ws->panel);
		mipi_dsi_device_unregister(ws->dsi);
		return ret;
	}

	dev_info(dev, "Waveshare 5-DSI-TOUCH-A ready (720x1280, 2-lane DSI)\n");
	return 0;
}

static int ws_i2c_remove(struct i2c_client *client)
{
	struct ws_panel *ws = i2c_get_clientdata(client);

	mipi_dsi_detach(ws->dsi);
	drm_panel_remove(&ws->panel);
	mipi_dsi_device_unregister(ws->dsi);
	return 0;
}

static const struct of_device_id ws_of_match[] = {
	{ .compatible = "waveshare,5inch-dsi-mcu" },
	{ }
};
MODULE_DEVICE_TABLE(of, ws_of_match);

static const struct i2c_device_id ws_i2c_ids[] = {
	{ "ws-5dsi-mcu", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ws_i2c_ids);

static struct i2c_driver ws_i2c_driver = {
	.driver = {
		.name           = "ws-5inch-dsi",
		.of_match_table = ws_of_match,
	},
	.probe    = ws_i2c_probe,
	.remove   = ws_i2c_remove,
	.id_table = ws_i2c_ids,
};
module_i2c_driver(ws_i2c_driver);

MODULE_AUTHOR("ArdelkaPocket");
MODULE_DESCRIPTION("Waveshare 5-DSI-TOUCH-A panel driver (HX8394 + MCU)");
MODULE_LICENSE("GPL");
