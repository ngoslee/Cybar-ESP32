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
#define LIN_UART_PORT UART_NUM_1
#define LIN_TX_PIN GPIO_NUM_4
#define LIN_RX_PIN GPIO_NUM_5
#define LIN_BAUD_RATE 19200
#define LIN_BREAK_DURATION_US 768 // 14 bits at 19200 baud = 0.729 ms

#define LIN_BREAK_BAUD 9600 // For 14-bit break at 19200 baud
#define LIN_MSG_INTERVAL_MS 20
#define LIN_TX_ID 0x0A // 8-byte TX
#define LIN_RX_ID 0x0B // 5-byte RX
#define LIN_BREAK_BITS 14
#define LIN_TX_DATA_LEN 8
#define LIN_RX_DATA_LEN 5
#define UART_BUF_SIZE 256

#define CONFIG_LIN_TRANSCEIVER_ECHO 1

static const char *TAG = "LIN_MASTER";
static uint8_t tx_data[LIN_TX_DATA_LEN] = {0}; // Buffer for TX data
static void (*rx_callback)(uint8_t *data, size_t len) = NULL;

#if 0
// Calculate LIN enhanced checksum (over PID + data)
static uint8_t lin_calc_checksum(uint8_t pid, uint8_t *data, size_t len) {
    uint16_t sum = pid;
    for (size_t i = 0; i < len; i++) {
        sum += data[i];
        if (sum >= 256) sum -= 255;
    }
    return (uint8_t)(~sum);
}

// Calculate PID from frame ID
static uint8_t lin_calc_pid(uint8_t id) {
    uint8_t p0 = ((id >> 0) & 1) ^ ((id >> 1) & 1) ^ ((id >> 2) & 1) ^ ((id >> 4) & 1);
    uint8_t p1 = ~(((id >> 1) & 1) ^ ((id >> 3) & 1) ^ ((id >> 4) & 1) ^ ((id >> 5) & 1));
    return (id & 0x3F) | (p0 << 6) | (p1 << 7);
}


// Send LIN break by driving TX pin low
static void lin_send_break(void) {
 //   ESP_LOGI(TAG, "Starting LIN break");

    // Disable UART TX signal to avoid interference
    ESP_ERROR_CHECK(uart_set_pin(LIN_UART_PORT, -1, LIN_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    // Configure TX pin as GPIO output
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LIN_TX_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    
    // Ensure idle high first
 //   gpio_set_level(LIN_TX_PIN, 1);
 //   esp_rom_delay_us(100); // Short delay for stability
    
    // Drive TX pin low for break duration
    gpio_set_level(LIN_TX_PIN, 0);
    esp_rom_delay_us(LIN_BREAK_DURATION_US);
    
    // Return to high (idle/delimiter)
    gpio_set_level(LIN_TX_PIN, 1);
 //   esp_rom_delay_us(100); // Short delimiter ensure

    // Restore UART TX pin
    ESP_ERROR_CHECK(uart_set_pin(LIN_UART_PORT, LIN_TX_PIN, LIN_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    
    // Wait for UART to stabilize
    esp_rom_delay_us(100);
    vTaskDelay(pdMS_TO_TICKS(1));
    
 //   ESP_LOGI(TAG, "LIN break completed");
}


// Send LIN frame header (break + sync + PID)
static void lin_send_header(uint8_t id) {
    uint8_t sync = 0x55;
    uint8_t pid = lin_calc_pid(id);
    lin_send_break();
    uart_write_bytes(LIN_UART_PORT, &sync, 1);
    uart_write_bytes(LIN_UART_PORT, &pid, 1);
    uart_flush(LIN_UART_PORT); // Clear RX buffer
}

// LIN TX task (ID 0x0A, 8 bytes)
static void lin_tx_frame(void) {
    lin_send_header(LIN_TX_ID);
    uart_write_bytes(LIN_UART_PORT, tx_data, LIN_TX_DATA_LEN);
    uint8_t checksum = lin_calc_checksum(lin_calc_pid(LIN_TX_ID), tx_data, LIN_TX_DATA_LEN);
    uart_write_bytes(LIN_UART_PORT, &checksum, 1);
    #ifdef CONFIG_LIN_TRANSCEIVER_ECHO
        // Read back echo (header + data + checksum)
        uint8_t echo_buf[1 + 1 + 1 + LIN_TX_DATA_LEN + 1];
        uart_read_bytes(LIN_UART_PORT, echo_buf, sizeof(echo_buf), pdMS_TO_TICKS(10));
    #endif
}

// LIN RX task (ID 0x0B, 5 bytes)
static void lin_rx_frame(void) {
    lin_send_header(LIN_RX_ID);
    uint8_t rx_buf[LIN_RX_DATA_LEN + 1]; // Data + checksum
    int len = uart_read_bytes(LIN_UART_PORT, rx_buf, sizeof(rx_buf), pdMS_TO_TICKS(4));
    if (len == LIN_RX_DATA_LEN + 1) {
        uint8_t checksum = lin_calc_checksum(lin_calc_pid(LIN_RX_ID), rx_buf, LIN_RX_DATA_LEN);
        if (checksum == rx_buf[LIN_RX_DATA_LEN]) {
            if (rx_callback) {
                rx_callback(rx_buf, LIN_RX_DATA_LEN);
            }
        } else {
            ESP_LOGE(TAG, "RX checksum error");
        }
    } else {
        ESP_LOGE(TAG, "RX timeout or wrong length: %d", len);
    }
}
#endif
static lin_port_t bar_lin_port = { .uart = 1,
                                    .tx_pin = 4,
                                    .rx_pin = 5,
                                };

static uint8_t rx_data[8]; //max length
// Periodic LIN task
static void bar_lin_task(void *arg) {
    lin_msg_t bar_tx_msg = {.id = LIN_TX_ID,
                        .len = LIN_TX_DATA_LEN,
                        .data = tx_data,
    };



    lin_msg_t bar_rx_msg = {.id = LIN_RX_ID,
                        .len = LIN_RX_DATA_LEN,
                        .data = rx_data,
    };

    while (1) {
        lin_tx_frame(bar_lin_port, bar_tx_msg);
        vTaskDelay(pdMS_TO_TICKS(LIN_MSG_INTERVAL_MS / 2));
        lin_rx_frame(bar_lin_port, bar_rx_msg);
        vTaskDelay(pdMS_TO_TICKS(LIN_MSG_INTERVAL_MS / 2));
    }
}

// Set TX data for ID 0x0A
void lin_set_tx_data(uint8_t *data, size_t len) {
    if (len <= LIN_TX_DATA_LEN) {
        memcpy(tx_data, data, len);
    } else {
        ESP_LOGE(TAG, "TX data too long");
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