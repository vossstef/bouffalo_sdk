#include "bflb_gpio.h"

int coex_board_spdt_gpio_prepare(int gpio_pin)
{
    struct bflb_device_s *gpio;

    if (gpio_pin < 0 || gpio_pin >= GPIO_PIN_MAX) {
        return -1;
    }

    gpio = bflb_device_get_by_name("gpio");
    if (gpio == NULL) {
        return -1;
    }

    bflb_gpio_init(gpio, (uint8_t)gpio_pin,
                   GPIO_FUNC_SPDT | GPIO_ALTERNATE);
    return 0;
}
