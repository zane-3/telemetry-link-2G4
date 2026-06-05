#include "app.h"

#include "app_config.h"
#include "app_framework_led.h"

#include <string.h>

#define APP_STRINGIFY_VALUE(value) #value
#define APP_STRINGIFY(value) APP_STRINGIFY_VALUE(value)

typedef struct
{
  app_hw_t hw;
  app_framework_led_t sys_led;
  uint8_t host_config_trigger_length;
  bool radio_config_mode;
  uint32_t radio_config_last_activity_ms;
  char radio_config_view[APP_RADIO_CONFIG_VIEW_BUFFER];
  uint8_t radio_config_bytes[APP_RADIO_CONFIG_VIEW_BUFFER];
  uint16_t radio_config_length;
  bool radio_config_seen_ok;
  uint8_t radio_config_ok_match;
} app_state_t;

static app_state_t g_app;

volatile uint32_t g_app_diag_uart1_rx_count;
volatile uint32_t g_app_diag_uart1_tx_count;
volatile uint32_t g_app_diag_uart2_rx_count;
volatile uint32_t g_app_diag_uart2_tx_count;
volatile uint32_t g_app_diag_uart3_rx_count;
volatile uint32_t g_app_diag_uart3_tx_count;
volatile uint32_t g_app_diag_dt_tag_rx_count;
volatile uint32_t g_app_diag_rtk_tag_rx_count;
volatile uint32_t g_app_diag_radio_config_entry_count;
volatile uint32_t g_app_diag_radio_config_first_boot_count;

static void App_ConfigureRadioTransparentMode(void);
static void App_ConfigureRadioCommandMode(void);
static void App_WaitRadioModeSwitch(void);
static void App_ConfigureRadioOnFirstBoot(void);
static bool App_RadioConfigMarkerIsWritten(void);
static void App_WriteRadioConfigMarker(void);
static void App_StartManualRadioConfig(void);
static bool App_EnterRadioCommandMode(uint32_t baudrate, bool forward_to_host);
static bool App_FindRadioCommandBaudrate(uint32_t *active_baudrate, bool forward_to_host);
static bool App_QueryRadioConfig(bool forward_to_host);
static bool App_AlignRadioBaudrate(uint32_t active_baudrate, bool forward_to_host);
static bool App_E28WriteConfig(bool forward_to_host);
static void App_E28BuildConfigFrame(uint8_t head, uint8_t frame[6]);
static bool App_E28ResponseHasParameters(void);
static void App_WriteHostHexDump(const uint8_t *bytes, uint16_t length);
static bool App_ParseRadioBaudSetting(uint32_t *s102_value);
static void App_ResetRadioOkDetector(void);
static void App_UpdateRadioOkDetector(uint8_t byte);
static void App_SendRadioBytes(const uint8_t *bytes, uint16_t length);
static void App_ReadRadioFor(uint32_t wait_ms, bool forward_to_host);
static void App_ClearRadioInput(void);
static bool App_SetUartBaudrate(UART_HandleTypeDef *uart, uint32_t baudrate);
static void App_ServiceWatchdog(void);
#if APP_UART_STARTUP_BANNER
static void App_SendStartupBanner(void);
#endif
static void App_WriteHostStatus(const char *text);
static bool App_TryReadUart(UART_HandleTypeDef *source, uint8_t *byte);
static void App_WriteUart(UART_HandleTypeDef *target, uint8_t byte);
static void App_WriteBytes(UART_HandleTypeDef *target, const uint8_t *bytes, uint16_t length);
static void App_PumpUart(UART_HandleTypeDef *source, UART_HandleTypeDef *target);
static void App_PumpUart2WithConfigTrigger(void);

void App_Init(const app_hw_t *hw)
{
  memset(&g_app, 0, sizeof(g_app));
  g_app.hw = *hw;

  AppFrameworkLed_Init(&g_app.sys_led,
                       g_app.hw.sys_led_port,
                       g_app.hw.sys_led_pin,
                       APP_SYS_LED_ACTIVE_LEVEL,
                       APP_SYS_LED_INACTIVE_LEVEL,
                       HAL_GetTick());
  App_ConfigureRadioTransparentMode();
  App_ConfigureRadioOnFirstBoot();
#if APP_UART_STARTUP_BANNER
  App_SendStartupBanner();
#endif
}

void App_Run(void)
{
  const uint32_t now = HAL_GetTick();

#if APP_UART_SELF_TEST_ECHO
  App_PumpUart(g_app.hw.radio_uart, g_app.hw.radio_uart);
  App_PumpUart(g_app.hw.host_uart1, g_app.hw.host_uart1);
  App_PumpUart(g_app.hw.host_uart2, g_app.hw.host_uart2);
#else
  if (g_app.radio_config_mode)
  {
    App_PumpUart(g_app.hw.host_uart1, g_app.hw.radio_uart);
    App_PumpUart(g_app.hw.radio_uart, g_app.hw.host_uart1);
    if ((HAL_GetTick() - g_app.radio_config_last_activity_ms) >= APP_RADIO_CONFIG_IDLE_MS)
    {
      g_app.radio_config_mode = false;
      App_ConfigureRadioTransparentMode();
    }
    AppFrameworkLed_Update(&g_app.sys_led, true, APP_SYS_LED_TOGGLE_MS, now);
    return;
  }

  App_PumpUart2WithConfigTrigger();
  App_PumpUart(g_app.hw.radio_uart, g_app.hw.host_uart1);
#endif
  AppFrameworkLed_Update(&g_app.sys_led, true, APP_SYS_LED_TOGGLE_MS, now);
}

static void App_ConfigureRadioTransparentMode(void)
{
  HAL_GPIO_WritePin(g_app.hw.radio_m0_port, g_app.hw.radio_m0_pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(g_app.hw.radio_m1_port, g_app.hw.radio_m1_pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(g_app.hw.radio_m2_port, g_app.hw.radio_m2_pin, GPIO_PIN_SET);
  App_WaitRadioModeSwitch();
}

static void App_ConfigureRadioCommandMode(void)
{
  HAL_GPIO_WritePin(g_app.hw.radio_m0_port, g_app.hw.radio_m0_pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(g_app.hw.radio_m1_port, g_app.hw.radio_m1_pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(g_app.hw.radio_m2_port, g_app.hw.radio_m2_pin, GPIO_PIN_SET);
  App_WaitRadioModeSwitch();
}

static void App_WaitRadioModeSwitch(void)
{
  const uint32_t start = HAL_GetTick();

  while ((HAL_GetTick() - start) < APP_RADIO_MODE_SWITCH_WAIT_MS)
  {
    App_ServiceWatchdog();
    if ((g_app.hw.radio_aux_port != NULL) &&
        (HAL_GPIO_ReadPin(g_app.hw.radio_aux_port, g_app.hw.radio_aux_pin) == GPIO_PIN_SET))
    {
      break;
    }
  }
}

static void App_ConfigureRadioOnFirstBoot(void)
{
#if APP_RADIO_CONFIG_ON_FIRST_BOOT
  uint32_t active_baudrate = APP_RADIO_DEFAULT_BAUDRATE;
  bool configured = false;
  const bool forward_to_host = (APP_RADIO_CONFIG_FIRST_BOOT_FORWARD != 0U);

  if ((g_app.hw.radio_uart == NULL) || App_RadioConfigMarkerIsWritten())
  {
    return;
  }

  ++g_app_diag_radio_config_first_boot_count;
  App_ReadRadioFor(APP_RADIO_CONFIG_STARTUP_WAIT_MS, forward_to_host);
  if (!App_FindRadioCommandBaudrate(&active_baudrate, forward_to_host))
  {
    (void)App_SetUartBaudrate(g_app.hw.radio_uart, APP_UART_BAUDRATE);
    return;
  }

  if (App_QueryRadioConfig(forward_to_host))
  {
    configured = App_E28WriteConfig(forward_to_host) && App_QueryRadioConfig(forward_to_host);
  }
  if (configured)
  {
    (void)App_SetUartBaudrate(g_app.hw.radio_uart, APP_UART_BAUDRATE);
    App_WriteRadioConfigMarker();
  }
  else
  {
    (void)App_SetUartBaudrate(g_app.hw.radio_uart, APP_RADIO_DEFAULT_BAUDRATE);
  }
  App_ConfigureRadioTransparentMode();
#endif
}

static bool App_RadioConfigMarkerIsWritten(void)
{
  return (*(const uint32_t *)APP_RADIO_CONFIG_MARKER_ADDR == APP_RADIO_CONFIG_MARKER_VALUE);
}

static void App_WriteRadioConfigMarker(void)
{
  FLASH_EraseInitTypeDef erase;
  uint32_t page_error = 0U;

  if (App_RadioConfigMarkerIsWritten())
  {
    return;
  }

  HAL_FLASH_Unlock();
  erase.TypeErase = FLASH_TYPEERASE_PAGES;
  erase.PageAddress = APP_RADIO_CONFIG_MARKER_ADDR;
  erase.NbPages = 1U;
  if (HAL_FLASHEx_Erase(&erase, &page_error) == HAL_OK)
  {
    (void)HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                            APP_RADIO_CONFIG_MARKER_ADDR,
                            APP_RADIO_CONFIG_MARKER_VALUE);
  }
  HAL_FLASH_Lock();
}

static void App_StartManualRadioConfig(void)
{
  uint32_t active_baudrate = APP_UART_BAUDRATE;

  ++g_app_diag_radio_config_entry_count;
  g_app.radio_config_mode = false;
  g_app.host_config_trigger_length = 0U;
  App_WriteHostStatus("\r\nCFG ENTER\r\n");

  if (!App_FindRadioCommandBaudrate(&active_baudrate, true))
  {
#if APP_RADIO_CONFIG_ALLOW_FALLBACK
    (void)App_SetUartBaudrate(g_app.hw.radio_uart, APP_RADIO_CONFIG_FALLBACK_BAUDRATE);
    g_app.radio_config_mode = true;
    g_app.radio_config_last_activity_ms = HAL_GetTick();
    App_WriteHostStatus("CFG WARN NO RADIO ACK\r\n");
    App_WriteHostStatus("CFG READY\r\n");
    return;
#else
    (void)App_SetUartBaudrate(g_app.hw.radio_uart, APP_RADIO_DEFAULT_BAUDRATE);
    App_ConfigureRadioTransparentMode();
    App_WriteHostStatus("CFG FAIL\r\n");
    return;
#endif
  }

  if (App_QueryRadioConfig(true))
  {
    (void)App_AlignRadioBaudrate(active_baudrate, true);
  }

  g_app.radio_config_mode = true;
  g_app.radio_config_last_activity_ms = HAL_GetTick();
  App_WriteHostStatus("CFG READY\r\n");
}

static bool App_FindRadioCommandBaudrate(uint32_t *active_baudrate, bool forward_to_host)
{
  static const uint32_t baudrates[] = {
      APP_RADIO_DEFAULT_BAUDRATE,
  };
  size_t index;
  size_t previous;
  bool duplicate;

  if (active_baudrate == NULL)
  {
    return false;
  }

  for (index = 0U; index < (sizeof(baudrates) / sizeof(baudrates[0])); ++index)
  {
    duplicate = false;
    for (previous = 0U; previous < index; ++previous)
    {
      if (baudrates[previous] == baudrates[index])
      {
        duplicate = true;
        break;
      }
    }
    if (duplicate)
    {
      continue;
    }

    if (App_EnterRadioCommandMode(baudrates[index], forward_to_host))
    {
      *active_baudrate = baudrates[index];
      return true;
    }
  }

  return false;
}

static bool App_EnterRadioCommandMode(uint32_t baudrate, bool forward_to_host)
{
  App_ConfigureRadioCommandMode();
  (void)App_SetUartBaudrate(g_app.hw.radio_uart, baudrate);
  App_ClearRadioInput();
  App_ReadRadioFor(APP_RADIO_CONFIG_GUARD_WAIT_MS, false);
  return App_QueryRadioConfig(forward_to_host);
}

static bool App_QueryRadioConfig(bool forward_to_host)
{
  const uint8_t read_command[] = {0xC1U, 0xC1U, 0xC1U};

  App_SendRadioBytes(read_command, (uint16_t)sizeof(read_command));
  App_ReadRadioFor(APP_RADIO_CONFIG_VIEW_WAIT_MS, false);
  if (App_E28ResponseHasParameters())
  {
    if (forward_to_host)
    {
      App_WriteHostHexDump(g_app.radio_config_bytes, g_app.radio_config_length);
    }
    return true;
  }
  return false;
}

static bool App_AlignRadioBaudrate(uint32_t active_baudrate, bool forward_to_host)
{
  uint32_t s102_value;
  bool changed = false;

  if (!App_ParseRadioBaudSetting(&s102_value))
  {
    return false;
  }

  if (s102_value != APP_RADIO_BAUD_S102_VALUE)
  {
    changed = App_E28WriteConfig(forward_to_host);
  }

  if ((active_baudrate != APP_UART_BAUDRATE) || changed)
  {
    (void)App_SetUartBaudrate(g_app.hw.radio_uart, APP_UART_BAUDRATE);
    if (App_EnterRadioCommandMode(APP_UART_BAUDRATE, forward_to_host))
    {
      return App_QueryRadioConfig(forward_to_host) && App_ParseRadioBaudSetting(&s102_value) &&
             (s102_value == APP_RADIO_BAUD_S102_VALUE);
    }
    return false;
  }

  return true;
}

static bool App_E28WriteConfig(bool forward_to_host)
{
  uint8_t frame[6];

  App_E28BuildConfigFrame(0xC0U, frame);
  App_SendRadioBytes(frame, (uint16_t)sizeof(frame));
  App_ReadRadioFor(APP_RADIO_CONFIG_CMD_WAIT_MS, false);
  if (App_E28ResponseHasParameters())
  {
    if (forward_to_host)
    {
      App_WriteHostHexDump(g_app.radio_config_bytes, g_app.radio_config_length);
    }
    return true;
  }
  return false;
}

static void App_E28BuildConfigFrame(uint8_t head, uint8_t frame[6])
{
  const uint16_t address = (uint16_t)APP_RADIO_E28_LINK_ADDRESS;

  frame[0] = head;
  frame[1] = (uint8_t)(address >> 8);
  frame[2] = (uint8_t)(address & 0xFFU);
  frame[3] = (uint8_t)((APP_RADIO_E28_UART_BAUD_BITS << 3) | APP_RADIO_E28_AIR_RATE_BITS);
  frame[4] = (uint8_t)APP_RADIO_E28_CHANNEL;
  frame[5] = (uint8_t)APP_RADIO_E28_OPTION;
}

static bool App_E28ResponseHasParameters(void)
{
  uint16_t index;

  if (g_app.radio_config_length < 6U)
  {
    return false;
  }

  for (index = 0U; index <= (uint16_t)(g_app.radio_config_length - 6U); ++index)
  {
    if (g_app.radio_config_bytes[index] == 0xC0U)
    {
      return true;
    }
  }
  return false;
}

static void App_WriteHostHexDump(const uint8_t *bytes, uint16_t length)
{
  static const char hex[] = "0123456789ABCDEF";
  uint16_t index;
  char out[4];

  if (bytes == NULL)
  {
    return;
  }

  for (index = 0U; index < length; ++index)
  {
    out[0] = hex[(bytes[index] >> 4) & 0x0FU];
    out[1] = hex[bytes[index] & 0x0FU];
    out[2] = (index + 1U == length) ? '\r' : ' ';
    out[3] = (index + 1U == length) ? '\n' : '\0';
    App_WriteBytes(g_app.hw.host_uart1, (const uint8_t *)out, (uint16_t)((index + 1U == length) ? 4U : 3U));
  }
}

static bool App_ParseRadioBaudSetting(uint32_t *s102_value)
{
  uint16_t index;

  if ((s102_value == NULL) || (g_app.radio_config_length < 6U))
  {
    return false;
  }

  for (index = 0U; index <= (uint16_t)(g_app.radio_config_length - 6U); ++index)
  {
    if (g_app.radio_config_bytes[index] == 0xC0U)
    {
      *s102_value = (uint32_t)((g_app.radio_config_bytes[index + 3U] >> 3) & 0x07U);
      return true;
    }
  }

  return false;
}

static void App_ResetRadioOkDetector(void)
{
  g_app.radio_config_seen_ok = false;
  g_app.radio_config_ok_match = 0U;
}

static void App_UpdateRadioOkDetector(uint8_t byte)
{
  const char ok[] = "OK";

  if (byte == (uint8_t)ok[g_app.radio_config_ok_match])
  {
    ++g_app.radio_config_ok_match;
    if (g_app.radio_config_ok_match == (uint8_t)(sizeof(ok) - 1U))
    {
      g_app.radio_config_seen_ok = true;
      g_app.radio_config_ok_match = 0U;
    }
    return;
  }

  g_app.radio_config_ok_match = (byte == (uint8_t)ok[0]) ? 1U : 0U;
}

static void App_SendRadioBytes(const uint8_t *bytes, uint16_t length)
{
  App_WriteBytes(g_app.hw.radio_uart, bytes, length);
}

static void App_ReadRadioFor(uint32_t wait_ms, bool forward_to_host)
{
  const uint32_t start = HAL_GetTick();
  uint16_t used = 0U;
  uint8_t byte;

  g_app.radio_config_view[0] = '\0';
  g_app.radio_config_length = 0U;
  App_ResetRadioOkDetector();
  while ((HAL_GetTick() - start) < wait_ms)
  {
    App_ServiceWatchdog();
    while (App_TryReadUart(g_app.hw.radio_uart, &byte))
    {
      App_UpdateRadioOkDetector(byte);
      if (used < (uint16_t)(sizeof(g_app.radio_config_view) - 1U))
      {
        g_app.radio_config_view[used] = (char)byte;
        ++used;
        g_app.radio_config_view[used] = '\0';
      }
      if (g_app.radio_config_length < (uint16_t)sizeof(g_app.radio_config_bytes))
      {
        g_app.radio_config_bytes[g_app.radio_config_length] = byte;
        ++g_app.radio_config_length;
      }
      if (forward_to_host)
      {
        App_WriteUart(g_app.hw.host_uart1, byte);
      }
    }
  }
}

static void App_ClearRadioInput(void)
{
  uint8_t byte;

  while (App_TryReadUart(g_app.hw.radio_uart, &byte))
  {
  }
}

static bool App_SetUartBaudrate(UART_HandleTypeDef *uart, uint32_t baudrate)
{
  if (uart == NULL)
  {
    return false;
  }

  if (uart->Init.BaudRate == baudrate)
  {
    return true;
  }

  if (HAL_UART_DeInit(uart) != HAL_OK)
  {
    return false;
  }
  uart->Init.BaudRate = baudrate;
  return HAL_UART_Init(uart) == HAL_OK;
}

static void App_ServiceWatchdog(void)
{
#if APP_WATCHDOG_ENABLE
  IWDG->KR = 0xAAAAU;
#endif
}

#if APP_UART_STARTUP_BANNER
static void App_SendStartupBanner(void)
{
  const uint8_t radio_banner[] = "USART1 RADIO READY\r\n";
#if APP_RADIO_DEVICE_ROLE == APP_RADIO_ROLE_MASTER
  const uint8_t host1_banner[] = "USART2 HOST READY ROLE=MASTER\r\n";
#else
  const uint8_t host1_banner[] = "USART2 HOST READY ROLE=SLAVE\r\n";
#endif
  const uint8_t host2_banner[] = "USART3 UNUSED READY\r\n";

  if (g_app.hw.radio_uart != NULL)
  {
    (void)HAL_UART_Transmit(g_app.hw.radio_uart,
                            (uint8_t *)radio_banner,
                            (uint16_t)(sizeof(radio_banner) - 1U),
                            APP_UART_POLL_TIMEOUT_MS);
  }
  if (g_app.hw.host_uart1 != NULL)
  {
    (void)HAL_UART_Transmit(g_app.hw.host_uart1,
                            (uint8_t *)host1_banner,
                            (uint16_t)(sizeof(host1_banner) - 1U),
                            APP_UART_POLL_TIMEOUT_MS);
  }
  if (g_app.hw.host_uart2 != NULL)
  {
    (void)HAL_UART_Transmit(g_app.hw.host_uart2,
                            (uint8_t *)host2_banner,
                            (uint16_t)(sizeof(host2_banner) - 1U),
                            APP_UART_POLL_TIMEOUT_MS);
  }
}
#endif

static void App_WriteHostStatus(const char *text)
{
  if (text != NULL)
  {
    App_WriteBytes(g_app.hw.host_uart1, (const uint8_t *)text, (uint16_t)strlen(text));
  }
}

static void App_PumpUart(UART_HandleTypeDef *source, UART_HandleTypeDef *target)
{
  uint8_t byte;

  if ((source == NULL) || (target == NULL))
  {
    return;
  }

  while (App_TryReadUart(source, &byte))
  {
    App_WriteUart(target, byte);
    g_app.radio_config_last_activity_ms = HAL_GetTick();
  }
}

static void App_PumpUart2WithConfigTrigger(void)
{
  const char *trigger = APP_UART2_CONFIG_TRIGGER;
  uint8_t byte;

  if ((g_app.hw.host_uart1 == NULL) || (g_app.hw.radio_uart == NULL))
  {
    return;
  }

  while (App_TryReadUart(g_app.hw.host_uart1, &byte))
  {
    if (byte == (uint8_t)trigger[g_app.host_config_trigger_length])
    {
      ++g_app.host_config_trigger_length;
      if (g_app.host_config_trigger_length == strlen(trigger))
      {
        g_app.host_config_trigger_length = 0U;
        App_StartManualRadioConfig();
        return;
      }
      continue;
    }

    while (g_app.host_config_trigger_length > 0U)
    {
      App_WriteUart(g_app.hw.radio_uart, (uint8_t)trigger[0]);
      --g_app.host_config_trigger_length;
    }
    App_WriteUart(g_app.hw.radio_uart, byte);
  }
}

static bool App_TryReadUart(UART_HandleTypeDef *source, uint8_t *byte)
{
  if ((source == NULL) || (byte == NULL))
  {
    return false;
  }

  if (__HAL_UART_GET_FLAG(source, UART_FLAG_RXNE) == RESET)
  {
    return false;
  }

  *byte = (uint8_t)(source->Instance->DR & 0xFFU);
  if (source->Instance == USART1)
  {
    ++g_app_diag_uart1_rx_count;
  }
  else if (source->Instance == USART2)
  {
    ++g_app_diag_uart2_rx_count;
  }
  else if (source->Instance == USART3)
  {
    ++g_app_diag_uart3_rx_count;
  }
  return true;
}

static void App_WriteUart(UART_HandleTypeDef *target, uint8_t byte)
{
  const uint32_t start = HAL_GetTick();

  if (target == NULL)
  {
    return;
  }

  while (__HAL_UART_GET_FLAG(target, UART_FLAG_TXE) == RESET)
  {
    if ((HAL_GetTick() - start) >= APP_UART_POLL_TIMEOUT_MS)
    {
      return;
    }
  }

  target->Instance->DR = byte;
  if (target->Instance == USART1)
  {
    ++g_app_diag_uart1_tx_count;
  }
  else if (target->Instance == USART2)
  {
    ++g_app_diag_uart2_tx_count;
  }
  else if (target->Instance == USART3)
  {
    ++g_app_diag_uart3_tx_count;
  }
}

static void App_WriteBytes(UART_HandleTypeDef *target, const uint8_t *bytes, uint16_t length)
{
  uint16_t index;

  if ((target == NULL) || (bytes == NULL))
  {
    return;
  }

  for (index = 0U; index < length; ++index)
  {
    App_WriteUart(target, bytes[index]);
  }
}
