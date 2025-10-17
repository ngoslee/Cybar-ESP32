#include <stdio.h>
#include <string.h>
#include "driver/uart.h"
#include "esp_mac.h"
#include "patterns.h"
#include "egg.h"
#include "esp_log.h"
#include "lin_bar.h"
#include "hardware.h"
#include "lin_truck.h"
#include "diag_port.h"
#include "egg.h"
#include "user.h"
#include "web_server.h"
#include "mesh_node.h"

static const char *TAG = "EGG";

typedef enum {
    EGG_WAIT_42 = 0,
    EGG_WAIT_69,
    EGG_ACTIVE,
} egg_state_enum_t;

static egg_state_enum_t egg_state = EGG_WAIT_42;
static uint16_t timeout;
static lin_bar_command_t user_cmd_prev, truck_cmd_prev, diag_cmd_prev, web_cmd_prev, cmd_prev, mesh_cmd_prev;
static uint8_t use_lin = 1;

uint8_t update_if_new(lin_bar_command_t * prev, lin_bar_command_t * current, lin_bar_command_t * out) {
    if (memcmp(prev, current, sizeof(lin_bar_command_t))) {
        memcpy(prev, current, sizeof(lin_bar_command_t));
        memcpy(out, current, sizeof(lin_bar_command_t));
        return 1;
    }
    return 0;
}

void egg_msg_handler(void) {
    static uint16_t mode_delay = 100;
    uint8_t changed = 0;
    lin_bar_command_t diag_cmd, user_cmd, truck_cmd, final_cmd, web_cmd, mesh_cmd;
    memcpy(final_cmd.bytes, cmd_prev.bytes, 8);

    truck_get_command(truck_cmd.bytes);
    if ( update_if_new(&truck_cmd_prev, &truck_cmd, &final_cmd)) {
        changed  = 1;
        mode_delay = 100;
   //     ESP_LOGI(TAG, "truck command %d %d %d %d %d %d ", truck_cmd.values.value0, truck_cmd.values.value1, truck_cmd.values.value2, truck_cmd.values.value3, truck_cmd.values.value4, truck_cmd.values.value5);
    }
    if (mode_delay) {
        mode_delay--;
    }

    //leaf nodes use mesh as a fall back for LIN
    //but if LIN is active, don't use mesh version
    if ((mode_delay ==0) || (!mesh_mode_is_lin())) {
        mesh_get_command(mesh_cmd.bytes);
        if (update_if_new(&mesh_cmd_prev, &mesh_cmd, &final_cmd)) {
            changed  = 2;
   //         ESP_LOGI(TAG, "mesh command %d %d %d %d %d %d ", mesh_cmd.values.value0, mesh_cmd.values.value1, mesh_cmd.values.value2, mesh_cmd.values.value3, mesh_cmd.values.value4, mesh_cmd.values.value5);
        }
    }
    user_get_command(user_cmd.bytes);
    if ( update_if_new(&user_cmd_prev, &user_cmd, &final_cmd)) {
        changed  = 3;
 //       ESP_LOGI(TAG, "user command %d %d %d %d %d %d ", user_cmd.values.value0, user_cmd.values.value1, user_cmd.values.value2, user_cmd.values.value3, user_cmd.values.value4, user_cmd.values.value5);
    }
    diag_get_command(diag_cmd.bytes);
    if (update_if_new(&diag_cmd_prev, &diag_cmd, &final_cmd)) {
        changed  = 4;
 //       ESP_LOGI(TAG, "diag command %d %d %d %d %d %d ", diag_cmd.values.value0, diag_cmd.values.value1, diag_cmd.values.value2, diag_cmd.values.value3, diag_cmd.values.value4, diag_cmd.values.value5);
    }

    web_get_command(web_cmd.bytes);
    if (update_if_new(&web_cmd_prev, &web_cmd, &final_cmd)) {
        changed  = 5;
        ESP_LOGI(TAG, "web command %d %d %d %d %d %d ", web_cmd.values.value0, web_cmd.values.value1, web_cmd.values.value2, web_cmd.values.value3, web_cmd.values.value4, web_cmd.values.value5);
    }

    if (changed == 0) return;
    memcpy(cmd_prev.bytes, final_cmd.bytes, 8);

        //now add easter egg
    /* if command is side lights on, main off and 42 brightness is selected followed by 69, engage KITT , truck_cmd.values.value0, truck, truck_cmd.values.value0, truck_cmd.values.value0_cmd.values.value0
    if lights are alll off, disable it*/
    
    //are mains off?
    switch(egg_state) {
        case EGG_WAIT_42:
            if ((final_cmd.values.value1 == 42) || (final_cmd.values.value0 == 42)) {
                ESP_LOGI(TAG, "Wait 69");
                egg_state = EGG_WAIT_69;
                timeout = 2000 / 20; //three seconds to select 69
            } 
            break;

        case EGG_WAIT_69:
            if  ((final_cmd.values.value1 == 42) || (final_cmd.values.value0 == 42)) {
                timeout = 2000 / 20; //three seconds to select 69
            }
            else if ((final_cmd.values.value1 == 69) || (final_cmd.values.value0 == 69)){
                ESP_LOGI(TAG, "Active");
                egg_state = EGG_ACTIVE;
            } else if (((final_cmd.values.value0 < 60) && (final_cmd.values.value0 >= 50)) || ((final_cmd.values.value1 < 60) && (final_cmd.values.value1 >= 50)) ) {
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
            break;

        default:
            egg_state = EGG_WAIT_42;
            break;        
    }    
    //put after switch to take effect immediately
    if (egg_state == EGG_ACTIVE) {
            if ((final_cmd.values.value0 == 0) && (final_cmd.values.value1 == 100)) {
                sequenceSelect(SEQ_IDLE);
                egg_state = EGG_WAIT_42;
                ESP_LOGI(TAG, "Off");
            } else if ((final_cmd.values.value0 == 0 ) && (final_cmd.values.value1 > 0)) {
                sequenceSelect(SEQ_KITT);
            } else if ((final_cmd.values.value0 > 0 ) && (final_cmd.values.value1 > 0)) {
                sequenceSelect(SEQ_SWEEP);
            } else if ((final_cmd.values.value0 > 0 ) && (final_cmd.values.value1 == 0)) {
                sequenceSelect(SEQ_WIG_WAG);
            }
    }
    if (egg_state == EGG_ACTIVE) {        
        changed = 6;
    }
    if (changed >= 2) {
        
        use_lin = 0;
    } else {
        use_lin = 1;
    }
    bar_lin_truck_cmd(final_cmd.bytes);
  //  hw_load_set_cmd(final_cmd.bytes);
}

bool egg_is_lin_mode(void) {
    return use_lin == 1;
}   