#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "hid_drivers.h"
#include "sensor_ipc.h"
#include "settings.h"

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#define WM_SENSORS_UPDATED (WM_USER + 1)
#define WM_INIT_DONE       (WM_USER + 2)
#define TIMER_REFRESH_ID   101

// Colori tema Dark
#define COL_BG_SIDEBAR  RGB(18, 18, 28)
#define COL_BG_MAIN     RGB(22, 22, 31)
#define COL_BG_CARD     RGB(30, 30, 46)
#define COL_BG_HOVER    RGB(45, 45, 72)
#define COL_ACCENT      RGB(79, 70, 229)
#define COL_CYAN        RGB(56, 189, 248)
#define COL_GREEN       RGB(22, 163, 74)
#define COL_TEXT        RGB(248, 250, 252)
#define COL_MUTED       RGB(148, 163, 184)

typedef enum { PAGE_RGB = 0, PAGE_COOLER = 1, PAGE_FANS = 2 } AppPage;

typedef struct {
    RECT rc;
    int  id;
    char label[64];
} HitRect;

static char           g_base_dir[MAX_PATH] = {0};
static AppSettings    g_cfg;
static RGBDevice      g_rgb_devs[MAX_RGB_DEVICES];
static int            g_rgb_count = 0;
static int            g_selected_dev = 0;
static CoolerUsbInfo  g_cooler;
static SensorSnapshot g_snap;
static CRITICAL_SECTION g_snap_cs;

static AppPage        g_page = PAGE_RGB;
static HWND           g_hwnd = NULL;
static HFONT          g_font_title = NULL, g_font_bold = NULL, g_font_norm = NULL, g_font_big = NULL;
static HBITMAP        g_wheel_bmp = NULL;
static int            g_wheel_size = 200;
static int            g_wheel_cx = 360, g_wheel_cy = 180;
static bool           g_dragging_wheel = false;
static bool           g_dragging_bright = false;
static int            g_fan_scroll = 0;

static const char *EFFECTS[] = {"Static", "Breathing", "Rainbow", "Color Cycle", "Chase", "Wave", "Flicker"};
static int g_effect_idx = 0;

static const COLORREF PALETTE[11] = {
    RGB(255, 0, 0), RGB(255, 85, 0), RGB(255, 204, 0), RGB(0, 255, 51),
    RGB(0, 255, 204), RGB(0, 180, 255), RGB(0, 51, 255), RGB(124, 0, 255),
    RGB(255, 0, 204), RGB(255, 255, 255), RGB(0, 0, 0)
};

// ---------------------------------------------------------------------------
// Pre-rendering ruota HSV in bitmap (0ms durante il paint!)
// ---------------------------------------------------------------------------

static void hsv_to_rgb(double h, double s, double v, uint8_t *r, uint8_t *g, uint8_t *b) {
    double c = v * s;
    double x = c * (1.0 - fabs(fmod(h * 6.0, 2.0) - 1.0));
    double m = v - c;
    double r1 = 0, g1 = 0, b1 = 0;
    int sec = (int)(h * 6.0) % 6;
    if (sec == 0) { r1 = c; g1 = x; }
    else if (sec == 1) { r1 = x; g1 = c; }
    else if (sec == 2) { g1 = c; b1 = x; }
    else if (sec == 3) { g1 = x; b1 = c; }
    else if (sec == 4) { r1 = x; b1 = c; }
    else { r1 = c; b1 = x; }
    *r = (uint8_t)lrint((r1 + m) * 255.0);
    *g = (uint8_t)lrint((g1 + m) * 255.0);
    *b = (uint8_t)lrint((b1 + m) * 255.0);
}

static HBITMAP build_wheel_bitmap(int sz) {
    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = sz;
    bmi.bmiHeader.biHeight = -sz;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    uint32_t *bits = NULL;
    HBITMAP bmp = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    if (!bmp || !bits) return NULL;

    double half = sz / 2.0;
    double max_r = half - 4.0;
    for (int y = 0; y < sz; y++) {
        for (int x = 0; x < sz; x++) {
            double dx = x - half, dy = y - half;
            double dist = hypot(dx, dy);
            if (dist <= max_r) {
                double ang = atan2(dy, dx);
                if (ang < 0) ang += 2.0 * 3.14159265358979323846;
                double h = ang / (2.0 * 3.14159265358979323846);
                double s = dist / max_r;
                uint8_t r, g, b;
                hsv_to_rgb(h, s, 1.0, &r, &g, &b);
                bits[y * sz + x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            } else {
                bits[y * sz + x] = 0x001E1E2E; // COL_BG_CARD
            }
        }
    }
    return bmp;
}

// ---------------------------------------------------------------------------
// Azioni Hardware (eseguite in modo rapido e sicuro)
// ---------------------------------------------------------------------------

static void apply_rgb_action(bool all_devs) {
    const char *eff = EFFECTS[g_effect_idx];
    if (all_devs || g_cfg.sync_all) {
        for (int i = 0; i < g_rgb_count; i++) {
            if (g_rgb_devs[i].driver == DRV_RAM_SMBUS)
                bridge_set_ram_rgb(eff, g_cfg.r, g_cfg.g, g_cfg.b, g_cfg.brightness);
            else
                hid_apply_device(&g_rgb_devs[i], eff, g_cfg.r, g_cfg.g, g_cfg.b, g_cfg.brightness);
        }
    } else if (g_selected_dev >= 0 && g_selected_dev < g_rgb_count) {
        if (g_rgb_devs[g_selected_dev].driver == DRV_RAM_SMBUS)
            bridge_set_ram_rgb(eff, g_cfg.r, g_cfg.g, g_cfg.b, g_cfg.brightness);
        else
            hid_apply_device(&g_rgb_devs[g_selected_dev], eff, g_cfg.r, g_cfg.g, g_cfg.b, g_cfg.brightness);
    }
    snprintf(g_cfg.effect, sizeof(g_cfg.effect), "%s", eff);
    settings_save(g_base_dir, &g_cfg);
}

static void reload_all_devices(void) {
    g_rgb_count = hid_detect_rgb_devices(g_rgb_devs, MAX_RGB_DEVICES - 1, &g_cooler);

    SensorSnapshot tmp;
    if (bridge_refresh(&tmp)) {
        EnterCriticalSection(&g_snap_cs);
        g_snap = tmp;
        LeaveCriticalSection(&g_snap_cs);
        if (tmp.ram_rgb && g_rgb_count < MAX_RGB_DEVICES) {
            RGBDevice *d = &g_rgb_devs[g_rgb_count++];
            memset(d, 0, sizeof(*d));
            d->id = g_rgb_count - 1;
            d->driver = DRV_RAM_SMBUS;
            snprintf(d->category, sizeof(d->category), "RAM");
            snprintf(d->name, sizeof(d->name), "%s", tmp.ram_model[0] ? tmp.ram_model : "RAM RGB (SMBus)");
            d->zone_count = 1;
            snprintf(d->zones[0].name, 48, "Moduli RAM DDR4/DDR5");
        }
    }
    if (g_selected_dev >= g_rgb_count) g_selected_dev = 0;
}

static DWORD WINAPI worker_init_thread(LPVOID param) {
    (void)param;
    bridge_init(g_base_dir);
    reload_all_devices();
    apply_rgb_action(true);
    PostMessageA(g_hwnd, WM_INIT_DONE, 0, 0);
    return 0;
}

static DWORD WINAPI worker_poll_thread(LPVOID param) {
    (void)param;
    SensorSnapshot tmp;
    if (bridge_refresh(&tmp)) {
        EnterCriticalSection(&g_snap_cs);
        g_snap = tmp;
        LeaveCriticalSection(&g_snap_cs);
        PostMessageA(g_hwnd, WM_SENSORS_UPDATED, 0, 0);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Disegno GDI Double-Buffered (zero sfarfallio)
// ---------------------------------------------------------------------------

static void fill_round_rect(HDC hdc, RECT rc, COLORREF col, int r) {
    HBRUSH br = CreateSolidBrush(col);
    HPEN pen = CreatePen(PS_SOLID, 1, col);
    HGDIOBJ ob = SelectObject(hdc, br), op = SelectObject(hdc, pen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, r, r);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(br);
    DeleteObject(pen);
}

static void draw_text_c(HDC hdc, const char *txt, RECT rc, COLORREF col, HFONT font, UINT fmt) {
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, col);
    HGDIOBJ of = SelectObject(hdc, font);
    DrawTextA(hdc, txt, -1, &rc, fmt | DT_END_ELLIPSIS);
    SelectObject(hdc, of);
}

static void render_ui(HWND hwnd, HDC hdc, int w, int h) {
    RECT full = {0, 0, w, h};
    HBRUSH bgMain = CreateSolidBrush(COL_BG_MAIN);
    FillRect(hdc, &full, bgMain);
    DeleteObject(bgMain);

    // ---- Sidebar (210 px) ----
    RECT sb = {0, 0, 210, h};
    HBRUSH bgSb = CreateSolidBrush(COL_BG_SIDEBAR);
    FillRect(hdc, &sb, bgSb);
    DeleteObject(bgSb);

    RECT titleRc = {16, 18, 198, 52};
    draw_text_c(hdc, "RGBController C", titleRc, RGB(192, 132, 252), g_font_title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    const char *nav_names[3] = {"RGB LED", "Raffreddamento & LCD", "Ventole & Temp"};
    for (int i = 0; i < 3; i++) {
        RECT b = {12, 70 + i * 48, 198, 110 + i * 48};
        fill_round_rect(hdc, b, (g_page == i) ? COL_ACCENT : COL_BG_SIDEBAR, 10);
        b.left += 14;
        draw_text_c(hdc, nav_names[i], b, (g_page == i) ? COL_TEXT : COL_MUTED, g_font_bold, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    // Status in basso nella sidebar
    SensorSnapshot snap;
    EnterCriticalSection(&g_snap_cs);
    snap = g_snap;
    LeaveCriticalSection(&g_snap_cs);

    char st[96];
    snprintf(st, sizeof(st), "%d RGB  |  %d ventole  |  %d temp", g_rgb_count, snap.fan_count, snap.temp_count);
    RECT stRc = {14, h - 36, 200, h - 12};
    draw_text_c(hdc, st, stRc, COL_MUTED, g_font_norm, DT_LEFT | DT_SINGLELINE);

    int cx = 226;
    if (g_page == PAGE_RGB) {
        // ---- Lista Dispositivi (sinistra) ----
        RECT listCard = {cx, 16, cx + 240, h - 16};
        fill_round_rect(hdc, listCard, COL_BG_CARD, 12);

        RECT btnAll = {cx + 12, 28, cx + 228, 62};
        fill_round_rect(hdc, btnAll, COL_ACCENT, 8);
        draw_text_c(hdc, "Applica a TUTTI", btnAll, COL_TEXT, g_font_bold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        RECT btnRel = {cx + 12, 70, cx + 228, 104};
        fill_round_rect(hdc, btnRel, RGB(51, 65, 85), 8);
        draw_text_c(hdc, "Ricarica Luci", btnRel, COL_TEXT, g_font_bold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        for (int i = 0; i < g_rgb_count && i < 8; i++) {
            RECT dr = {cx + 12, 122 + i * 44, cx + 228, 158 + i * 44};
            fill_round_rect(hdc, dr, (i == g_selected_dev) ? COL_ACCENT : RGB(42, 42, 62), 8);
            char lbl[160];
            snprintf(lbl, sizeof(lbl), "[%s] %s", g_rgb_devs[i].category, g_rgb_devs[i].name);
            dr.left += 10; dr.right -= 8;
            draw_text_c(hdc, lbl, dr, COL_TEXT, g_font_norm, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }

        // ---- Pannello Editor (destra) ----
        int ex = cx + 256;
        RECT edCard = {ex, 16, w - 16, h - 16};
        fill_round_rect(hdc, edCard, COL_BG_CARD, 12);

        const char *devName = (g_selected_dev < g_rgb_count) ? g_rgb_devs[g_selected_dev].name : "Nessun dispositivo rilevato";
        RECT dnRc = {ex + 20, 28, w - 32, 56};
        draw_text_c(hdc, devName, dnRc, COL_TEXT, g_font_title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Ruota colori
        g_wheel_cx = ex + 125;
        g_wheel_cy = 180;
        if (g_wheel_bmp) {
            HDC m = CreateCompatibleDC(hdc);
            HGDIOBJ ob = SelectObject(m, g_wheel_bmp);
            BitBlt(hdc, g_wheel_cx - g_wheel_size / 2, g_wheel_cy - g_wheel_size / 2,
                   g_wheel_size, g_wheel_size, m, 0, 0, SRCCOPY);
            SelectObject(m, ob);
            DeleteDC(m);
        }

        // Preview colore + HEX
        RECT prevRc = {ex + 25, 295, ex + 90, 330};
        fill_round_rect(hdc, prevRc, RGB(g_cfg.r, g_cfg.g, g_cfg.b), 6);
        char hexStr[32];
        snprintf(hexStr, sizeof(hexStr), "#%02X%02X%02X", g_cfg.r, g_cfg.g, g_cfg.b);
        RECT hexRc = {ex + 100, 295, ex + 225, 330};
        fill_round_rect(hdc, hexRc, RGB(20, 20, 32), 6);
        draw_text_c(hdc, hexStr, hexRc, COL_TEXT, g_font_bold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Colonna destra dell'editor: Effetto, Luminosità, Palette, Sync, Applica
        int rx = ex + 255;
        RECT effLbl = {rx, 76, rx + 200, 96};
        draw_text_c(hdc, "Effetto (clicca per cambiare):", effLbl, COL_MUTED, g_font_norm, DT_LEFT | DT_SINGLELINE);
        RECT effBtn = {rx, 100, rx + 240, 134};
        fill_round_rect(hdc, effBtn, RGB(45, 45, 68), 8);
        draw_text_c(hdc, EFFECTS[g_effect_idx], effBtn, COL_TEXT, g_font_bold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        char bTxt[48];
        snprintf(bTxt, sizeof(bTxt), "Luminosita': %d%%", g_cfg.brightness);
        RECT bLbl = {rx, 150, rx + 240, 170};
        draw_text_c(hdc, bTxt, bLbl, COL_MUTED, g_font_norm, DT_LEFT | DT_SINGLELINE);
        RECT bTrack = {rx, 178, rx + 240, 190};
        fill_round_rect(hdc, bTrack, RGB(50, 50, 75), 6);
        RECT bFill = {rx, 178, rx + (240 * g_cfg.brightness) / 100, 190};
        fill_round_rect(hdc, bFill, COL_CYAN, 6);

        RECT pLbl = {rx, 208, rx + 260, 228};
        draw_text_c(hdc, "Colori vividi rapidi:", pLbl, COL_MUTED, g_font_norm, DT_LEFT | DT_SINGLELINE);
        for (int i = 0; i < 11; i++) {
            RECT pr = {rx + i * 28, 234, rx + i * 28 + 22, 256};
            fill_round_rect(hdc, pr, PALETTE[i], 11);
        }

        RECT syncRc = {rx, 274, rx + 300, 304};
        fill_round_rect(hdc, syncRc, g_cfg.sync_all ? RGB(30, 58, 138) : RGB(40, 40, 58), 8);
        draw_text_c(hdc, g_cfg.sync_all ? "[ON] Sincronizza tutti (RAM+Ventole+Mobo)" : "[OFF] Solo dispositivo selezionato",
                    syncRc, COL_TEXT, g_font_norm, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        RECT appBtn = {rx, 318, rx + 200, 360};
        fill_round_rect(hdc, appBtn, COL_GREEN, 10);
        draw_text_c(hdc, "Applica Colore", appBtn, COL_TEXT, g_font_bold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Header ARGB D_LED1 / D_LED2
        if (g_selected_dev < g_rgb_count && g_rgb_devs[g_selected_dev].driver == DRV_IT5702) {
            RECT zHdr = {ex + 20, 380, w - 32, 402};
            draw_text_c(hdc, "Zone & Header ARGB Scheda Madre:", zHdr, COL_CYAN, g_font_bold, DT_LEFT | DT_SINGLELINE);
            for (int z = 0; z < 4; z++) {
                RECT zr = {ex + 20 + (z % 2) * 280, 410 + (z / 2) * 42, ex + 288 + (z % 2) * 280, 444 + (z / 2) * 42};
                fill_round_rect(hdc, zr, RGB(42, 42, 66), 8);
                char zt[80];
                snprintf(zt, sizeof(zt), "Applica a %s", g_rgb_devs[g_selected_dev].zones[z].name);
                draw_text_c(hdc, zt, zr, COL_TEXT, g_font_norm, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }
    }
    else if (g_page == PAGE_COOLER) {
        RECT card = {cx, 16, w - 16, h - 16};
        fill_round_rect(hdc, card, COL_BG_CARD, 12);

        RECT tRc = {cx + 20, 26, w - 32, 56};
        draw_text_c(hdc, "Raffreddamento a Liquido & Monitor LCD", tRc, COL_TEXT, g_font_title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT usbRc = {cx + 20, 62, w - 32, 86};
        draw_text_c(hdc, g_cooler.connected ? g_cooler.name : "Modalita' Monitor Software (nessun LCD USB rilevato)",
                    usbRc, g_cooler.connected ? RGB(34, 197, 94) : RGB(234, 179, 8), g_font_bold, DT_LEFT | DT_SINGLELINE);

        // Anteprima circolare LCD 1:1
        int lx = cx + 170, ly = 250, lr = 130;
        HBRUSH bLcd = CreateSolidBrush(RGB(15, 23, 42));
        HPEN pLcd = CreatePen(PS_SOLID, 4, COL_CYAN);
        HGDIOBJ ob = SelectObject(hdc, bLcd), op = SelectObject(hdc, pLcd);
        Ellipse(hdc, lx - lr, ly - lr, lx + lr, ly + lr);
        SelectObject(hdc, ob); SelectObject(hdc, op);
        DeleteObject(bLcd); DeleteObject(pLcd);

        RECT l1 = {lx - 100, ly - 80, lx + 100, ly - 55};
        draw_text_c(hdc, g_cfg.lcd_label, l1, COL_CYAN, g_font_bold, DT_CENTER | DT_SINGLELINE);

        char ct[32];
        snprintf(ct, sizeof(ct), snap.cpu_temp > 0 ? "%.0f C" : "-- C", snap.cpu_temp);
        RECT l2 = {lx - 100, ly - 40, lx + 100, ly + 25};
        draw_text_c(hdc, ct, l2, COL_TEXT, g_font_big, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        int max_rpm = 0;
        for (int i = 0; i < snap.fan_count; i++) if (snap.fans[i].rpm > max_rpm) max_rpm = snap.fans[i].rpm;
        char sub[96];
        snprintf(sub, sizeof(sub), "GPU %.0f C  |  CPU %.0f%%  |  %d RPM", snap.gpu_temp, snap.cpu_load, max_rpm);
        RECT l3 = {lx - 120, ly + 40, lx + 120, ly + 65};
        draw_text_c(hdc, sub, l3, COL_MUTED, g_font_norm, DT_CENTER | DT_SINGLELINE);

        // Card telemetria a destra
        int tx = cx + 340;
        RECT tele = {tx, 120, w - 36, 360};
        fill_round_rect(hdc, tele, RGB(20, 20, 34), 12);
        char lines[4][80];
        snprintf(lines[0], 80, "Temperatura CPU:    %.1f C", snap.cpu_temp);
        snprintf(lines[1], 80, "Temperatura GPU:    %.1f C", snap.gpu_temp);
        snprintf(lines[2], 80, "Carico CPU / RAM:   %.0f%% / %.0f%%", snap.cpu_load, snap.ram_load);
        snprintf(lines[3], 80, "Velocita' Max Fan:  %d RPM", max_rpm);
        for (int i = 0; i < 4; i++) {
            RECT tr = {tx + 24, 145 + i * 48, w - 50, 180 + i * 48};
            draw_text_c(hdc, lines[i], tr, COL_TEXT, g_font_bold, DT_LEFT | DT_SINGLELINE);
        }
    }
    else if (g_page == PAGE_FANS) {
        RECT card = {cx, 16, w - 16, h - 16};
        fill_round_rect(hdc, card, COL_BG_CARD, 12);

        RECT tRc = {cx + 20, 26, w - 200, 56};
        draw_text_c(hdc, "Ventole & Temperature Live", tRc, COL_TEXT, g_font_title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        for (int i = 0; i < snap.fan_count && i < 10; i++) {
            int y = 76 + i * 46;
            RECT fr = {cx + 20, y, w - 32, y + 40};
            fill_round_rect(hdc, fr, RGB(22, 22, 36), 8);

            char fn[140];
            snprintf(fn, sizeof(fn), "%s (%s)", snap.fans[i].name, snap.fans[i].hardware);
            RECT r1 = {cx + 32, y, cx + 340, y + 40};
            draw_text_c(hdc, fn, r1, COL_TEXT, g_font_norm, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            char rpm[32];
            snprintf(rpm, sizeof(rpm), "%d RPM", snap.fans[i].rpm);
            RECT r2 = {cx + 350, y, cx + 440, y + 40};
            draw_text_c(hdc, rpm, r2, COL_CYAN, g_font_bold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            if (snap.fans[i].controllable) {
                RECT b50 = {cx + 460, y + 6, cx + 525, y + 34};
                fill_round_rect(hdc, b50, RGB(51, 65, 85), 6);
                draw_text_c(hdc, "50%", b50, COL_TEXT, g_font_norm, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                RECT b100 = {cx + 535, y + 6, cx + 605, y + 34};
                fill_round_rect(hdc, b100, COL_GREEN, 6);
                draw_text_c(hdc, "100%", b100, COL_TEXT, g_font_norm, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                RECT bAuto = {cx + 615, y + 6, cx + 685, y + 34};
                fill_round_rect(hdc, bAuto, COL_ACCENT, 6);
                draw_text_c(hdc, "AUTO", bAuto, COL_TEXT, g_font_norm, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            } else {
                RECT r3 = {cx + 460, y, cx + 650, y + 40};
                draw_text_c(hdc, "Solo lettura (BIOS)", r3, COL_MUTED, g_font_norm, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }
        }
    }
}

static bool pt_in(int x, int y, int l, int t, int r, int b) {
    return x >= l && x <= r && y >= t && y <= b;
}

static void pick_wheel_color(int x, int y) {
    double dx = x - g_wheel_cx, dy = y - g_wheel_cy;
    double max_r = g_wheel_size / 2.0 - 4.0;
    double dist = hypot(dx, dy);
    if (dist > max_r + 12.0) return;
    if (dist > max_r) dist = max_r;
    double ang = atan2(dy, dx);
    if (ang < 0) ang += 2.0 * 3.14159265358979323846;
    double h = ang / (2.0 * 3.14159265358979323846);
    double s = (dist >= max_r * 0.80) ? 1.0 : (dist / max_r);
    hsv_to_rgb(h, s, 1.0, &g_cfg.r, &g_cfg.g, &g_cfg.b);
    InvalidateRect(g_hwnd, NULL, FALSE);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
        HGDIOBJ ob = SelectObject(mem, bmp);
        render_ui(hwnd, mem, w, h);
        BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
        // Sidebar nav
        for (int i = 0; i < 3; i++) {
            if (pt_in(x, y, 12, 70 + i * 48, 198, 110 + i * 48)) {
                g_page = (AppPage)i;
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
        }
        int cx = 226;
        if (g_page == PAGE_RGB) {
            if (pt_in(x, y, cx + 12, 28, cx + 228, 62)) {
                apply_rgb_action(true);
                return 0;
            }
            if (pt_in(x, y, cx + 12, 70, cx + 228, 104)) {
                reload_all_devices();
                apply_rgb_action(true);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            for (int i = 0; i < g_rgb_count && i < 8; i++) {
                if (pt_in(x, y, cx + 12, 122 + i * 44, cx + 228, 158 + i * 44)) {
                    g_selected_dev = i;
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
            }
            int ex = cx + 256, rx = ex + 255;
            if (hypot(x - g_wheel_cx, y - g_wheel_cy) <= g_wheel_size / 2.0) {
                g_dragging_wheel = true;
                SetCapture(hwnd);
                pick_wheel_color(x, y);
                return 0;
            }
            if (pt_in(x, y, rx, 100, rx + 240, 134)) {
                g_effect_idx = (g_effect_idx + 1) % 7;
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            if (pt_in(x, y, rx, 170, rx + 240, 198)) {
                g_dragging_bright = true;
                SetCapture(hwnd);
                g_cfg.brightness = ((x - rx) * 100) / 240;
                if (g_cfg.brightness < 0) g_cfg.brightness = 0;
                if (g_cfg.brightness > 100) g_cfg.brightness = 100;
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            for (int i = 0; i < 11; i++) {
                if (pt_in(x, y, rx + i * 28, 234, rx + i * 28 + 22, 256)) {
                    g_cfg.r = GetRValue(PALETTE[i]);
                    g_cfg.g = GetGValue(PALETTE[i]);
                    g_cfg.b = GetBValue(PALETTE[i]);
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
            }
            if (pt_in(x, y, rx, 274, rx + 300, 304)) {
                g_cfg.sync_all = !g_cfg.sync_all;
                settings_save(g_base_dir, &g_cfg);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            if (pt_in(x, y, rx, 318, rx + 200, 360)) {
                apply_rgb_action(false);
                return 0;
            }
            if (g_selected_dev < g_rgb_count && g_rgb_devs[g_selected_dev].driver == DRV_IT5702) {
                for (int z = 0; z < 4; z++) {
                    int l = ex + 20 + (z % 2) * 280, t = 410 + (z / 2) * 42;
                    if (pt_in(x, y, l, t, l + 268, t + 34)) {
                        hid_apply_zone(&g_rgb_devs[g_selected_dev], z, EFFECTS[g_effect_idx], g_cfg.r, g_cfg.g, g_cfg.b, g_cfg.brightness);
                        return 0;
                    }
                }
            }
        }
        else if (g_page == PAGE_FANS) {
            EnterCriticalSection(&g_snap_cs);
            SensorSnapshot snap = g_snap;
            LeaveCriticalSection(&g_snap_cs);
            for (int i = 0; i < snap.fan_count && i < 10; i++) {
                if (!snap.fans[i].controllable) continue;
                int fy = 76 + i * 46;
                if (pt_in(x, y, cx + 460, fy + 6, cx + 525, fy + 34))
                    bridge_set_fan_duty(snap.fans[i].control_id, 50);
                else if (pt_in(x, y, cx + 535, fy + 6, cx + 605, fy + 34))
                    bridge_set_fan_duty(snap.fans[i].control_id, 100);
                else if (pt_in(x, y, cx + 615, fy + 6, cx + 685, fy + 34))
                    bridge_set_fan_auto(snap.fans[i].control_id);
            }
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (g_dragging_wheel) {
            pick_wheel_color((short)LOWORD(lp), (short)HIWORD(lp));
        } else if (g_dragging_bright) {
            int x = (short)LOWORD(lp);
            int rx = 226 + 256 + 255;
            g_cfg.brightness = ((x - rx) * 100) / 240;
            if (g_cfg.brightness < 0) g_cfg.brightness = 0;
            if (g_cfg.brightness > 100) g_cfg.brightness = 100;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if (g_dragging_wheel || g_dragging_bright) {
            g_dragging_wheel = false;
            g_dragging_bright = false;
            ReleaseCapture();
        }
        return 0;
    case WM_INIT_DONE:
    case WM_SENSORS_UPDATED:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_TIMER:
        if (wp == TIMER_REFRESH_ID) {
            HANDLE t = CreateThread(NULL, 0, worker_poll_thread, NULL, 0, NULL);
            if (t) CloseHandle(t);
        }
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, TIMER_REFRESH_ID);
        bridge_stop();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    (void)hPrev; (void)lpCmd;
    GetModuleFileNameA(NULL, g_base_dir, MAX_PATH);
    char *slash = strrchr(g_base_dir, '\\');
    if (slash) *slash = '\0';

    InitializeCriticalSection(&g_snap_cs);
    settings_load(g_base_dir, &g_cfg);
    for (int i = 0; i < 7; i++) if (strcmp(EFFECTS[i], g_cfg.effect) == 0) g_effect_idx = i;

    // Rilevamento HID istantaneo (< 2 ms) prima ancora di aprire la finestra
    g_rgb_count = hid_detect_rgb_devices(g_rgb_devs, MAX_RGB_DEVICES - 1, &g_cooler);

    g_font_title = CreateFontA(22, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    g_font_bold  = CreateFontA(15, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    g_font_norm  = CreateFontA(14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    g_font_big   = CreateFontA(46, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    g_wheel_bmp  = build_wheel_bitmap(g_wheel_size);

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "RGBControllerNativeWnd";
    RegisterClassA(&wc);

    g_hwnd = CreateWindowExA(0, wc.lpszClassName, "RGBController (Native C)",
                             WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
                             CW_USEDEFAULT, CW_USEDEFAULT, 1140, 660,
                             NULL, NULL, hInst, NULL);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(g_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    ShowWindow(g_hwnd, nShow);
    UpdateWindow(g_hwnd);

    HANDLE wt = CreateThread(NULL, 0, worker_init_thread, NULL, 0, NULL);
    if (wt) CloseHandle(wt);
    SetTimer(g_hwnd, TIMER_REFRESH_ID, 2000, NULL);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}
