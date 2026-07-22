/* output format: YUYV */
struct bflb_pec_dvp_cam_s cam_cfg_bf3901 = {
    .mem = 0,                                           /*!< memory address of first instruction */
    .div = 0,                                           /*!< divisor, N = div + 1 */
    .dma_enable = 1,                                    /*!< enable or disable dma */
    .fifo_threshold = 3,                                /*!< rx fifo threshold, 3 means DMA burst INCR4 */
    .pixel_bits = 16,                                   /*!< serial clock count of every pixel */
    .sample_dly = 0,                                    /*!< delay after sampling before checking PCLK again, in PEC clocks */
    .hsync_dly = -1,                                    /*!< -1: sample DATA before HSYNC active; 0~255: delay after HSYNC active before checking PCLK */
    .pclk_sample_level = 0,                             /*!< physical PCLK sample level, 0: low, 1: high */
    .vsync_active_level = 1,                            /*!< 0: VSYNC active low, 1: active high */
    .hsync_active_level = 1,                            /*!< 0: HSYNC active low, 1: active high */
    .fifo_direction = PEC_SHIFT_DIR_TO_RIGHT,           /*!< FIFO direction, PEC_SHIFT_DIR_TO_LEFT or PEC_SHIFT_DIR_TO_RIGHT */
    .bits_every_push = 32,                              /*!< bits of every push when sample data into ISR, 1~32, must be multiple of pin_data_count */
    .delay_first_us = 200000,                           /*!< delay time in microsecond before start capature camera data */
    .pin_vsync = PEC_DVP_CAM_VSYNC_PIN,                 /*!< DVP CAM VSYNC pin index */
    .pin_hsync = PEC_DVP_CAM_HSYNC_PIN,                 /*!< DVP CAM HSYNC pin index */
    .pin_pclk = PEC_DVP_CAM_PCLK_PIN,                   /*!< DVP CAM pixel clock pin index */
    .pin_data = PEC_DVP_CAM_DATA0_PIN,                  /*!< DVP CAM data start pin index */
    .pin_data_count = 1,                                /*!< DVP CAM data pin count, data pins must be continuous from pin_data */
};
