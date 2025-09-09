#include <stdio.h>
#include <string.h>
#include "driver/uart.h"
#include "lin_bar.h"
#include "patterns.h"
#include "hardware.h"
#define USER_BUFFER_SIZE 255

static uint8_t userInput[USER_BUFFER_SIZE+1];
static volatile uint8_t userInputFlag = 0;

void handle_user_input(char * data, uint16_t len)
{
    if (userInputFlag)
    {
        char msg[]="Overrun\n";
        uart_write_bytes(UART_NUM_0, msg, strlen(msg));
        return;
    }
    if (len > USER_BUFFER_SIZE)
    {
        memcpy(userInput, data, USER_BUFFER_SIZE);
        userInput[USER_BUFFER_SIZE] = 0;
    }
    else
    {
        memcpy(userInput, data, len);
        userInput[len]=0;
    }
    userInputFlag = 1;

}

int16_t  update_user_input(uint16_t * newValues)
{
    uint8_t chunks = 0;
 //   char outStr[255];
    char c;
    uint8_t chunk = 0;
    int i =0;
    uint16_t value = 0;
    uint16_t values[6] = {0, 0, 0, 0, 0, 0};
    static uint16_t user_prev[6];
    if (userInputFlag == 0) return 1;
    
    while ((i < USER_BUFFER_SIZE) && userInput[i] && (chunks < 6))
    {
        c = userInput[i++];
        if (c>='0' && c<='9')
        {
            value *= 10;
            value += c - '0';
            chunk = 1; //in chunk
        }
        else
        {
            if (chunk == 1)
            {
                values[chunks++]=value;
                value = 0;
                chunk = 0;
            }
        }

    }
    if (chunk == 1) //no end whitespace
    {
        values[chunks++]=value;
        value = 0;
        chunk = 0;
    }
    if (chunks != 6)
    {
        if (strcmp((char*)userInput, "kitt") == 0) {
            sequenceSelect(SEQ_KITT);
            uart_write_bytes(UART_NUM_0, "KITT MODE", 9);

        } else if (strcmp((char*)userInput, "off") == 0 ) {
            sequenceSelect(SEQ_IDLE);
            uart_write_bytes(UART_NUM_0, "IDLE MODE", 9);
        } else if ((strncmp((char*)userInput, "load", 4) == 0 ) && (chunks == 1)) {
            hardawre_load_set_states(values[0]);
            uart_write_bytes(UART_NUM_0, "Load", 4);
        }
        else {
                    uart_write_bytes(UART_NUM_0, userInput, strlen((char*)userInput));
        }
        userInputFlag = 0;

        return -1;
    }
    //update on change
    userInputFlag = 0;
    if (memcpy(user_prev, values, sizeof(values))) {
        memcpy(user_prev, values, sizeof(values));
        memcpy(newValues, values, sizeof(values));

    }
    return 0;
//    bar_lin_set_tx_data(values);
    #if 0
    for (int i =0; i<6; i++)
    {
        char str[]="Parsed input\n";
//        int l = sprintf(outStr, "Value %d is %d\n", i, values[i]);
 //       uart_write_bytes(UART_NUM_0, outStr, l);
  //      uart_write_bytes(UART_NUM_0, str, strlen(str));
    }
  #endif
}