#ifndef _LCD_CONF_USER_H_
#define _LCD_CONF_USER_H_

/* clang-format off */

/* Select screen Type, Optional:

  mipi dsi video interface (panel drives its own PLL/clock/GPIO via lcd_init)
    LCD_DSI_ILI9881C_KD050020      (ILI9881C 720x1280, 4-lane, e.g. KD050HDFIA020, BL618DG)
    LCD_DSI_ILI9881C_KD050023W4    (ILI9881C 720x1280, 2-lane, e.g. KD050HDFIA023-W4, BL618DG)
    LCD_DSI_ILI9806E_KD050FWFIA019 (ILI9806E 480x854, 2-lane, e.g. KD050FWFIA019, BL618DG)
    LCD_DSI_ST7102_YH494           (ST7102 480x960, 2-lane, e.g. YH-494BSAC002N1, BL618DG)
    LCD_DSI_ST7102_KD027HVF        (ST7102 320x320, 1-lane, e.g. KD027HVFID009, BL618DG)
    LCD_DSI_AXS15231B_HS035        (AXS15231B 172x640, 1-lane, firmware-init, BL618DG)
    LCD_DSI_AXS15231E_JX371        (AXS15231E 258x960, 1-lane, e.g. JX 3.71+15231B, BL618DG)
    LCD_DSI_EK79007_WKS70WSV114    (EK79007 1024x600, 2/4-lane selectable, e.g. WKS70WSV114, BL618DG)

  mipi dpi (RGB) interface (parallel RGB; main.c sets up DPI GPIO + display clock)
    LCD_DPI_GC9503V                (GC9503V 480x480, BL618DG)
    LCD_DPI_ST7701P               (ST7701P 480x480, BL618DG)
    LCD_DPI_ST7701S                (ST7701S 480x480, BL618DG)
    LCD_DPI_JD9165BA               (JD9165BA 1024x600, BL618DG)
    LCD_DPI_ILI9488                (ILI9488 320x480, BL618DG)
    LCD_DPI_STANDARD               (generic parallel RGB, 800x480/1024x600, BL618DG)

  Switching is a one-line change here. Everything else (dpi_manager.c, main.c,
  filesystem_reader.c, CMakeLists.txt) keys off LCD_INTERFACE_TYPE (derived from
  this macro in bsp/common/lcd/lcd.h) so the same project builds for either panel.
*/
#define LCD_DSI_EK79007_WKS70WSV114

/* --------- DSI panels: reset + backlight live here (board-specific timing) --------- */
#if defined LCD_DSI_ST7102_YH494 || defined LCD_DSI_ST7102_KD027HVF || defined LCD_DSI_ILI9881C_KD050023W4 || defined LCD_DSI_ILI9881C_KD050020 || defined LCD_DSI_ILI9806E_KD050FWFIA019 || defined LCD_DSI_AXS15231B_HS035 || defined LCD_DSI_AXS15231E_JX371 || defined LCD_DSI_EK79007_WKS70WSV114

    /* DSI panel reset is driven by the LCD framework: lcd_init() pulses the reset
     * GPIO (per these macros) before calling into the panel's bring-up. Reset
     * timing is a per-panel/board property, so it lives here in the board config
     * rather than hard-coded in each panel driver. Adjust PIN/level/delays to
     * match the selected panel's datasheet. */
    #define LCD_RESET_EN            1
    #define LCD_RESET_PIN           GPIO_PIN_2   /* panel reset line */
    #define LCD_RESET_ACTIVE_LEVEL  0            /* active-low: idle high, pulse low */
    #define LCD_RESET_HOLD_MS       10           /* assert (low) hold */
    #define LCD_RESET_DELAY         120          /* recovery after release (datasheet: >=120ms) */

    /* Backlight enable pin. Driven by lcd_backlight_toggle(), which main.c calls
     * only after LVGL's first frame has been scanned out, so the user never sees
     * an uninitialized frame flash on power-up. Enabling LCD_BACKLIGHT_EN is also
     * what compiles lcd_backlight_toggle() in. */
    #define LCD_BACKLIGHT_EN            1
    #define LCD_BACKLIGHT_PIN          GPIO_PIN_40 /* backlight enable on this board */
    #define LCD_BACKLIGHT_ACTIVE_LEVEL 1          /* active-high: drive high to light */

    /* ILI9806E panel orientation via MADCTL (0x36), applied after its init table.
     * Board-mounting property, so it lives here rather than in the panel driver.
     * Only consumed when LCD_DSI_ILI9806E_KD050FWFIA019 is the selected panel.
     *   0: panel default orientation
     *   1: 180-degree rotation/mirror */
    #define ILI9806E_KD050FWFIA019_ROTATE_180  0

    /* dsi ek79007 wks70wsv114 config */
    /* Number of MIPI DSI data lanes the panel is wired for
        2: 2-lane (vendor EK79007_Initial_Code_2lane, writes 0xB2 = 0x10)
        4: 4-lane (vendor EK79007_Initial_Code_4lane, leaves 0xB2 at its default)
    */
    #define EK79007_WKS70WSV114_LANE_NUM 2
    #define EK79007_WKS70WSV114_FB_MODE EK79007_WKS70WSV114_FB_MODE_OSD

/* --------- DPI panel configs (copied verbatim from bsp/common/lcd/lcd_conf.h) --------- */

/* dpi gc9503v config */
#elif defined LCD_DPI_GC9503V

    /* Selecting DPI working mode
        1: DPI peripheral (support: bl618dg)
        2: PEC simulation (support: bl616, bl618dg)
        3. DPI v2 peripheral (support: bl618dg)
    */
    #define LCD_DPI_INTERFACE_TYPE 1

    /* Selecting initialization interface
        0: Not using or custom
        1: Software spi 3-wires 9-bits mode, any pin can be used.
        2: Software spi 4-wires 8-bits mode, any pin can be used.
    */
    #define LCD_DPI_INIT_INTERFACE_TYPE 1

    /* Selecting pixel format
        1: rgb565 (16-bits)
    */
    #define GC9503V_DPI_PIXEL_FORMAT 1

    /* RGB-BGR Order control
        0: output R-G-B
        1: output B-G-R
    */
    #define GC9503V_DPI_RGB_ORDER_MODE 0

    /* enabled DE(data enable) signal
        0: disable DE signal output
        1: enable DE signal output
    */
    #define GC9503V_DPI_DE_MODE 1

    /* ILI9488 LCD width and height */
    #define GC9503V_DPI_W 480
    #define GC9503V_DPI_H 480


/* dpi st7701p config */
#elif defined LCD_DPI_ST7701P

    /* Selecting DPI working mode
        2: PEC simulation (support: bl616, bl618dg)
        3: DPI v2 peripheral (support: bl618dg)
    */
    #define LCD_DPI_INTERFACE_TYPE 3

    /* Selecting initialization interface
        0: Not using or custom
        1: Software spi 3-wires 9-bits mode, any pin can be used.
        2: Software spi 4-wires 8-bits mode, any pin can be used.
    */
    #define LCD_DPI_INIT_INTERFACE_TYPE 1

    /* Selecting framebuffer format
        1: rgb565 (16-bits)
        2: nrgb8888 (32-bits, output rgb888)
    */
    #define ST7701P_DPI_PIXEL_FORMAT 2

    /* RGB-BGR Order control
        0: output R-G-B
        1: output B-G-R
    */
    #define ST7701P_DPI_RGB_ORDER_MODE 0

    #define ST7701P_DPI_W 480
    #define ST7701P_DPI_H 480


/* dpi st7701s config */
#elif defined LCD_DPI_ST7701S

    /* Selecting DPI working mode
        1: DPI peripheral (support: bl618dg)
        2: PEC simulation (support: bl616, bl618dg)
        3. DPI v2 peripheral (support: bl618dg)
    */
    #define LCD_DPI_INTERFACE_TYPE 1

    /* Selecting initialization interface
        0: Not using or custom
        1: Software spi 3-wires 9-bits mode, any pin can be used.
        2: Software spi 4-wires 8-bits mode, any pin can be used.
    */
    #define LCD_DPI_INIT_INTERFACE_TYPE 1

    /* Selecting pixel format
        1: rgb565 (16-bits)
    */
    #define ST7701S_DPI_PIXEL_FORMAT 1

    /* RGB-BGR Order control
        0: output R-G-B
        1: output B-G-R
    */
    #define ST7701S_DPI_RGB_ORDER_MODE 1

    /* ILI9488 LCD width and height */
    #define ST7701S_DPI_W 480
    #define ST7701S_DPI_H 480


/* dpi jd9165ba config */
#elif defined LCD_DPI_JD9165BA

    /* Selecting DPI working mode
        3. DPI v2 peripheral (support: bl618dg)
    */
    #define LCD_DPI_INTERFACE_TYPE 3

    /* Selecting initialization interface
        0: Not using or custom
        1: Software spi 3-wires 9-bits mode, any pin can be used.
        2: Software spi 4-wires 8-bits mode, any pin can be used. */
    #define LCD_DPI_INIT_INTERFACE_TYPE 0

    /* Selecting pixel format
        1: rgb565 (16-bits)
        2: nrgb8888 (32-bits, output rgb888)
    */
    #define JD9165BA_DPI_PIXEL_FORMAT 2

    /* RGB-BGR Order control
        0: output R-G-B
        1: output B-G-R
    */
    #define JD9165BA_DPI_RGB_ORDER_MODE 0

    /* JD9165BA LCD width and height */
    #define JD9165BA_DPI_W 1024
    #define JD9165BA_DPI_H 600

    /* RGB timing and frame rate are fixed by the panel and defined in jd9165ba_dpi.c */


/* dpi ili9488 config */
#elif defined LCD_DPI_ILI9488

    /* Selecting DPI working mode
        1: DPI peripheral (support: bl618dg)
        2: PEC simulation (support: bl616, bl618dg)
        3. DPI v2 peripheral (support: bl618dg)
    */
    #define LCD_DPI_INTERFACE_TYPE 1

    /* Selecting initialization interface
        0: Not using or custom
        1: Software spi 3-wires 9-bits mode, any pin can be used.
        2: Software spi 4-wires 8-bits mode, any pin can be used.
    */
    #define LCD_DPI_INIT_INTERFACE_TYPE 2

    /* Selecting pixel format
        1: rgb565 (16-bits)
    */
    #define ILI9488_DPI_PIXEL_FORMAT 1

    /* ILI9488 LCD width and height */
    #define ILI9488_DPI_W 320
    #define ILI9488_DPI_H 480


/* dpi standard config */
#elif defined(LCD_DPI_STANDARD)

    /* Selecting DPI working mode
        1: DPI peripheral (support: bl618dg)
        2: PEC simulation old version (support: bl616, bl618dg)
        3. DPI v2 peripheral (support: bl618dg)
        4. PEC simulation v2 (support: bl616cl, bl618dg)
    */
    #define LCD_DPI_INTERFACE_TYPE 3

    /* Selecting initialization interface
        0: Not using or custom
        1: Software spi 3-wires 9-bits mode, any pin can be used.
        2: Software spi 4-wires 8-bits mode, any pin can be used.
    */
    #define LCD_DPI_INIT_INTERFACE_TYPE 0

    /* DPI standard panel has no hard reset line on this board */
    #define LCD_RESET_EN 0

    /* Selecting pixel format
        1: rgb565 (16-bits)
        2: nrgb8888 (32-bits)
    */
    #define STANDARD_DPI_PIXEL_FORMAT 2

    /* STANDARD LCD width and height */
    #define STANDARD_DPI_W 1024
    #define STANDARD_DPI_H 600

    /* RGB timing parameter Settings
       Total Width = HSW + HBP + Active_Width + HFP
       Total Height = VSW + VBP + Active_Height + VFP */

#if ((STANDARD_DPI_W == 1024)&&(STANDARD_DPI_H == 600))
    /* Hsync Pulse Width */
    #define STANDARD_DPI_HSW 20 
    /* Hsync Back Porch */
    #define STANDARD_DPI_HBP 140
    /* Hsync Front Porch */
    #define STANDARD_DPI_HFP 160

    /* Vsync Pulse Width */
    #define STANDARD_DPI_VSW 3
    /* Vsync Back Porch */
    #define STANDARD_DPI_VBP 23
    /* Vsync Front Porch */
    #define STANDARD_DPI_VFP 12
#else
    /* Hsync Pulse Width */
    #define STANDARD_DPI_HSW 4
    /* Hsync Back Porch */
    #define STANDARD_DPI_HBP 82
    /* Hsync Front Porch */
    #define STANDARD_DPI_HFP 14

    /* Vsync Pulse Width */
    #define STANDARD_DPI_VSW 5
    /* Vsync Back Porch */
    #define STANDARD_DPI_VBP 6
    /* Vsync Front Porch */
    #define STANDARD_DPI_VFP 39
#endif

    /* Maximum refresh frame rate per second, Used to automatically calculate the clock frequency */
    #define STANDARD_DPI_FRAME_RATE 70

#endif

/********** DPI v2 configuration (for any DPI panel with LCD_DPI_INTERFACE_TYPE==3) **********/
#if (defined(LCD_DPI_INTERFACE_TYPE) && (LCD_DPI_INTERFACE_TYPE == 3))

    /* Enable DPI v2 functionality */
    #define LCD_V2_DPI_ENABLE

    /* Selecting initialization interface */
    #if (LCD_DPI_INIT_INTERFACE_TYPE == 1)
        /* Software spi 3-wires 9-bits mode, any pin can be used. */
        #define LCD_DPI_INIT_SPI_SOFT_3_PIN_CS    GPIO_PIN_0
        #define LCD_DPI_INIT_SPI_SOFT_3_PIN_CLK   GPIO_PIN_1
        #define LCD_DPI_INIT_SPI_SOFT_3_PIN_DAT   GPIO_PIN_3
    #elif (LCD_DPI_INIT_INTERFACE_TYPE == 2)
        /* Software spi 4-wires 8-bits mode, any pin can be used. */
        #define LCD_DPI_INIT_SPI_SOFT_4_PIN_CS    GPIO_PIN_0
        #define LCD_DPI_INIT_SPI_SOFT_4_PIN_CLK   GPIO_PIN_1
        #define LCD_DPI_INIT_SPI_SOFT_4_PIN_DAT   GPIO_PIN_3
        #define LCD_DPI_INIT_SPI_SOFT_4_PIN_DC    GPIO_PIN_4
    #endif

    /* Interface type selection:
        0: 24PIN mode (D0 ~ D23)
        1: 18PIN mode 1 (D0 ~ D17)
        2: 18PIN mode 2 (D0 ~ D5, D8 ~ D13, D16 ~ D21)
        3: 16PIN mode 1 (D0 ~ D15)
        4: 16PIN mode 2 (D0 ~ D4, D8 ~ D13, D16 ~ D20)
        5: 16PIN mode 3 (D1 ~ D5, D8 ~ D13, D17 ~ D21)
    */
    #define LCD_DPI_V2_INTERFACE_TYPE  0

    /* Input source selection:
        0: Test pattern without OSD
        1: Test pattern with OSD
        2: Framebuffer without OSD
        3: Framebuffer with OSD
    */
    #define LCD_DPI_V2_INPUT_SEL       3

    /* Test pattern selection (only valid when input_sel is 0 or 1):
        0: NULL (no test pattern)
        1: Black
        2: Red
        3: Green
        4: Yellow
    */
    #define LCD_DPI_V2_TEST_PATTERN    0
#endif

/* clang-format on */
#endif /* _LCD_CONF_USER_H_ */
