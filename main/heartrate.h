#ifndef _HEARTRATE_
#define _HEARTRATE_

#include <stdio.h>
#include <stdbool.h>
struct oxy_readingTAG;

typedef struct oxy_readingTAG{
    int finalheartRate;
    float heart_error;
    float oxygenLevel;
    float oxy_error;
}oxy_reading;
bool isTaskCompleted;
extern bool finger_not_placed;
void init_heartrate();
void deinit_heartrate();
void take_oxy_reading();
oxy_reading get_oxy_result();


#endif 
