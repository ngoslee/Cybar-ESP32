#include "system.h"
static system_load_mode_enum_t load_mode = LOAD_MODE_OFF;
static system_lin_mode_enum_t lin_mode = LIN_MODE_MIM;

void system_set_load_mode(system_load_mode_enum_t mode)
{
    // Just a stub for now
    if (mode > LOAD_MODE_NUM) mode = LOAD_MODE_OFF;
    load_mode = mode;
}

system_load_mode_enum_t system_get_load_mode(void)
{
    return load_mode;
}

void system_set_lin_mode(system_lin_mode_enum_t mode)
{
    // Just a stub for now
    if (mode > LIN_MODE_NUM) mode = LIN_MODE_MIM;
    lin_mode = mode;
}
system_lin_mode_enum_t system_get_lin_mode(void)
{
    return lin_mode;
}