#ifndef _LIN_BAR_H_
#define _LIN_BAR_H_

// Calculate LIN enhanced checksum (over PID + data)
typedef union {
    uint8_t bytes[8]; // 8-byte array for direct access
    struct {
        uint16_t value0 : 10; // First 10-bit value
        uint16_t value1 : 10; // Second 10-bit value
        uint16_t value2 : 10; // Third 10-bit value
        uint16_t value3 : 10; // Fourth 10-bit value
        uint16_t value4 : 10; // Fifth 10-bit value
        uint16_t value5 : 10; // Sixth 10-bit value
        uint16_t padding : 4; // Fill remaining 4 bits to reach 64 bits
    } __attribute__((packed)) values; // Packed to avoid padding
} __attribute__((packed)) lin_bar_command_t;


// Register RX callback for ID 0x0B
void lin_register_rx_callback(void (*callback)(uint8_t *data, size_t len)) ;
// Initialize UART and LIN
void bar_lin_init(void) ;
//void bar_lin_set_tx_data(uint16_t *data);
void bar_lin_truck_cmd(uint8_t * cmd) ;

void bar_handle_truck_3c(uint8_t * data);
uint8_t bar_handle_truck_3d(uint8_t * data);

#endif