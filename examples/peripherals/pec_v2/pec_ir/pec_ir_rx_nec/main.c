#include "board.h"
#include "bflb_irq.h"
#include "bflb_pec_v2_instance.h"

static struct bflb_device_s *pec_ir;
static volatile uint8_t nec_repeat;
static volatile uint8_t nec_error;
static volatile uint8_t nec_frame_ready;
static volatile uint16_t nec_frame_bits;
static uint32_t data[32]; /*!< one frame supports up to 1024 bits */

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
static struct bflb_pec_ir_nec_rx_timing_s nec_timing = {
    .start_carrier = 9000,     /*!< NEC leader carrier pulse width, about 9ms */
    .start_idle = 4500,        /*!< NEC leader idle pulse width, about 4.5ms */
    .repeat_idle = 2250,       /*!< NEC repeat idle pulse width, about 2.25ms */
    .bit_carrier = 560,        /*!< NEC bit carrier pulse width, about 560us */
    .bit0_idle = 560,          /*!< NEC bit 0 idle pulse width, about 560us */
    .bit1_idle = 1690,         /*!< NEC bit 1 idle pulse width, about 1.69ms */
    .frame_timeout = 10000,    /*!< finish the current frame after 10ms idle */
    .tolerance_percent = 20,   /*!< acceptable pulse width error percent */
};

static void nec_data_print(uint16_t frame_bits)
{
    uint8_t i;
    uint8_t word_count;
    uint8_t address;
    uint8_t address_inv;
    uint8_t command;
    uint8_t command_inv;

    word_count = (frame_bits + 31) / 32;
    printf("nec data, bits = %u:", frame_bits);
    for (i = 0; i < word_count; i++) {
        printf(" [%u] = 0x%08x", i, data[i]);
    }
    if (frame_bits == 32) {
        address = data[0] & 0xFF;
        address_inv = (data[0] >> 8) & 0xFF;
        command = (data[0] >> 16) & 0xFF;
        command_inv = (data[0] >> 24) & 0xFF;
        if (((address ^ address_inv) != 0xFF) || ((command ^ command_inv) != 0xFF)) {
            printf(", inverse check error\r\n");
            return;
        }
        printf(", address = 0x%02x, command = 0x%02x", (uint32_t)address, (uint32_t)command);
    }
    printf("\r\n");
}

static void pec_ir_isr(int irq, void *arg)
{
    uint16_t frame_bits;

    if (bflb_pec_ir_nec_rx_repeat_get(pec_ir)) {
        nec_repeat++;
    }
    if (bflb_pec_ir_nec_rx_get_err(pec_ir)) {
        nec_frame_ready = 0;
        nec_frame_bits = 0;
        bflb_pec_fifo_clr_rx(pec_ir);
        bflb_pec_ir_nec_rx_clear_frame(pec_ir);
        bflb_pec_ir_nec_rx_clear_err(pec_ir);
        nec_error++;
        return;
    }

    frame_bits = bflb_pec_ir_nec_rx_get_frame_bits(pec_ir);
    if (frame_bits) {
        nec_frame_bits = frame_bits;
        nec_frame_ready++;
        bflb_pec_ir_nec_rx_clear_frame(pec_ir);
    }
}

int main(void)
{
    int ret;

    board_init();
    board_pec_ir_rx_gpio_init();

    pec_ir = bflb_device_get_by_name(BFLB_NAME_PEC_SM0);
    printf("press an NEC remote key to receive ir data\r\n");

    ret = bflb_pec_ir_nec_rx_init(pec_ir, &ir_rx_cfg, &nec_timing);
    if (ret != 0) {
        printf("nec rx init error, ret = %d\r\n", ret);
        return ret;
    }
    bflb_irq_attach(pec_ir->irq_num, pec_ir_isr, NULL);
    bflb_irq_enable(pec_ir->irq_num);
    bflb_pec_ir_nec_rx_start(pec_ir);
    bflb_pec_ir_nec_repeat_int_mask(pec_ir, false);
    bflb_pec_ir_nec_err_int_mask(pec_ir, false);
    bflb_pec_ir_nec_frame_done_int_mask(pec_ir, false);

    while (1) {
        if (nec_error) {
            nec_error--;
            printf("nec frame error\r\n");
        }
        if (nec_frame_ready) {
            uint8_t word_count;

            nec_frame_ready--;
            word_count = (nec_frame_bits + 31) / 32;
            for (uint8_t i = 0; i < word_count; i++) {
                data[i] = bflb_pec_fifo_read(pec_ir);
            }
            nec_data_print(nec_frame_bits);
        }
        if (nec_repeat) {
            nec_repeat--;
            printf("nec repeat, long press\r\n");
        }
    }
}
