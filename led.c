#include "led.h"
#include "boards.h"

void led_init(void)
{
  bsp_board_init(BSP_INIT_LEDS);
}

/* turn on or turn off the LED */
void led_control(enum LedColor Color, bool on)
{
  switch (Color) {
    
    case LED_WHITE:
      if (on) {
        bsp_board_led_on(1);
        bsp_board_led_on(2);
        bsp_board_led_on(3);
      }
      else {
        bsp_board_led_off(1);
        bsp_board_led_off(2);
        bsp_board_led_off(3);
      }
      break;
    
    case LED_BLUE:
      if (on) 
        bsp_board_led_on(1);
      else 
        bsp_board_led_off(1);
      break;
    
    case LED_GREEN:
      if (on) 
        bsp_board_led_on(2);
      else 
        bsp_board_led_off(2);
      break;
    
    case LED_RED:
      if (on) 
        bsp_board_led_on(3);
      else 
        bsp_board_led_off(3);
      break;
	
    default:
      break;
  }
  
}