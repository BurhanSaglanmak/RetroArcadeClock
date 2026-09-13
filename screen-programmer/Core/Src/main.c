#include "main.h"
#include "st7789.h"
#include <string.h>

SPI_HandleTypeDef hspi1;

/* ================= Prototipler ================= */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);

/* ================= Ekran Modları ================= */
typedef enum {
    CLK_DIGITAL = 0,
    CLK_ANALOG,
    CLK_RETRO,
    CLK_FLAPPY,
    CLK_TIMESET,
    CLK_MODE_COUNT
} ClockMode_t;

#define IDLE_TIMEOUT_MS     10000

/* Flappy sabitleri */
#define FLAPPY_GRAVITY      1
#define FLAPPY_JUMP         (-8)
#define FLAPPY_MAX_VY       8
#define FLAPPY_PIPE_W       50
#define FLAPPY_PIPE_GAP     75
#define FLAPPY_PIPE_SPEED   2
#define FLAPPY_PIPE_SPAWN   90
#define FLAPPY_BIRD_X       50
#define FLAPPY_BIRD_SIZE    12
#define FLAPPY_PLAY_TOP     20
#define FLAPPY_PLAY_BOT     220
#define FLAPPY_NUM_PIPES    3

/* ================= Global Durum ================= */
static volatile ClockMode_t currentMode = CLK_DIGITAL;
static volatile uint8_t forceRedraw = 0;

static uint32_t g_seconds   = 10 * 3600 + 8 * 60 + 30;
static uint32_t g_last_tick = 0;

static uint8_t  screen_on     = 1;
static uint32_t last_activity = 0;

/* Çizim önbellekleri */
static uint8_t prev_hh = 255, prev_mm = 255, prev_ss = 255;
static int16_t prev_hx, prev_hy, prev_mx, prev_my, prev_sx, prev_sy;
static int16_t prev_h_idx = -1;
static uint8_t analog_ready = 0;
static uint8_t retro_ready  = 0;

/* Saat ayarı (sadece saat + dakika) */
static uint32_t edit_seconds = 0;
static uint8_t  edit_field   = 0;     /* 0=HOUR, 1=MINUTE */
static uint8_t  blink_state  = 1;
static uint32_t blink_t      = 0;
static uint8_t  timeset_ready = 0;

/* Flappy */
typedef struct {
    int16_t  y;
    int8_t   vy;
    int16_t  px[FLAPPY_NUM_PIPES];
    int16_t  gap[FLAPPY_NUM_PIPES];
    uint8_t  active[FLAPPY_NUM_PIPES];
    uint16_t score;
    uint8_t  game_over;
    uint16_t spawn_counter;
    int16_t  prev_bird_y;
    uint8_t  game_over_drawn;
} Flappy_t;

static Flappy_t fl;
static uint8_t  flappy_jump = 0;
static uint8_t  flappy_ready = 0;
static uint32_t flappy_rand = 12345;

/* Buton durumları */
typedef struct {
    uint8_t  prev;
    uint32_t press_t;
    uint8_t  long_fired;
    uint8_t  ignore_release;
} BtnState_t;

static BtnState_t btn_mode   = { 1, 0, 0, 0 };
static BtnState_t btn_action = { 1, 0, 0, 0 };

/* 7-segment tablosu */
static const uint8_t seg_table[10] = {
    0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F
};

/* sin(6°·n)*1000 */
static const int16_t sin_tab[60] = {
    0,105,208,309,407,500,588,669,743,809,866,914,
    951,978,995,1000,995,978,951,914,866,809,743,669,
    588,500,407,309,208,105,0,-105,-208,-309,-407,-500,
    -588,-669,-743,-809,-866,-914,-951,-978,-995,-1000,
    -995,-978,-951,-914,-866,-809,-743,-669,-588,-500,
    -407,-309,-208,-105
};

/* ================= Yardımcı prototipler ================= */
static uint8_t GetHours24(uint32_t s);
static uint8_t GetHours12(uint32_t s);
static uint8_t GetMinutes(uint32_t s);
static uint8_t GetSeconds(uint32_t s);

static void Draw7Seg(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                     uint8_t t, uint8_t digit, uint16_t on, uint16_t off);
static void DrawClockDigital(uint32_t s);
static void DrawClockAnalog (uint32_t s);
static void DrawClockRetro  (uint32_t s);
static void DrawFlappy      (void);
static void DrawTimeSet     (void);
static void DrawTimeSetBlink(void);

static void Screen_Off(void);
static void Screen_On (void);
static void Button_Process(void);
static void NextMode(void);

static void Flappy_DrawPipeFull (int16_t px, int16_t gap_y);
static void Flappy_ScrollPipe   (int16_t old_px, int16_t new_px, int16_t gap_y);
static void Flappy_ErasePipeOff (int16_t px, int16_t gap_y);

/* =================================================================
 *                              MAIN
 * ================================================================= */
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_SPI1_Init();

    ST7789_Init();
    ST7789_FillScreen(COLOR_BLACK);

    g_last_tick   = HAL_GetTick();
    last_activity = HAL_GetTick();
    blink_t       = HAL_GetTick();
    uint32_t last_drawn_second = 0xFFFFFFFF;
    uint32_t last_flappy_tick  = 0;

    while (1)
    {
        uint32_t now = HAL_GetTick();

        /* 1) Zamanı ilerlet */
        while ((now - g_last_tick) >= 1000) {
            g_last_tick += 1000;
            g_seconds++;
        }

        /* 2) Butonlar */
        Button_Process();

        /* 3) Auto-off */
        if (screen_on && (now - last_activity) > IDLE_TIMEOUT_MS) {
            Screen_Off();
        }

        /* 4) Flappy frame */
        if (currentMode == CLK_FLAPPY && screen_on) {
            if (now - last_flappy_tick >= 16) {
                last_flappy_tick = now;
                DrawFlappy();
            }
        }

        /* 5) Saat ayarı blink — SADECE seçili alanı yenile */
        if (currentMode == CLK_TIMESET && screen_on) {
            if (now - blink_t >= 500) {
                blink_t = now;
                blink_state = !blink_state;
                if (timeset_ready) DrawTimeSetBlink();
            }
        }

        /* 6) Ekranları çiz */
        if (currentMode == CLK_TIMESET) {
            /* TimeSet sadece buton veya mod değişikliğinde tamamen çizilir */
            if (forceRedraw) {
                forceRedraw = 0;
                if (screen_on) DrawTimeSet();
            }
        }
        else if (currentMode != CLK_FLAPPY &&
                 (forceRedraw || g_seconds != last_drawn_second)) {
            forceRedraw = 0;
            last_drawn_second = g_seconds;
            if (screen_on) {
                switch (currentMode) {
                case CLK_DIGITAL: DrawClockDigital(g_seconds); break;
                case CLK_ANALOG:  DrawClockAnalog (g_seconds); break;
                case CLK_RETRO:   DrawClockRetro  (g_seconds); break;
                default: break;
                }
            }
        }

        HAL_Delay(10);
    }
}

/* =================================================================
 *                       EKRAN AÇ / KAPA
 * ================================================================= */
static void Screen_Off(void)
{
    BLK_OFF();
    screen_on = 0;
}

static void Screen_On(void)
{
    BLK_ON();
    screen_on     = 1;
    last_activity = HAL_GetTick();
}

/* =================================================================
 *                       MOD DEĞİŞTİR / ALAN SEÇ
 * ================================================================= */
static void NextMode(void)
{
    /* TIME SET: HOUR → MINUTE → (save & Digital) */
    if (currentMode == CLK_TIMESET) {
        if (edit_field < 1) {
            edit_field++;
            blink_state = 1;
            blink_t = HAL_GetTick();
            forceRedraw = 1;
            return;
        }
        /* Kaydet: saniye = 0 */
        uint8_t h = GetHours24(edit_seconds);
        uint8_t m = GetMinutes(edit_seconds);
        g_seconds = (uint32_t)h * 3600 + (uint32_t)m * 60;

        timeset_ready = 0;
        currentMode = CLK_DIGITAL;

        prev_hh = prev_mm = prev_ss = 255;
        prev_h_idx = -1;
        analog_ready = 0;
        retro_ready  = 0;
        forceRedraw  = 1;
        return;
    }

    if (currentMode == CLK_FLAPPY) {
        flappy_ready = 0;
        flappy_jump  = 0;
    }

    currentMode = (ClockMode_t)((currentMode + 1) % CLK_MODE_COUNT);

    prev_hh = prev_mm = prev_ss = 255;
    prev_h_idx = -1;
    analog_ready = 0;
    retro_ready  = 0;
    forceRedraw  = 1;
}

/* =================================================================
 *                          BUTON İŞLEME
 * ================================================================= */
static void Button_Process(void)
{
    uint32_t now = HAL_GetTick();
    uint8_t  bm = HAL_GPIO_ReadPin(BTN_MODE_GPIO_Port,   BTN_MODE_Pin);
    uint8_t  ba = HAL_GPIO_ReadPin(BTN_ACTION_GPIO_Port, BTN_ACTION_Pin);

    /* -------- BTN_MODE -------- */
    if (btn_mode.prev == 1 && bm == 0) {
        btn_mode.press_t = now;
        btn_mode.long_fired = 0;
        last_activity = now;

        if (!screen_on) {
            Screen_On();
            btn_mode.ignore_release = 1;
            btn_mode.prev = bm;
            goto check_action;
        }
    }
    else if (btn_mode.prev == 0 && bm == 0) {
        if (!btn_mode.long_fired && (now - btn_mode.press_t) >= 1000) {
            btn_mode.long_fired = 1;
            Screen_Off();
            btn_mode.prev = 1;
            goto check_action;
        }
    }
    else if (btn_mode.prev == 0 && bm == 1) {
        last_activity = now;
        if (btn_mode.ignore_release) {
            btn_mode.ignore_release = 0;
        } else if (!btn_mode.long_fired && screen_on) {
            NextMode();
        }
    }
    btn_mode.prev = bm;

check_action:
    /* -------- BTN_ACTION -------- */
    if (btn_action.prev == 1 && ba == 0) {
        btn_action.press_t = now;
        btn_action.long_fired = 0;
        last_activity = now;

        if (screen_on && currentMode == CLK_FLAPPY && !fl.game_over) {
            flappy_jump = 1;
        }
    }
    else if (btn_action.prev == 0 && ba == 1) {
        last_activity = now;
        if (!btn_action.long_fired && screen_on) {
            if (currentMode == CLK_TIMESET) {
                uint8_t h = GetHours24(edit_seconds);
                uint8_t m = GetMinutes(edit_seconds);

                if (edit_field == 0)      h = (h + 1) % 24;
                else if (edit_field == 1) m = (m + 1) % 60;

                edit_seconds = (uint32_t)h * 3600 + (uint32_t)m * 60;
                /* Sadece seçili alanı yenile */
                DrawTimeSetBlink();
            }
            else if (currentMode == CLK_FLAPPY && fl.game_over) {
                flappy_ready = 0;
                forceRedraw  = 1;
            }
        }
    }
    btn_action.prev = ba;
}

/* =================================================================
 *                             7-SEGMENT
 * ================================================================= */
static void Draw7Seg(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                     uint8_t t, uint8_t digit, uint16_t on, uint16_t off)
{
    uint8_t s = seg_table[digit & 0x0F];
    ST7789_FillRect(x + t,     y,         w - 2*t, t,       (s & 0x01) ? on : off);
    ST7789_FillRect(x + w - t, y + t,     t,       h/2 - t, (s & 0x02) ? on : off);
    ST7789_FillRect(x + w - t, y + h/2,   t,       h/2 - t, (s & 0x04) ? on : off);
    ST7789_FillRect(x + t,     y + h - t, w - 2*t, t,       (s & 0x08) ? on : off);
    ST7789_FillRect(x,         y + h/2,   t,       h/2 - t, (s & 0x10) ? on : off);
    ST7789_FillRect(x,         y + t,     t,       h/2 - t, (s & 0x20) ? on : off);
    ST7789_FillRect(x + t,     y + h/2 - t/2, w - 2*t, t,   (s & 0x40) ? on : off);
}

/* =================================================================
 *                    DİJİTAL (24 SAAT)
 * ================================================================= */
static void DrawClockDigital(uint32_t secs)
{
    uint8_t hh = GetHours24(secs);
    uint8_t mm = GetMinutes(secs);
    uint8_t ss = GetSeconds(secs);

    if (prev_hh == 255 || forceRedraw)
        ST7789_FillScreen(COLOR_BLACK);

    const uint16_t dw = 90, dh = 100, t = 10;
    const uint16_t x1 = 25, x2 = 125;
    const uint16_t yh = 15, ym = 125;

    if (hh != prev_hh || forceRedraw) {
        Draw7Seg(x1, yh, dw, dh, t, hh / 10, COLOR_RED, COLOR_BLACK);
        Draw7Seg(x2, yh, dw, dh, t, hh % 10, COLOR_RED, COLOR_BLACK);
        prev_hh = hh;
    }
    if (mm != prev_mm || forceRedraw) {
        Draw7Seg(x1, ym, dw, dh, t, mm / 10, COLOR_CYAN, COLOR_BLACK);
        Draw7Seg(x2, ym, dw, dh, t, mm % 10, COLOR_CYAN, COLOR_BLACK);
        prev_mm = mm;
    }
    if (ss != prev_ss || forceRedraw) {
        char buf[3];
        buf[0] = '0' + ss / 10;
        buf[1] = '0' + ss % 10;
        buf[2] = 0;
        ST7789_DrawString(222, 4, buf, COLOR_GRAY, COLOR_BLACK, 1);
        prev_ss = ss;
    }
}

/* =================================================================
 *                    ANALOG
 * ================================================================= */
static void DrawClockAnalog(uint32_t secs)
{
    const int16_t cx = 120, cy = 120, R = 110;

    if (!analog_ready || forceRedraw) {
        ST7789_FillScreen(COLOR_BLACK);
        ST7789_DrawCircle(cx, cy, R,     COLOR_WHITE);
        ST7789_DrawCircle(cx, cy, R - 1, COLOR_DARKGRAY);

        for (int i = 0; i < 12; i++) {
            int idx = (i * 5) % 60;
            int16_t s = sin_tab[idx];
            int16_t c = sin_tab[(idx + 15) % 60];
            int16_t len = (i % 3 == 0) ? 18 : 10;
            int16_t x1 = cx + (R - 4)       * s / 1000;
            int16_t y1 = cy - (R - 4)       * c / 1000;
            int16_t x2 = cx + (R - 4 - len) * s / 1000;
            int16_t y2 = cy - (R - 4 - len) * c / 1000;
            ST7789_DrawLine(x1, y1, x2, y2, COLOR_WHITE);
        }
        analog_ready = 1;
        prev_hh = prev_mm = prev_ss = 255;
        prev_h_idx = -1;
    }

    uint8_t hh = GetHours12(secs);
    uint8_t mm = GetMinutes(secs);
    uint8_t ss = GetSeconds(secs);

    int h_idx = (hh % 12) * 5 + (mm / 12);

    int16_t hs = sin_tab[h_idx % 60], hc = sin_tab[(h_idx + 15) % 60];
    int16_t h_x = cx + 55 * hs / 1000, h_y = cy - 55 * hc / 1000;

    int16_t ms = sin_tab[mm], mc = sin_tab[(mm + 15) % 60];
    int16_t m_x = cx + 75 * ms / 1000, m_y = cy - 75 * mc / 1000;

    int16_t ss_s = sin_tab[ss], ss_c = sin_tab[(ss + 15) % 60];
    int16_t s_x = cx + 85 * ss_s / 1000, s_y = cy - 85 * ss_c / 1000;

    if (prev_ss != 255) {
        ST7789_DrawLine(cx, cy, prev_sx, prev_sy, COLOR_BLACK);
    }
    if (prev_mm != 255 && prev_mm != mm) {
        ST7789_DrawLine(cx,     cy, prev_mx,     prev_my,     COLOR_BLACK);
        ST7789_DrawLine(cx + 1, cy, prev_mx + 1, prev_my,     COLOR_BLACK);
    }
    if (prev_h_idx != -1 && prev_h_idx != h_idx) {
        ST7789_DrawLine(cx,     cy,     prev_hx,     prev_hy,     COLOR_BLACK);
        ST7789_DrawLine(cx + 1, cy,     prev_hx + 1, prev_hy,     COLOR_BLACK);
        ST7789_DrawLine(cx - 1, cy,     prev_hx - 1, prev_hy,     COLOR_BLACK);
        ST7789_DrawLine(cx,     cy + 1, prev_hx,     prev_hy + 1, COLOR_BLACK);
        ST7789_DrawLine(cx,     cy - 1, prev_hx,     prev_hy - 1, COLOR_BLACK);
    }

    ST7789_DrawLine(cx,     cy,     h_x,     h_y,     COLOR_WHITE);
    ST7789_DrawLine(cx + 1, cy,     h_x + 1, h_y,     COLOR_WHITE);
    ST7789_DrawLine(cx,     cy + 1, h_x,     h_y + 1, COLOR_WHITE);

    ST7789_DrawLine(cx,     cy,     m_x,     m_y,     COLOR_YELLOW);
    ST7789_DrawLine(cx + 1, cy,     m_x + 1, m_y,     COLOR_YELLOW);

    ST7789_DrawLine(cx,     cy,     s_x,     s_y,     COLOR_RED);

    ST7789_FillCircle(cx, cy, 5, COLOR_WHITE);
    ST7789_FillCircle(cx, cy, 3, COLOR_RED);

    prev_hx = h_x; prev_hy = h_y;
    prev_mx = m_x; prev_my = m_y;
    prev_sx = s_x; prev_sy = s_y;
    prev_h_idx = h_idx;
    prev_hh = hh; prev_mm = mm; prev_ss = ss;
}

/* =================================================================
 *                    RETRO (24 SAAT)
 * ================================================================= */
static void DrawClockRetro(uint32_t secs)
{
    uint8_t hh = GetHours24(secs);
    uint8_t mm = GetMinutes(secs);
    uint8_t ss = GetSeconds(secs);

    if (!retro_ready || forceRedraw) {
        ST7789_FillRect(0,   0, LCD_W, 40, COLOR_MAGENTA);
        ST7789_FillRect(0,  40, LCD_W, 40, COLOR_RED);
        ST7789_FillRect(0,  80, LCD_W, 40, COLOR_ORANGE);
        ST7789_FillRect(0, 120, LCD_W, 40, COLOR_YELLOW);
        ST7789_FillRect(0, 160, LCD_W, 40, COLOR_CYAN);
        ST7789_FillRect(0, 200, LCD_W, 40, COLOR_BLUE);

        uint16_t px = 15, py = 80, pw = 210, ph = 90;
        ST7789_FillRect(px, py, pw, ph, COLOR_WHITE);
        ST7789_DrawRect(px,     py,     pw,     ph,     COLOR_BLACK);
        ST7789_DrawRect(px + 3, py + 3, pw - 6, ph - 6, COLOR_BLACK);

        ST7789_DrawString(82, 8, "RETRO", COLOR_WHITE, COLOR_MAGENTA, 3);

        retro_ready = 1;
        prev_hh = prev_mm = prev_ss = 255;
    }

    const uint16_t dw = 40, dh = 55, t = 6;
    const uint16_t y   = 88;
    const uint16_t xh1 = 30;
    const uint16_t xh2 = xh1 + dw + 3;
    const uint16_t xc  = xh2 + dw + 3;
    const uint16_t xm1 = xc + 12;
    const uint16_t xm2 = xm1 + dw + 3;

    if (hh != prev_hh || forceRedraw) {
        Draw7Seg(xh1, y, dw, dh, t, hh / 10, COLOR_BLACK, COLOR_WHITE);
        Draw7Seg(xh2, y, dw, dh, t, hh % 10, COLOR_BLACK, COLOR_WHITE);
        ST7789_FillRect(xc, y + 18, 6, 6, COLOR_RED);
        ST7789_FillRect(xc, y + 35, 6, 6, COLOR_RED);
        prev_hh = hh;
    }
    if (mm != prev_mm || forceRedraw) {
        Draw7Seg(xm1, y, dw, dh, t, mm / 10, COLOR_BLACK, COLOR_WHITE);
        Draw7Seg(xm2, y, dw, dh, t, mm % 10, COLOR_BLACK, COLOR_WHITE);
        prev_mm = mm;
    }
    if (ss != prev_ss || forceRedraw) {
        char buf[3];
        buf[0] = '0' + ss / 10;
        buf[1] = '0' + ss % 10;
        buf[2] = 0;
        ST7789_DrawString(108, 148, buf, COLOR_BLACK, COLOR_WHITE, 2);
        prev_ss = ss;
    }
}

/* =================================================================
 *                          FLAPPY
 * ================================================================= */
static void Flappy_DrawBird(int16_t x, int16_t y)
{
    int16_t bx = x - FLAPPY_BIRD_SIZE/2;
    int16_t by = y - FLAPPY_BIRD_SIZE/2;
    ST7789_FillRect(bx, by, FLAPPY_BIRD_SIZE, FLAPPY_BIRD_SIZE, COLOR_YELLOW);
    ST7789_FillRect(bx + FLAPPY_BIRD_SIZE - 4, by + 2, 2, 2, COLOR_BLACK);
    ST7789_FillRect(bx + FLAPPY_BIRD_SIZE, by + 4, 3, 3, COLOR_ORANGE);
}

static void Flappy_ClearBird(int16_t x, int16_t y)
{
    int16_t bx = x - FLAPPY_BIRD_SIZE/2;
    int16_t by = y - FLAPPY_BIRD_SIZE/2;
    ST7789_FillRect(bx - 1, by - 1, FLAPPY_BIRD_SIZE + 6, FLAPPY_BIRD_SIZE + 2, COLOR_BLACK);
}

static void Flappy_DrawPipeFull(int16_t px, int16_t gap_y)
{
    if (px >= LCD_W) return;
    int16_t top_h = gap_y - FLAPPY_PIPE_GAP/2 - FLAPPY_PLAY_TOP;
    int16_t bot_y = gap_y + FLAPPY_PIPE_GAP/2;

    int16_t draw_x = px;
    int16_t draw_w = FLAPPY_PIPE_W;
    if (draw_x < 0) { draw_w += draw_x; draw_x = 0; }
    if (draw_x + draw_w > LCD_W) draw_w = LCD_W - draw_x;
    if (draw_w <= 0) return;

    if (top_h > 0)
        ST7789_FillRect(draw_x, FLAPPY_PLAY_TOP, draw_w, top_h, COLOR_GREEN);
    if (bot_y < FLAPPY_PLAY_BOT)
        ST7789_FillRect(draw_x, bot_y, draw_w, FLAPPY_PLAY_BOT - bot_y, COLOR_GREEN);
}

static void Flappy_ScrollPipe(int16_t old_px, int16_t new_px, int16_t gap_y)
{
    int16_t top_h = gap_y - FLAPPY_PIPE_GAP/2 - FLAPPY_PLAY_TOP;
    int16_t bot_y = gap_y + FLAPPY_PIPE_GAP/2;

    int16_t clear_x = new_px + FLAPPY_PIPE_W;
    if (clear_x < LCD_W && clear_x >= 0) {
        int16_t w = old_px - new_px;
        if (clear_x + w > LCD_W) w = LCD_W - clear_x;
        if (w > 0) {
            if (top_h > 0)
                ST7789_FillRect(clear_x, FLAPPY_PLAY_TOP, w, top_h, COLOR_BLACK);
            if (bot_y < FLAPPY_PLAY_BOT)
                ST7789_FillRect(clear_x, bot_y, w, FLAPPY_PLAY_BOT - bot_y, COLOR_BLACK);
        }
    }

    int16_t draw_x = new_px;
    int16_t draw_w = old_px - new_px;
    if (draw_x < 0) { draw_w += draw_x; draw_x = 0; }
    if (draw_x + draw_w > LCD_W) draw_w = LCD_W - draw_x;
    if (draw_w > 0) {
        if (top_h > 0)
            ST7789_FillRect(draw_x, FLAPPY_PLAY_TOP, draw_w, top_h, COLOR_GREEN);
        if (bot_y < FLAPPY_PLAY_BOT)
            ST7789_FillRect(draw_x, bot_y, draw_w, FLAPPY_PLAY_BOT - bot_y, COLOR_GREEN);
    }
}

static void Flappy_ErasePipeOff(int16_t px, int16_t gap_y)
{
    int16_t top_h = gap_y - FLAPPY_PIPE_GAP/2 - FLAPPY_PLAY_TOP;
    int16_t bot_y = gap_y + FLAPPY_PIPE_GAP/2;

    int16_t x = px;
    int16_t w = FLAPPY_PIPE_W;
    if (x < 0) { w += x; x = 0; }
    if (x + w > LCD_W) w = LCD_W - x;
    if (w <= 0) return;

    if (top_h > 0)
        ST7789_FillRect(x, FLAPPY_PLAY_TOP, w, top_h, COLOR_BLACK);
    if (bot_y < FLAPPY_PLAY_BOT)
        ST7789_FillRect(x, bot_y, w, FLAPPY_PLAY_BOT - bot_y, COLOR_BLACK);
}

static void Flappy_Init(void)
{
    fl.y = 120;
    fl.vy = 0;
    fl.score = 0;
    fl.game_over = 0;
    fl.game_over_drawn = 0;
    fl.spawn_counter = 30;

    for (int i = 0; i < FLAPPY_NUM_PIPES; i++) {
        fl.px[i] = LCD_W + i * 90;
        fl.gap[i] = 60 + (i * 40) % 100;
        fl.active[i] = 0;
    }
    fl.prev_bird_y = -100;

    ST7789_FillScreen(COLOR_BLACK);
    ST7789_FillRect(0, FLAPPY_PLAY_BOT, LCD_W, LCD_H - FLAPPY_PLAY_BOT, COLOR_BROWN);
    ST7789_FillRect(0, FLAPPY_PLAY_BOT, LCD_W, 3, COLOR_GREEN);

    flappy_ready = 1;
}

static uint32_t Flappy_Rand(void)
{
    flappy_rand = flappy_rand * 1103515245 + 12345;
    return (flappy_rand >> 16) & 0x7FFF;
}

static void Flappy_DrawScore(void)
{
    char buf[8];
    uint16_t sc = fl.score;
    int i = 0;
    if (sc == 0) buf[i++] = '0';
    else {
        char tmp[6]; int j = 0;
        while (sc > 0) { tmp[j++] = '0' + sc % 10; sc /= 10; }
        while (j > 0) buf[i++] = tmp[--j];
    }
    buf[i] = 0;
    ST7789_FillRect(80, 2, 80, 16, COLOR_BLACK);
    ST7789_DrawString(90, 2, buf, COLOR_WHITE, COLOR_BLACK, 2);
}

static uint8_t Flappy_CheckCollision(int16_t bx, int16_t by, int16_t px, int16_t gap_y)
{
    if (px + FLAPPY_PIPE_W < bx - FLAPPY_BIRD_SIZE/2) return 0;
    if (px > bx + FLAPPY_BIRD_SIZE/2) return 0;

    int16_t gap_top = gap_y - FLAPPY_PIPE_GAP/2;
    int16_t gap_bot = gap_y + FLAPPY_PIPE_GAP/2;
    int16_t b_top = by - FLAPPY_BIRD_SIZE/2;
    int16_t b_bot = by + FLAPPY_BIRD_SIZE/2;

    if (b_top < gap_top) return 1;
    if (b_bot > gap_bot) return 1;
    return 0;
}

static void DrawFlappy(void)
{
    if (!flappy_ready) {
        Flappy_Init();
        return;
    }

    if (fl.game_over) {
        if (!fl.game_over_drawn) {
            ST7789_DrawString(70, 100, "GAME OVER",   COLOR_RED,   COLOR_BLACK, 2);
            ST7789_DrawString(45, 130, "ACTION=retry", COLOR_WHITE, COLOR_BLACK, 1);
            ST7789_DrawString(45, 145, "MODE=set",     COLOR_WHITE, COLOR_BLACK, 1);
            fl.game_over_drawn = 1;
        }
        return;
    }

    /* ---- Fizik: zıplama tam hız, DÜŞÜŞ yarı hız ---- */
    if (flappy_jump) {
        fl.vy = FLAPPY_JUMP;
        flappy_jump = 0;
    }
    fl.vy += FLAPPY_GRAVITY;
    if (fl.vy > FLAPPY_MAX_VY) fl.vy = FLAPPY_MAX_VY;

    /* Yükselirken tam hız, düşerken yarı hız */
    if (fl.vy > 0)
        fl.y += fl.vy / 2;
    else
        fl.y += fl.vy;

    if (fl.y - FLAPPY_BIRD_SIZE/2 <= FLAPPY_PLAY_TOP) {
        fl.y = FLAPPY_PLAY_TOP + FLAPPY_BIRD_SIZE/2;
        fl.game_over = 1;
    }
    if (fl.y + FLAPPY_BIRD_SIZE/2 >= FLAPPY_PLAY_BOT) {
        fl.y = FLAPPY_PLAY_BOT - FLAPPY_BIRD_SIZE/2;
        fl.game_over = 1;
    }

    for (int i = 0; i < FLAPPY_NUM_PIPES; i++) {
        if (!fl.active[i]) continue;

        int16_t old_px = fl.px[i];
        fl.px[i] -= FLAPPY_PIPE_SPEED;
        int16_t new_px = fl.px[i];

        Flappy_ScrollPipe(old_px, new_px, fl.gap[i]);

        if ((old_px + FLAPPY_PIPE_W) >= FLAPPY_BIRD_X &&
            (new_px + FLAPPY_PIPE_W) <  FLAPPY_BIRD_X) {
            fl.score++;
            Flappy_DrawScore();
        }

        if (Flappy_CheckCollision(FLAPPY_BIRD_X, fl.y, fl.px[i], fl.gap[i])) {
            fl.game_over = 1;
        }

        if (new_px + FLAPPY_PIPE_W < 0) {
            Flappy_ErasePipeOff(new_px, fl.gap[i]);
            fl.active[i] = 0;
        }
    }

    fl.spawn_counter++;
    if (fl.spawn_counter >= FLAPPY_PIPE_SPAWN) {
        fl.spawn_counter = 0;
        for (int i = 0; i < FLAPPY_NUM_PIPES; i++) {
            if (!fl.active[i]) {
                fl.active[i] = 1;
                fl.px[i] = LCD_W;
                fl.gap[i] = 60 + (Flappy_Rand() % 100);
                Flappy_DrawPipeFull(fl.px[i], fl.gap[i]);
                break;
            }
        }
    }

    if (fl.prev_bird_y != -100) {
        Flappy_ClearBird(FLAPPY_BIRD_X, fl.prev_bird_y);
    }
    Flappy_DrawBird(FLAPPY_BIRD_X, fl.y);
    fl.prev_bird_y = fl.y;
}

/* =================================================================
 *                    TIME SET (HOUR + MINUTE)
 * ================================================================= */

/* Ortak yerleşim sabitleri */
#define TS_DW    46
#define TS_DH    100
#define TS_T     9
#define TS_Y     60
#define TS_XH1   8
#define TS_XH2   58
#define TS_XC    112     /* iki nokta alanı 112..128, 16 px */
#define TS_XM1   136
#define TS_XM2   186

static void DrawTimeSet(void)
{
    if (!timeset_ready) {
        edit_seconds  = g_seconds;
        edit_field    = 0;
        blink_state   = 1;
        blink_t       = HAL_GetTick();
        timeset_ready = 1;

        ST7789_FillScreen(COLOR_BLACK);
        ST7789_FillRect(0, 0, LCD_W, 30, COLOR_BLUE);
        ST7789_DrawString(62, 8, "TIME SET", COLOR_WHITE, COLOR_BLUE, 2);

        ST7789_FillRect(0, 200, LCD_W, 40, COLOR_DARKGRAY);
        ST7789_DrawString(10, 208, "ACTION: +1",       COLOR_WHITE, COLOR_DARKGRAY, 1);
        ST7789_DrawString(10, 222, "MODE: field/save",  COLOR_WHITE, COLOR_DARKGRAY, 1);
    }

    uint8_t h = GetHours24(edit_seconds);
    uint8_t m = GetMinutes (edit_seconds);

    /* İki nokta — hep beyaz */
    ST7789_FillRect(TS_XC + 5, TS_Y + 28, 6, 6, COLOR_WHITE);
    ST7789_FillRect(TS_XC + 5, TS_Y + 58, 6, 6, COLOR_WHITE);

    /* Blink etkisi */
    uint16_t hc_on = (edit_field == 0 && !blink_state) ? COLOR_BLACK : COLOR_RED;
    uint16_t mc_on = (edit_field == 1 && !blink_state) ? COLOR_BLACK : COLOR_CYAN;

    Draw7Seg(TS_XH1, TS_Y, TS_DW, TS_DH, TS_T, h / 10, hc_on, COLOR_BLACK);
    Draw7Seg(TS_XH2, TS_Y, TS_DW, TS_DH, TS_T, h % 10, hc_on, COLOR_BLACK);
    Draw7Seg(TS_XM1, TS_Y, TS_DW, TS_DH, TS_T, m / 10, mc_on, COLOR_BLACK);
    Draw7Seg(TS_XM2, TS_Y, TS_DW, TS_DH, TS_T, m % 10, mc_on, COLOR_BLACK);

    /* Alan etiketi */
    const char *label = (edit_field == 0) ? "HOUR" : "MINUTE";
    ST7789_FillRect(0, 40, LCD_W, 24, COLOR_BLACK);
    ST7789_DrawString(90, 42, label, COLOR_ORANGE, COLOR_BLACK, 2);
}

/* Sadece seçili alanı yeniden çiz — blink ve +1 için */
static void DrawTimeSetBlink(void)
{
    uint8_t h = GetHours24(edit_seconds);
    uint8_t m = GetMinutes (edit_seconds);

    if (edit_field == 0) {
        uint16_t hc_on = blink_state ? COLOR_RED : COLOR_BLACK;
        Draw7Seg(TS_XH1, TS_Y, TS_DW, TS_DH, TS_T, h / 10, hc_on, COLOR_BLACK);
        Draw7Seg(TS_XH2, TS_Y, TS_DW, TS_DH, TS_T, h % 10, hc_on, COLOR_BLACK);
    } else {
        uint16_t mc_on = blink_state ? COLOR_CYAN : COLOR_BLACK;
        Draw7Seg(TS_XM1, TS_Y, TS_DW, TS_DH, TS_T, m / 10, mc_on, COLOR_BLACK);
        Draw7Seg(TS_XM2, TS_Y, TS_DW, TS_DH, TS_T, m % 10, mc_on, COLOR_BLACK);
    }
}

/* =================================================================
 *                       ZAMAN YARDIMCILARI
 * ================================================================= */
static uint8_t GetHours24(uint32_t s) { return (uint8_t)((s / 3600) % 24); }
static uint8_t GetHours12(uint32_t s) {
    uint32_t h = (s / 3600) % 12;
    if (h == 0) h = 12;
    return (uint8_t)h;
}
static uint8_t GetMinutes(uint32_t s) { return (uint8_t)((s / 60) % 60); }
static uint8_t GetSeconds(uint32_t s) { return (uint8_t)(s % 60); }

/* =================================================================
 *                     SYSTEM / PERIPHERAL INIT
 * ================================================================= */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
        Error_Handler();

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                                |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_HSI;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
        Error_Handler();
}

static void MX_SPI1_Init(void)
{
    hspi1.Instance = SPI1;
    hspi1.Init.Mode = SPI_MODE_MASTER;
    hspi1.Init.Direction = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity = SPI_POLARITY_HIGH;
    hspi1.Init.CLKPhase    = SPI_PHASE_2EDGE;
    hspi1.Init.NSS = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
    hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial = 10;
    if (HAL_SPI_Init(&hspi1) != HAL_OK)
        Error_Handler();
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOA, BLK_Pin|RST_Pin|DC_Pin, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin   = BLK_Pin|RST_Pin|DC_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin   = BTN_MODE_Pin | BTN_ACTION_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull  = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) { }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line) { (void)file; (void)line; }
#endif
