#define App_Run App_Run_Legacy
#define App_PumpHostToRadio App_PumpHostToRadio_Legacy
#define App_ForwardHostByte App_ForwardHostByte_Legacy
#define App_TryHandleHostConfigByte App_TryHandleHostConfigByte_Legacy
#include "app_legacy.c"
#undef App_TryHandleHostConfigByte
#undef App_ForwardHostByte
#undef App_PumpHostToRadio
#undef App_Run

#ifndef APP_HOST_PACKET_IDLE_TIMEOUT_MS
#define APP_HOST_PACKET_IDLE_TIMEOUT_MS 20U
#endif

static void App_PumpHostToRadio(void);
static void App_ForwardHostByte(uint8_t byte);
static void App_FlushHostBodyPacket(void);
static void App_CheckHostBodyPacketTimeout(void);

static uint8_t g_host_body_packet[APP_FRAME_MAX_BODY_LEN];
static uint16_t g_host_body_packet_length;
static uint32_t g_host_body_packet_deadline_ms;

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
    if (g_app.cfg_mode_active || (g_app.cfg_mode_prefix_index > 0U) || (byte == (uint8_t)'+'))
    {
      if (App_TryHandleHostConfigByte_Legacy(byte))
      {
        continue;
      }
    }
#endif
    App_ForwardHostByte(byte);
  }
}

static void App_ForwardHostByte(uint8_t byte)
{
  if (!App_RadioFilterIsEnabled())
  {
    App_WriteUart(g_app.hw.radio_uart, byte);
    return;
  }

  if (g_host_body_packet_length >= APP_FRAME_MAX_BODY_LEN)
  {
    App_FlushHostBodyPacket();
  }

  if (g_host_body_packet_length < APP_FRAME_MAX_BODY_LEN)
  {
    g_host_body_packet[g_host_body_packet_length++] = byte;
    g_host_body_packet_deadline_ms = HAL_GetTick() + APP_HOST_PACKET_IDLE_TIMEOUT_MS;
  }

  if (g_host_body_packet_length >= APP_FRAME_MAX_BODY_LEN)
  {
    App_FlushHostBodyPacket();
  }
}

static void App_FlushHostBodyPacket(void)
{
  uint8_t frame[APP_FRAME_MAX_SIZE];
  uint16_t index;
  uint16_t frame_length;

  if (g_host_body_packet_length == 0U)
  {
    return;
  }

  if (g_host_body_packet_length < APP_FRAME_MIN_BODY_LEN)
  {
    g_host_body_packet_length = 0U;
    return;
  }

  frame_length = (uint16_t)(g_host_body_packet_length + 3U);
  frame[0] = APP_FRAME_HEAD;
  frame[1] = (uint8_t)g_host_body_packet_length;
  for (index = 0U; index < g_host_body_packet_length; ++index)
  {
    frame[(uint16_t)(2U + index)] = g_host_body_packet[index];
  }
  frame[frame_length - 1U] = App_FrameChecksum(frame, (uint16_t)(frame_length - 1U));

  App_SendRadioBytes(frame, frame_length);
  App_ProcessFrame(frame, frame_length, false);

  g_host_body_packet_length = 0U;
}

static void App_CheckHostBodyPacketTimeout(void)
{
  if (App_RadioFilterIsEnabled() &&
      (g_host_body_packet_length > 0U) &&
      ((int32_t)(HAL_GetTick() - g_host_body_packet_deadline_ms) >= 0))
  {
    App_FlushHostBodyPacket();
  }
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
  App_CheckHostBodyPacketTimeout();
  App_PumpRadioToHost();
  App_CheckPendingAckTimeout();
#if APP_HOST_CFG_COMMAND_ENABLE
  App_CheckCfgModeTimeout();
#endif
#endif
  AppFrameworkLed_Update(&g_app.sys_led, true, APP_SYS_LED_TOGGLE_MS, now);
}
