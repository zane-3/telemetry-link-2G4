#include "boot_jump.h"

#include "boot_config.h"
#include "stm32f1xx_hal.h"

#include <stdint.h>

typedef void (*entry_fn_t)(void);

static bool boot_is_stack_in_sram(uint32_t stack_addr)
{
  return stack_addr >= SRAM_BASE_ADDR &&
         stack_addr <= (SRAM_BASE_ADDR + SRAM_SIZE_BYTES);
}

static bool boot_is_entry_in_flash(uint32_t entry_addr)
{
  return entry_addr >= APPLICATION_FLASH_BASE &&
         entry_addr < (APPLICATION_FLASH_BASE + APPLICATION_FLASH_SIZE_BYTES);
}

bool boot_application_is_valid(void)
{
  const uint32_t *vector = (const uint32_t *)APPLICATION_FLASH_BASE;
  const uint32_t initial_sp = vector[0];
  const uint32_t reset_handler = vector[1];

  return boot_is_stack_in_sram(initial_sp) && boot_is_entry_in_flash(reset_handler);
}

bool boot_should_stay_in_loader(void)
{
  __HAL_RCC_CLEAR_RESET_FLAGS();
  return false;
}

void boot_refresh_watchdog(void)
{
  IWDG->KR = 0xAAAAU;
}

void boot_jump_to_application(void)
{
  const uint32_t *vector = (const uint32_t *)APPLICATION_FLASH_BASE;
  const uint32_t initial_sp = vector[0];
  const uint32_t reset_handler = vector[1];
  entry_fn_t app_entry = (entry_fn_t)reset_handler;

  __disable_irq();
  HAL_RCC_DeInit();
  HAL_DeInit();
  SysTick->CTRL = 0U;
  SysTick->LOAD = 0U;
  SysTick->VAL = 0U;
  SCB->VTOR = APPLICATION_FLASH_BASE;
  __set_MSP(initial_sp);
  __DSB();
  __ISB();
  __enable_irq();
  app_entry();
}
