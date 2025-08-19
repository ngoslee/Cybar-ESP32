#include <stdio.h>
#include <string.h>
#include "driver/uart.h"
#include "esp_mac.h"
#include "patterns.h"
#include "egg.h"
#include "esp_log.h"
#include "lin_bar.h"

static const char *TAG = "EGG";

typedef enum {
    EGG_WAIT_42 = 0,
    EGG_WAIT_69,
    EGG_ACTIVE,
} egg_state_enum_t;

static egg_state_enum_t egg_state = EGG_WAIT_42;
static uint16_t timeout;
void egg_msg_handler(uint8_t *lin_data, uint8_t rxByteCount) {

    lin_bar_command_t truck_cmd;

    if (rxByteCount != 8) {
        return;
    }
    memcpy(&truck_cmd, lin_data, 8);

    //now add easter egg
    /* if command is side lights on, main off and 42 brightness is selected followed by 69, engage KITT mode
    if lights are alll off, disable it*/
    
    //are mains off?
    switch(egg_state) {
        case EGG_WAIT_42:
            if ((truck_cmd.values.value1 == 0) && (truck_cmd.values.value0 == 42)) {
                ESP_LOGI(TAG, "Wait 69");
                egg_state = EGG_WAIT_69;
                timeout = 2000 / 20; //three seconds to select 69
            } 
            break;

        case EGG_WAIT_69:
            if (truck_cmd.values.value1 != 0) {
                egg_state = EGG_WAIT_42;
                ESP_LOGI(TAG, "Mains on");
            } else if  (truck_cmd.values.value0 == 42) {
                timeout = 2000 / 20; //three seconds to select 69
            }
            else if (truck_cmd.values.value0 == 69) {
                sequenceSelect(SEQ_KITT);
                ESP_LOGI(TAG, "Active");
                egg_state = EGG_ACTIVE;
            } else if ((truck_cmd.values.value0 < 60) && (truck_cmd.values.value0 >= 50)) {
                ESP_LOGI(TAG, "50s ");
                egg_state = EGG_WAIT_42;
            } else {
                if (--timeout == 0) {
                    egg_state = EGG_WAIT_42;
                    ESP_LOGI(TAG, "Timeout");
                }
            }
            break;

        case EGG_ACTIVE:
            if ((truck_cmd.values.value1 != 0) || (truck_cmd.values.value0 == 00)) {
                sequenceSelect(SEQ_IDLE);
                egg_state = EGG_WAIT_42;
                ESP_LOGI(TAG, "Off");
            }
            break;

        default:
            egg_state = EGG_WAIT_42;
            break;        
    }    
    bar_lin_truck_cmd(truck_cmd.bytes);
}
