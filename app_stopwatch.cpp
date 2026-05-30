/*
 * app_stopwatch.cpp — precise stopwatch
 *
 * Portrait 368×448, canvas for flicker-free time updates.
 *
 * Controls:
 *   BOOT short press – start / stop
 *   PWR  short press – reset (only when stopped)
 */

#include "app_stopwatch.h"
#include "app_common.h"
#include <Arduino.h>
#include <math.h>
#include "canvas/Arduino_Canvas.h"
#include "pin_config.h"
#include "HWCDC.h"

extern USBCDC USBSerial;
extern Arduino_Canvas *g_canvas;

#define BOOT_BTN    0
#define PWR_POLL_MS 50

static Arduino_Canvas *canvas    = nullptr;
static bool            s_running = false;
static uint32_t        s_elapsed = 0;   // accumulated ms
static uint32_t        s_startMs = 0;   // millis() when last started
static bool            s_bootWas = false;
static uint32_t        s_lastPwr = 0;
static uint32_t        s_lastDraw = 0;

// ── Helpers ───────────────────────────────────────────────────────────────────
static uint32_t totalMs() {
    return s_running ? (s_elapsed + (millis() - s_startMs)) : s_elapsed;
}

static void formatTime(uint32_t ms, char *buf, int cap) {
    uint32_t h   = ms / 3600000UL;
    uint32_t m   = (ms % 3600000UL) / 60000UL;
    uint32_t s   = (ms % 60000UL) / 1000UL;
    uint32_t cs  = (ms % 1000UL) / 10UL;
    if (h > 0)
        snprintf(buf, cap, "%lu:%02lu:%02lu", h, m, s);
    else
        snprintf(buf, cap, "%02lu:%02lu.%02lu", m, s, cs);
}

static void draw() {
    canvas->fillScreen(0x0000);

    int16_t cx = LCD_WIDTH / 2;
    const int16_t cy = 200;
    const int16_t ringR = 130;

    // ── Status ring ─────────────────────────────────────────────────────
    uint16_t ringCol = s_running ? 0x0400 : (s_elapsed > 0 ? 0x4200 : 0x1082);
    uint16_t accentCol = s_running ? 0x07E0 : (s_elapsed > 0 ? 0xFFE0 : 0x2945);
    canvas->fillCircle(cx, cy, ringR, ringCol);
    canvas->fillCircle(cx, cy, ringR - 8, 0x0000);

    // Orbiting dot — one full revolution per second. When stopped, freezes at
    // the fractional-second position where it was paused (12 o'clock if ready).
    {
        uint32_t ms = totalMs();
        float ang = -90.0f + (float)(ms % 1000UL) * 0.36f;
        float rad = ang * (float)M_PI / 180.0f;
        int16_t orbitR = ringR - 4;
        int16_t dx = cx + (int16_t)(cosf(rad) * orbitR);
        int16_t dy = cy + (int16_t)(sinf(rad) * orbitR);
        canvas->fillCircle(dx, dy, 9, accentCol);
    }

    // ── Time display (large, pixelated hero, centered in ring) ─────────
    char timeBuf[16];
    formatTime(totalMs(), timeBuf, sizeof(timeBuf));
    int n = strlen(timeBuf);
    // Fit inside ring: size 5 for short, 3 for long; pixelated for sz>=4
    uint8_t sz = (n <= 5) ? 5 : (n <= 8) ? 4 : 3;
    int16_t cw;
    if (sz == 5)      { canvas->setTextSize(5, 6, 1); cw = 31; }
    else if (sz == 4) { canvas->setTextSize(4, 5, 1); cw = 25; }
    else              { canvas->setTextSize(3);       cw = 18; }
    int16_t tw = n * cw;
    canvas->setTextColor(0xFFFF);
    canvas->setCursor((LCD_WIDTH - tw) / 2, 200 - sz * 4);
    canvas->print(timeBuf);

    // ── Status label below ring ─────────────────────────────────────────
    const char *status = s_running ? "RUNNING" : (s_elapsed > 0 ? "PAUSED" : "READY");
    canvas->setTextSize(2);
    canvas->setTextColor(accentCol);
    canvas->setCursor((LCD_WIDTH - (int16_t)(strlen(status) * 12)) / 2, 348);
    canvas->print(status);

    // Pill labels anchored to hardware buttons
    const char *bootLbl = s_running ? "stop" : (s_elapsed > 0 ? "go" : "start");
    draw_pill_label(canvas, 0, 0, bootLbl);
    if (s_elapsed > 0 && !s_running) {
        draw_pill_label(canvas, 0, 1, "reset");
    }

    draw_battery_g(canvas, LCD_WIDTH, LCD_HEIGHT);
    draw_watermark_g(canvas, LCD_WIDTH, LCD_HEIGHT);
    canvas->flush();
}

void app_stopwatch_setup(Arduino_SH8601 *gfx) {
    (void)gfx;
    canvas    = g_canvas;
    s_running = false;
    s_elapsed = 0;
    s_startMs = 0;
    s_bootWas = false;
    s_lastPwr = 0;
    s_lastDraw = 0;
    pinMode(BOOT_BTN, INPUT_PULLUP);
    draw();
}

void app_stopwatch_loop() {
    common_tick();
    uint32_t now = millis();

    // Redraw every 50 ms when running, else only on events
    if (s_running && now - s_lastDraw >= 50) {
        s_lastDraw = now;
        draw();
    }

    bool boot = (digitalRead(BOOT_BTN) == LOW);
    if (boot && !s_bootWas) {
        common_activity();
        if (!s_running) {
            s_startMs = millis();
            s_running = true;
        } else {
            s_elapsed += millis() - s_startMs;
            s_running = false;
        }
        draw();
    }
    s_bootWas = boot;

    if (common_consume_pwr_short()) {
        common_activity();
        if (!s_running) {
            s_elapsed = 0;
            draw();
        }
    }

    delay(10);
}
