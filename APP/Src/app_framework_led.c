#include "app_framework_led.h"

void AppFrameworkLed_Init(app_framework_led_t *led,
                          GPIO_TypeDef *port,
                          uint16_t pin,
                          GPIO_PinState active_level,
                          GPIO_PinState inactive_level,
                          uint32_t now)
{
  led->port = port;
  led->pin = pin;
  led->active_level = active_level;
  led->inactive_level = inactive_level;
  led->last_toggle_ms = now;
  led->on = false;
  AppFrameworkLed_Off(led);
}

void AppFrameworkLed_On(app_framework_led_t *led)
{
  led->on = true;
  HAL_GPIO_WritePin(led->port, led->pin, led->active_level);
}

void AppFrameworkLed_Off(app_framework_led_t *led)
{
  led->on = false;
  HAL_GPIO_WritePin(led->port, led->pin, led->inactive_level);
}

void AppFrameworkLed_Update(app_framework_led_t *led,
                            bool enabled,
                            uint32_t toggle_ms,
                            uint32_t now)
{
  if (!enabled)
  {
    AppFrameworkLed_Off(led);
    return;
  }

  if ((now - led->last_toggle_ms) < toggle_ms)
  {
    return;
  }

  led->last_toggle_ms = now;
  if (led->on)
  {
    AppFrameworkLed_Off(led);
  }
  else
  {
    AppFrameworkLed_On(led);
  }
}
