#ifndef _WEB_MESH_H_
#define _WEB_MESH_H_   
#include <stdio.h>
#include <stdbool.h>
#include "lin_bar.h"

void web_mesh_init(void);

void mesh_update(bool override, lin_bar_command_t * newValues);

#endif
