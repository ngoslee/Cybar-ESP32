#include <stdio.h>
#include <string.h>
#include "esp_mac.h"
#include "patterns.h"

#define PATTERN_PERIOD 20 //MS
static uint16_t seq_timer;
static seq_enum_t seq_active = SEQ_IDLE;
static uint16_t seq_last[6] = {0};

typedef struct {
    uint16_t duration;
    uint16_t values[6];
} sequence_step_t;


void sequenceSelect(seq_enum_t newSequence) {
    if (newSequence < SEQ_COUNT) {
        seq_timer = 0;
        seq_active = newSequence;
    }
}

void fadeLights(float factor, uint16_t * values) {
    for (int i = 0; i< 6; i++) {
        values[i] = factor * values[i];
    }
}

void sequenceNext(uint16_t * values) {
    uint8_t active;
    switch (seq_active) {
        case SEQ_IDLE:
            seq_timer = 0;
            seq_last[0] = seq_last[1] = seq_last[2] = seq_last[3] = seq_last[4] = seq_last[5] =  0;
            return; //don't update values
            break;
        case SEQ_KITT:
            int16_t position = seq_timer / ((700.0 / 6.0));
            //0 1 2 3 4 5 5 5 4 3 2 1 0 0
            switch (position)
            {
                case 0:
                case 12:
                case 13:
                    active = 0;
                    break;

                case 1:
                case 11:
                    active = 1;
                    break;

                case 2:
                case 10:
                    active = 2;
                    break;

                case 3:
                case 9:
                    active = 3;
                    break;

                case 4:
                case 8:
                    active = 4;
                    break;

                case 5:
                case 6:
                case 7:
                    active = 5;
                    break;

                default:
                    active = 0;
                    seq_timer =0;

            }
            fadeLights(0.6, seq_last);
            seq_last[active] = 100;            
            break;
        default:
            break;
    }
    memcpy(values, seq_last, sizeof(seq_last));
    seq_timer += PATTERN_PERIOD;
}

