#ifndef _HARDWARE_H_
#define _HARDWARE_H_

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "esp_mac.h"

typedef enum {
    HW_LOAD_NA = 0,
    HW_LOAD_1,
    HW_LOAD_2,
    HW_LOAD_3,
} hardware_load_enum_t;
void hardawre_load_set_state(hardware_load_enum_t load, bool state);
void hardware_init(void);
void hardawre_load_set_states(uint8_t value);
void hw_led_init(void);
void hw_toggle_led(void);
void hw_lin_enable(void);
void hw_load_set_cmd(uint8_t * cmd);

#endif