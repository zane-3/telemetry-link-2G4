#ifndef APP_CRSF_H
#define APP_CRSF_H

#include "stm32f1xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

#define APP_CRSF_CHANNEL_COUNT         16U
#define APP_CRSF_CHANNEL_MIN           172U
#define APP_CRSF_CHANNEL_MID           992U
#define APP_CRSF_CHANNEL_MAX           1811U

void AppCrsf_SendRcChannels(UART_HandleTypeDef *uart, const uint16_t channels[APP_CRSF_CHANNEL_COUNT]);
void AppCrsf_SendFailsafe(UART_HandleTypeDef *uart);

#endif
