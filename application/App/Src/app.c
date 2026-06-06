#include "app.h"

#include "app_config.h"
#include "app_framework_led.h"

#include <string.h>

#define APP_STRINGIFY_VALUE(value) #value
#define APP_STRINGIFY(value) APP_STRINGIFY_VALUE(value)

typedef struct
{
  uint8_t bytes[APP_FRAME_MAX_SIZE];
  uint16_t length;
  uint16_t expected_length;
} app_frame_parser_t;

typedef struct
{
  bool active;
  uint8_t target;
  uint8_t seq;
  uint8_t cmd;
  uint32_t deadline_ms;
} app_pending_ack_t;

typedef struct
{
  uint32_t magic;
  uint32_t version;
  uint32_t crc;
  uint32_t role;
  uint32_t local_id;
  uint32_t remote_id;
  uint32_t channel;
  uint32_t option;
  uint32_t filter_enable;
  uint32_t ack_enable;
  uint32_t marker;
} app_radio_runtime_config_t;

typedef struct
{
  app_hw_t hw;
  app_framework_led_t sys_led;
  uint8_t radio_config_bytes[APP_RADIO_CONFIG_VIEW_BUFFER];
  uint16_t radio_config_length;
  app_frame_parser_t host_frame_parser;
  app_frame_parser_t radio_frame_parser;
  app_pending_ack_t pending_ack;
  app_radio_runtime_config_t radio_config;
#if APP_HOST_CFG_COMMAND_ENABLE
  uint8_t host_cfg_command[APP_HOST_CFG_COMMAND_BUFFER];
  uint16_t host_cfg_command_length;
#endif
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
volatile uint32_t g_app_diag_radio_config_first_boot_count;
volatile uint32_t g_app_diag_frame_rx_count;
volatile uint32_t g_app_diag_frame_drop_count;
volatile uint32_t g_app_diag_frame_ack_tx_count;
volatile uint32_t g_app_diag_frame_ack_rx_count;
volatile uint32_t g_app_diag_frame_ack_timeout_count;

static void App_ConfigureRadioTransparentMode(void);
static void App_ConfigureRadioCommandMode(void);
static void App_WaitRadioModeSwitch(void);
static bool App_ConfigureRadioOnFirstBoot(void);
static void App_LoadRadioDefaultConfig(void);
static void App_LoadRadioRuntimeConfig(void);
static bool App_RadioConfigStorageIsValid(const app_radio_runtime_config_t *config);
static uint32_t App_RadioConfigCrc(const app_radio_runtime_config_t *config);
static bool App_RadioConfigMarkerIsWritten(void);
static void App_WriteRadioConfigMarker(void);
static void App_WriteRadioConfigStorage(bool marker_written);
static bool App_EnterRadioCommandMode(uint32_t baudrate, bool forward_to_host);
static bool App_FindRadioCommandBaudrate(uint32_t *active_baudrate, bool forward_to_host);
static bool App_QueryRadioConfig(bool forward_to_host);
static bool App_E28WriteConfig(bool forward_to_host);
static void App_E28BuildConfigFrame(uint8_t head, uint8_t frame[6]);
static uint16_t App_E28LocalAddress(void);
static uint8_t App_RadioLocalFrameId(void);
static bool App_RadioRoleIsMaster(void);
static bool App_RadioRoleIsSlave(void);
static bool App_RadioFilterIsEnabled(void);
static bool App_RadioAckIsEnabled(void);
static uint32_t App_RadioConfigMarkerValue(void);
static bool App_E28ResponseHasParameters(void);
static bool App_E28ResponseMatchesConfig(void);
static bool App_E28FrameMatchesAt(uint16_t index, const uint8_t expected[6]);
static void App_WriteHostHexDump(const uint8_t *bytes, uint16_t length);
static void App_SendRadioBytes(const uint8_t *bytes, uint16_t length);
static void App_ReadRadioFor(uint32_t wait_ms, bool forward_to_host);
static void App_ClearRadioInput(void);
static bool App_SetUartBaudrate(UART_HandleTypeDef *uart, uint32_t baudrate);
static void App_ServiceWatchdog(void);
#if APP_UART_STARTUP_BANNER
static void App_SendStartupBanner(void);
#endif
static bool App_TryReadUart(UART_HandleTypeDef *source, uint8_t *byte);
static void App_WriteUart(UART_HandleTypeDef *target, uint8_t byte);
static void App_WriteBytes(UART_HandleTypeDef *target, const uint8_t *bytes, uint16_t length);
#if APP_UART_SELF_TEST_ECHO
static void App_PumpUart(UART_HandleTypeDef *source, UART_HandleTypeDef *target);
#endif
static void App_PumpHostToRadio(void);
static void App_PumpRadioToHost(void);
static void App_ForwardHostByte(uint8_t byte);
#if APP_HOST_CFG_COMMAND_ENABLE
static bool App_TryHandleHostConfigByte(uint8_t byte);
static void App_FlushHostConfigCommand(void);
static void App_ProcessHostConfigCommand(void);
static bool App_ConfigCommandPrefixMatches(void);
static bool App_ParseConfigAssignment(char *key, uint16_t key_size, uint16_t *value, bool *hex_prefixed);
static bool App_ParseConfigRole(uint32_t *role);
static bool App_ParseConfigNumber(const uint8_t *bytes, uint16_t length, uint16_t *value);
static bool App_ApplyRadioRuntimeConfig(void);
static void App_PrintRuntimeConfig(void);
static void App_WriteHostString(const char *text);
static void App_WriteHostHex16(uint16_t value);
#endif
static void App_FrameParserFeed(app_frame_parser_t *parser, uint8_t byte, bool from_radio);
static void App_FrameParserReset(app_frame_parser_t *parser);
static void App_ProcessFrame(const uint8_t *frame, uint16_t length, bool from_radio);
static bool App_FrameIsValid(const uint8_t *frame, uint16_t length);
static bool App_FrameTargetMatches(uint8_t target);
static uint8_t App_FrameChecksum(const uint8_t *bytes, uint16_t length);
static void App_SendAck(uint8_t request_seq, uint8_t request_cmd);
static void App_RecordPendingAck(uint8_t target, uint8_t seq, uint8_t cmd);
static void App_CheckPendingAckTimeout(void);

void App_Init(const app_hw_t *hw)
{
  memset(&g_app, 0, sizeof(g_app));
  g_app.hw = *hw;
  App_LoadRadioDefaultConfig();
  App_LoadRadioRuntimeConfig();

  AppFrameworkLed_Init(&g_app.sys_led,
                       g_app.hw.sys_led_port,
                       g_app.hw.sys_led_pin,
                       APP_SYS_LED_ACTIVE_LEVEL,
                       APP_SYS_LED_INACTIVE_LEVEL,
                       HAL_GetTick());
  App_ConfigureRadioTransparentMode();
  (void)App_ConfigureRadioOnFirstBoot();
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
  App_PumpHostToRadio();
  App_PumpRadioToHost();
  App_CheckPendingAckTimeout();
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

static bool App_ConfigureRadioOnFirstBoot(void)
{
#if APP_RADIO_CONFIG_ON_FIRST_BOOT
  uint32_t active_baudrate = APP_RADIO_DEFAULT_BAUDRATE;
  bool configured = false;
  const bool forward_to_host = (APP_RADIO_CONFIG_FIRST_BOOT_FORWARD != 0U);

  if ((g_app.hw.radio_uart == NULL) || App_RadioConfigMarkerIsWritten())
  {
    return true;
  }

  ++g_app_diag_radio_config_first_boot_count;
  App_ReadRadioFor(APP_RADIO_CONFIG_STARTUP_WAIT_MS, forward_to_host);
  if (!App_FindRadioCommandBaudrate(&active_baudrate, forward_to_host))
  {
    (void)App_SetUartBaudrate(g_app.hw.radio_uart, APP_UART_BAUDRATE);
    App_ConfigureRadioTransparentMode();
    return false;
  }

  if (App_QueryRadioConfig(forward_to_host))
  {
    configured = App_E28WriteConfig(forward_to_host) &&
                 App_QueryRadioConfig(forward_to_host) &&
                 App_E28ResponseMatchesConfig();
  }
  if (configured)
  {
    App_WriteRadioConfigMarker();
  }
  (void)App_SetUartBaudrate(g_app.hw.radio_uart, APP_UART_BAUDRATE);
  App_ConfigureRadioTransparentMode();
  return configured;
#else
  return true;
#endif
}

static void App_LoadRadioRuntimeConfig(void)
{
  const app_radio_runtime_config_t *config =
      (const app_radio_runtime_config_t *)APP_RADIO_CONFIG_MARKER_ADDR;

  if (App_RadioConfigStorageIsValid(config))
  {
    g_app.radio_config = *config;
  }
}

static void App_LoadRadioDefaultConfig(void)
{
  memset(&g_app.radio_config, 0, sizeof(g_app.radio_config));
  g_app.radio_config.magic = APP_RADIO_CONFIG_FLASH_MAGIC;
  g_app.radio_config.version = APP_RADIO_CONFIG_FLASH_VERSION;
  g_app.radio_config.role = APP_RADIO_DEFAULT_ROLE;
  g_app.radio_config.local_id = APP_RADIO_DEFAULT_LOCAL_ID;
  g_app.radio_config.remote_id = APP_RADIO_DEFAULT_REMOTE_ID;
  g_app.radio_config.channel = APP_RADIO_E28_CHANNEL;
  g_app.radio_config.option = APP_RADIO_E28_OPTION;
  g_app.radio_config.filter_enable = APP_RADIO_DEFAULT_FILTER_ENABLE;
  g_app.radio_config.ack_enable = APP_RADIO_DEFAULT_ACK_ENABLE;
  g_app.radio_config.marker = 0xFFFFFFFFU;
  g_app.radio_config.crc = App_RadioConfigCrc(&g_app.radio_config);
}

static bool App_RadioConfigStorageIsValid(const app_radio_runtime_config_t *config)
{
  if (config == NULL)
  {
    return false;
  }
  if ((config->magic != APP_RADIO_CONFIG_FLASH_MAGIC) ||
      (config->version != APP_RADIO_CONFIG_FLASH_VERSION))
  {
    return false;
  }
  if ((config->role != APP_RADIO_ROLE_MASTER) &&
      (config->role != APP_RADIO_ROLE_SLAVE))
  {
    return false;
  }
  if ((config->local_id < 1U) || (config->local_id > 0xFEU) ||
      (config->remote_id < 1U) || (config->remote_id > 0xFEU) ||
      (config->channel > 0xFFU) ||
      (config->option > 0xFFU) ||
      (config->filter_enable > 1U) ||
      (config->ack_enable > 1U))
  {
    return false;
  }
  return config->crc == App_RadioConfigCrc(config);
}

static uint32_t App_RadioConfigCrc(const app_radio_runtime_config_t *config)
{
  uint32_t crc = 0xA5C35A3CU;

  if (config == NULL)
  {
    return 0U;
  }

  crc ^= config->magic;
  crc ^= config->version << 1;
  crc ^= config->role << 2;
  crc ^= config->local_id << 3;
  crc ^= config->remote_id << 4;
  crc ^= config->channel << 5;
  crc ^= config->option << 6;
  crc ^= config->filter_enable << 7;
  crc ^= config->ack_enable << 8;
  crc ^= config->marker << 9;
  return crc;
}

static bool App_RadioConfigMarkerIsWritten(void)
{
  const app_radio_runtime_config_t *config =
      (const app_radio_runtime_config_t *)APP_RADIO_CONFIG_MARKER_ADDR;

  return App_RadioConfigStorageIsValid(config) &&
         (config->role == g_app.radio_config.role) &&
         (config->local_id == g_app.radio_config.local_id) &&
         (config->remote_id == g_app.radio_config.remote_id) &&
         (config->channel == g_app.radio_config.channel) &&
         (config->option == g_app.radio_config.option) &&
         (config->filter_enable == g_app.radio_config.filter_enable) &&
         (config->ack_enable == g_app.radio_config.ack_enable) &&
         (config->marker == App_RadioConfigMarkerValue());
}

static void App_WriteRadioConfigMarker(void)
{
  App_WriteRadioConfigStorage(true);
}

static void App_WriteRadioConfigStorage(bool marker_written)
{
  FLASH_EraseInitTypeDef erase;
  uint32_t page_error = 0U;
  app_radio_runtime_config_t config = g_app.radio_config;
  const uint32_t *values = (const uint32_t *)&config;
  uint32_t address = APP_RADIO_CONFIG_MARKER_ADDR;
  uint32_t index;

  if (marker_written && App_RadioConfigMarkerIsWritten())
  {
    return;
  }

  config.magic = APP_RADIO_CONFIG_FLASH_MAGIC;
  config.version = APP_RADIO_CONFIG_FLASH_VERSION;
  config.marker = marker_written ? App_RadioConfigMarkerValue() : 0xFFFFFFFFU;
  config.crc = App_RadioConfigCrc(&config);

  HAL_FLASH_Unlock();
  erase.TypeErase = FLASH_TYPEERASE_PAGES;
  erase.PageAddress = APP_RADIO_CONFIG_MARKER_ADDR;
  erase.NbPages = 1U;
  if (HAL_FLASHEx_Erase(&erase, &page_error) == HAL_OK)
  {
    for (index = 0U; index < (sizeof(config) / sizeof(uint32_t)); ++index)
    {
      if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address, values[index]) != HAL_OK)
      {
        break;
      }
      address += 4U;
    }
  }
  HAL_FLASH_Lock();
  g_app.radio_config = config;
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

static bool App_E28WriteConfig(bool forward_to_host)
{
  uint8_t frame[6];

  App_E28BuildConfigFrame(0xC0U, frame);
  App_SendRadioBytes(frame, (uint16_t)sizeof(frame));
  App_ReadRadioFor(APP_RADIO_CONFIG_CMD_WAIT_MS, false);
  if (App_E28ResponseMatchesConfig())
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
  const uint16_t address = App_E28LocalAddress();

  frame[0] = head;
  frame[1] = (uint8_t)(address >> 8);
  frame[2] = (uint8_t)(address & 0xFFU);
  frame[3] = (uint8_t)((APP_RADIO_E28_UART_BAUD_BITS << 3) | APP_RADIO_E28_AIR_RATE_BITS);
  frame[4] = (uint8_t)g_app.radio_config.channel;
  frame[5] = (uint8_t)g_app.radio_config.option;
}

static uint16_t App_E28LocalAddress(void)
{
  if (App_RadioRoleIsMaster())
  {
    return (uint16_t)APP_RADIO_MASTER_ADDRESS;
  }
  return (uint16_t)(APP_RADIO_SLAVE_BASE_ADDR + g_app.radio_config.local_id);
}

static uint8_t App_RadioLocalFrameId(void)
{
  return (uint8_t)g_app.radio_config.local_id;
}

static bool App_RadioRoleIsMaster(void)
{
  return g_app.radio_config.role == APP_RADIO_ROLE_MASTER;
}

static bool App_RadioRoleIsSlave(void)
{
  return g_app.radio_config.role == APP_RADIO_ROLE_SLAVE;
}

static bool App_RadioFilterIsEnabled(void)
{
  return g_app.radio_config.filter_enable != 0U;
}

static bool App_RadioAckIsEnabled(void)
{
  return g_app.radio_config.ack_enable != 0U;
}

static uint32_t App_RadioConfigMarkerValue(void)
{
  uint32_t marker = 0xE2800000U;

  marker ^= ((uint32_t)g_app.radio_config.role & 0x0FU) << 24;
  marker ^= ((uint32_t)App_RadioLocalFrameId() & 0xFFU) << 16;
  marker ^= ((uint32_t)g_app.radio_config.remote_id & 0xFFU) << 12;
  marker ^= ((uint32_t)App_E28LocalAddress() & 0xFFFFU);
  marker ^= ((uint32_t)g_app.radio_config.channel & 0xFFU) << 8;
  marker ^= ((uint32_t)g_app.radio_config.option & 0xFFU);
  marker ^= ((uint32_t)APP_RADIO_E28_UART_BAUD_BITS & 0x07U) << 5;
  marker ^= ((uint32_t)APP_RADIO_E28_AIR_RATE_BITS & 0x07U) << 2;
  return marker;
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

static bool App_E28ResponseMatchesConfig(void)
{
  uint8_t expected[6];
  uint16_t index;

  if (g_app.radio_config_length < 6U)
  {
    return false;
  }

  App_E28BuildConfigFrame(0xC0U, expected);
  for (index = 0U; index <= (uint16_t)(g_app.radio_config_length - 6U); ++index)
  {
    if (App_E28FrameMatchesAt(index, expected))
    {
      return true;
    }
  }
  return false;
}

static bool App_E28FrameMatchesAt(uint16_t index, const uint8_t expected[6])
{
  uint8_t offset;

  if ((expected == NULL) || ((uint16_t)(index + 6U) > g_app.radio_config_length))
  {
    return false;
  }

  for (offset = 0U; offset < 6U; ++offset)
  {
    if (g_app.radio_config_bytes[index + offset] != expected[offset])
    {
      return false;
    }
  }
  return true;
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

static void App_SendRadioBytes(const uint8_t *bytes, uint16_t length)
{
  App_WriteBytes(g_app.hw.radio_uart, bytes, length);
}

static void App_ReadRadioFor(uint32_t wait_ms, bool forward_to_host)
{
  const uint32_t start = HAL_GetTick();
  uint8_t byte;

  g_app.radio_config_length = 0U;
  while ((HAL_GetTick() - start) < wait_ms)
  {
    App_ServiceWatchdog();
    while (App_TryReadUart(g_app.hw.radio_uart, &byte))
    {
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
  const uint8_t host1_banner[] = "USART2 HOST READY\r\n";
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

#if APP_UART_SELF_TEST_ECHO
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
  }
}
#endif

static void App_PumpHostToRadio(void)
{
  uint8_t byte;

  if ((g_app.hw.host_uart1 == NULL) || (g_app.hw.radio_uart == NULL))
  {
    return;
  }

  while (App_TryReadUart(g_app.hw.host_uart1, &byte))
  {
#if APP_HOST_CFG_COMMAND_ENABLE
    if (App_TryHandleHostConfigByte(byte))
    {
      continue;
    }
#endif
    App_ForwardHostByte(byte);
  }
}

static void App_ForwardHostByte(uint8_t byte)
{
  App_WriteUart(g_app.hw.radio_uart, byte);
  if (App_RadioFilterIsEnabled())
  {
    App_FrameParserFeed(&g_app.host_frame_parser, byte, false);
  }
}

#if APP_HOST_CFG_COMMAND_ENABLE
static bool App_TryHandleHostConfigByte(uint8_t byte)
{
  if (g_app.host_cfg_command_length == 0U)
  {
    if (byte != (uint8_t)'C')
    {
      return false;
    }
    g_app.host_cfg_command[g_app.host_cfg_command_length++] = byte;
    return true;
  }

  if (g_app.host_cfg_command_length >= (uint16_t)sizeof(g_app.host_cfg_command))
  {
    App_FlushHostConfigCommand();
    return false;
  }

  g_app.host_cfg_command[g_app.host_cfg_command_length++] = byte;
  if (!App_ConfigCommandPrefixMatches())
  {
    App_FlushHostConfigCommand();
    return true;
  }

  if ((byte == (uint8_t)'\r') ||
      (byte == (uint8_t)'\n') ||
      ((g_app.host_cfg_command_length == 4U) && (byte == (uint8_t)'?')))
  {
    App_ProcessHostConfigCommand();
  }

  return true;
}

static void App_FlushHostConfigCommand(void)
{
  uint16_t index;

  for (index = 0U; index < g_app.host_cfg_command_length; ++index)
  {
    App_ForwardHostByte(g_app.host_cfg_command[index]);
  }
  g_app.host_cfg_command_length = 0U;
}

static void App_ProcessHostConfigCommand(void)
{
  char key[8];
  uint16_t value = 0U;
  uint32_t role = APP_RADIO_ROLE_SLAVE;
  bool hex_prefixed = false;

  if ((g_app.host_cfg_command_length >= 4U) &&
      (g_app.host_cfg_command[0] == (uint8_t)'C') &&
      (g_app.host_cfg_command[1] == (uint8_t)'F') &&
      (g_app.host_cfg_command[2] == (uint8_t)'G') &&
      ((g_app.host_cfg_command[3] == (uint8_t)'?') ||
       (g_app.host_cfg_command[3] == (uint8_t)' ') ||
       (g_app.host_cfg_command[3] == (uint8_t)'\r') ||
       (g_app.host_cfg_command[3] == (uint8_t)'\n')))
  {
    if ((g_app.host_cfg_command[3] == (uint8_t)'?') ||
        (g_app.host_cfg_command[3] == (uint8_t)'\r') ||
        (g_app.host_cfg_command[3] == (uint8_t)'\n'))
    {
      App_PrintRuntimeConfig();
    }
    else if (App_ParseConfigRole(&role))
    {
      g_app.radio_config.role = role;
      App_WriteRadioConfigStorage(false);
      App_WriteHostString("OK\r\n");
    }
    else if (App_ParseConfigAssignment(key, (uint16_t)sizeof(key), &value, &hex_prefixed))
    {
      if (strcmp(key, "ID") == 0)
      {
        if ((value < 1U) || (value > 0xFEU))
        {
          App_WriteHostString("ERR ID_RANGE\r\n");
        }
        else
        {
          g_app.radio_config.local_id = value;
          App_WriteRadioConfigStorage(false);
          App_WriteHostString("OK\r\n");
        }
      }
      else if (strcmp(key, "REMOTE") == 0)
      {
        if ((value < 1U) || (value > 0xFEU))
        {
          App_WriteHostString("ERR REMOTE_RANGE\r\n");
        }
        else
        {
          g_app.radio_config.remote_id = value;
          App_WriteRadioConfigStorage(false);
          App_WriteHostString("OK\r\n");
        }
      }
      else if (strcmp(key, "CH") == 0)
      {
        if (!hex_prefixed && (value <= 99U))
        {
          value = (uint16_t)(((value / 10U) << 4) | (value % 10U));
        }
        if (value > 0xFFU)
        {
          App_WriteHostString("ERR CH_RANGE\r\n");
        }
        else
        {
          g_app.radio_config.channel = value;
          App_WriteRadioConfigStorage(false);
          App_WriteHostString("OK\r\n");
        }
      }
      else if (strcmp(key, "OPTION") == 0)
      {
        if (!hex_prefixed && (value <= 99U))
        {
          value = (uint16_t)(((value / 10U) << 4) | (value % 10U));
        }
        if (value > 0xFFU)
        {
          App_WriteHostString("ERR OPTION_RANGE\r\n");
        }
        else
        {
          g_app.radio_config.option = value;
          App_WriteRadioConfigStorage(false);
          App_WriteHostString("OK\r\n");
        }
      }
      else if (strcmp(key, "FILTER") == 0)
      {
        if (value > 1U)
        {
          App_WriteHostString("ERR FILTER_RANGE\r\n");
        }
        else
        {
          g_app.radio_config.filter_enable = value;
          App_WriteRadioConfigStorage(false);
          App_WriteHostString("OK\r\n");
        }
      }
      else if (strcmp(key, "ACK") == 0)
      {
        if (value > 1U)
        {
          App_WriteHostString("ERR ACK_RANGE\r\n");
        }
        else
        {
          g_app.radio_config.ack_enable = value;
          App_WriteRadioConfigStorage(false);
          App_WriteHostString("OK\r\n");
        }
      }
      else
      {
        App_WriteHostString("ERR KEY\r\n");
      }
    }
    else if ((g_app.host_cfg_command_length >= 9U) &&
             (memcmp(&g_app.host_cfg_command[4], "SAVE", 4U) == 0))
    {
      App_WriteRadioConfigStorage(App_RadioConfigMarkerIsWritten());
      App_WriteHostString("OK\r\n");
    }
    else if ((g_app.host_cfg_command_length >= 10U) &&
             (memcmp(&g_app.host_cfg_command[4], "APPLY", 5U) == 0))
    {
      App_WriteRadioConfigStorage(false);
      if (App_ApplyRadioRuntimeConfig())
      {
        App_WriteHostString("OK\r\n");
      }
      else
      {
        App_WriteHostString("ERR RADIO_CONFIG\r\n");
      }
    }
    else if ((g_app.host_cfg_command_length >= 12U) &&
             (memcmp(&g_app.host_cfg_command[4], "DEFAULT", 7U) == 0))
    {
      App_LoadRadioDefaultConfig();
      App_WriteRadioConfigStorage(false);
      App_WriteHostString("OK\r\n");
    }
    else
    {
      App_WriteHostString("ERR CMD\r\n");
    }
  }
  else
  {
    App_FlushHostConfigCommand();
    return;
  }

  g_app.host_cfg_command_length = 0U;
}

static bool App_ConfigCommandPrefixMatches(void)
{
  static const uint8_t prefix[] = {'C', 'F', 'G'};
  uint16_t index;

  for (index = 0U; (index < g_app.host_cfg_command_length) && (index < 3U); ++index)
  {
    if (g_app.host_cfg_command[index] != prefix[index])
    {
      return false;
    }
  }
  if ((g_app.host_cfg_command_length >= 4U) &&
      (g_app.host_cfg_command[3] != (uint8_t)' ') &&
      (g_app.host_cfg_command[3] != (uint8_t)'?') &&
      (g_app.host_cfg_command[3] != (uint8_t)'\r') &&
      (g_app.host_cfg_command[3] != (uint8_t)'\n'))
  {
    return false;
  }
  return true;
}

static bool App_ParseConfigAssignment(char *key,
                                      uint16_t key_size,
                                      uint16_t *value,
                                      bool *hex_prefixed)
{
  uint16_t index = 4U;
  uint16_t key_index = 0U;
  uint16_t value_start;

  if ((key == NULL) || (key_size == 0U) || (value == NULL) || (hex_prefixed == NULL))
  {
    return false;
  }

  while ((index < g_app.host_cfg_command_length) &&
         (g_app.host_cfg_command[index] == (uint8_t)' '))
  {
    ++index;
  }
  while ((index < g_app.host_cfg_command_length) &&
         (g_app.host_cfg_command[index] != (uint8_t)'='))
  {
    if (key_index + 1U >= key_size)
    {
      return false;
    }
    key[key_index++] = (char)g_app.host_cfg_command[index++];
  }
  if ((key_index == 0U) || (index >= g_app.host_cfg_command_length))
  {
    return false;
  }
  key[key_index] = '\0';
  value_start = (uint16_t)(index + 1U);
  *hex_prefixed = ((value_start + 1U) < g_app.host_cfg_command_length) &&
                  (g_app.host_cfg_command[value_start] == (uint8_t)'0') &&
                  ((g_app.host_cfg_command[value_start + 1U] == (uint8_t)'x') ||
                   (g_app.host_cfg_command[value_start + 1U] == (uint8_t)'X'));
  return App_ParseConfigNumber(&g_app.host_cfg_command[value_start],
                               (uint16_t)(g_app.host_cfg_command_length - value_start),
                               value);
}

static bool App_ParseConfigRole(uint32_t *role)
{
  if (role == NULL)
  {
    return false;
  }
  if ((g_app.host_cfg_command_length >= 16U) &&
      (memcmp(&g_app.host_cfg_command[4], "ROLE=MASTER", 11U) == 0))
  {
    *role = APP_RADIO_ROLE_MASTER;
    return true;
  }
  if ((g_app.host_cfg_command_length >= 15U) &&
      (memcmp(&g_app.host_cfg_command[4], "ROLE=SLAVE", 10U) == 0))
  {
    *role = APP_RADIO_ROLE_SLAVE;
    return true;
  }
  return false;
}

static bool App_ParseConfigNumber(const uint8_t *bytes, uint16_t length, uint16_t *value)
{
  uint16_t index = 0U;
  uint16_t parsed = 0U;
  bool has_digit = false;
  bool hex = false;

  if ((bytes == NULL) || (value == NULL))
  {
    return false;
  }

  if ((length >= 2U) && (bytes[0] == (uint8_t)'0') &&
      ((bytes[1] == (uint8_t)'x') || (bytes[1] == (uint8_t)'X')))
  {
    hex = true;
    index = 2U;
  }

  for (; index < length; ++index)
  {
    const uint8_t ch = bytes[index];
    uint8_t digit;

    if ((ch == (uint8_t)'\r') || (ch == (uint8_t)'\n'))
    {
      break;
    }
    if ((ch >= (uint8_t)'0') && (ch <= (uint8_t)'9'))
    {
      digit = (uint8_t)(ch - (uint8_t)'0');
    }
    else if (hex && (ch >= (uint8_t)'A') && (ch <= (uint8_t)'F'))
    {
      digit = (uint8_t)(10U + ch - (uint8_t)'A');
    }
    else if (hex && (ch >= (uint8_t)'a') && (ch <= (uint8_t)'f'))
    {
      digit = (uint8_t)(10U + ch - (uint8_t)'a');
    }
    else
    {
      return false;
    }

    has_digit = true;
    parsed = hex ? (uint16_t)((parsed << 4) | digit)
                 : (uint16_t)((parsed * 10U) + digit);
  }

  if (!has_digit)
  {
    return false;
  }

  *value = parsed;
  return true;
}

static bool App_ApplyRadioRuntimeConfig(void)
{
  if (!App_ConfigureRadioOnFirstBoot())
  {
    return false;
  }
  App_FrameParserReset(&g_app.host_frame_parser);
  App_FrameParserReset(&g_app.radio_frame_parser);
  g_app.pending_ack.active = false;
  return true;
}

static void App_PrintRuntimeConfig(void)
{
  App_WriteHostString("CFG ROLE=");
  App_WriteHostString(App_RadioRoleIsMaster() ? "MASTER" : "SLAVE");
  App_WriteHostString(" ID=");
  App_WriteHostHex16(App_RadioLocalFrameId());
  App_WriteHostString(" REMOTE=");
  App_WriteHostHex16((uint16_t)g_app.radio_config.remote_id);
  App_WriteHostString(" CH=");
  App_WriteHostHex16((uint16_t)g_app.radio_config.channel);
  App_WriteHostString(" OPTION=");
  App_WriteHostHex16((uint16_t)g_app.radio_config.option);
  App_WriteHostString(" FILTER=");
  App_WriteHostString(App_RadioFilterIsEnabled() ? "1" : "0");
  App_WriteHostString(" ACK=");
  App_WriteHostString(App_RadioAckIsEnabled() ? "1" : "0");
  App_WriteHostString(" E28=");
  App_WriteHostHex16(App_E28LocalAddress());
  App_WriteHostString("\r\n");
}

static void App_WriteHostString(const char *text)
{
  if (text == NULL)
  {
    return;
  }
  App_WriteBytes(g_app.hw.host_uart1, (const uint8_t *)text, (uint16_t)strlen(text));
}

static void App_WriteHostHex16(uint16_t value)
{
  static const char hex[] = "0123456789ABCDEF";
  char out[4];

  out[0] = hex[(value >> 12) & 0x0FU];
  out[1] = hex[(value >> 8) & 0x0FU];
  out[2] = hex[(value >> 4) & 0x0FU];
  out[3] = hex[value & 0x0FU];
  App_WriteBytes(g_app.hw.host_uart1, (const uint8_t *)out, (uint16_t)sizeof(out));
}
#endif

static void App_PumpRadioToHost(void)
{
  uint8_t byte;

  if ((g_app.hw.radio_uart == NULL) || (g_app.hw.host_uart1 == NULL))
  {
    return;
  }

  while (App_TryReadUart(g_app.hw.radio_uart, &byte))
  {
    if (App_RadioFilterIsEnabled())
    {
      App_FrameParserFeed(&g_app.radio_frame_parser, byte, true);
    }
    else
    {
      App_WriteUart(g_app.hw.host_uart1, byte);
    }
  }
}

static void App_FrameParserFeed(app_frame_parser_t *parser, uint8_t byte, bool from_radio)
{
  if (parser == NULL)
  {
    return;
  }

  if (parser->length == 0U)
  {
    if (byte != APP_FRAME_HEAD)
    {
      if (from_radio)
      {
        ++g_app_diag_frame_drop_count;
      }
      return;
    }
    parser->bytes[parser->length++] = byte;
    parser->expected_length = 0U;
    return;
  }

  parser->bytes[parser->length++] = byte;
  if (parser->length == 2U)
  {
    if ((byte < APP_FRAME_MIN_BODY_LEN) || (byte > APP_FRAME_MAX_BODY_LEN))
    {
      App_FrameParserReset(parser);
      if (from_radio)
      {
        ++g_app_diag_frame_drop_count;
      }
      return;
    }
    parser->expected_length = (uint16_t)byte + 3U;
  }

  if ((parser->expected_length > 0U) && (parser->length >= parser->expected_length))
  {
    App_ProcessFrame(parser->bytes, parser->expected_length, from_radio);
    App_FrameParserReset(parser);
  }
  else if (parser->length >= (uint16_t)sizeof(parser->bytes))
  {
    App_FrameParserReset(parser);
    if (from_radio)
    {
      ++g_app_diag_frame_drop_count;
    }
  }
}

static void App_FrameParserReset(app_frame_parser_t *parser)
{
  if (parser == NULL)
  {
    return;
  }
  parser->length = 0U;
  parser->expected_length = 0U;
}

static void App_ProcessFrame(const uint8_t *frame, uint16_t length, bool from_radio)
{
  uint8_t target;
  uint8_t seq;
  uint8_t cmd;
  bool is_ack;

  if (!App_FrameIsValid(frame, length))
  {
    if (from_radio)
    {
      ++g_app_diag_frame_drop_count;
    }
    return;
  }

  target = frame[2];
  seq = frame[4];
  cmd = frame[5];
  is_ack = ((cmd & APP_FRAME_CMD_ACK_BASE) != 0U);

  if (from_radio)
  {
    if (!App_FrameTargetMatches(target))
    {
      ++g_app_diag_frame_drop_count;
      return;
    }

    ++g_app_diag_frame_rx_count;
    App_WriteBytes(g_app.hw.host_uart1, frame, length);

    if (App_RadioRoleIsSlave() &&
        App_RadioAckIsEnabled() &&
        (target == App_RadioLocalFrameId()) &&
        !is_ack)
    {
      App_SendAck(seq, cmd);
    }
    if (App_RadioRoleIsMaster() &&
        App_RadioAckIsEnabled() &&
        is_ack &&
        g_app.pending_ack.active &&
        (frame[3] == g_app.pending_ack.target) &&
        (seq == g_app.pending_ack.seq) &&
        ((uint8_t)(cmd & (uint8_t)~APP_FRAME_CMD_ACK_BASE) == g_app.pending_ack.cmd))
    {
      g_app.pending_ack.active = false;
      ++g_app_diag_frame_ack_rx_count;
    }
  }
  else
  {
    if (App_RadioRoleIsMaster() &&
        App_RadioAckIsEnabled() &&
        (target != APP_FRAME_TARGET_BROADCAST) &&
        !is_ack)
    {
      App_RecordPendingAck(target, seq, cmd);
    }
  }
}

static bool App_FrameIsValid(const uint8_t *frame, uint16_t length)
{
  uint16_t expected_length;

  if ((frame == NULL) || (length < (APP_FRAME_MIN_BODY_LEN + 3U)))
  {
    return false;
  }
  if (frame[0] != APP_FRAME_HEAD)
  {
    return false;
  }
  if ((frame[1] < APP_FRAME_MIN_BODY_LEN) || (frame[1] > APP_FRAME_MAX_BODY_LEN))
  {
    return false;
  }
  expected_length = (uint16_t)frame[1] + 3U;
  if (expected_length != length)
  {
    return false;
  }
  return App_FrameChecksum(frame, (uint16_t)(length - 1U)) == frame[length - 1U];
}

static bool App_FrameTargetMatches(uint8_t target)
{
  return (target == App_RadioLocalFrameId()) ||
         (target == APP_FRAME_TARGET_BROADCAST);
}

static uint8_t App_FrameChecksum(const uint8_t *bytes, uint16_t length)
{
  uint16_t index;
  uint8_t sum = 0U;

  if (bytes == NULL)
  {
    return 0U;
  }

  for (index = 0U; index < length; ++index)
  {
    sum = (uint8_t)(sum + bytes[index]);
  }
  return sum;
}

static void App_SendAck(uint8_t request_seq, uint8_t request_cmd)
{
  uint8_t ack[8];

  ack[0] = APP_FRAME_HEAD;
  ack[1] = 5U;
  ack[2] = (uint8_t)g_app.radio_config.remote_id;
  ack[3] = App_RadioLocalFrameId();
  ack[4] = request_seq;
  ack[5] = (uint8_t)(request_cmd | APP_FRAME_CMD_ACK_BASE);
  ack[6] = APP_FRAME_ACK_STATUS_OK;
  ack[7] = App_FrameChecksum(ack, 7U);
  App_SendRadioBytes(ack, (uint16_t)sizeof(ack));
  ++g_app_diag_frame_ack_tx_count;
}

static void App_RecordPendingAck(uint8_t target, uint8_t seq, uint8_t cmd)
{
  g_app.pending_ack.active = true;
  g_app.pending_ack.target = target;
  g_app.pending_ack.seq = seq;
  g_app.pending_ack.cmd = cmd;
  g_app.pending_ack.deadline_ms = HAL_GetTick() + APP_FRAME_ACK_TIMEOUT_MS;
}

static void App_CheckPendingAckTimeout(void)
{
  if (App_RadioRoleIsMaster() &&
      App_RadioAckIsEnabled() &&
      g_app.pending_ack.active &&
      ((int32_t)(HAL_GetTick() - g_app.pending_ack.deadline_ms) >= 0))
  {
    g_app.pending_ack.active = false;
    ++g_app_diag_frame_ack_timeout_count;
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
