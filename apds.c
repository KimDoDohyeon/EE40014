#include "apds.h"

#include "boards.h"
#include "app_util_platform.h"
#include "app_error.h"

#include "nrf_drv_twi.h"
#include "nrf_delay.h"
#include "nrf_gpio.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"

/* TWI instance ID. */
#define INTERNAL_TWI_INSTANCE_ID 0 // for internal I2C, APDS-9960

/* APDS-9960 */
#define APDS_ADDR           0x39U
#define APDS_REG_ENABLE     0x80U
#define APDS_REG_ID         0x92U

#define APDS_REG_GCONFIG1   0xA2U
#define APDS_REG_GCONFIG2   0xA3U

#define APDS_REG_GPULSE     0xA6U

#define APDS_REG_GCONFIG3   0xAAU
#define APDS_REG_GCONFIG4   0xABU

#define APDS_REG_GFLVL      0xAEU
#define APDS_REG_GSTATUS    0xAFU

#define APDS_REG_GFIFO_U    0xFCU
#define APDS_REG_GFIFO_D    0xFDU
#define APDS_REG_GFIFO_L    0xFEU
#define APDS_REG_GFIFO_R    0xFFU

#define APDS_REG_PILT       0x89U
#define APDS_REG_PIHT       0x8BU
#define APDS_REG_PERS       0x8CU
#define APDS_REG_PPULSE     0x8EU
#define APDS_REG_CONTROL    0x8FU
#define APDS_CMD_PICLEAR    0xE5U

#define APDS_PROX_HIGH_THRESHOLD  80U
#define APDS_MODE_EXIT_RETRIES    50U

/* Internal I2C bus: APDS-9960, TWI instance. */
static const nrf_drv_twi_t m_twi = NRF_DRV_TWI_INSTANCE(INTERNAL_TWI_INSTANCE_ID);

/* Gesture variables */
bool _gestureIn = true;
int _gestureDirectionX = 0;
int _gestureDirectionY = 0;
int _gestureDirInX = 0;
int _gestureDirInY = 0;
int _gestureSensitivity = 30; // You may change the sensitivity for better use of gesture sensing.
int _detectedGesture = GESTURE_NONE;


/* twi initialization. */
void twi_init (void)
{
  ret_code_t err_code;

  const nrf_drv_twi_config_t twi_config = {
        .scl                = ARDUINO_SCL1_PIN,
   	.sda                = ARDUINO_SDA1_PIN,
   	.frequency          = NRF_DRV_TWI_FREQ_100K,
   	.interrupt_priority = APP_IRQ_PRIORITY_HIGH,
   	.clear_bus_init     = false
  };

  err_code = nrf_drv_twi_init(&m_twi, &twi_config, NULL, NULL);
  APP_ERROR_CHECK(err_code);

  nrf_drv_twi_enable(&m_twi);

  // To secure correct signal levels on the pins used by the TWI master when the system is in OFF mode, and
  // when the TWI master is disabled, SDA and SCL pins must be configured in the GPIO peripheral as follows:
  // SCL As specified in PSEL.SCL: Input H0D1
  // SDA As specified in PSEL.SDA: Input H0D1
  nrf_gpio_cfg(ARDUINO_SDA1_PIN,NRF_GPIO_PIN_DIR_INPUT,
  NRF_GPIO_PIN_INPUT_CONNECT,NRF_GPIO_PIN_PULLUP,NRF_GPIO_PIN_H0D1,
  NRF_GPIO_PIN_NOSENSE);
  nrf_gpio_cfg(ARDUINO_SCL1_PIN,NRF_GPIO_PIN_DIR_INPUT,
  NRF_GPIO_PIN_INPUT_CONNECT,NRF_GPIO_PIN_PULLUP,NRF_GPIO_PIN_H0D1,
  NRF_GPIO_PIN_NOSENSE);
}

/* read data from APDS-9960 */
ret_code_t apds_read_data(uint8_t reg, uint8_t *data, uint8_t length)
{
  if (data == NULL || length == 0) 
    return NRF_ERROR_INVALID_PARAM;
  
  ret_code_t err = nrf_drv_twi_tx(&m_twi, APDS_ADDR, &reg, 1, false);
  nrf_delay_ms(1);
  
  if (err != NRF_SUCCESS) 
    return err;
  
  err = nrf_drv_twi_rx(&m_twi, APDS_ADDR, data, length);
  nrf_delay_ms(1);
  
  return err;
}

/* write data to APDS-9960 */
static ret_code_t apds_write_data(uint8_t reg, uint8_t value)
{
  uint8_t tx_data[2] = {reg, value};
  
  ret_code_t err = nrf_drv_twi_tx(&m_twi, APDS_ADDR, tx_data, sizeof(tx_data), false);
  nrf_delay_ms(1);
  
  return err;
}

/* APDS-9960 initialization */
bool apds_init(void)
{
  // check Device ID, APDS-9960 returns 0xAB
  uint8_t id;  
  if (apds_read_data(APDS_REG_ID, &id, 1) != NRF_SUCCESS)
    return false;
  if (id != 0xABU)
    return false;
  
  // PON=1
  if (apds_write_data(APDS_REG_ENABLE, 0x01) != NRF_SUCCESS)
    return false;
  
  // Set gesture pulse count and length: 16 us, 16 pulses
  if (apds_write_data(APDS_REG_GPULSE, 0x8F) != NRF_SUCCESS)
    return false;
  
  // Set proximity pulse count and length: 8 us, 8 pulses
  if (apds_write_data(APDS_REG_PPULSE, 0x47) != NRF_SUCCESS)
    return false;

  // Initially gesture mode off, GMODE=GCONF4[0]=0
  if (apds_write_data(APDS_REG_GCONFIG4, 0x00) != NRF_SUCCESS)
    return false;

  return true;
}

static bool apds_clear_proximity_interrupt(void)
{
  uint8_t command = APDS_CMD_PICLEAR;

  return nrf_drv_twi_tx(
        &m_twi,
        APDS_ADDR,
        &command,
        1,
        false
    ) == NRF_SUCCESS;
}

static bool apds_prepare_mode_change(void)
{
  // PON=1, other are 0
  if (apds_write_data(APDS_REG_ENABLE, 0x01) != NRF_SUCCESS)
    return false;
  
  // gesture interrupt off, gesture mode off
  // GIEN=0, GMODE=0
  if (apds_write_data(APDS_REG_GCONFIG4, 0x00) != NRF_SUCCESS)
    return false;
  
  // check gesture mode exited
  bool stopped = false;
  
  for (uint32_t i = 0; i < APDS_MODE_EXIT_RETRIES; ++i) {
    uint8_t gconf4;
    if (apds_read_data(APDS_REG_GCONFIG4, &gconf4, 1) != NRF_SUCCESS)
      return false;
    if ((gconf4 & 0x01) == 0x00) {
      stopped = true;
      break;
    }
    nrf_delay_ms(1);
  }
  if (!stopped)
    return false;
  
  // GFIFO_CLR
  if (apds_write_data(APDS_REG_GCONFIG4, 0x04) != NRF_SUCCESS)
    return false;
  if (apds_write_data(APDS_REG_GCONFIG4, 0x00) != NRF_SUCCESS)
    return false;

  // clear remaining proximity interrupt
  if (!apds_clear_proximity_interrupt())
    return false;
  
  // 기존 코드의 제스처 판정 변수 초기화
  _gestureIn = true;
  _gestureDirectionX = 0;
  _gestureDirectionY = 0;
  _gestureDirInX = 0;
  _gestureDirInY = 0;
  _detectedGesture = GESTURE_NONE;

  return true;
}

bool apds_proximity_mode(void)
{
  if (!apds_prepare_mode_change())
    return false;
  
  // do not use proximity lower bound
  if (apds_write_data(APDS_REG_PILT, 0x00) != NRF_SUCCESS)
    return false;
  // set proximity upper bound threshold
  if (apds_write_data(APDS_REG_PIHT, APDS_PROX_HIGH_THRESHOLD) != NRF_SUCCESS)
    return false;
  
  // set proximity interrupt persistence
  // interrupt happen when 3 consecutive proximity values out of range
  if (apds_write_data(APDS_REG_PERS, 0x30) != NRF_SUCCESS)
    return false;

  // PON=1, PEN=1, PIEN=1, GEN=0 -> 0x25
  if (apds_write_data(APDS_REG_ENABLE, 0x25) != NRF_SUCCESS)
    return false;
  
  return true;
}

bool apds_gesture_mode(void)
{
  if (!apds_prepare_mode_change())
    return false;
  
  // PON=1, PEN=1, PIEN=0, GEN=1 -> 0x45
  if (apds_write_data(APDS_REG_ENABLE, 0x45) != NRF_SUCCESS)
    return false;

  // GMODE=1, immediate entry in to the gesture state machine
  if (apds_write_data(APDS_REG_GCONFIG4, 0x01) != NRF_SUCCESS)
    return false;
    
  return true;
}

/* Functions for Gesture sensing */
static bool getGSTATUS(uint8_t* r) {
    
  if (r == NULL)
    return false;
  
  return apds_read_data(APDS_REG_GSTATUS, r, 1) == NRF_SUCCESS;
  
}

static bool getGFLVL(uint8_t* r) {
  
  if (r == NULL)
    return false;
  
  return apds_read_data(APDS_REG_GFLVL, r, 1) == NRF_SUCCESS;
  
}

/* return detected gesture */
int readGesture(void) {
  
  int gesture = _detectedGesture;
  _detectedGesture = GESTURE_NONE;
  return gesture;
  
} 

/* how many FIFO data available? */
static int gestureFIFOAvailable(void) {
  uint8_t r;
  if (!getGSTATUS(&r)) return -1;
  // Fill in the appropriate register
  if ((r & 0x01) == 0x00) return -2;
  if (!getGFLVL(&r)) return -3;
  return r;
}

int handleGesture(void) {
  // You may change the threshold for better use of gesture sensing
  const int gestureThreshold = 30;

  int available = gestureFIFOAvailable();
  if (available <= 0) return 0;

  uint8_t fifo_data[128];
  uint8_t bytes_read = (available * 4);

  if (apds_read_data(APDS_REG_GFIFO_U, fifo_data, bytes_read) != NRF_SUCCESS){
    NRF_LOG_INFO("FIFO read failed");
    NRF_LOG_FLUSH();
    return -1;
  }


  for (int i = 0; i+3 < available*4; i+=4) {
    uint8_t u,d,l,r;
    u = fifo_data[i];
    d = fifo_data[i+1];
    l = fifo_data[i+2];
    r = fifo_data[i+3];
    //NRF_LOG_INFO("%x,%x,%x,%x",u,d,l,r);

    if ((u<gestureThreshold) && (d<gestureThreshold) && (l<gestureThreshold) && (r<gestureThreshold)) {
      _gestureIn = true;
      if (_gestureDirInX != 0 || _gestureDirInY != 0) {
        int totalX = _gestureDirInX - _gestureDirectionX;
        int totalY = _gestureDirInY - _gestureDirectionY;
        NRF_LOG_INFO("OUT %d, %d",totalX,totalY);

        if (totalX < -_gestureSensitivity) { _detectedGesture = GESTURE_LEFT; }
        if (totalX > _gestureSensitivity) { _detectedGesture = GESTURE_RIGHT; }

        if (totalY < -_gestureSensitivity) { _detectedGesture = GESTURE_DOWN; }
        if (totalY > _gestureSensitivity) { _detectedGesture = GESTURE_UP; }

        NRF_LOG_INFO("X=%d Y=%d sensitivity=%d result=%d",
             totalX, totalY,
             _gestureSensitivity, _detectedGesture);

        _gestureDirectionX = 0;
        _gestureDirectionY = 0;
        _gestureDirInX = 0;
        _gestureDirInY = 0;
      }
      continue;
    }

    _gestureDirectionX = r - l;
    _gestureDirectionY = u - d;
    if (_gestureIn) {
      _gestureIn = false;
      _gestureDirInX = _gestureDirectionX;
      _gestureDirInY = _gestureDirectionY;
    }
  }
  return 1;
}
