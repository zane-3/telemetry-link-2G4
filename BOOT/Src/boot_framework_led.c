#include "boot_framework_led.h"

void BootFrameworkLed_Init(const boot_framework_led_t *led)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  GPIO_InitStruct.Pin = led->pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(led->port, &GPIO_InitStruct);
  BootFrameworkLed_Off(led);
}

void BootFrameworkLed_On(const boot_framework_led_t *led)
{
  HAL_GPIO_WritePin(led->port, led->pin, led->active_level);
}

void BootFrameworkLed_Off(const boot_framework_led_t *led)
{
  HAL_GPIO_WritePin(led->port, led->pin, led->inactive_level);
}

void BootFrameworkLed_Blink(const boot_framework_led_t *led,
                            uint8_t count,
                            uint32_t on_ms,
                            uint32_t off_ms)
{
  uint8_t index;

  for (index = 0U; index < count; ++index)
  {
    BootFrameworkLed_On(led);
    HAL_Delay(on_ms);
    BootFrameworkLed_Off(led);
    HAL_Delay(off_ms);
  }
}
