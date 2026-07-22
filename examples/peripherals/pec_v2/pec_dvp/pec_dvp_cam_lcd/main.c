#include "board.h"
#include "bflb_dma.h"
#include "bflb_dma2d.h"
#include "bflb_dpi.h"
#include "bflb_i2c.h"
#include "bflb_l1c.h"
#include "bflb_mtimer.h"
#include "bflb_osd.h"
#include "bflb_pec_v2_instance.h"
#include "image_sensor.h"
#include "lcd.h"
#include "sensor/common.h"

#if defined(LCD_DSI_AXS15231B_HS035)
#define PEC_CAM_LCD_GET_TIMING() axs15231b_hs035_dsi_get_timing()
#elif defined(LCD_DSI_ILI9806E_KD050FWFIA019)
#define PEC_CAM_LCD_GET_TIMING() ili9806e_kd050fwfia019_dsi_get_timing()
#elif defined(LCD_DSI_ILI9881C_KD050020)
#define PEC_CAM_LCD_GET_TIMING() ili9881c_kd050020_dsi_get_timing()
#elif defined(LCD_DSI_ILI9881C_KD050023W4)
#define PEC_CAM_LCD_GET_TIMING() ili9881c_kd050023w4_dsi_get_timing()
#elif defined(LCD_DSI_ST7102_YH494)
#define PEC_CAM_LCD_GET_TIMING() st7102_yh494_dsi_get_timing()
#else
#error "pec_dvp_cam_lcd does not support the selected DSI v2 panel."
#endif

#define CAM_FRAME_MAX_PIXELS    (1280 * 720)
#define CAM_FRAME_COUNT         (2)
#define CAM_FRAME_BUFFER_BYTES  (CAM_FRAME_MAX_PIXELS * 2)
#define DISP_FRAME_COUNT        (2)
#define DISP_FRAME_WIDTH        (LCD_W)
#define DISP_FRAME_HEIGHT       (LCD_H)
#define DISP_FRAME_BYTES        (DISP_FRAME_WIDTH * DISP_FRAME_HEIGHT * 2)
#define CAM_DMA_LLI_COUNT       (((CAM_FRAME_BUFFER_BYTES * CAM_FRAME_COUNT) / 4064) + 2)

static struct bflb_device_s *i2c0;
static struct bflb_device_s *pec_cam;
static struct bflb_device_s *dma0_ch0;
static struct bflb_device_s *dma2d;
static struct bflb_device_s *dpi;
static struct bflb_pec_dvp_cam_s *pec_cam_cfg;
static uint32_t cam_frame_bytes;
static uint16_t crop_start_x;
static uint16_t crop_start_y;
static uint16_t blit_width;
static uint16_t blit_height;
static uint16_t disp_start_x;
static uint16_t disp_start_y;
static volatile uint8_t dma2d_done;
static volatile uint8_t disp_write_index;
static volatile uint8_t dma2d_frame_index;
static volatile uint32_t capture_frame_count;

static ATTR_NOINIT_PSRAM_SECTION __attribute__((aligned(BFLB_CACHE_LINE_SIZE))) uint8_t cam_frame[CAM_FRAME_COUNT][CAM_FRAME_BUFFER_BYTES];
static ATTR_NOINIT_PSRAM_SECTION __attribute__((aligned(BFLB_CACHE_LINE_SIZE))) uint8_t disp_frame[DISP_FRAME_COUNT][DISP_FRAME_BYTES];
static struct bflb_dma_channel_lli_transfer_s transfers[CAM_FRAME_COUNT];
static struct bflb_dma_channel_lli_pool_s lli[CAM_DMA_LLI_COUNT];

static void yuyv_fill_black(uint8_t *buf, uint32_t pixels)
{
    for (uint32_t i = 0; i < pixels; i += 2) {
        buf[0] = 0x00;
        buf[1] = 0x80;
        buf[2] = 0x00;
        buf[3] = 0x80;
        buf += 4;
    }
}

static void pec_cam_capture_start(void)
{
    bflb_dma_channel_start(dma0_ch0);
    bflb_pec_dvp_cam_start(pec_cam);
}

static void dma0_ch0_isr(void *arg)
{
    (void)arg;

    capture_frame_count++;
}

static void dma2d_ch0_isr(void *arg)
{
    (void)arg;

    dma2d_done = 1;
}

static void dma2d_blit_to_lcd_frame(uint8_t *src, uint8_t *dst)
{
    struct bflb_dma2d_channel_config_s cfg = {
        .next_lli_addr = 0,
        .control = {
            .bits = {
                .transfer_size = 0,
                .src_burst = DMA2D_BURST_INCR4,
                .dst_burst = DMA2D_BURST_INCR4,
                .src_incr = 1,
                .dst_incr = 1,
                .int_enable = 1,
            },
        },
    };
    struct bflb_dma2d_image_s img = {
        .transfer_data_width = DMA2D_DATA_WIDTH_32BIT,
        .pixel_data_width = 2,
        .src_image_addr = (uint32_t)(uintptr_t)src,
        .src_image_width = pec_cam_cfg->resolution_x,
        .src_x_start = crop_start_x,
        .src_y_start = crop_start_y,
        .src_x_end = (crop_start_x + blit_width),
        .src_y_end = crop_start_y + blit_height,
        .dst_image_addr = (uint32_t)(uintptr_t)dst,
        .dst_image_width = DISP_FRAME_WIDTH,
        .dst_x_start = disp_start_x,
        .dst_y_start = disp_start_y,
    };
    bflb_dma2d_image_geometric_transfor_calculate(dma2d, &cfg, &img, DMA2D_IMAGE_TRANSLATE);
    bflb_dma2d_channel_init(dma2d, &cfg);
    bflb_dma2d_channel_tcint_clear(dma2d);
    bflb_dma2d_channel_start(dma2d);
}

static void dpi_yuyv_init(uint8_t *framebuffer)
{
    const mipi_dsi_v2_timing_t *timing = PEC_CAM_LCD_GET_TIMING();
    struct bflb_dpi_config_s dpi_config = {
        .width = DISP_FRAME_WIDTH,
        .height = DISP_FRAME_HEIGHT,
        .hsw = timing->hsw,
        .hbp = timing->hbp,
        .hfp = timing->hfp,
        .vsw = timing->vsw,
        .vbp = timing->vbp,
        .vfp = timing->vfp,
        .interface = DPI_INTERFACE_24_PIN,
        .input_sel = DPI_INPUT_SEL_FRAMEBUFFER_WITH_OSD,
        .test_pattern = DPI_TEST_PATTERN_NULL,
        .data_format = DPI_DATA_FORMAT_YUYV,
        .framebuffer_addr = (uint32_t)(uintptr_t)framebuffer,
    };

    dpi = bflb_device_get_by_name(BFLB_NAME_DPI);
    if (dpi == NULL) {
        printf("get dpi failed\r\n");
        while (1) {
        }
    }

    bflb_dpi_init(dpi, &dpi_config);
    bflb_dpi_feature_control(dpi, DPI_CMD_SET_BURST, DPI_BURST_INCR8);
    bflb_dpi_enable(dpi);
}

static void dma_config_init(void)
{
    struct bflb_dma_channel_config_s dma_config;
    int used_count;

    dma_config.direction = DMA_PERIPH_TO_MEMORY;
    dma_config.src_req = DMA_REQUEST_PEC_SM0_RX;
    dma_config.dst_req = DMA_REQUEST_NONE;
    dma_config.src_addr_inc = DMA_ADDR_INCREMENT_DISABLE;
    dma_config.dst_addr_inc = DMA_ADDR_INCREMENT_ENABLE;
    if (pec_cam_cfg->fifo_threshold == (1 - 1)) {
        dma_config.src_burst_count = DMA_BURST_INCR1;
        dma_config.dst_burst_count = DMA_BURST_INCR1;
    } else if (pec_cam_cfg->fifo_threshold == (4 - 1)) {
        dma_config.src_burst_count = DMA_BURST_INCR4;
        dma_config.dst_burst_count = DMA_BURST_INCR4;
    } else if (pec_cam_cfg->fifo_threshold == (8 - 1)) {
        dma_config.src_burst_count = DMA_BURST_INCR8;
        dma_config.dst_burst_count = DMA_BURST_INCR8;
    } else if (pec_cam_cfg->fifo_threshold == (16 - 1)) {
        dma_config.src_burst_count = DMA_BURST_INCR16;
        dma_config.dst_burst_count = DMA_BURST_INCR16;
    } else {
        printf("not support fifo threshold: %d\r\n", pec_cam_cfg->fifo_threshold);
        while (1) {
        }
    }

    if (pec_cam_cfg->bits_every_push == 8) {
        dma_config.src_width = DMA_DATA_WIDTH_8BIT;
        dma_config.dst_width = DMA_DATA_WIDTH_8BIT;
    } else if (pec_cam_cfg->bits_every_push == 16) {
        dma_config.src_width = DMA_DATA_WIDTH_16BIT;
        dma_config.dst_width = DMA_DATA_WIDTH_16BIT;
    } else if (pec_cam_cfg->bits_every_push == 32) {
        dma_config.src_width = DMA_DATA_WIDTH_32BIT;
        dma_config.dst_width = DMA_DATA_WIDTH_32BIT;
    } else {
        printf("not support bits every push: %d\r\n", pec_cam_cfg->bits_every_push);
        while (1) {
        }
    }

    bflb_dma_channel_init(dma0_ch0, &dma_config);
    bflb_dma_channel_irq_attach(dma0_ch0, dma0_ch0_isr, NULL);

    for (uint8_t i = 0; i < CAM_FRAME_COUNT; i++) {
        transfers[i].src_addr = (uint32_t)DMA_ADDR_PEC_SM0_RDR;
        transfers[i].dst_addr = (uint32_t)(uintptr_t)cam_frame[i];
        transfers[i].nbytes = cam_frame_bytes;
    }

    /* Each transfer is one frame; link the two transfers as a capture ring. */
    used_count = bflb_dma_channel_lli_reload(dma0_ch0, lli, CAM_DMA_LLI_COUNT, transfers, CAM_FRAME_COUNT);
    if (used_count < 0) {
        printf("dma lli reload failed, ret = %d\r\n", used_count);
        while (1) {
        }
    }
    bflb_dma_channel_lli_link_head(dma0_ch0, lli, used_count);
}

int main(void)
{
    struct image_sensor_config_s *sensor_config;
    struct bflb_device_s *osd;
    uint32_t sensor_index;
    uint32_t processed_frame_count;
    uint8_t dma2d_busy = 0;

    board_init();
    board_pec_dvp_cam_gpio_init();

    if (lcd_init(NULL) != 0) {
        printf("lcd init failed\r\n");
        while (1) {
        }
    }

    osd = bflb_device_get_by_name(BFLB_NAME_OSD0);
    bflb_osd_blend_disable(osd);
    bflb_osd_int_mask(osd, true);
    bflb_osd_int_clear(osd);

    yuyv_fill_black(disp_frame[0], DISP_FRAME_WIDTH * DISP_FRAME_HEIGHT);
    yuyv_fill_black(disp_frame[1], DISP_FRAME_WIDTH * DISP_FRAME_HEIGHT);
    bflb_l1c_dcache_clean_invalidate_range(disp_frame, sizeof(disp_frame));
    dpi_yuyv_init(disp_frame[0]);

#if defined(LCD_BACKLIGHT_EN) && LCD_BACKLIGHT_EN
    lcd_backlight_toggle(true);
#endif

    i2c0 = bflb_device_get_by_name(BFLB_NAME_I2C0);
    pec_cam = bflb_device_get_by_name(BFLB_NAME_PEC_SM0);
    dma0_ch0 = bflb_device_get_by_name(BFLB_NAME_DMA0_CH0);
    dma2d = bflb_device_get_by_name(BFLB_NAME_DMA2D_CH0);
    if (dma2d == NULL) {
        printf("get dma2d failed\r\n");
        while (1) {
            bflb_mtimer_delay_ms(1000);
        }
    }
    bflb_dma2d_channel_irq_attach(dma2d, dma2d_ch0_isr, NULL);
    sensor_index = image_sensor_scan(i2c0, &sensor_config);
    if (sensor_index == 0) {
        printf("can't identify sensor\r\n");
        while (1) {
            bflb_mtimer_delay_ms(1000);
        }
    }
    printf("sensor: %s, %u x %u\r\n",
           sensor_config->name, sensor_config->resolution_x, sensor_config->resolution_y);

    pec_cam_cfg = bflb_pec_dvp_cam_get_cfg(sensor_config->name);
    if (pec_cam_cfg == NULL) {
        printf("no matched pec dvp cam config for this sensor\r\n");
        while (1) {
            bflb_mtimer_delay_ms(1000);
        }
    }
    pec_cam_cfg->resolution_x = sensor_config->resolution_x;
    pec_cam_cfg->resolution_y = sensor_config->resolution_y;
    cam_frame_bytes = pec_cam_cfg->resolution_x * pec_cam_cfg->resolution_y * pec_cam_cfg->pixel_bits / 8;
    if (pec_cam_cfg->pixel_bits != 16 || cam_frame_bytes > sizeof(cam_frame[0])) {
        printf("unsupported camera frame: %u x %u, bits = %u, bytes = %lu\r\n",
               pec_cam_cfg->resolution_x, pec_cam_cfg->resolution_y, pec_cam_cfg->pixel_bits,
               (unsigned long)cam_frame_bytes);
        while (1) {
            bflb_mtimer_delay_ms(1000);
        }
    }
    blit_width = pec_cam_cfg->resolution_x < DISP_FRAME_WIDTH ? pec_cam_cfg->resolution_x : DISP_FRAME_WIDTH;
    blit_width &= ~1U;
    blit_height = pec_cam_cfg->resolution_y < DISP_FRAME_HEIGHT ? pec_cam_cfg->resolution_y : DISP_FRAME_HEIGHT;
    crop_start_x = (pec_cam_cfg->resolution_x - blit_width) / 2;
    crop_start_x &= ~1U;
    crop_start_y = (pec_cam_cfg->resolution_y - blit_height) / 2;
    disp_start_x = (DISP_FRAME_WIDTH - blit_width) / 2;
    disp_start_x &= ~1U;
    disp_start_y = (DISP_FRAME_HEIGHT - blit_height) / 2;
    dma_config_init();
    if (bflb_pec_dvp_cam_init(pec_cam, pec_cam_cfg) != 0) {
        printf("pec dvp cam init failed\r\n");
        while (1) {
            bflb_mtimer_delay_ms(1000);
        }
    }

    printf("pec dvp cam lcd start\r\n");
    disp_write_index = 1;
    bflb_mtimer_delay_us(pec_cam_cfg->delay_first_us);
    processed_frame_count = capture_frame_count;
    pec_cam_capture_start();

    while (1) {
        uint32_t current_count = capture_frame_count;

        if (!dma2d_busy && current_count != processed_frame_count) {
            uint8_t cam_index = (current_count - 1) % CAM_FRAME_COUNT;
            uint8_t disp_index = disp_write_index;

            processed_frame_count = current_count;
            dma2d_busy = 1;
            dma2d_done = 0;
            dma2d_frame_index = disp_index;
            dma2d_blit_to_lcd_frame(cam_frame[cam_index], disp_frame[disp_index]);
        }
        if (dma2d_done) {
            uint8_t index = dma2d_frame_index;

            dma2d_done = 0;
            dma2d_busy = 0;
            bflb_dma2d_channel_tcint_clear(dma2d);
            bflb_dma2d_channel_stop(dma2d);
            bflb_dpi_framebuffer_switch(dpi, (uint32_t)(uintptr_t)disp_frame[index]);
            disp_write_index ^= 1;
        }
    }
}
