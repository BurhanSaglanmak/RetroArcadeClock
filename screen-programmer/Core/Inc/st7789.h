#ifndef ST7789_DRIVER_H
#define ST7789_DRIVER_H

#include "stm32f4xx_hal.h"

#define LCD_W    240
#define LCD_H    240

#define BLK_ON()   HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_SET)
#define BLK_OFF()  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_RESET)
#define RST_HIGH() HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_SET)
#define RST_LOW()  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_RESET)
#define DC_HIGH()  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_SET)
#define DC_LOW()   HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_RESET)

#define COLOR_BLACK    0x0000
#define COLOR_WHITE    0xFFFF
#define COLOR_RED      0xF800
#define COLOR_GREEN    0x07E0
#define COLOR_BLUE     0x001F
#define COLOR_CYAN     0x07FF
#define COLOR_YELLOW   0xFFE0
#define COLOR_MAGENTA  0xF81F
#define COLOR_ORANGE   0xFD20
#define COLOR_GRAY     0x8410
#define COLOR_DARKGRAY 0x4208
#define COLOR_BROWN    0x8200
#define COLOR_DARKGREEN 0x03E0
#define COLOR_LIGHTGREEN 0x87E0

#define FONT_W    5
#define FONT_H    8
#define FONT_ADV  6

void ST7789_Init(void);
void ST7789_FillScreen(uint16_t color);
void ST7789_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void ST7789_DrawPixel(uint16_t x, uint16_t y, uint16_t color);
void ST7789_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void ST7789_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
void ST7789_DrawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);
void ST7789_FillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);
void ST7789_DrawChar  (uint16_t x, uint16_t y, char ch,
                       uint16_t color, uint16_t bg, uint8_t size);
void ST7789_DrawString(uint16_t x, uint16_t y, const char *str,
                       uint16_t color, uint16_t bg, uint8_t size);

#endif /* ST7789_DRIVER_H */
