#define App_Run App_Run_Legacy
#define App_PumpHostToRadio App_PumpHostToRadio_Legacy
#define App_ForwardHostByte App_ForwardHostByte_Legacy
#include "app_legacy.c"
#undef App_ForwardHostByte
#undef App_PumpHostToRadio
#undef App_Run

static void App_PumpHostToRadio(void);
static void App_ForwardHostByte(uint8_t byte);

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
  static uint8_t body[APP_FRAME_MIN_BODY_LEN];
  static uint16_t body_length;
  uint8_t frame[APP_FRAME_MIN_BODY_LEN + 3U];

  if (!App_RadioFilterIsEnabled())
  {
    App_WriteUart(g_app.hw.radio_uart, byte);
    return;
  }

  body[body_length++] = byte;
  if (body_length < APP_FRAME_MIN_BODY_LEN)
  {
    return;
  }

  frame[0] = APP_FRAME_HEAD;
  frame[1] = APP_FRAME_MIN_BODY_LEN;
  frame[2] = body[0];
  frame[3] = body[1];
  frame[4] = body[2];
  frame[5] = body[3];
  frame[6] = App_FrameChecksum(frame, 6U);

  App_SendRadioBytes(frame, (uint16_t)sizeof(frame));
  App_ProcessFrame(frame, (uint16_t)sizeof(frame), false);

  body_length = 0U;
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
#if APP_HOST_CFG_COMMAND_ENABLE
  App_CheckCfgModeTimeout();
#endif
#endif
  AppFrameworkLed_Update(&g_app.sys_led, true, APP_SYS_LED_TOGGLE_MS, now);
}
