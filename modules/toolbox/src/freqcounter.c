#include "cads/toolbox/freqcounter.h"

void cads_freqcounter_init(cads_freqcounter_t* fc) {
    if(!fc) return;
    fc->last_capture = 0u;
    fc->have_last_capture = false;
    fc->missed_count = 0u;
    fc->period_count = 0u;
    fc->min_period_ticks = 0u;
    fc->max_period_ticks = 0u;
    fc->sum_period_ticks = 0u;
}

bool cads_freqcounter_capture(
    cads_freqcounter_t* fc, uint32_t capture, bool overcaptured, uint32_t* period_ticks) {
    if(!fc) return false;

    if(overcaptured) fc->missed_count++;

    if(!fc->have_last_capture || overcaptured) {
        fc->last_capture = capture;
        fc->have_last_capture = true;
        return false;
    }

    uint32_t period = capture - fc->last_capture; /* wraps correctly, unsigned */
    fc->last_capture = capture;

    if(fc->period_count == 0u) {
        fc->min_period_ticks = period;
        fc->max_period_ticks = period;
    } else {
        if(period < fc->min_period_ticks) fc->min_period_ticks = period;
        if(period > fc->max_period_ticks) fc->max_period_ticks = period;
    }
    fc->sum_period_ticks += period;
    fc->period_count++;

    if(period_ticks) *period_ticks = period;
    return true;
}

uint32_t cads_freqcounter_period_count(const cads_freqcounter_t* fc) {
    return fc ? fc->period_count : 0u;
}

uint32_t cads_freqcounter_missed_count(const cads_freqcounter_t* fc) {
    return fc ? fc->missed_count : 0u;
}

uint32_t cads_freqcounter_min_period_ticks(const cads_freqcounter_t* fc) {
    return fc ? fc->min_period_ticks : 0u;
}

uint32_t cads_freqcounter_max_period_ticks(const cads_freqcounter_t* fc) {
    return fc ? fc->max_period_ticks : 0u;
}

uint32_t cads_freqcounter_avg_period_ticks(const cads_freqcounter_t* fc) {
    if(!fc || fc->period_count == 0u) return 0u;
    return (uint32_t)(fc->sum_period_ticks / fc->period_count);
}
