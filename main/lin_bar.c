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
#include "user.h"
#include "patterns.h"
#include "lin_truck.h"
#include "pinout.h"
#include "hardware.h"

#define BAR_LIN_UART_PORT UART_NUM_1

#define LIN_BAUD_RATE 19200
#define LIN_BREAK_DURATION_US 768 // 14 bits at 19200 baud = 0.729 ms

#define LIN_MSG_INTERVAL_MS 20
#define UART_BUF_SIZE 256

void bar_lin_set_tx_data(uint16_t *data, uint8_t * msg);
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
        .id = TRUCK_TO_BAR_ID,
        .len = TRUCK_TO_BAR_DATA_LEN,
        .data = tx_data_shadow,
    };

static lin_msg_t bar_rx_msg = {
        .id = BAR_TO_TRUCK_ID,
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

// Periodic LIN task
static void bar_lin_task(void *arg) {
    static uint16_t newValues[6];
    uint16_t values_final[6];


    while (1) {
        truck_input(newValues); //updates if truck sent value
        update_user_input(newValues); //updates if user sent value

        sequenceNext(newValues, values_final); //overrides if mode active

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