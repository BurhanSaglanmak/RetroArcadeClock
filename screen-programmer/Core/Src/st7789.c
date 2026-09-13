/* Core/Src/st7789.c */
#include "st7789.h"
#include <string.h>

extern SPI_HandleTypeDef hspi1;

/* ---------- Düşük seviye ---------- */
static void SendCmd(uint8_t cmd)
{
    DC_LOW();
    HAL_SPI_Transmit(&hspi1, &cmd, 1, HAL_MAX_DELAY);
}
static void SendData8(uint8_t data)
{
    DC_HIGH();
    HAL_SPI_Transmit(&hspi1, &data, 1, HAL_MAX_DELAY);
}
static void SendData16(uint16_t data)
{
    uint8_t buf[2] = { data >> 8, data & 0xFF };
    DC_HIGH();
    HAL_SPI_Transmit(&hspi1, buf, 2, HAL_MAX_DELAY);
}
static void SetWindow(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    uint8_t buf[4];
    SendCmd(0x2A);
    buf[0] = x1 >> 8; buf[1] = x1 & 0xFF;
    buf[2] = x2 >> 8; buf[3] = x2 & 0xFF;
    DC_HIGH();
    HAL_SPI_Transmit(&hspi1, buf, 4, HAL_MAX_DELAY);

    SendCmd(0x2B);
    buf[0] = y1 >> 8; buf[1] = y1 & 0xFF;
    buf[2] = y2 >> 8; buf[3] = y2 & 0xFF;
    DC_HIGH();
    HAL_SPI_Transmit(&hspi1, buf, 4, HAL_MAX_DELAY);

    SendCmd(0x2C);
}

/* ---------- Init ---------- */
void ST7789_Init(void)
{
    __HAL_SPI_ENABLE(&hspi1);
    BLK_OFF();
    RST_LOW();
    HAL_Delay(100);
    RST_HIGH();
    HAL_Delay(120);

    SendCmd(0x11); HAL_Delay(120);
    SendCmd(0x3A); SendData8(0x55);
    SendCmd(0x36); SendData8(0x00);
    SendCmd(0x21);

    SendCmd(0xB2);
    SendData8(0x0C); SendData8(0x0C);
    SendData8(0x00); SendData8(0x33); SendData8(0x33);
    SendCmd(0xBB); SendData8(0x19);
    SendCmd(0xC0); SendData8(0x2C);
    SendCmd(0xC2); SendData8(0x01);
    SendCmd(0xC3); SendData8(0x12);
    SendCmd(0xC4); SendData8(0x20);
    SendCmd(0xD0); SendData8(0xA4); SendData8(0xA1);
    SendCmd(0x29); HAL_Delay(10);

    ST7789_FillScreen(COLOR_BLACK);
    BLK_ON();
}

/* ---------- Çizim ---------- */
void ST7789_DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (x >= LCD_W || y >= LCD_H) return;
    SetWindow(x, y, x, y);
    SendData16(color);
}

void ST7789_FillRect(uint16_t x, uint16_t y,
                     uint16_t w, uint16_t h, uint16_t color)
{
    if (x >= LCD_W || y >= LCD_H || w == 0 || h == 0) return;
    if (x + w > LCD_W) w = LCD_W - x;
    if (y + h > LCD_H) h = LCD_H - y;

    SetWindow(x, y, x + w - 1, y + h - 1);

    uint8_t buf[256];
    uint8_t hi = color >> 8, lo = color & 0xFF;
    for (int i = 0; i < 128; i++) {
        buf[i*2]   = hi;
        buf[i*2+1] = lo;
    }

    uint32_t total = (uint32_t)w * h;
    DC_HIGH();
    while (total) {
        uint16_t n = (total > 128) ? 128 : (uint16_t)total;
        HAL_SPI_Transmit(&hspi1, buf, n * 2, HAL_MAX_DELAY);
        total -= n;
    }
}

void ST7789_FillScreen(uint16_t color)
{
    ST7789_FillRect(0, 0, LCD_W, LCD_H, color);
}

void ST7789_DrawRect(uint16_t x, uint16_t y,
                     uint16_t w, uint16_t h, uint16_t color)
{
    ST7789_FillRect(x,         y,         w, 1, color);
    ST7789_FillRect(x,         y + h - 1, w, 1, color);
    ST7789_FillRect(x,         y,         1, h, color);
    ST7789_FillRect(x + w - 1, y,         1, h, color);
}

/* ---------- Bresenham + FillRect batch ---------- */
void ST7789_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color)
{
    if (y0 == y1) {
        if (x0 > x1) { int16_t t = x0; x0 = x1; x1 = t; }
        ST7789_FillRect((uint16_t)x0, (uint16_t)y0,
                        (uint16_t)(x1 - x0 + 1), 1, color);
        return;
    }
    if (x0 == x1) {
        if (y0 > y1) { int16_t t = y0; y0 = y1; y1 = t; }
        ST7789_FillRect((uint16_t)x0, (uint16_t)y0, 1,
                        (uint16_t)(y1 - y0 + 1), color);
        return;
    }

    int16_t dx =  (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int16_t dy = -((y1 > y0) ? (y1 - y0) : (y0 - y1));
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = dx + dy;

    int16_t run_x0 = x0, run_x1 = x0, run_y = y0;

    while (1) {
        if (x0 == x1 && y0 == y1) {
            ST7789_FillRect((uint16_t)run_x0, (uint16_t)run_y,
                            (uint16_t)(run_x1 - run_x0 + 1), 1, color);
            break;
        }
        int16_t e2 = 2 * err;
        int16_t nx = x0, ny = y0;
        if (e2 >= dy) { err += dy; nx += sx; }
        if (e2 <= dx) { err += dx; ny += sy; }

        if (ny != run_y) {
            ST7789_FillRect((uint16_t)run_x0, (uint16_t)run_y,
                            (uint16_t)(run_x1 - run_x0 + 1), 1, color);
            run_x0 = run_x1 = nx;
            run_y = ny;
        } else {
            if (nx < run_x0) run_x0 = nx;
            if (nx > run_x1) run_x1 = nx;
        }
        x0 = nx; y0 = ny;
    }
}

/* ---------- Daireler ---------- */
void ST7789_DrawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color)
{
    int16_t f = 1 - r, ddF_x = 1, ddF_y = -2 * r;
    int16_t x = 0, y = r;
    ST7789_DrawPixel(x0, y0 + r, color);
    ST7789_DrawPixel(x0, y0 - r, color);
    ST7789_DrawPixel(x0 + r, y0, color);
    ST7789_DrawPixel(x0 - r, y0, color);
    while (x < y) {
        if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
        x++; ddF_x += 2; f += ddF_x;
        ST7789_DrawPixel(x0 + x, y0 + y, color);
        ST7789_DrawPixel(x0 - x, y0 + y, color);
        ST7789_DrawPixel(x0 + x, y0 - y, color);
        ST7789_DrawPixel(x0 - x, y0 - y, color);
        ST7789_DrawPixel(x0 + y, y0 + x, color);
        ST7789_DrawPixel(x0 - y, y0 + x, color);
        ST7789_DrawPixel(x0 + y, y0 - x, color);
        ST7789_DrawPixel(x0 - y, y0 - x, color);
    }
}

void ST7789_FillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color)
{
    ST7789_DrawLine(x0 - r, y0, x0 + r, y0, color);
    int16_t f = 1 - r, ddF_x = 1, ddF_y = -2 * r;
    int16_t x = 0, y = r;
    while (x < y) {
        if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
        x++; ddF_x += 2; f += ddF_x;
        ST7789_DrawLine(x0 - x, y0 + y, x0 + x, y0 + y, color);
        ST7789_DrawLine(x0 - x, y0 - y, x0 + x, y0 - y, color);
        ST7789_DrawLine(x0 - y, y0 + x, x0 + y, y0 + x, color);
        ST7789_DrawLine(x0 - y, y0 - x, x0 + y, y0 - x, color);
    }
}

/* ---------- 5x7 font ---------- */
static const uint8_t Font5x7[][FONT_W] = {
    {0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x5F,0x00,0x00}, {0x00,0x07,0x00,0x07,0x00},
    {0x14,0x7F,0x14,0x7F,0x14}, {0x24,0x2A,0x7F,0x2A,0x12}, {0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x55,0x22,0x50}, {0x00,0x05,0x03,0x00,0x00}, {0x00,0x1C,0x22,0x41,0x00},
    {0x00,0x41,0x22,0x1C,0x00}, {0x14,0x08,0x3E,0x08,0x14}, {0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00}, {0x08,0x08,0x08,0x08,0x08}, {0x00,0x60,0x60,0x00,0x00},
    {0x20,0x10,0x08,0x04,0x02}, {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31}, {0x18,0x14,0x12,0x7F,0x10},
    {0x27,0x45,0x45,0x45,0x39}, {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E}, {0x00,0x36,0x36,0x00,0x00},
    {0x00,0x56,0x36,0x00,0x00}, {0x08,0x14,0x22,0x41,0x00}, {0x14,0x14,0x14,0x14,0x14},
    {0x00,0x41,0x22,0x14,0x08}, {0x02,0x01,0x51,0x09,0x06}, {0x32,0x49,0x79,0x41,0x3E},
    {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C}, {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x41,0x51,0x32}, {0x7F,0x08,0x08,0x08,0x7F}, {0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01}, {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x04,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06}, {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01}, {0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F}, {0x7F,0x20,0x18,0x20,0x7F}, {0x63,0x14,0x08,0x14,0x63},
    {0x03,0x04,0x78,0x04,0x03}, {0x61,0x51,0x49,0x45,0x43}, {0x00,0x00,0x7F,0x41,0x41},
    {0x02,0x04,0x08,0x10,0x20}, {0x41,0x41,0x7F,0x00,0x00}, {0x04,0x02,0x01,0x02,0x04},
    {0x40,0x40,0x40,0x40,0x40}, {0x00,0x01,0x02,0x04,0x00}, {0x20,0x54,0x54,0x54,0x78},
    {0x7F,0x48,0x44,0x44,0x38}, {0x38,0x44,0x44,0x44,0x20}, {0x38,0x44,0x44,0x48,0x7F},
    {0x38,0x54,0x54,0x54,0x18}, {0x08,0x7E,0x09,0x01,0x02}, {0x08,0x14,0x54,0x54,0x3C},
    {0x7F,0x08,0x04,0x04,0x78}, {0x00,0x44,0x7D,0x40,0x00}, {0x20,0x40,0x44,0x3D,0x00},
    {0x00,0x7F,0x10,0x28,0x44}, {0x00,0x41,0x7F,0x40,0x00}, {0x7C,0x04,0x18,0x04,0x78},
    {0x7C,0x08,0x04,0x04,0x78}, {0x38,0x44,0x44,0x44,0x38}, {0x7C,0x14,0x14,0x14,0x08},
    {0x08,0x14,0x14,0x18,0x7C}, {0x7C,0x08,0x04,0x04,0x08}, {0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3F,0x44,0x40,0x20}, {0x3C,0x40,0x40,0x20,0x7C}, {0x1C,0x20,0x40,0x20,0x1C},
    {0x3C,0x40,0x30,0x40,0x3C}, {0x44,0x28,0x10,0x28,0x44}, {0x0C,0x50,0x50,0x50,0x3C},
    {0x44,0x64,0x54,0x4C,0x44}, {0x00,0x08,0x36,0x41,0x00}, {0x00,0x00,0x7F,0x00,0x00},
    {0x00,0x41,0x36,0x08,0x00}, {0x08,0x08,0x2A,0x1C,0x08},
};

void ST7789_DrawChar(uint16_t x, uint16_t y, char ch,
                     uint16_t color, uint16_t bg, uint8_t size)
{
    char buf[2] = { ch, 0 };
    ST7789_DrawString(x, y, buf, color, bg, size);
}

void ST7789_DrawString(uint16_t x, uint16_t y, const char *str,
                       uint16_t color, uint16_t bg, uint8_t size)
{
    if (size == 0) size = 1;
    uint16_t len = 0;
    while (str[len]) len++;
    if (len == 0 || x >= LCD_W || y >= LCD_H) return;

    uint16_t w = len * FONT_ADV * size;
    uint16_t h = FONT_H * size;
    if (x + w > LCD_W) w = LCD_W - x;
    if (y + h > LCD_H) h = LCD_H - y;

    SetWindow(x, y, x + w - 1, y + h - 1);
    DC_HIGH();

    static uint8_t linebuf[LCD_W * 2];
    for (uint16_t py = 0; py < h; py++) {
        uint8_t row = py / size;
        uint16_t idx = 0;
        for (uint16_t px = 0; px < w; px++) {
            uint16_t cidx = px / size;
            uint16_t cch = cidx / FONT_ADV;
            uint8_t ccol = cidx % FONT_ADV;
            uint16_t c = bg;
            if (cch < len && ccol < FONT_W) {
                char ch = str[cch];
                if (ch < 0x20 || ch > 0x7E) ch = '?';
                if (Font5x7[ch - 0x20][ccol] & (1 << row)) c = color;
            }
            linebuf[idx++] = c >> 8;
            linebuf[idx++] = c & 0xFF;
        }
        HAL_SPI_Transmit(&hspi1, linebuf, w * 2, HAL_MAX_DELAY);
    }
}
