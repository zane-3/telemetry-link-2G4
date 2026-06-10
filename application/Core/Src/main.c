#include "main.h"

#include "app.h"
#include "app_config.h"
#include "app_framework_clock.h"

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
UART_HandleTypeDef huart3;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_USART3_UART_Init(void);
static void AppWatchdog_Init(void);
static void AppWatchdog_Refresh(void);

#if APP_WATCHDOG_RELOAD > 0x0FFFU
#error "APP_WATCHDOG_TIMEOUT_MS is too large for the selected IWDG prescaler."
#endif

int main(void)
{
#if APP_TEST_HANG_AFTER_BOOT_MS > 0U
  uint32_t app_start_ms;
#endif
  app_hw_t app_hw;

  HAL_Init();
  AppWatchdog_Refresh();
  SystemClock_Config();
  AppWatchdog_Refresh();
  MX_GPIO_Init();
  AppWatchdog_Refresh();
  MX_USART1_UART_Init();
  AppWatchdog_Refresh();
  MX_USART2_UART_Init();
  AppWatchdog_Refresh();
  MX_USART3_UART_Init();
  AppWatchdog_Refresh();

  app_hw.radio_uart = &huart1;
  app_hw.host_uart1 = &huart2;
  app_hw.host_uart2 = &huart3;
  app_hw.sys_led_port = LED_SYS_GPIO_Port;
  app_hw.sys_led_pin = LED_SYS_Pin;
  app_hw.radio_m0_port = RADIO_M0_GPIO_Port;
  app_hw.radio_m0_pin = RADIO_M0_Pin;
  app_hw.radio_m1_port = RADIO_M1_GPIO_Port;
  app_hw.radio_m1_pin = RADIO_M1_Pin;
  app_hw.radio_m2_port = RADIO_M2_GPIO_Port;
  app_hw.radio_m2_pin = RADIO_M2_Pin;
  app_hw.radio_aux_port = RADIO_AUX_GPIO_Port;
  app_hw.radio_aux_pin = RADIO_AUX_Pin;

  App_Init(&app_hw);
  AppWatchdog_Init();
  AppWatchdog_Refresh();

#if APP_TEST_HANG_AFTER_BOOT_MS > 0U
  app_start_ms = HAL_GetTick();
#endif

  while (1)
  {
#if APP_TEST_HANG_AFTER_BOOT_MS > 0U
    if ((HAL_GetTick() - app_start_ms) >= APP_TEST_HANG_AFTER_BOOT_MS)
    {
      while (1)
      {
      }
    }
#endif
    App_Run();
    AppWatchdog_Refresh();
  }
}

void SystemClock_Config(void)
{
  if (AppFrameworkClock_ConfigHsePll6AdcDiv6() != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_USART1_UART_Init(void)
{
  huart1.Instance = USART1;
  huart1.Init.BaudRate = APP_RADIO_UART_BAUDRATE;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_USART2_UART_Init(void)
{
  huart2.Instance = USART2;
  huart2.Init.BaudRate = APP_CRSF_UART_BAUDRATE;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_USART3_UART_Init(void)
{
  huart3.Instance = USART3;
  huart3.Init.BaudRate = APP_DEBUG_UART_BAUDRATE;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  HAL_GPIO_WritePin(LED_SYS_GPIO_Port, LED_SYS_Pin, APP_SYS_LED_INACTIVE_LEVEL);
  HAL_GPIO_WritePin(RADIO_M0_GPIO_Port, RADIO_M0_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(RADIO_M1_GPIO_Port, RADIO_M1_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(RADIO_M2_GPIO_Port, RADIO_M2_Pin, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin = LED_SYS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_SYS_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = RADIO_M0_Pin | RADIO_M1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = RADIO_M2_Pin;
  HAL_GPIO_Init(RADIO_M2_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = RADIO_AUX_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(RADIO_AUX_GPIO_Port, &GPIO_InitStruct);
}

static void AppWatchdog_Init(void)
{
  uint32_t reload;

  RCC->CSR |= RCC_CSR_LSION;
  while ((RCC->CSR & RCC_CSR_LSIRDY) == 0U)
  {
  }

  reload = ((APP_WATCHDOG_TIMEOUT_MS * (APP_WATCHDOG_LSI_HZ / 1000U)) / APP_WATCHDOG_PRESCALER);
  if (reload > 0U)
  {
    --reload;
  }
  if (reload > 0x0FFFU)
  {
    reload = 0x0FFFU;
  }

  IWDG->KR = 0x5555U;
  while ((IWDG->SR & IWDG_SR_PVU) != 0U)
  {
  }
  IWDG->PR = APP_WATCHDOG_PR_BITS;
  while ((IWDG->SR & IWDG_SR_RVU) != 0U)
  {
  }
  IWDG->RLR = reload;
  IWDG->KR = 0xAAAAU;
  IWDG->KR = 0xCCCCU;
}

static void AppWatchdog_Refresh(void)
{
  IWDG->KR = 0xAAAAU;
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
