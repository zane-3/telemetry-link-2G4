#ifndef AEROCORE_APP_FRAMEWORK_POWER_H
#define AEROCORE_APP_FRAMEWORK_POWER_H

#include "stm32f1xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
  GPIO_TypeDef *port;
  uint16_t pin;
  GPIO_PinState active_level;
  GPIO_PinState inactive_level;
} app_framework_power_output_t;

void AppFrameworkPower_InitOutput(const app_framework_power_output_t *output);
void AppFrameworkPower_SetOutput(const app_framework_power_output_t *output, bool enabled);

#endif
