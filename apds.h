#ifndef APDS_H
#define APDS_H

#include <stdbool.h>
#include <stdint.h>
#include "sdk_errors.h"

/* Prototypes */
enum gesture{
  GESTURE_NONE = -1,
  GESTURE_UP = 0,
  GESTURE_DOWN = 1,
  GESTURE_LEFT = 2,
  GESTURE_RIGHT = 3
};

#define APDS_REG_STATUS     0x93U

/* twi initialization. */
void twi_init(void);

bool apds_init(void);
bool apds_proximity_mode(void);
bool apds_gesture_mode(void);

ret_code_t apds_read_data(
  uint8_t reg, 
  uint8_t *data, 
  uint8_t length
);

int handleGesture(void);
int readGesture(void);

#endif