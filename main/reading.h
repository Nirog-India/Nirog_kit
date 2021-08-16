#ifndef _READING_
#define _READING_


typedef struct reading_tag{
    float heartrate;
    float heart_precision;
    float oxygenLevel;
    float oxy_precision;
    float temperature;
}reading;

reading take_reading();


#endif
