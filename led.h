#ifndef  LED_H
#define LED_H

#include <stdbool.h>

/* LED types */
enum LedColor {
  LED_WHITE = 0,
  LED_BLUE = 1,
  LED_GREEN = 2,
  LED_RED = 3
};

void led_init(void);

void led_control(enum LedColor color, bool on);

#endif