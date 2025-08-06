// Calculate LIN enhanced checksum (over PID + data)


// Register RX callback for ID 0x0B
void lin_register_rx_callback(void (*callback)(uint8_t *data, size_t len)) ;
// Initialize UART and LIN
void bar_lin_init(void) ;
//void bar_lin_set_tx_data(uint16_t *data);