#ifndef AEROCORE_APP_FRAMEWORK_LED_H
#define AEROCORE_APP_FRAMEWORK_LED_H

#include "stm32f1xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
  GPIO_TypeDef *port;
  uint16_t pin;
  GPIO_PinState active_level;
  GPIO_PinState inactive_level;
  uint32_t last_toggle_ms;
  bool on;
} app_framework_led_t;

void AppFrameworkLed_Init(app_framework_led_t *led,
                          GPIO_TypeDef *port,
                          uint16_t pin,
                          GPIO_PinState active_level,
                          GPIO_PinState inactive_level,
                          uint32_t now);
void AppFrameworkLed_On(app_framework_led_t *led);
void AppFrameworkLed_Off(app_framework_led_t *led);
void AppFrameworkLed_Update(app_framework_led_t *led,
                            bool enabled,
                            uint32_t toggle_ms,
                            uint32_t now);

#endif
