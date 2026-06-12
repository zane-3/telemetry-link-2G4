#include "app_crsf.h"

#define CRSF_ADDRESS_FLIGHT_CONTROLLER 0xC8U
#define CRSF_FRAMETYPE_RC_CHANNELS_PACKED 0x16U
#define CRSF_FRAME_SIZE_RC_CHANNELS    0x18U
#define CRSF_PAYLOAD_SIZE_RC_CHANNELS  22U
#define CRSF_CRC_POLY                  0xD5U

static uint8_t AppCrsf_Crc8(const uint8_t *data, uint8_t length);
static void AppCrsf_PackChannels(const uint16_t channels[APP_CRSF_CHANNEL_COUNT], uint8_t packed[CRSF_PAYLOAD_SIZE_RC_CHANNELS]);
static uint16_t AppCrsf_ClampChannel(uint16_t value);
static void AppCrsf_SendFrame(UART_HandleTypeDef *uart, const uint8_t *frame, uint8_t length);

void AppCrsf_SendRcChannels(UART_HandleTypeDef *uart, const uint16_t channels[APP_CRSF_CHANNEL_COUNT])
{
  uint8_t frame[26];
  uint16_t clamped_channels[APP_CRSF_CHANNEL_COUNT];
  uint8_t i;

  if (uart == NULL || channels == NULL)
  {
    return;
  }

  // Clamp all channels to valid CRSF range
  for (i = 0; i < APP_CRSF_CHANNEL_COUNT; i++)
  {
    clamped_channels[i] = AppCrsf_ClampChannel(channels[i]);
  }

  // Build CRSF frame
  frame[0] = CRSF_ADDRESS_FLIGHT_CONTROLLER;
  frame[1] = CRSF_FRAME_SIZE_RC_CHANNELS;
  frame[2] = CRSF_FRAMETYPE_RC_CHANNELS_PACKED;

  // Pack 16 channels into 22 bytes
  AppCrsf_PackChannels(clamped_channels, &frame[3]);

  // Calculate CRC over type + payload
  frame[25] = AppCrsf_Crc8(&frame[2], 23);

  AppCrsf_SendFrame(uart, frame, 26);
}

void AppCrsf_SendFailsafe(UART_HandleTypeDef *uart)
{
  uint16_t failsafe_channels[APP_CRSF_CHANNEL_COUNT];
  uint8_t i;

  // CH1 Roll = 992 (mid)
  failsafe_channels[0] = APP_CRSF_CHANNEL_MID;
  // CH2 Pitch = 992 (mid)
  failsafe_channels[1] = APP_CRSF_CHANNEL_MID;
  // CH3 Throttle = 172 (min)
  failsafe_channels[2] = APP_CRSF_CHANNEL_MIN;
  // CH4 Yaw = 992 (mid)
  failsafe_channels[3] = APP_CRSF_CHANNEL_MID;
  // CH5 Arm = 172 (disarm)
  failsafe_channels[4] = APP_CRSF_CHANNEL_MIN;
  // CH6-16 = 992 (mid)
  for (i = 5; i < APP_CRSF_CHANNEL_COUNT; i++)
  {
    failsafe_channels[i] = APP_CRSF_CHANNEL_MID;
  }

  AppCrsf_SendRcChannels(uart, failsafe_channels);
}

static uint16_t AppCrsf_ClampChannel(uint16_t value)
{
  if (value < APP_CRSF_CHANNEL_MIN)
  {
    return APP_CRSF_CHANNEL_MIN;
  }
  if (value > APP_CRSF_CHANNEL_MAX)
  {
    return APP_CRSF_CHANNEL_MAX;
  }
  return value;
}

static void AppCrsf_PackChannels(const uint16_t channels[APP_CRSF_CHANNEL_COUNT], uint8_t packed[CRSF_PAYLOAD_SIZE_RC_CHANNELS])
{
  // CRSF uses 11-bit per channel, packed into bytes
  // 16 channels * 11 bits = 176 bits = 22 bytes

  packed[0]  = (uint8_t)(channels[0] & 0x07FF);
  packed[1]  = (uint8_t)((channels[0] & 0x07FF) >> 8 | (channels[1] & 0x07FF) << 3);
  packed[2]  = (uint8_t)((channels[1] & 0x07FF) >> 5 | (channels[2] & 0x07FF) << 6);
  packed[3]  = (uint8_t)((channels[2] & 0x07FF) >> 2);
  packed[4]  = (uint8_t)((channels[2] & 0x07FF) >> 10 | (channels[3] & 0x07FF) << 1);
  packed[5]  = (uint8_t)((channels[3] & 0x07FF) >> 7 | (channels[4] & 0x07FF) << 4);
  packed[6]  = (uint8_t)((channels[4] & 0x07FF) >> 4 | (channels[5] & 0x07FF) << 7);
  packed[7]  = (uint8_t)((channels[5] & 0x07FF) >> 1);
  packed[8]  = (uint8_t)((channels[5] & 0x07FF) >> 9 | (channels[6] & 0x07FF) << 2);
  packed[9]  = (uint8_t)((channels[6] & 0x07FF) >> 6 | (channels[7] & 0x07FF) << 5);
  packed[10] = (uint8_t)((channels[7] & 0x07FF) >> 3);
  packed[11] = (uint8_t)((channels[8] & 0x07FF));
  packed[12] = (uint8_t)((channels[8] & 0x07FF) >> 8 | (channels[9] & 0x07FF) << 3);
  packed[13] = (uint8_t)((channels[9] & 0x07FF) >> 5 | (channels[10] & 0x07FF) << 6);
  packed[14] = (uint8_t)((channels[10] & 0x07FF) >> 2);
  packed[15] = (uint8_t)((channels[10] & 0x07FF) >> 10 | (channels[11] & 0x07FF) << 1);
  packed[16] = (uint8_t)((channels[11] & 0x07FF) >> 7 | (channels[12] & 0x07FF) << 4);
  packed[17] = (uint8_t)((channels[12] & 0x07FF) >> 4 | (channels[13] & 0x07FF) << 7);
  packed[18] = (uint8_t)((channels[13] & 0x07FF) >> 1);
  packed[19] = (uint8_t)((channels[13] & 0x07FF) >> 9 | (channels[14] & 0x07FF) << 2);
  packed[20] = (uint8_t)((channels[14] & 0x07FF) >> 6 | (channels[15] & 0x07FF) << 5);
  packed[21] = (uint8_t)((channels[15] & 0x07FF) >> 3);
}

static uint8_t AppCrsf_Crc8(const uint8_t *data, uint8_t length)
{
  uint8_t crc = 0;
  uint8_t i, j;

  for (i = 0; i < length; i++)
  {
    crc ^= data[i];
    for (j = 0; j < 8; j++)
    {
      if (crc & 0x80)
      {
        crc = (uint8_t)((crc << 1) ^ CRSF_CRC_POLY);
      }
      else
      {
        crc = (uint8_t)(crc << 1);
      }
    }
  }
  return crc;
}

static void AppCrsf_SendFrame(UART_HandleTypeDef *uart, const uint8_t *frame, uint8_t length)
{
  uint8_t i;
  uint32_t start;
  const uint32_t timeout_ms = 10;

  if (uart == NULL || frame == NULL)
  {
    return;
  }

  for (i = 0; i < length; i++)
  {
    start = HAL_GetTick();
    while (__HAL_UART_GET_FLAG(uart, UART_FLAG_TXE) == RESET)
    {
      if ((HAL_GetTick() - start) >= timeout_ms)
      {
        return;
      }
    }
    uart->Instance->DR = frame[i];
  }
}
