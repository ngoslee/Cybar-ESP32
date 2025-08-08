#ifndef _PATTERNS_H_
#define _PATTERNS_H_

#include <stdio.h>
#include <string.h>
#include "esp_mac.h"
typedef enum {
    SEQ_IDLE =0,
    SEQ_KITT,
    SEQ_LEFT,
    SEQ_RIGHT,
    SEQ_SWEEP,
    SEQ_COUNT,
} seq_enum_t;

void sequenceNext(uint16_t * values);
void sequenceSelect(seq_enum_t newSequence);


#endif