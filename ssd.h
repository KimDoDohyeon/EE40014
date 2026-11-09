#ifndef SSD_H
#define SSD_H

#include <stdbool.h>
#include <stdint.h>
#include "sdk_errors.h"

enum display {
    LOGO = 0,
    CLOCK = 1,
    NORMAL = 2,
    INV = 3
};

extern int8_t current_display;

// initialize external twi for ssd-1306
void external_twi_init(void);

// initialize ssd-1306
bool ssd_init(void);

// write command to ssd-1306
ret_code_t ssd_write_commands( const uint8_t *commands,uint8_t length);

//write time on display
void display_time(uint8_t hour, uint8_t min, uint8_t sec);

//change time of display(use only page 3,4)
void display_time_partial( uint8_t hour, uint8_t min, uint8_t sec);

//change display(normal, inverted, clock, logo)
void display_control(enum display mode);


#endif /* SSD_H */