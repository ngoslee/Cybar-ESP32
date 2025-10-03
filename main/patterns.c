#include <stdio.h>
#include <string.h>
#include "esp_mac.h"
#include "patterns.h"
#include "esp_log.h"

#define PATTERN_PERIOD 20 //MS
#define TAG "SEQUENCER"
static uint16_t seq_timer;
static seq_enum_t seq_active = SEQ_IDLE;
static uint16_t seq_last[6] = {0};

typedef struct {
    uint16_t duration;
    uint16_t values[6];
} sequence_step_t;


uint16_t sequenceActive(void)
{
    if (seq_active != SEQ_IDLE) return 1;
    return 0;
}
void sequenceSelect(seq_enum_t newSequence) {
    if ((newSequence < SEQ_COUNT) && (seq_active != newSequence)) {
        seq_timer = 0;
        seq_active = newSequence;
 //       ESP_LOGI(TAG, "sequence  %d", seq_active);
    }
}

void fadeLights(float factor, uint16_t * values) {
    for (int i = 0; i< 6; i++) {
        values[i] = factor * values[i];
    }
}

void sequenceNext(uint16_t * values, uint16_t * newValues) {
    uint8_t active;
    uint8_t retain = 0;
    static seq_enum_t seqPrev = SEQ_IDLE;
    int16_t position;
    switch (seq_active) {
        case SEQ_IDLE:
            if (seqPrev != SEQ_IDLE) {
                seq_timer = 0;
                seq_last[0] = seq_last[1] = seq_last[2] = seq_last[3] = seq_last[4] = seq_last[5] =  0;            
            } else {
                retain = 1;
                //no-op to allow truck to pass through
            }           
            break;
        case SEQ_KITT:
            position = seq_timer / ((600.0 / 6.0));
            //0 1 2 3 4 5 5 5 4 3 2 1 0 0
            switch (position)
            {
                case 0:
                case 11:
                    active = 0;
                    break;

                case 1:
                case 9:
                    active = 1;
                    break;

                case 2:
                case 8:
                    active = 2;
                    break;

                case 3:
                case 7:
                    active = 3;
                    break;

                case 4:
                case 6:
                    active = 4;
                    break;

                case 5:
                    active = 5;
                    break;

                default:
                    active = 0;
                    seq_timer =0;

            }
            fadeLights(0.4, seq_last);
            seq_last[active] = 100;            
            break;

        case SEQ_SWEEP:
            //split up 6 sections, 5 bands light based on interpolated value
            int16_t value = values[0];
            if (!value) value = values[1];
            position = (value  / 20);
            int16_t offset = (value % 20); //0 is max left, 19 is in min left
            seq_last[0] = seq_last[1] = seq_last[2] = seq_last[3] = seq_last[4] = seq_last[5] =  0;            
            seq_last[5 - position] = 100 - 5*offset;
            if ((5 - (position+1) >=0) && (5-(position+1) <= 5)) seq_last[5-(position + 1)] = 5*offset;
            break;

        case SEQ_WIG_WAG:
            seq_last[0] = seq_last[1] = seq_last[2] = seq_last[3] = seq_last[4] = seq_last[5] =  0;            

            if (seq_timer >= 400) seq_timer = 0;
            if (seq_timer < 200) 
            {
                seq_last[0] = 100;
            } else {
                seq_last[5] = 100;
            }
            break;

        default:
            break;
    }
    seqPrev = seq_active;
    if (!retain) {
        memcpy(newValues, seq_last, sizeof(seq_last));
    } else {
        memcpy(newValues, values, sizeof(seq_last));
    }
    seq_timer += PATTERN_PERIOD;
}

