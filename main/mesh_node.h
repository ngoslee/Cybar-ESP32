#ifndef _MESH_NODE_H_
#define _MESH_NODE_H_  


#include <stdio.h>
#include <string.h>
#include "esp_mac.h"

void mesh_node_init(void);
uint8_t mesh_mode_is_lin(void);
void mesh_get_command(uint8_t * cmd);


#endif
