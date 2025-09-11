#include <stdio.h>
#include <string.h>
#include "esp_mac.h"
#include "hardware.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "led_strip.h"
#include "driver/rmt.h"

#define TAG "HW"
#define HW_LOAD_0_PIN (18)
#define HW_LOAD_1_PIN (17)

#define BLINK_GPIO (16) //CONFIG_BLINK_GPIO

static uint8_t s_led_state = 0;

static led_strip_handle_t led_strip;


void hardawre_load_set_state(hardware_load_enum_t load, bool state)
{
    switch (load) {
        case HW_LOAD_0:
            gpio_set_level(HW_LOAD_0_PIN, state);
            break;
        case HW_LOAD_1:
            gpio_set_level(HW_LOAD_1_PIN, state);
            break;
        default:
            ESP_LOGE(TAG, "unknown load %d", load);

    }
}

void hardawre_load_set_states(uint8_t value) {
        if ((value >= 0) && (value <=3 )) {
            hardawre_load_set_state(HW_LOAD_0, value & 0x01);
            hardawre_load_set_state(HW_LOAD_1, value & 0x02);
        } else {
            ESP_LOGE(TAG, "Load value out of range: %d", value);
        }
}

void hardware_init(void) {
    gpio_set_level(HW_LOAD_0_PIN, 0);
    gpio_set_direction(HW_LOAD_0_PIN, GPIO_MODE_OUTPUT);

    gpio_set_level(HW_LOAD_1_PIN, 0);
    gpio_set_direction(HW_LOAD_1_PIN, GPIO_MODE_OUTPUT);

}

void hw_led_init(void) {
    /* LED strip initialization with the GPIO and pixels number*/
    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINK_GPIO,
        .max_leds = 1, // at least one LED on board
    };

    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));

    /* Set all LED off to clear all pixels */
    led_strip_clear(led_strip);

}

void hw_toggle_led(void)
{
    /* Toggle the LED state */
    s_led_state++;
    if (s_led_state > 3) s_led_state = 0;

    /* If the addressable LED is enabled */
    if (s_led_state) {
        /* Set the LED pixel using RGB from 0 (0%) to 255 (100%) for each color */
        led_strip_set_pixel(led_strip, 0, s_led_state==1?16:0, s_led_state==2?16:0, s_led_state==3?16:0);
        /* Refresh the strip to send data */
        led_strip_refresh(led_strip);
    } else {
        /* Set all LED off to clear all pixels */
        led_strip_clear(led_strip);
    }
}