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
#define BAR_LIN_UART_PORT UART_NUM_1
#define BAR_LIN_TX_PIN GPIO_NUM_4
#define BAR_LIN_RX_PIN GPIO_NUM_5
#define LIN_BAUD_RATE 19200
#define LIN_BREAK_DURATION_US 768 // 14 bits at 19200 baud = 0.729 ms

#define LIN_MSG_INTERVAL_MS 20
#define BAR_LIN_TX_ID 0x0A // 8-byte TX
#define BAR_LIN_RX_ID 0x0B // 5-byte RX

#define BAR_LIN_TX_DATA_LEN 8
#define BAR_LIN_RX_DATA_LEN 5
#define UART_BUF_SIZE 256

void bar_lin_set_tx_data(uint16_t *data, uint8_t * msg);
static const char *TAG = "LIN_BAR";
static uint8_t tx_data[BAR_LIN_TX_DATA_LEN] = {0}; // Buffer for TX data
static uint8_t tx_data_shadow[BAR_LIN_TX_DATA_LEN] = {0}; // Buffer for TX data

static void (*rx_callback)(uint8_t *data, size_t len) = NULL;

static volatile uint8_t lin_tx_data_updated = 0;

static lin_port_t bar_lin_port = { .uart = BAR_LIN_UART_PORT,
                                    .tx_pin = BAR_LIN_TX_PIN,
                                    .rx_pin = BAR_LIN_RX_PIN,
                                };

static uint8_t rx_data[8]; //max length
static lin_msg_t bar_tx_msg = {
        .id = BAR_LIN_TX_ID,
        .len = BAR_LIN_TX_DATA_LEN,
        .data = tx_data_shadow,
    };

static lin_msg_t bar_rx_msg = {
        .id = BAR_LIN_RX_ID,
        .len = BAR_LIN_RX_DATA_LEN,
        .data = rx_data,
    };  

// Periodic LIN task
static void bar_lin_task(void *arg) {
    uint16_t newValues[6];

    while (1) {
        if (update_user_input(newValues) == 0)
        {
            bar_lin_set_tx_data(newValues, tx_data_shadow);
        }
        lin_tx_frame(bar_lin_port, bar_tx_msg);
        vTaskDelay(pdMS_TO_TICKS(LIN_MSG_INTERVAL_MS / 2));
        lin_rx_frame(bar_lin_port, bar_rx_msg);
        vTaskDelay(pdMS_TO_TICKS(LIN_MSG_INTERVAL_MS / 2));
    }
}

// Set TX data for ID 0x0A
void bar_lin_set_tx_data(uint16_t *data, uint8_t * msg) {

    uint8_t i, v;
    uint16_t in_mask, bitcount;
    bitcount = 0;

    //format is value every 10 bits
    for (i=0; i< BAR_LIN_TX_DATA_LEN; i++)
    {
        msg[i] = 0;
    }
    for (v=0; v< 6; v++)
    {
        vTaskDelay(1 / portTICK_PERIOD_MS);
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
    ESP_ERROR_CHECK(uart_set_pin(bar_lin_port.uart, bar_lin_port.tx_pin, bar_lin_port.rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    xTaskCreate(bar_lin_task, "bar_lin_task", 4096, NULL, 10, NULL);
}