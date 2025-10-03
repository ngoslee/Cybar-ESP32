#ifndef _USER_H_
#define _USER_H_
#include <stdio.h>
#include <string.h>

void handle_user_input(char * data, uint16_t len);
int16_t  update_user_input(void);
void user_get_command(uint8_t * data);

#endif