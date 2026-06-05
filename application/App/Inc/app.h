#ifndef APP_H
#define APP_H

#include "main.h"

typedef struct
{
  UART_HandleTypeDef *radio_uart;
  UART_HandleTypeDef *host_uart1;
  UART_HandleTypeDef *host_uart2;
  GPIO_TypeDef *sys_led_port;
  uint16_t sys_led_pin;
  GPIO_TypeDef *radio_m0_port;
  uint16_t radio_m0_pin;
  GPIO_TypeDef *radio_m1_port;
  uint16_t radio_m1_pin;
  GPIO_TypeDef *radio_m2_port;
  uint16_t radio_m2_pin;
  GPIO_TypeDef *radio_aux_port;
  uint16_t radio_aux_pin;
} app_hw_t;

void App_Init(const app_hw_t *hw);
void App_Run(void);

#endif
