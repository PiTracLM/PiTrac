/*
 * led.h — onboard LED abstraction.
 *
 * Plain Pico: LED on GPIO 25, direct. Pico W: LED on the CYW43 WiFi chip,
 * driven via the CYW43 SPI driver. Same led_set(bool) API either way; the
 * implementation is selected on PICO_BOARD.
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

/* Bring up the LED path. Call once at boot before any led_set(). On Pico W
 * this also inits the CYW43 SPI driver — a few hundred ms to load the WiFi-chip
 * firmware blob, required for GPIO control even without WiFi. */
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
