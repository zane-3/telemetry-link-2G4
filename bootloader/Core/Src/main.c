#include "main.h"

#include "boot_config.h"
#include "boot_framework_clock.h"
#include "boot_framework_led.h"
#include "boot_jump.h"

static const boot_framework_led_t sys_led = {
  LED_SYS_GPIO_Port,
  LED_SYS_Pin,
  SYS_LED_ACTIVE_LEVEL,
  SYS_LED_INACTIVE_LEVEL
};

void SystemClock_Config(void);
static void MX_GPIO_Init(void);

int main(void)
{
  uint32_t boot_start_ms;

  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();

  BootFrameworkLed_Init(&sys_led);
  BootFrameworkLed_On(&sys_led);
  boot_start_ms = HAL_GetTick();

  while (1)
  {
    boot_refresh_watchdog();

    if (((HAL_GetTick() - boot_start_ms) >= BOOT_JUMP_DELAY_MS) &&
        boot_application_is_valid() &&
        !boot_should_stay_in_loader())
    {
      boot_jump_to_application();
    }
  }
}

void SystemClock_Config(void)
{
  if (BootFrameworkClock_ConfigHse() != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_GPIO_Init(void)
{
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
}

void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  (void)file;
  (void)line;
}
#endif
