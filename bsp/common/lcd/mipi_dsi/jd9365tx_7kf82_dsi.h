#ifndef __JD9365TX_7KF82_DSI_H__
#define __JD9365TX_7KF82_DSI_H__

#include "../lcd_conf.h"
#include <stdint.h>

#if defined(LCD_DSI_JD9365TX_7KF82)

#include "mipi_dsi_v2.h"

/* JD9365TX 7KF82: 720x1280, 2-lane MIPI DSI, RGB565 framebuffer input. */
#define JD9365TX_7KF82_DSI_W           720
#define JD9365TX_7KF82_DSI_H           1280
#define JD9365TX_7KF82_DSI_COLOR_DEPTH 16

/* The panel exposes RGB565 scanout and OSD1 SEOF frame callbacks through the
 * public lcd_* API, which is required by cam_lcd_mipi. */
#define LCD_DSI_CAMERA_RGB565_FRAMEBUFFER_MODE 1

typedef uint16_t jd9365tx_7kf82_dsi_color_t;

int jd9365tx_7kf82_dsi_init(jd9365tx_7kf82_dsi_color_t *screen_buffer);
int jd9365tx_7kf82_dsi_screen_switch(jd9365tx_7kf82_dsi_color_t *screen_buffer);
jd9365tx_7kf82_dsi_color_t *jd9365tx_7kf82_dsi_get_screen_using(void);
int jd9365tx_7kf82_dsi_frame_callback_register(uint32_t callback_type, void (*callback)(void));
const mipi_dsi_v2_timing_t *jd9365tx_7kf82_dsi_get_timing(void);

#endif

#endif /* __JD9365TX_7KF82_DSI_H__ */
