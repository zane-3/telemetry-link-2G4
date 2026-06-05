#include "boot_framework_power.h"

void BootFrameworkPower_InitOutput(const boot_framework_power_output_t *output)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  GPIO_InitStruct.Pin = output->pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(output->port, &GPIO_InitStruct);
}

void BootFrameworkPower_SetOutput(const boot_framework_power_output_t *output, bool enabled)
{
  HAL_GPIO_WritePin(output->port,
                    output->pin,
                    enabled ? output->active_level : output->inactive_level);
}
