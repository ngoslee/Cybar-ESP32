#ifndef _SYSTEM_H_
#define _SYSTEM_H_  

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    LOAD_MODE_OFF = 0,
    LOAD_MODE_LEFT = 1,
    LOAD_MODE_RIGHT = 2,
    LOAD_MODE_COMBO = 3,
    LOAD_MODE_NUM

} system_load_mode_enum_t;
void system_set_load_mode(system_load_mode_enum_t mode);
system_load_mode_enum_t system_get_load_mode(void);

typedef enum {
    LIN_MODE_MIM = 0, //full man in the middle, pass through
    LIN_MODE_LISTEN = 1, //act on messages, don't respond
    LIN_MODE_LOG = 2, //log from single LIN connection
    LIN_MODE_BAR = 3, //act as bar, no pass through
    LIN_MODE_NUM
} system_lin_mode_enum_t;

typedef enum {
    NODE_TYPE_WEB = 0, //LIN in and out, web interface
    NODE_TYPE_MODULE = 1, //LIN in and out, no web, mesh node
    NODE_TYPE_NUM
} system_node_type_enum_t;
system_node_type_enum_t system_get_node_type(void);
void system_set_lin_mode(system_lin_mode_enum_t mode);
system_lin_mode_enum_t system_get_lin_mode(void);

char * system_get_name(void);
void system_init(void);


#endif
