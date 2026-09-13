#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"

void Error_Handler(void);

/* LCD pinleri */
#define BLK_Pin             GPIO_PIN_0
#define BLK_GPIO_Port       GPIOA
#define RST_Pin             GPIO_PIN_1
#define RST_GPIO_Port       GPIOA
#define DC_Pin              GPIO_PIN_2
#define DC_GPIO_Port        GPIOA

/* Buton pinleri */
#define BTN_MODE_Pin        GPIO_PIN_3       /* Mod değiştir / uyandır */
#define BTN_MODE_GPIO_Port  GPIOA
#define BTN_ACTION_Pin      GPIO_PIN_4
#define BTN_ACTION_GPIO_Port GPIOA

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
