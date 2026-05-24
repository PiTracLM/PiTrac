/*
 * led.h — onboard LED abstraction.
 *
 * On a plain Pi Pico the LED is on GPIO 25, directly addressable. On a Pico W
 * the LED is on the CYW43 WiFi chip and we have to route writes through the
 * CYW43 SPI driver. Both call sites use the same `led_set(bool)` API; this
 * header picks the right implementation based on PICO_BOARD.
 */

#ifndef PITRAC_PICO_LED_H
#define PITRAC_PICO_LED_H

#include <stdbool.h>
#include "config.h"

#if defined(CYW43_WL_GPIO_LED_PIN)
#  include "pico/cyw43_arch.h"
#  define PITRAC_LED_VIA_CYW43 1
#else
#  include "hardware/gpio.h"
#  define PITRAC_LED_VIA_CYW43 0
#endif

/* Bring up whichever path the LED lives on. Returns true on success.
 * Must be called once at boot before any led_set(). On Pico W this also
 * brings up the CYW43 SPI driver — takes a few hundred ms while it loads
 * the WiFi-chip firmware blob (we need it for GPIO control even if we never
 * touch WiFi). On plain Pico it's a couple of register writes. */
static inline bool led_init(void) {
#if PITRAC_LED_VIA_CYW43
    return cyw43_arch_init() == 0;
#else
    gpio_init(PIN_STATUS_LED);
    gpio_set_dir(PIN_STATUS_LED, GPIO_OUT);
    return true;
#endif
}

static inline void led_set(bool on) {
#if PITRAC_LED_VIA_CYW43
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
#else
    gpio_put(PIN_STATUS_LED, on ? 1 : 0);
#endif
}

#endif /* PITRAC_PICO_LED_H */
