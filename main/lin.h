#ifndef _LIN_H_
#define _LIN_H_
#include <stdio.h>
#include <string.h>
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"

//uint8_t lin_calc_checksum(uint8_t pid, uint8_t *data, size_t len);
//uint8_t lin_calc_pid(uint8_t id);
//static void lin_send_break(uint8_t port, uint8_t tx_pin, uint8_t rx_pin) ;
//void lin_send_header(uint8_t port, uint8_t id) ;

typedef struct {
    uint8_t uart;
    uint8_t tx_pin;
    uint8_t rx_pin;
    esp_timer_handle_t one_shot_timer;
} lin_port_t;
typedef struct {
    uint8_t id;
    uint8_t len;
    uint8_t * data;
} lin_msg_t;
void lin_tx_frame(lin_port_t port, lin_msg_t msg);
uint8_t lin_rx_frame(lin_port_t port, lin_msg_t msg);
void lin_init(lin_port_t * port);
uint8_t lin_calc_pid(uint8_t id);
uint8_t lin_calc_checksum(uint8_t pid, uint8_t *data, size_t len);

#endif