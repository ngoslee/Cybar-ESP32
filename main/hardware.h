#ifndef _HARDWARE_H_
#define _HARDWARE_H_

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "esp_mac.h"

typedef enum {
    HW_LOAD_0 = 0,
    HW_LOAD_1
} hardware_load_enum_t;
void hardawre_load_set_state(hardware_load_enum_t load, bool state);
void hardware_init(void);
void hardawre_load_set_states(uint8_t value);
#endif