#include <stdio.h>
#include <string.h>
#include "esp_mac.h"
#include "hardware.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define TAG "HW"
#define HW_LOAD_0_PIN (16)
#define HW_LOAD_1_PIN (17)

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