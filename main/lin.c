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

#define LIN_BAUD_RATE 19200
#define LIN_BREAK_DURATION_US 768 // 14 bits at 19200 baud = 0.729 ms

#define LIN_MSG_INTERVAL_MS 20

#define LIN_BREAK_BITS 14

#define UART_BUF_SIZE 256

#define CONFIG_LIN_TRANSCEIVER_ECHO 1

static const char *TAG = "LIN_MASTER";

// Calculate LIN enhanced checksum (over PID + data)
uint8_t lin_calc_checksum(uint8_t pid, uint8_t *data, size_t len) {
    uint16_t sum = pid;
    for (size_t i = 0; i < len; i++) {
        sum += data[i];
        if (sum >= 256) sum -= 255;
    }
    return (uint8_t)(~sum);
}

// Calculate PID from frame ID
uint8_t lin_calc_pid(uint8_t id) {
    uint8_t p0 = ((id >> 0) & 1) ^ ((id >> 1) & 1) ^ ((id >> 2) & 1) ^ ((id >> 4) & 1);
    uint8_t p1 = ~(((id >> 1) & 1) ^ ((id >> 3) & 1) ^ ((id >> 4) & 1) ^ ((id >> 5) & 1));
    return (id & 0x3F) | (p0 << 6) | (p1 << 7);
}


// Send LIN break by driving TX pin low
void lin_send_break(lin_port_t port) {
 //   ESP_LOGI(TAG, "Starting LIN break");

    // Disable UART TX signal to avoid interference
    ESP_ERROR_CHECK(uart_set_pin(port.uart, -1, port.rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    // Configure TX pin as GPIO output
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << port.tx_pin),
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
    gpio_set_level(port.tx_pin, 0);
    esp_rom_delay_us(LIN_BREAK_DURATION_US);
    
    // Return to high (idle/delimiter)
    gpio_set_level(port.tx_pin, 1);
 //   esp_rom_delay_us(100); // Short delimiter ensure

    // Restore UART TX pin
    ESP_ERROR_CHECK(uart_set_pin(port.uart, port.tx_pin, port.rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    
    // Wait for UART to stabilize
    esp_rom_delay_us(100);
 //   vTaskDelay(pdMS_TO_TICKS(1));
    
 //   ESP_LOGI(TAG, "LIN break completed");
}


// Send LIN frame header (break + sync + PID)
void lin_send_header(lin_port_t port, uint8_t id) {
    uint8_t sync = 0x55;
    uint8_t pid = lin_calc_pid(id);
    lin_send_break(port);
    uart_write_bytes(port.uart, &sync, 1);
    uart_write_bytes(port.uart, &pid, 1);
    uart_flush(port.uart); // Clear RX buffer
}

// LIN TX task (ID 0x0A, 8 bytes)
void lin_tx_frame(lin_port_t port, lin_msg_t msg) {
    lin_send_header(port, msg.id);
    uart_write_bytes(port.uart, msg.data, msg.len);
    uint8_t checksum = lin_calc_checksum(lin_calc_pid(msg.id), msg.data, msg.len);
    uart_write_bytes(port.uart, &checksum, 1);
    #ifdef CONFIG_LIN_TRANSCEIVER_ECHO
        // Read back echo (header + data + checksum)
        uint8_t echo_buf[1 + 1 + 1 + msg.len + 1];
        uart_read_bytes(port.uart, echo_buf, sizeof(echo_buf), pdMS_TO_TICKS(10));
    #endif
}

// LIN RX task (ID 0x0B, 5 bytes)
uint8_t lin_rx_frame(lin_port_t port, lin_msg_t msg) {
    lin_send_header(port, msg.id);
    uint8_t rx_buf[msg.len + 1]; // Data + checksum
    int len = uart_read_bytes(port.uart, rx_buf, msg.len+1, pdMS_TO_TICKS(4));
    if (len == msg.len + 1) {
        uint8_t checksum = lin_calc_checksum(lin_calc_pid(msg.id), rx_buf, len);
        if (checksum == rx_buf[len]) {
            return len;
        } else {
            ESP_LOGE(TAG, "RX checksum error");
            return 0;
        }
    } else {
 //       ESP_LOGE(TAG, "RX timeout or wrong length: %d", len);
        return 0;
    }
}

// Initialize UART and LIN
void lin_init(lin_port_t port) {
    uart_config_t uart_config = {
        .baud_rate = LIN_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(port.uart, UART_BUF_SIZE, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(port.uart, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(port.uart, port.tx_pin, port.rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}