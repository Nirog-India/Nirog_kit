#ifndef _READING_
#define _READING_


typedef struct reading_tag{
    float heartrate;
    float oxygenLevel;
    float temperature;
}reading;

reading take_reading();


#endif
