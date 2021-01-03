#ifndef _HEARTRATE_
#define _HEARTRATE_

#include <stdio.h>
struct oxy_readingTAG;

typedef struct oxy_readingTAG{
    float finalheartRate;
    float oxygenLevel;
}oxy_reading;

oxy_reading get_oxy_result();


#endif 
