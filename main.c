/* embeded system EE40014 lab 3 team08 */

#include <stdio.h>
#include <stdbool.h>
#include "boards.h"
#include "app_util_platform.h"
#include "app_error.h"
#include "nrf_drv_twi.h"
#include "nrf_delay.h"
#include "nrf_gpio.h"
#include "nrf_drv_timer.h"
#include "nrf_twi_mngr.h"
#include "nrf_drv_gpiote.h"

#include "apds.h"
#include "ssd.h"
#include "led.h"

/* log header */
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

/* Pin number from lab1 guide */
#define PIN_VDD_ENV         22
#define PIN_R_PULLUP        32

#define LED_TIMEOUT_MS          500U
#define GESTURE_TIMEOUT_MS      3000U

#define DISPLAY_TIMEOUT_TICKS   5U

typedef enum {
  PROXIMITY,
  GESTURE
} State;

/* TIMER instance */
// timer that counts for 1s
static const nrf_drv_timer_t timer0 = NRF_DRV_TIMER_INSTANCE(0);
// timer1 for led timeout and APDS sensor timeout
static const nrf_drv_timer_t timer1 = NRF_DRV_TIMER_INSTANCE(1);

/* Global variable for 1s Timer interrupt */
volatile uint32_t total_sec = 0;
volatile bool timer_update_flag = false;

volatile uint32_t led_started_ticks = 0;
volatile bool is_led_on = false;

// display off when this timer >= 5
volatile uint32_t display_global_timer = 0;
volatile bool is_display_on = false;

volatile uint32_t gesture_activity_ticks = 0;

volatile State current_state = PROXIMITY;
volatile bool proximity_interrupt_flag = false;

#define PIN_IN NRF_GPIO_PIN_MAP(0,19)

void in_pin_handler(nrf_drv_gpiote_pin_t pin, nrf_gpiote_polarity_t action)
{
  (void)action;

  if (pin == PIN_IN) {
    proximity_interrupt_flag = true;
  }
}

/**
 * @brief Function for configuring: PIN_IN pin for input, PIN_OUT pin for output,
 * and configures GPIOTE to give an interrupt on pin change.
 */
static void gpio_init(void)
{
    ret_code_t err_code;

    err_code = nrf_drv_gpiote_init();
    APP_ERROR_CHECK(err_code);

    nrf_drv_gpiote_in_config_t in_config = GPIOTE_CONFIG_IN_SENSE_HITOLO(true);
    in_config.pull = NRF_GPIO_PIN_PULLUP;

    err_code = nrf_drv_gpiote_in_init(PIN_IN, &in_config, in_pin_handler);
    APP_ERROR_CHECK(err_code);

    nrf_drv_gpiote_in_event_enable(PIN_IN, true);
}


void timer_interrupt_handler(nrf_timer_event_t event_type, void* p_context)
{
  (void)p_context;

  if (event_type != NRF_TIMER_EVENT_COMPARE0) 
    return;

  total_sec++;
  
  if (is_display_on && display_global_timer < DISPLAY_TIMEOUT_TICKS) 
    display_global_timer++;

  timer_update_flag = true;

}

/* TIMER0 initialization */
static void timer0_init (void)
{
  uint32_t time_ms = 1000; //Time(in miliseconds) between consecutive compare events.
  uint32_t time_ticks;

  nrf_drv_timer_config_t config = NRF_DRV_TIMER_DEFAULT_CONFIG;

  // Set TIMER1 1 MHz, 32-bit
  config.frequency = NRF_TIMER_FREQ_1MHz;
  config.mode      = NRF_TIMER_MODE_TIMER;
  config.bit_width = NRF_TIMER_BIT_WIDTH_32;
  
  APP_ERROR_CHECK(
      nrf_drv_timer_init(&timer0, &config, timer_interrupt_handler)
  );
  time_ticks = nrf_drv_timer_ms_to_ticks(&timer0, time_ms);

  nrf_drv_timer_extended_compare(
         &timer0, NRF_TIMER_CC_CHANNEL0, time_ticks, NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK, true);

  nrf_drv_timer_enable(&timer0);

}

/* Fake timer handler */
static void timer1_handler (nrf_timer_event_t event, void *context)
{
  (void)event;
  (void)context;
}

static void timer1_init(void)
{
  nrf_drv_timer_config_t config = NRF_DRV_TIMER_DEFAULT_CONFIG;

  // Set TIMER1 1 MHz, 32-bit
  config.frequency = NRF_TIMER_FREQ_1MHz;
  config.mode      = NRF_TIMER_MODE_TIMER;
  config.bit_width = NRF_TIMER_BIT_WIDTH_32;

  APP_ERROR_CHECK(nrf_drv_timer_init(&timer1, &config, timer1_handler));

  nrf_drv_timer_clear(&timer1);
  nrf_drv_timer_enable(&timer1);
}

static uint32_t timer1_now(void)
{
  return nrf_drv_timer_capture(&timer1, NRF_TIMER_CC_CHANNEL0);
}

static void refresh_clock(void)
{
    uint32_t now = total_sec;

    uint8_t hour = (now / 3600U) % 24U;
    uint8_t min  = (now / 60U) % 60U;
    uint8_t sec  = now % 60U;

    display_time_partial(hour, min, sec);
}

/* Gesture detected. Turn on peripherals. */
void on_gesture(int gesture)
{
  const uint8_t commands_addr[] = {
      0x20, 0x00,        // Horizontal addressing
      0x21, 0x00, 0x7F,  // Column: 0~127(full)
      0x22, 0x00, 0x07   // Page: 0~7(full)
  };

  //write addr commands on ssd
  ssd_write_commands(commands_addr, sizeof(commands_addr));

  // LED on
  led_control(LED_WHITE, false);

  led_started_ticks = timer1_now();
  is_led_on = true;

  switch (gesture) {
    case GESTURE_UP:
      led_control(LED_RED, true);
      display_control(INV);
      break;

    case GESTURE_DOWN:
      led_control(LED_BLUE, true);
      display_control(NORMAL);
      break;

    case GESTURE_LEFT:
      current_display = LOGO;
      led_control(LED_GREEN, true);
      display_control(LOGO);
      break;

    case GESTURE_RIGHT:
      current_display = CLOCK;
      led_control(LED_WHITE, true);
      display_control(CLOCK);
      break;

    default:
      break;
  }
}


/**
 * @brief Function for main application entry.
 */
int main(void)
{
  /********************************************/
  /* All the initialization should be done here */
  
  // 로그 생성 및 출력 기능 준비
  APP_ERROR_CHECK(NRF_LOG_INIT(NULL));
  NRF_LOG_DEFAULT_BACKENDS_INIT();
  NRF_LOG_INFO("embedded lab program started.");
  NRF_LOG_FLUSH();
  
  // LED 출력 준비, LED GPIO를 출력으로 초기화
  led_init();

  // 센서 전원과 풀업 회로를 활성화
  nrf_gpio_pin_set(PIN_VDD_ENV);
  nrf_gpio_pin_set(PIN_R_PULLUP);
  // Power up and wait for voltage to rise
  nrf_gpio_cfg(PIN_VDD_ENV,NRF_GPIO_PIN_DIR_OUTPUT,
  NRF_GPIO_PIN_INPUT_DISCONNECT,NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_S0H1,
  NRF_GPIO_PIN_NOSENSE);
  nrf_gpio_cfg(PIN_R_PULLUP,NRF_GPIO_PIN_DIR_OUTPUT,
  NRF_GPIO_PIN_INPUT_DISCONNECT,NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_S0H1,
  NRF_GPIO_PIN_NOSENSE);
  nrf_delay_ms(10);

  // Internal I2C bus
  twi_init();

  // External I2C bus
  external_twi_init();
  NRF_LOG_INFO("External TWI manager initialized");
  NRF_LOG_FLUSH();

  // initialize APDS-9960 sensor
  while (!apds_init()){
    NRF_LOG_INFO("APDS-9960 initialization failed");
    NRF_LOG_FLUSH();
    nrf_delay_ms(1000);
  }
  NRF_LOG_INFO("APDS-9960 initialized");
  NRF_LOG_FLUSH();

  // APDS interrupt gpio init
  gpio_init();

  // initialize SSD-1306 display
  while (!ssd_init()){
    NRF_LOG_INFO("SSD-1306 initialization failed");
    NRF_LOG_FLUSH();
    nrf_delay_ms(1000);
  }
  NRF_LOG_INFO("SSD-1306 initialized");
  NRF_LOG_FLUSH();

  while (!apds_proximity_mode()){
    NRF_LOG_INFO("APDS-9960 proximity mode failed");
    NRF_LOG_FLUSH();
    nrf_delay_ms(1000);
  }

  is_display_on = true;
  is_led_on = false;
  current_state = PROXIMITY;

  display_global_timer = 0;
  timer_update_flag = false;
  
  //set initial time
  int initial_hour = 12;
  int initial_min = 34;
  int initial_sec = 56;
  total_sec = 3600U * initial_hour + 60 * initial_min + initial_sec;

  // timer initialization
  timer1_init();
  timer0_init();

  const uint32_t led_timeout_ticks = 
    nrf_drv_timer_ms_to_ticks(
      &timer1, 
      LED_TIMEOUT_MS
    );
  
  const uint32_t gesture_timeout_ticks = 
    nrf_drv_timer_ms_to_ticks(
      &timer1, 
      GESTURE_TIMEOUT_MS
    );

  /**********************************************/

  while(true) {
    
    switch (current_state) {
      
      // 담당 기능: 근접 이벤트 -> 제스처 모드 전환
      case PROXIMITY:{
        bool pending;

        CRITICAL_REGION_ENTER();
        pending = proximity_interrupt_flag;
        proximity_interrupt_flag = false;
        CRITICAL_REGION_EXIT();

        // proximity interrupt 발생했으면 진행
        if (pending) {
            uint8_t status;

            APP_ERROR_CHECK(apds_read_data(APDS_REG_STATUS, &status, 1));
            
            // PINT 확인
            if ((status & 0x20U) != 0) {
                // change to gesture mode
                APP_ERROR_CHECK_BOOL(apds_gesture_mode());

                gesture_activity_ticks = timer1_now();
                current_state = GESTURE;
            }
        }

        break;
      }
      
      // 담당 기능: FIFO 읽기, 제스처 판정
      case GESTURE: {
        // read gesture
        handleGesture();
        int gesture = readGesture();

        // gesture detected
        if (gesture != GESTURE_NONE) {
          
          // init global time counter
          CRITICAL_REGION_ENTER();
          display_global_timer = 0;
          CRITICAL_REGION_EXIT();

          gesture_activity_ticks = timer1_now();

          // control display, control led
          on_gesture(gesture);

          // display on
          if (!is_display_on) {
            uint8_t command = 0xAF;
            APP_ERROR_CHECK(ssd_write_commands(&command, 1));
            is_display_on = true;
          }

        }

        break;
      }
      
      default:
        break;
    }

    uint32_t now = timer1_now();

    // LED 500ms 자동 끄기: 매 루프
    if (is_led_on && 
      (uint32_t)(now - led_started_ticks) >= led_timeout_ticks) 
    {
      led_control(LED_WHITE, false);
      is_led_on = false;
    }

    // OLED 5초 절전: 매 루프
    if (is_display_on && display_global_timer >= DISPLAY_TIMEOUT_TICKS) {
      const uint8_t command = 0xAE;
      APP_ERROR_CHECK(ssd_write_commands(&command, 1));
      is_display_on = false;
    }

    // 제스처 모드 3000ms 자동 끄기: 매 루프
    // gesture mode off, proximity on
    if (current_state == GESTURE && 
      (now - gesture_activity_ticks) >= gesture_timeout_ticks) 
    {
      CRITICAL_REGION_ENTER();
      proximity_interrupt_flag = false;
      CRITICAL_REGION_EXIT();

      APP_ERROR_CHECK_BOOL(apds_proximity_mode());

      current_state = PROXIMITY;
    }
    
    bool update;

    CRITICAL_REGION_ENTER();
    update = timer_update_flag;
    timer_update_flag = false;
    CRITICAL_REGION_EXIT();

    // 켜진 시계 화면 갱신: 매 루프 타이머 이벤트에 따라
    if (update && is_display_on && current_display==CLOCK) {
      //calculate hour, min from second
      refresh_clock();
    }

    (void)NRF_LOG_PROCESS();

  } // while(1)
  
}

