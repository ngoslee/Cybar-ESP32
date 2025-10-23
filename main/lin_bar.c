#include <stdio.h>
#include <string.h>
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "hal/uart_hal.h"
#include "lin_bar.h"
#include "soc/uart_struct.h"

#include "lin.h"
#include "patterns.h"
#include "lin_truck.h"
#include "pinout.h"
#include "hardware.h"
#include "web_mesh.h"
#include "egg.h"
#include "system.h"

#define BAR_LIN_UART_PORT UART_NUM_1

#define LIN_BAUD_RATE 19200
#define LIN_BREAK_DURATION_US 768 // 14 bits at 19200 baud = 0.729 ms

#define LIN_MSG_INTERVAL_MS 20
#define UART_BUF_SIZE 256

void bar_lin_set_tx_data(uint16_t *data, uint8_t * msg);
uint8_t bar_diag_handler(void);

static const char *TAG = "LIN_BAR";
static uint8_t tx_data[TRUCK_TO_BAR_DATA_LEN] = {0}; // Buffer for TX data
static uint8_t tx_data_shadow[TRUCK_TO_BAR_DATA_LEN] = {0}; // Buffer for TX data

static void (*rx_callback)(uint8_t *data, size_t len) = NULL;

static volatile uint8_t lin_tx_data_updated = 0;

static lin_port_t bar_lin_port = { .uart = BAR_LIN_UART_PORT,
                                    .tx_pin = BAR_LIN_TX_PIN,
                                    .rx_pin = BAR_LIN_RX_PIN,
                                };

static uint8_t rx_data[8]; //max length
static lin_msg_t bar_tx_msg = {
        .id = LIN_ID_BAR_CMD,
        .len = TRUCK_TO_BAR_DATA_LEN,
        .data = tx_data_shadow,
    };

static lin_msg_t bar_rx_msg = {
        .id = LIN_ID_BAR_STATUS,
        .len = BAR_TO_TRUCK_DATA_LEN,
        .data = rx_data,
    };  

static lin_bar_command_t truck_cmd;    
static volatile uint8_t truck_cmd_flag = 0;
static lin_bar_command_t truck_cmd_prev;
void bar_lin_truck_cmd(uint8_t * cmd) {
    if (truck_cmd_flag) return;
    if (memcmp(truck_cmd_prev.bytes, cmd, 8)) {
        memcpy(truck_cmd_prev.bytes, cmd, 8);
        memcpy(truck_cmd.bytes, cmd, 8);
        truck_cmd_flag = 1;
    }
}

void truck_input(uint16_t * data) {
    if (!truck_cmd_flag) return;
    data[0] = truck_cmd.values.value0;
    data[1] = truck_cmd.values.value1;
    data[2] = truck_cmd.values.value2;
    data[3] = truck_cmd.values.value3;
    data[4] = truck_cmd.values.value4;
    data[5] = truck_cmd.values.value5;

    truck_cmd_flag = 0;    
}

// Periodic LIN task also handle loads
static void bar_lin_task(void *arg) {
    static uint16_t newValues[6];
    uint16_t values_final[6];

    //handle diag messages first
    while(bar_diag_handler() != 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    while (1) {   
        truck_input(newValues); //updates on new values


        sequenceNext(newValues, values_final); //overrides if mode active

        //load act on final values to move with sequencer
        lin_bar_command_t load_cmd;
        load_cmd.values.value0 = values_final[0];
        load_cmd.values.value1 = values_final[1];
        load_cmd.values.value2 = values_final[2];
        load_cmd.values.value3 = values_final[3];
        load_cmd.values.value4 = values_final[4];
        load_cmd.values.value5 = values_final[5];

        //if control node, send out update to mesh
        if (system_get_node_type() == NODE_TYPE_WEB) {
            mesh_update(!egg_is_lin_mode(), &load_cmd); //overrides if mesh command
        }
        hw_load_set_cmd(load_cmd.bytes);

        bar_lin_set_tx_data(values_final, tx_data_shadow); //convert to bitfield
 //       ESP_LOGI(TAG, "%d %d %d %d %d %d", newValues[0], newValues[1], newValues[2], newValues[3], newValues[4], newValues[5] );
        lin_tx_frame(bar_lin_port, bar_tx_msg);
 //           ESP_LOGI(TAG, "Packet sent");
        vTaskDelay(pdMS_TO_TICKS(LIN_MSG_INTERVAL_MS / 2));

        if(lin_rx_frame(bar_lin_port, bar_rx_msg)) {
            truck_lin_set_bar_data_response(bar_rx_msg.data); //pass through data

//            ESP_LOGI(TAG, "Packet received");
        } 
        vTaskDelay(pdMS_TO_TICKS(LIN_MSG_INTERVAL_MS / 2));
    }
}

// Set TX data for ID 0x0A
void bar_lin_set_tx_data(uint16_t *data, uint8_t * msg) {

    uint8_t i, v;
    uint16_t in_mask, bitcount;
    bitcount = 0;

    //format is value every 10 bits
    for (i=0; i< TRUCK_TO_BAR_DATA_LEN; i++)
    {
        msg[i] = 0;
    }
    for (v=0; v< 6; v++)
    {
 //       vTaskDelay(1 / portTICK_PERIOD_MS);
 //       uart_write_bytes(UART_NUM_0, "Byte ", 5);
        in_mask = 0x0001;
        for (i=0; i< 10; i++)
        {
            if(data[v] & in_mask)
            {
                msg[bitcount/8] |= (1 <<(bitcount % 8));
            }
            in_mask <<= 1;
            bitcount ++;
        }
    }
}

// Register RX callback for ID 0x0B
void lin_register_rx_callback(void (*callback)(uint8_t *data, size_t len)) {
    rx_callback = callback;
}

// Initialize UART and LIN
void bar_lin_init(void) {
    hw_lin_enable();
    lin_init(&bar_lin_port);
    #if 0
    uart_config_t uart_config = {
        .baud_rate = LIN_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(bar_lin_port.uart, UART_BUF_SIZE, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(bar_lin_port.uart, &uart_config));
    ESP_ERROR_CHECK(gpio_set_pull_mode(bar_lin_port.rx_pin, GPIO_PULLUP_ONLY));
    ESP_ERROR_CHECK(gpio_set_pull_mode(bar_lin_port.tx_pin, GPIO_PULLUP_ONLY));
    ESP_ERROR_CHECK(uart_set_pin(bar_lin_port.uart, bar_lin_port.tx_pin, bar_lin_port.rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    #endif
    xTaskCreate(bar_lin_task, "bar_lin_task", 4096, NULL, 11, NULL);
}

/*need to handle 3C 3D messages from the truck, either we go full transparent or buffer bar data and send to truck when requested
  buffer allows pretenting to be a bar without having one connected
  system works without 3C/3D, but truck throws errors*/

/*bar side:
Send 3C, get 3D, save response

Truck side, on 3C, set type, on 3D respode with data*/
/*use state machine to track bar data collection*/

typedef enum {
    BAR_DIAG_INIT,
    BAR_DIAG_SEND_3C,
    BAR_DIAG_SEND_3D,
    BAR_DIAG_DONE,
    BAR_DIAG_FAILED,
} bar_diag_state_t;

static bar_diag_state_t state = BAR_DIAG_INIT;

#define DIAG_MESG_NUM 6

static uint8_t messageIndex = 0;
static const uint8_t data_3c_0[8] = {0x81,0x00,0x00,0x0A,0x00,0x00,0x00,0x00};
static const uint8_t data_3c_1[8] = {0x81,0x00,0x00,0x0A,0x01,0x00,0x00,0x00};
static const uint8_t data_3c_2[8] = {0x81,0x00,0x00,0x0D,0x00,0x00,0x00,0x00};
static const uint8_t data_3c_3[8] = {0x81,0x00,0x00,0x0D,0x01,0x00,0x00,0x00};
static const uint8_t data_3c_4[8] = {0x81,0x00,0x00,0x0D,0x02,0x00,0x00,0x00};
static const uint8_t data_3c_5[8] = {0x81,0x00,0x00,0x1D,0x00,0x00,0x00,0x00};
static const lin_msg_t bar_diag_3c[DIAG_MESG_NUM] = {
    { .id = 0x3C, .len = 8, .data = (uint8_t*)data_3c_0 },
    { .id = 0x3C, .len = 8, .data = (uint8_t*)data_3c_1 },
    { .id = 0x3C, .len = 8, .data = (uint8_t*)data_3c_2 },
    { .id = 0x3C, .len = 8, .data = (uint8_t*)data_3c_3 },
    { .id = 0x3C, .len = 8, .data = (uint8_t*)data_3c_4 },
    { .id = 0x3C, .len = 8, .data = (uint8_t*)data_3c_5 }
};

static  uint8_t data_3d_0[8] = {0x81,0x00,0x01,0x0A,0x00,0x05,0x06,0x00};
static  uint8_t data_3d_1[8] = {0x81,0x00,0x01,0x0A,0x01,0xA4,0x23,0x00};
static  uint8_t data_3d_2[8] = {0x81,0x00,0x01,0x0D,0x00,0x00,0x18,0x00};
static  uint8_t data_3d_3[8] = {0x81,0x00,0x01,0x0D,0x01,0x00,0x01,0x00};
static  uint8_t data_3d_4[8] = {0x81,0x00,0x01,0x0D,0x02,0x00,0x01,0x00};
static  uint8_t data_3d_5[8] = {0x81,0x00,0x01,0x1D,0x00,0x01,0x2C,0x00};
static lin_msg_t bar_diag_3d[DIAG_MESG_NUM] = {
    { .id = 0x3D, .len = 8, .data = (uint8_t*)data_3d_0 },
    { .id = 0x3D, .len = 8, .data = (uint8_t*)data_3d_1 },
    { .id = 0x3D, .len = 8, .data = (uint8_t*)data_3d_2 },
    { .id = 0x3D, .len = 8, .data = (uint8_t*)data_3d_3 },
    { .id = 0x3D, .len = 8, .data = (uint8_t*)data_3d_4 },
    { .id = 0x3D, .len = 8, .data = (uint8_t*)data_3d_5 }
};

#define MAX_RETRIES (2000 / 10) //2 seconds worth
static uint16_t retries = MAX_RETRIES;

uint8_t bar_diag_handler(void)
{
    static bool alerted = false;

    uint8_t orig_data[8];
    if ((state == BAR_DIAG_DONE) || (state == BAR_DIAG_FAILED)) {
        return 0;
    }

    switch (state) {
        case BAR_DIAG_INIT:
            state = BAR_DIAG_SEND_3C;
            break;
        case BAR_DIAG_SEND_3C:
            lin_tx_frame(bar_lin_port, bar_diag_3c[messageIndex]);
            //prepare to send 3D
            state = BAR_DIAG_SEND_3D;
            break;
        case BAR_DIAG_SEND_3D:
            memcpy(orig_data, bar_diag_3d[messageIndex].data, 8); //save original
            //send 3D and read response
            if (lin_rx_frame(bar_lin_port, bar_diag_3d[messageIndex]) == 0) {
                //retry
                if (--retries == 0) {
                    ESP_LOGW(TAG,"Bar diagnostic read failed");
                    messageIndex = DIAG_MESG_NUM; //send canned responses to truck
                    state = BAR_DIAG_FAILED;
                    
                    break;
                }
                if (!alerted) {
                    ESP_LOGW(TAG, "Diag 3D RX failed, retrying");
                    alerted = true;
                }
                state = BAR_DIAG_SEND_3C;
                break;
            }
            alerted = false;
            if (memcmp(orig_data, bar_diag_3d[messageIndex].data, 8) != 0) {
                    ESP_LOGW(TAG, "Diag message %d differs: %02X %02X %02X %02X %02X %02X %02X %02X", messageIndex,
                    bar_diag_3d[messageIndex].data[0], bar_diag_3d[messageIndex].data[1],
                    bar_diag_3d[messageIndex].data[2], bar_diag_3d[messageIndex].data[3],
                    bar_diag_3d[messageIndex].data[4], bar_diag_3d[messageIndex].data[5],
                    bar_diag_3d[messageIndex].data[6], bar_diag_3d[messageIndex].data[7]);
            }
            if (messageIndex < (DIAG_MESG_NUM -1)) {
                messageIndex++;
                state = BAR_DIAG_SEND_3C;
            } else {
                state = BAR_DIAG_DONE;
                ESP_LOGI(TAG, "Bar diag complete");
            }
            break;
        case BAR_DIAG_DONE:
            //do nothing
            return 0;
            break;
        default:
            state = BAR_DIAG_INIT;
            break;
    }
    return 1;
}    

uint8_t bar_diag_in_progress(void)
{
    if (state != BAR_DIAG_DONE) return 1;
    return 0;
}
uint8_t data_3d_response[8];
uint8_t data_3d_valid = 0;

void bar_handle_truck_3c(uint8_t * data)
{
    //search known messages and prepare response
    //use messageIndex to require live data and prevent race condition
    //TODO: add timout based on module mode
    int i;
    for (i=0; i< messageIndex; i++) {
        if (memcmp(data, bar_diag_3c[i].data, 8) == 0) {
            //match found, prepare response
            memcpy(data_3d_response, bar_diag_3d[i].data, 8);
            ESP_LOGI(TAG, "Responding with 3D message %d", i);
            data_3d_valid = 1;
            return;
        }
    }
    for (; i< DIAG_MESG_NUM; i++) {
        if (memcmp(data, bar_diag_3c[i].data, 8) == 0) {
            //match found, prepare response
            ESP_LOGI(TAG, "Stale 3D message %d not responding", i);
            data_3d_valid = 0;
            return;
        }
    }
    ESP_LOGW(TAG, "No known response for 3C message 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X",  data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
    data_3d_valid = 0;
}

uint8_t bar_handle_truck_3d(uint8_t * data) {
    if (data_3d_valid) {
        memcpy(data, data_3d_response, 8);
        data_3d_valid = 0;
        return 1;
    }
    return 0;
}