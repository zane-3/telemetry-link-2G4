#ifndef AEROCORE_BOOT_FRAMEWORK_POWER_H
#define AEROCORE_BOOT_FRAMEWORK_POWER_H

#include "stm32f1xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
  GPIO_TypeDef *port;
  uint16_t pin;
  GPIO_PinState active_level;
  GPIO_PinState inactive_level;
} boot_framework_power_output_t;

void BootFrameworkPower_InitOutput(const boot_framework_power_output_t *output);
void BootFrameworkPower_SetOutput(const boot_framework_power_output_t *output, bool enabled);

#endif
