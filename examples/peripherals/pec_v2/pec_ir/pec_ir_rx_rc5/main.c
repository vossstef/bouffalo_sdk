#include "board.h"
#include "bflb_irq.h"
#include "bflb_pec_v2_instance.h"

static struct bflb_device_s *pec_ir;
static volatile uint8_t rc5_error;
static volatile uint8_t rc5_frame_ready;
static volatile uint16_t rc5_frame_bits;
static uint32_t data[32]; /*!< one frame supports up to 1024 data bits */

static struct bflb_pec_ir_rx_s ir_rx_cfg = {
    .mem = 0,                                /*!< memory address of first instruction */
    .div = 0,                                /*!< PEC clock divider; 0: automatic timing mode, !0: manual timing mode */
    .pin = PEC_IR_RX_PIN,                    /*!< pin of ir rx */
    .idle_level = PEC_IR_RX_IDLE_LEVEL_HIGH, /*!< most IR receivers output high level when idle */
    .fifo_threshold = 0,
    .dma_enable = 0,
};

/*
 * div = 0: each timing value directly represents the pulse width in us.
 * div != 0: timing value = pulse width (us) * PEC clock (MHz) / (2 * (div + 1)).
 */
static struct bflb_pec_ir_rc5_rx_timing_s rc5_timing = {
    .bit0_carrier = 889,     /*!< RC5 bit 0 carrier pulse width, about 889us */
    .bit0_idle = 889,        /*!< RC5 bit 0 idle pulse width, about 889us */
    .bit1_carrier = 889,     /*!< RC5 bit 1 carrier pulse width, about 889us */
    .bit1_idle = 889,        /*!< RC5 bit 1 idle pulse width, about 889us */
    .frame_timeout = 4000,   /*!< finish the current frame after 4ms idle */
    .start_bit = 1,          /*!< standard RC5 starts with bit 1, used only for synchronization */
    .tolerance_percent = 20, /*!< acceptable pulse width error percent */
};

static void rc5_data_print(uint16_t frame_bits)
{
    uint8_t i;
    uint8_t word_count;
    uint8_t field;
    uint8_t toggle;
    uint8_t address;
    uint8_t command;

    word_count = (frame_bits + 31) / 32;
    printf("rc5 data, bits = %u:", frame_bits);
    for (i = 0; i < word_count; i++) {
        printf(" [%u] = 0x%08x", i, data[i]);
    }
    /* The Start bit is used only for synchronization and is not stored as received data. */
    if (frame_bits == 13) {
        field = (data[0] >> 12) & 0x01;
        toggle = (data[0] >> 11) & 0x01;
        address = (data[0] >> 6) & 0x1F;
        command = data[0] & 0x3F;
        if (field == 0) {
            command |= 0x40;
        }
        printf(", field = %u, toggle = %u, address = %u, command = %u", (uint32_t)field, (uint32_t)toggle, (uint32_t)address, (uint32_t)command);
    }
    printf("\r\n");
}

static void pec_ir_isr(int irq, void *arg)
{
    uint16_t frame_bits;

    if (bflb_pec_ir_rc5_rx_get_err(pec_ir)) {
        rc5_frame_ready = 0;
        rc5_frame_bits = 0;
        bflb_pec_fifo_clr_rx(pec_ir);
        bflb_pec_ir_rc5_rx_clear_frame(pec_ir);
        bflb_pec_ir_rc5_rx_clear_err(pec_ir);
        rc5_error++;
        return;
    }

    frame_bits = bflb_pec_ir_rc5_rx_get_frame_bits(pec_ir);
    if (frame_bits) {
        rc5_frame_bits = frame_bits;
        rc5_frame_ready++;
        bflb_pec_ir_rc5_rx_clear_frame(pec_ir);
    }
}

int main(void)
{
    int ret;

    board_init();
    board_pec_ir_rx_gpio_init();

    pec_ir = bflb_device_get_by_name(BFLB_NAME_PEC_SM0);
    printf("press an RC5 remote key to receive ir data\r\n");

    ret = bflb_pec_ir_rc5_rx_init(pec_ir, &ir_rx_cfg, &rc5_timing);
    if (ret != 0) {
        printf("rc5 rx init error, ret = %d\r\n", ret);
        return ret;
    }
    bflb_irq_attach(pec_ir->irq_num, pec_ir_isr, NULL);
    bflb_irq_enable(pec_ir->irq_num);
    bflb_pec_ir_rc5_rx_start(pec_ir);
    bflb_pec_ir_rc5_err_int_mask(pec_ir, false);
    bflb_pec_ir_rc5_frame_done_int_mask(pec_ir, false);

    while (1) {
        if (rc5_error) {
            rc5_error--;
            printf("rc5 frame error\r\n");
        }
        if (rc5_frame_ready) {
            uint8_t word_count;

            rc5_frame_ready--;
            word_count = (rc5_frame_bits + 31) / 32;
            for (uint8_t i = 0; i < word_count; i++) {
                data[i] = bflb_pec_fifo_read(pec_ir);
            }
            rc5_data_print(rc5_frame_bits);
        }
    }
}
