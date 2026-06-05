#ifndef AEROCORE_BOOT_FRAMEWORK_LED_H
#define AEROCORE_BOOT_FRAMEWORK_LED_H

#include "stm32f1xx_hal.h"

#include <stdint.h>

typedef struct
{
  GPIO_TypeDef *port;
  uint16_t pin;
  GPIO_PinState active_level;
  GPIO_PinState inactive_level;
} boot_framework_led_t;

void BootFrameworkLed_Init(const boot_framework_led_t *led);
void BootFrameworkLed_On(const boot_framework_led_t *led);
void BootFrameworkLed_Off(const boot_framework_led_t *led);
void BootFrameworkLed_Blink(const boot_framework_led_t *led,
                            uint8_t count,
                            uint32_t on_ms,
                            uint32_t off_ms);

#endif
