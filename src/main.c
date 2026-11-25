#define _WIN32_WINNT 0x0A00
#include <windows.h>
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
#define M_PI_D             3.14159265358979323846

// Colori identici al tema CustomTkinter originale
#define COL_BG_SIDEBAR  RGB(18, 18, 28)
#define COL_BG_MAIN     RGB(22, 22, 31)
#define COL_BG_CARD     RGB(30, 30, 46)
#define COL_BG_INNER    RGB(20, 20, 34)
#define COL_BG_BTN      RGB(45, 45, 63)
#define COL_ACCENT      RGB(79, 70, 229)
#define COL_NAV_ACTIVE  RGB(45, 45, 95)
#define COL_CYAN        RGB(56, 189, 248)
#define COL_PURPLE      RGB(192, 132, 252)
#define COL_GREEN       RGB(22, 163, 74)
#define COL_RED         RGB(185, 28, 28)
#define COL_TEXT        RGB(248, 250, 252)
#define COL_MUTED       RGB(148, 163, 184)

typedef enum { PAGE_RGB = 0, PAGE_COOLER = 1, PAGE_FANS = 2, PAGE_PROFILES = 3, PAGE_SETTINGS = 4 } AppPage;

typedef struct {
    const char *name;
    COLORREF bg;
    COLORREF accent;
    COLORREF text;
} LcdPreset;

static const LcdPreset LCD_PRESETS[4] = {
    {"Cyberpunk",     RGB(15, 23, 42),  RGB(56, 189, 248), RGB(248, 250, 252)},
    {"Minimal Dark",  RGB(9, 9, 11),    RGB(168, 85, 247), RGB(250, 250, 250)},
    {"Ice Blue",      RGB(8, 47, 73),   RGB(34, 211, 238), RGB(236, 254, 255)},
    {"Thermal Alert", RGB(28, 10, 10),  RGB(239, 68, 68),  RGB(254, 242, 242)},
};

static char             g_base_dir[MAX_PATH] = {0};
static AppSettings      g_cfg;
static RGBDevice        g_rgb_devs[MAX_RGB_DEVICES];
static int              g_rgb_count = 0;
static int              g_selected_dev = 0;
static CoolerUsbInfo    g_cooler;
static SensorSnapshot   g_snap;
static CRITICAL_SECTION g_snap_cs;

static AppPage          g_page = PAGE_RGB;
static int              g_fan_subtab = 0; // 0 = Ventole, 1 = Temperature
static int              g_fan_duty_ui[MAX_FANS];
static bool             g_fan_manual_ui[MAX_FANS];
static bool             g_fan_ui_init = false;

static HWND             g_hwnd = NULL;
static HFONT            g_font_title = NULL, g_font_bold = NULL, g_font_norm = NULL, g_font_small = NULL, g_font_big = NULL;
static HBITMAP          g_wheel_bmp = NULL;
static int              g_wheel_size = 196;
static int              g_wheel_cx = 0, g_wheel_cy = 0;
static bool             g_dragging_wheel = false;
static bool             g_dragging_bright = false;
static int              g_dragging_fan = -1;

static const char *EFFECTS[] = {"Static", "Breathing", "Rainbow", "Color Cycle", "Chase", "Wave", "Flicker"};
static int g_effect_idx = 0;

static const COLORREF PALETTE[11] = {
    RGB(255, 0, 0), RGB(255, 85, 0), RGB(255, 204, 0), RGB(0, 255, 51),
    RGB(0, 255, 204), RGB(0, 180, 255), RGB(0, 51, 255), RGB(124, 0, 255),
    RGB(255, 0, 204), RGB(255, 255, 255), RGB(0, 0, 0)
};

// ---------------------------------------------------------------------------
// Ruota colori HSV
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
                if (ang < 0) ang += 2.0 * M_PI_D;
                double h = ang / (2.0 * M_PI_D);
                double s = dist / max_r;
                uint8_t r, g, b;
                hsv_to_rgb(h, s, 1.0, &r, &g, &b);
                bits[y * sz + x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            } else {
                bits[y * sz + x] = 0x001E1E2E;
            }
        }
    }
    return bmp;
}

// ---------------------------------------------------------------------------
// Azioni Hardware & Threading
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

static void sync_fan_ui_from_snap(const SensorSnapshot *s) {
    if (!g_fan_ui_init && s->fan_count > 0) {
        for (int i = 0; i < s->fan_count && i < MAX_FANS; i++) {
            g_fan_duty_ui[i] = s->fans[i].duty > 0 ? s->fans[i].duty : 50;
            g_fan_manual_ui[i] = false;
        }
        g_fan_ui_init = true;
    }
}

static void reload_all_devices(void) {
    g_rgb_count = hid_detect_rgb_devices(g_rgb_devs, MAX_RGB_DEVICES - 1, &g_cooler);

    SensorSnapshot tmp;
    if (bridge_refresh(&tmp)) {
        EnterCriticalSection(&g_snap_cs);
        g_snap = tmp;
        sync_fan_ui_from_snap(&tmp);
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
    if (g_cfg.startup_profile >= 0 && g_cfg.startup_profile < g_cfg.profile_count) {
        const SavedProfile *pr = &g_cfg.profiles[g_cfg.startup_profile];
        g_cfg.r = pr->r; g_cfg.g = pr->g; g_cfg.b = pr->b;
        g_cfg.brightness = pr->brightness;
        g_cfg.lcd_preset = pr->lcd_preset;
        for (int i = 0; i < 7; i++) if (strcmp(EFFECTS[i], pr->effect) == 0) g_effect_idx = i;
    }
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
        sync_fan_ui_from_snap(&tmp);
        LeaveCriticalSection(&g_snap_cs);
        PostMessageA(g_hwnd, WM_SENSORS_UPDATED, 0, 0);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Helpers GDI Double-Buffered + Gauge Circolari
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

static void draw_arc_segment(HDC hdc, int cx, int cy, int r, double start_deg, double sweep_deg, COLORREF col, int width) {
    if (sweep_deg <= 0.5) return;
    HPEN pen = CreatePen(PS_SOLID, width, col);
    HGDIOBJ op = SelectObject(hdc, pen);
    int steps = 28;
    for (int i = 0; i <= steps; i++) {
        double deg = start_deg - (sweep_deg * i) / steps;
        double rad = deg * M_PI_D / 180.0;
        int px = cx + (int)lrint(r * cos(rad));
        int py = cy - (int)lrint(r * sin(rad));
        if (i == 0) MoveToEx(hdc, px, py, NULL);
        else LineTo(hdc, px, py);
    }
    SelectObject(hdc, op);
    DeleteObject(pen);
}

static void draw_temp_gauge(HDC hdc, int x, int y, int w, int h, const char *label, float celsius) {
    RECT card = {x, y, x + w, y + h};
    fill_round_rect(hdc, card, RGB(35, 35, 58), 10);

    int cx = x + w / 2;
    int cy = y + (int)(h * 0.65);
    int r = (int)(w * 0.34);

    draw_arc_segment(hdc, cx, cy, r, 180.0, 180.0, RGB(55, 55, 75), 9);

    double pct = celsius / 110.0;
    if (pct < 0.0) pct = 0.0;
    if (pct > 1.0) pct = 1.0;
    COLORREF arcCol = (pct >= 0.85) ? RGB(255, 68, 68) : (pct >= 0.65 ? RGB(255, 165, 0) : RGB(0, 204, 102));
    draw_arc_segment(hdc, cx, cy, r, 180.0, pct * 180.0, arcCol, 9);

    char valStr[24];
    snprintf(valStr, sizeof(valStr), "%.0f C", celsius);
    RECT vRc = {x + 6, cy - 30, x + w - 6, cy + 4};
    draw_text_c(hdc, valStr, vRc, COL_TEXT, g_font_title, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    RECT lRc = {x + 6, y + h - 28, x + w - 6, y + h - 6};
    draw_text_c(hdc, label, lRc, COL_MUTED, g_font_small, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// ---------------------------------------------------------------------------
// Rendering delle 5 Pagine (identiche alla GUI Python)
// ---------------------------------------------------------------------------

static void render_ui(HDC hdc, int w, int h) {
    RECT full = {0, 0, w, h};
    HBRUSH bgMain = CreateSolidBrush(COL_BG_MAIN);
    FillRect(hdc, &full, bgMain);
    DeleteObject(bgMain);

    // ---- Sidebar (215 px) ----
    RECT sb = {0, 0, 215, h};
    HBRUSH bgSb = CreateSolidBrush(COL_BG_SIDEBAR);
    FillRect(hdc, &sb, bgSb);
    DeleteObject(bgSb);

    RECT logo1 = {16, 18, 92, 48};
    draw_text_c(hdc, "RGB", logo1, COL_PURPLE, g_font_title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT logo2 = {68, 20, 198, 48};
    draw_text_c(hdc, "Controller", logo2, COL_MUTED, g_font_bold, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT divLine = {14, 56, 200, 57};
    HBRUSH divBr = CreateSolidBrush(RGB(50, 50, 68));
    FillRect(hdc, &divLine, divBr);
    DeleteObject(divBr);

    const char *nav_names[5] = {"RGB LED", "Raffreddamento & LCD", "Ventole & Temp", "Profili", "Impostazioni"};
    for (int i = 0; i < 5; i++) {
        RECT b = {10, 70 + i * 46, 204, 110 + i * 46};
        bool active = ((int)g_page == i);
        fill_round_rect(hdc, b, active ? COL_NAV_ACTIVE : COL_BG_SIDEBAR, 8);
        b.left += 14;
        draw_text_c(hdc, nav_names[i], b, active ? COL_TEXT : COL_MUTED, g_font_bold, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    SensorSnapshot snap;
    EnterCriticalSection(&g_snap_cs);
    snap = g_snap;
    LeaveCriticalSection(&g_snap_cs);

    RECT rgbStRc = {14, h - 54, 204, h - 34};
    draw_text_c(hdc, g_rgb_count > 0 ? "Motore RGB: OK (Nativo)" : "Motore RGB: Nessuno",
                rgbStRc, g_rgb_count > 0 ? RGB(0, 204, 102) : RGB(255, 85, 85), g_font_small, DT_LEFT | DT_SINGLELINE);

    char st[96];
    snprintf(st, sizeof(st), "%d RGB  -  %d ventole  -  %d sensori", g_rgb_count, snap.fan_count, snap.temp_count);
    RECT stRc = {14, h - 30, 204, h - 10};
    draw_text_c(hdc, st, stRc, RGB(110, 110, 135), g_font_small, DT_LEFT | DT_SINGLELINE);

    int cx = 228;

    // =======================================================================
    // PAGINA 1: RGB LED
    // =======================================================================
    if (g_page == PAGE_RGB) {
        RECT listCard = {cx, 12, cx + 235, h - 12};
        fill_round_rect(hdc, listCard, COL_BG_CARD, 10);

        RECT hdrL = {cx + 12, 20, cx + 223, 42};
        draw_text_c(hdc, "Dispositivi RGB", hdrL, COL_TEXT, g_font_bold, DT_CENTER | DT_SINGLELINE);

        RECT btnAll = {cx + 12, 48, cx + 223, 82};
        fill_round_rect(hdc, btnAll, COL_ACCENT, 8);
        draw_text_c(hdc, "Applica a TUTTI", btnAll, COL_TEXT, g_font_bold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        RECT btnRel = {cx + 12, 88, cx + 223, 120};
        fill_round_rect(hdc, btnRel, RGB(51, 65, 85), 8);
        draw_text_c(hdc, "Ricarica Luci", btnRel, COL_TEXT, g_font_bold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        for (int i = 0; i < g_rgb_count && i < 10; i++) {
            RECT dr = {cx + 12, 134 + i * 44, cx + 223, 170 + i * 44};
            fill_round_rect(hdc, dr, (i == g_selected_dev) ? COL_ACCENT : COL_BG_BTN, 8);
            char lbl[160];
            snprintf(lbl, sizeof(lbl), "[%s] %s", g_rgb_devs[i].category, g_rgb_devs[i].name);
            dr.left += 10; dr.right -= 8;
            draw_text_c(hdc, lbl, dr, COL_TEXT, g_font_norm, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }

        int ex = cx + 248;
        RECT edCard = {ex, 12, w - 12, h - 12};
        fill_round_rect(hdc, edCard, COL_BG_CARD, 10);

        if (g_selected_dev < g_rgb_count) {
            const RGBDevice *d = &g_rgb_devs[g_selected_dev];
            RECT dnRc = {ex + 18, 22, w - 24, 50};
            draw_text_c(hdc, d->name, dnRc, COL_TEXT, g_font_title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            char meta[96];
            snprintf(meta, sizeof(meta), "Categoria: %s  |  Zone: %d", d->category, d->zone_count);
            RECT mRc = {ex + 18, 52, w - 24, 72};
            draw_text_c(hdc, meta, mRc, COL_MUTED, g_font_small, DT_LEFT | DT_SINGLELINE);
        }

        g_wheel_cx = ex + 118;
        g_wheel_cy = 186;
        if (g_wheel_bmp) {
            HDC m = CreateCompatibleDC(hdc);
            HGDIOBJ ob = SelectObject(m, g_wheel_bmp);
            BitBlt(hdc, g_wheel_cx - g_wheel_size / 2, g_wheel_cy - g_wheel_size / 2,
                   g_wheel_size, g_wheel_size, m, 0, 0, SRCCOPY);
            SelectObject(m, ob);
            DeleteDC(m);
        }

        RECT prevRc = {ex + 22, 298, ex + 82, 332};
        fill_round_rect(hdc, prevRc, RGB(g_cfg.r, g_cfg.g, g_cfg.b), 6);
        char hexStr[24];
        snprintf(hexStr, sizeof(hexStr), "#%02X%02X%02X", g_cfg.r, g_cfg.g, g_cfg.b);
        RECT hexRc = {ex + 92, 298, ex + 214, 332};
        fill_round_rect(hdc, hexRc, COL_BG_INNER, 6);
        draw_text_c(hdc, hexStr, hexRc, COL_TEXT, g_font_bold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        int rx = ex + 242;
        RECT effLbl = {rx, 84, rx + 220, 102};
        draw_text_c(hdc, "Effetto (clicca per scorrere)", effLbl, COL_MUTED, g_font_small, DT_LEFT | DT_SINGLELINE);
        RECT effBtn = {rx, 106, rx + 220, 138};
        fill_round_rect(hdc, effBtn, COL_BG_BTN, 8);
        draw_text_c(hdc, EFFECTS[g_effect_idx], effBtn, COL_TEXT, g_font_bold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        char bTxt[48];
        snprintf(bTxt, sizeof(bTxt), "Luminosita': %d%%", g_cfg.brightness);
        RECT bLbl = {rx, 150, rx + 220, 168};
        draw_text_c(hdc, bTxt, bLbl, COL_MUTED, g_font_small, DT_LEFT | DT_SINGLELINE);
        RECT bTrack = {rx, 174, rx + 240, 188};
        fill_round_rect(hdc, bTrack, RGB(50, 50, 75), 6);
        RECT bFill = {rx, 174, rx + (240 * g_cfg.brightness) / 100, 188};
        fill_round_rect(hdc, bFill, COL_CYAN, 6);

        RECT pLbl = {rx, 202, rx + 220, 220};
        draw_text_c(hdc, "Colori vividi rapidi", pLbl, COL_MUTED, g_font_small, DT_LEFT | DT_SINGLELINE);
        for (int i = 0; i < 11; i++) {
            RECT pr = {rx + i * 28, 226, rx + i * 28 + 22, 248};
            fill_round_rect(hdc, pr, PALETTE[i], 11);
        }

        RECT syncRc = {rx, 266, rx + 320, 296};
        fill_round_rect(hdc, syncRc, g_cfg.sync_all ? RGB(30, 58, 138) : COL_BG_BTN, 8);
        draw_text_c(hdc, g_cfg.sync_all ? "[ON] Sincronizza tutti (RAM + Ventole + Mobo)" : "[OFF] Solo dispositivo selezionato",
                    syncRc, COL_TEXT, g_font_norm, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        RECT appBtn = {rx, 308, rx + 210, 350};
        fill_round_rect(hdc, appBtn, COL_GREEN, 8);
        draw_text_c(hdc, "Applica Colore", appBtn, COL_TEXT, g_font_bold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        if (g_selected_dev < g_rgb_count && g_rgb_devs[g_selected_dev].driver == DRV_IT5702) {
            RECT zHdr = {ex + 18, 368, w - 24, 390};
            draw_text_c(hdc, "Zone & Header ARGB (Ventole Case / AIO collegate alla Scheda Madre)",
                        zHdr, COL_CYAN, g_font_bold, DT_LEFT | DT_SINGLELINE);
            for (int z = 0; z < 6; z++) {
                int y = 398 + z * 42;
                RECT zr = {ex + 18, y, w - 24, y + 36};
                fill_round_rect(hdc, zr, COL_BG_INNER, 8);
                RECT zl = {ex + 30, y, ex + 320, y + 36};
                draw_text_c(hdc, g_rgb_devs[g_selected_dev].zones[z].name, zl, COL_TEXT, g_font_norm, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                RECT zb = {w - 230, y + 4, w - 34, y + 32};
                fill_round_rect(hdc, zb, COL_ACCENT, 6);
                draw_text_c(hdc, "Applica colore a zona", zb, COL_TEXT, g_font_small, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }
    }

    // =======================================================================
    // PAGINA 2: RAFFREDDAMENTO & LCD (stile TRCC user-friendly)
    // =======================================================================
    else if (g_page == PAGE_COOLER) {
        RECT hdr = {cx, 12, w - 12, 48};
        draw_text_c(hdc, "Raffreddamento a Liquido & Monitor LCD", hdr, COL_TEXT, g_font_title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT usbRc = {cx + 420, 16, w - 16, 44};
        draw_text_c(hdc, g_cooler.connected ? g_cooler.name : "Modalita' Monitor Software (nessun LCD USB)",
                    usbRc, g_cooler.connected ? RGB(34, 197, 94) : RGB(234, 179, 8), g_font_bold, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        // Colonna sinistra: Anteprima LCD 1:1 + Telemetria
        RECT leftCard = {cx, 56, cx + 340, h - 12};
        fill_round_rect(hdc, leftCard, COL_BG_CARD, 10);

        RECT pTitle = {cx + 12, 68, cx + 328, 90};
        draw_text_c(hdc, "Anteprima Display AIO", pTitle, COL_MUTED, g_font_bold, DT_CENTER | DT_SINGLELINE);

        const LcdPreset *pr = &LCD_PRESETS[g_cfg.lcd_preset % 4];
        int lx = cx + 170, ly = 235, lr = 128;
        HBRUSH bLcd = CreateSolidBrush(pr->bg);
        HPEN pLcd = CreatePen(PS_SOLID, 4, pr->accent);
        HGDIOBJ ob = SelectObject(hdc, bLcd), op = SelectObject(hdc, pLcd);
        if (g_cfg.lcd_circle)
            Ellipse(hdc, lx - lr, ly - lr, lx + lr, ly + lr);
        else
            Rectangle(hdc, lx - lr, ly - lr, lx + lr, ly + lr);
        SelectObject(hdc, ob); SelectObject(hdc, op);
        DeleteObject(bLcd); DeleteObject(pLcd);

        RECT l1 = {lx - 110, ly - 88, lx + 110, ly - 64};
        draw_text_c(hdc, g_cfg.lcd_label, l1, pr->accent, g_font_bold, DT_CENTER | DT_SINGLELINE);

        if (g_cfg.lcd_show_cpu) {
            char ct[32];
            snprintf(ct, sizeof(ct), snap.cpu_temp > 0 ? "%.0f C" : "-- C", snap.cpu_temp);
            RECT l2 = {lx - 110, ly - 44, lx + 110, ly + 18};
            draw_text_c(hdc, ct, l2, pr->text, g_font_big, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            RECT l2s = {lx - 80, ly + 16, lx + 80, ly + 34};
            draw_text_c(hdc, "CPU TEMP", l2s, COL_MUTED, g_font_small, DT_CENTER | DT_SINGLELINE);
        }

        int max_rpm = 0;
        for (int i = 0; i < snap.fan_count; i++) if (snap.fans[i].rpm > max_rpm) max_rpm = snap.fans[i].rpm;

        char sub[128] = "";
        if (g_cfg.lcd_show_gpu) { char b[32]; snprintf(b, 32, "GPU %.0fC  ", snap.gpu_temp); strcat(sub, b); }
        if (g_cfg.lcd_show_load) { char b[32]; snprintf(b, 32, "LOAD %.0f%%  ", snap.cpu_load); strcat(sub, b); }
        if (g_cfg.lcd_show_rpm) { char b[32]; snprintf(b, 32, "%d RPM", max_rpm); strcat(sub, b); }
        RECT l3 = {lx - 122, ly + 46, lx + 122, ly + 68};
        draw_text_c(hdc, sub, l3, pr->text, g_font_small, DT_CENTER | DT_SINGLELINE);

        if (g_cfg.lcd_show_clock) {
            SYSTEMTIME stime;
            GetLocalTime(&stime);
            char ck[24];
            snprintf(ck, sizeof(ck), "%02d:%02d:%02d", stime.wHour, stime.wMinute, stime.wSecond);
            RECT l4 = {lx - 80, ly + 74, lx + 80, ly + 96};
            draw_text_c(hdc, ck, l4, pr->accent, g_font_bold, DT_CENTER | DT_SINGLELINE);
        }

        // Card telemetria sotto l'anteprima
        RECT tele = {cx + 16, 390, cx + 324, h - 28};
        fill_round_rect(hdc, tele, COL_BG_INNER, 10);
        char tlines[4][64];
        snprintf(tlines[0], 64, "CPU Temp:       %.1f C", snap.cpu_temp);
        snprintf(tlines[1], 64, "GPU Temp:       %.1f C", snap.gpu_temp);
        snprintf(tlines[2], 64, "CPU / RAM:      %.0f%% / %.0f%%", snap.cpu_load, snap.ram_load);
        snprintf(tlines[3], 64, "Max Fan/Pump:   %d RPM", max_rpm);
        for (int i = 0; i < 4; i++) {
            RECT tr = {cx + 28, 408 + i * 44, cx + 312, 438 + i * 44};
            draw_text_c(hdc, tlines[i], tr, COL_TEXT, g_font_bold, DT_LEFT | DT_SINGLELINE);
        }

        // Colonna destra: Controlli LCD (Temi, Forma, Dati mostrati)
        int rx = cx + 356;
        RECT rightCard = {rx, 56, w - 12, h - 12};
        fill_round_rect(hdc, rightCard, COL_BG_CARD, 10);

        RECT s1 = {rx + 18, 72, w - 24, 94};
        draw_text_c(hdc, "Tema Rapido Display", s1, COL_CYAN, g_font_bold, DT_LEFT | DT_SINGLELINE);
        for (int i = 0; i < 4; i++) {
            RECT pb = {rx + 18 + i * 130, 102, rx + 140 + i * 130, 136};
            fill_round_rect(hdc, pb, (g_cfg.lcd_preset == i) ? COL_ACCENT : COL_BG_BTN, 8);
            draw_text_c(hdc, LCD_PRESETS[i].name, pb, COL_TEXT, g_font_norm, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        RECT s2 = {rx + 18, 158, w - 24, 180};
        draw_text_c(hdc, "Forma Schermo AIO", s2, COL_CYAN, g_font_bold, DT_LEFT | DT_SINGLELINE);
        RECT sh1 = {rx + 18, 188, rx + 150, 222};
        fill_round_rect(hdc, sh1, g_cfg.lcd_circle ? COL_ACCENT : COL_BG_BTN, 8);
        draw_text_c(hdc, "Cerchio", sh1, COL_TEXT, g_font_bold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        RECT sh2 = {rx + 162, 188, rx + 294, 222};
        fill_round_rect(hdc, sh2, !g_cfg.lcd_circle ? COL_ACCENT : COL_BG_BTN, 8);
        draw_text_c(hdc, "Quadrato", sh2, COL_TEXT, g_font_bold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        RECT s3 = {rx + 18, 246, w - 24, 268};
        draw_text_c(hdc, "Dati mostrati sul display (clicca per attivare/disattivare)", s3, COL_CYAN, g_font_bold, DT_LEFT | DT_SINGLELINE);
        const char *chk_lbl[5] = {"Temperatura CPU", "Temperatura GPU", "Carico CPU (%)", "Velocita' Ventole (RPM)", "Orologio digitale"};
        bool chk_val[5] = {g_cfg.lcd_show_cpu, g_cfg.lcd_show_gpu, g_cfg.lcd_show_load, g_cfg.lcd_show_rpm, g_cfg.lcd_show_clock};
        for (int i = 0; i < 5; i++) {
            RECT cr = {rx + 18, 280 + i * 40, rx + 300, 312 + i * 40};
            fill_round_rect(hdc, cr, chk_val[i] ? RGB(22, 101, 52) : COL_BG_BTN, 8);
            char txt[48];
            snprintf(txt, sizeof(txt), "%s  %s", chk_val[i] ? "[ON]" : "[OFF]", chk_lbl[i]);
            cr.left += 12;
            draw_text_c(hdc, txt, cr, COL_TEXT, g_font_norm, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
    }

    // =======================================================================
    // PAGINA 3: VENTOLE & TEMPERATURE (con sub-tab + Gauge Circolari)
    // =======================================================================
    else if (g_page == PAGE_FANS) {
        RECT hdr = {cx, 12, cx + 320, 48};
        draw_text_c(hdc, "Ventole & Temperature", hdr, COL_TEXT, g_font_title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT tb0 = {cx + 340, 14, cx + 470, 46};
        fill_round_rect(hdc, tb0, (g_fan_subtab == 0) ? COL_ACCENT : COL_BG_BTN, 8);
        draw_text_c(hdc, "Ventole", tb0, COL_TEXT, g_font_bold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        RECT tb1 = {cx + 480, 14, cx + 620, 46};
        fill_round_rect(hdc, tb1, (g_fan_subtab == 1) ? COL_ACCENT : COL_BG_BTN, 8);
        draw_text_c(hdc, "Temperature", tb1, COL_TEXT, g_font_bold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        RECT card = {cx, 56, w - 12, h - 12};
        fill_round_rect(hdc, card, COL_BG_CARD, 10);

        if (g_fan_subtab == 0) {
            RECT th = {cx + 12, 66, w - 24, 96};
            fill_round_rect(hdc, th, RGB(42, 42, 58), 6);
            RECT c1 = {cx + 24, 66, cx + 280, 96}; draw_text_c(hdc, "Ventola / Hardware", c1, COL_MUTED, g_font_bold, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            RECT c2 = {cx + 300, 66, cx + 390, 96}; draw_text_c(hdc, "RPM", c2, COL_MUTED, g_font_bold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            RECT c3 = {cx + 410, 66, cx + 630, 96}; draw_text_c(hdc, "Velocita' % (trascina)", c3, COL_MUTED, g_font_bold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            RECT c4 = {cx + 650, 66, w - 28, 96}; draw_text_c(hdc, "Controllo", c4, COL_MUTED, g_font_bold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            for (int i = 0; i < snap.fan_count && i < 10; i++) {
                int y = 104 + i * 50;
                RECT fr = {cx + 12, y, w - 24, y + 44};
                fill_round_rect(hdc, fr, COL_BG_INNER, 8);

                char fn[140];
                snprintf(fn, sizeof(fn), "%s (%s)", snap.fans[i].name, snap.fans[i].hardware);
                RECT r1 = {cx + 24, y, cx + 295, y + 44};
                draw_text_c(hdc, fn, r1, COL_TEXT, g_font_norm, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                char rpm[24];
                snprintf(rpm, sizeof(rpm), "%d RPM", snap.fans[i].rpm);
                RECT r2 = {cx + 300, y, cx + 390, y + 44};
                draw_text_c(hdc, rpm, r2, COL_CYAN, g_font_bold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                int duty = g_fan_duty_ui[i];
                if (snap.fans[i].controllable) {
                    RECT trk = {cx + 410, y + 16, cx + 570, y + 28};
                    fill_round_rect(hdc, trk, RGB(50, 50, 75), 5);
                    RECT fl = {cx + 410, y + 16, cx + 410 + (160 * duty) / 100, y + 28};
                    fill_round_rect(hdc, fl, COL_CYAN, 5);

                    char dp[16];
                    snprintf(dp, sizeof(dp), "%d%%", duty);
                    RECT dl = {cx + 576, y, cx + 628, y + 44};
                    draw_text_c(hdc, dp, dl, COL_TEXT, g_font_bold, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                    RECT bAp = {cx + 645, y + 7, cx + 735, y + 37};
                    fill_round_rect(hdc, bAp, COL_GREEN, 6);
                    draw_text_c(hdc, "Applica", bAp, COL_TEXT, g_font_bold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                    RECT bAu = {cx + 745, y + 7, cx + 825, y + 37};
                    fill_round_rect(hdc, bAu, g_fan_manual_ui[i] ? COL_BG_BTN : COL_ACCENT, 6);
                    draw_text_c(hdc, "AUTO", bAu, COL_TEXT, g_font_bold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                } else {
                    RECT r3 = {cx + 420, y, w - 34, y + 44};
                    draw_text_c(hdc, "Solo lettura (controllata dal BIOS)", r3, COL_MUTED, g_font_small, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                }
            }
        } else {
            // Griglia 4x3 di Gauge Circolari animate
            int shown = snap.temp_count < 12 ? snap.temp_count : 12;
            for (int i = 0; i < shown; i++) {
                int col = i % 4, row = i / 4;
                int gx = cx + 18 + col * 218;
                int gy = 70 + row * 172;
                draw_temp_gauge(hdc, gx, gy, 204, 158, snap.temps[i].name, snap.temps[i].celsius);
            }
        }
    }

    // =======================================================================
    // PAGINA 4: PROFILI
    // =======================================================================
    else if (g_page == PAGE_PROFILES) {
        RECT hdr = {cx, 12, w - 12, 48};
        draw_text_c(hdc, "Gestione Profili", hdr, COL_TEXT, g_font_title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT card = {cx, 56, w - 12, h - 12};
        fill_round_rect(hdc, card, COL_BG_CARD, 10);

        RECT bar = {cx + 16, 72, w - 28, 122};
        fill_round_rect(hdc, bar, COL_BG_INNER, 8);
        RECT bl = {cx + 28, 72, cx + 380, 122};
        draw_text_c(hdc, "Salva la configurazione corrente (RGB + LCD) come nuovo profilo:", bl, COL_TEXT, g_font_norm, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT bSave = {w - 220, 80, w - 42, 114};
        fill_round_rect(hdc, bSave, COL_GREEN, 8);
        draw_text_c(hdc, "+ Salva Profilo", bSave, COL_TEXT, g_font_bold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        for (int i = 0; i < g_cfg.profile_count; i++) {
            int y = 140 + i * 50;
            RECT pr = {cx + 16, y, w - 28, y + 42};
            fill_round_rect(hdc, pr, COL_BG_INNER, 8);

            char pn[96];
            snprintf(pn, sizeof(pn), "%s  (#%02X%02X%02X - %s)%s",
                     g_cfg.profiles[i].name, g_cfg.profiles[i].r, g_cfg.profiles[i].g, g_cfg.profiles[i].b,
                     g_cfg.profiles[i].effect, (g_cfg.startup_profile == i) ? "  [AVVIO]" : "");
            RECT pnr = {cx + 30, y, cx + 480, y + 42};
            draw_text_c(hdc, pn, pnr, COL_TEXT, g_font_norm, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            RECT bLoad = {w - 340, y + 6, w - 245, y + 36};
            fill_round_rect(hdc, bLoad, COL_ACCENT, 6);
            draw_text_c(hdc, "Carica", bLoad, COL_TEXT, g_font_bold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            RECT bBoot = {w - 235, y + 6, w - 135, y + 36};
            fill_round_rect(hdc, bBoot, (g_cfg.startup_profile == i) ? COL_CYAN : COL_BG_BTN, 6);
            draw_text_c(hdc, "Avvio Auto", bBoot, (g_cfg.startup_profile == i) ? RGB(10, 10, 20) : COL_TEXT, g_font_bold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            RECT bDel = {w - 125, y + 6, w - 40, y + 36};
            fill_round_rect(hdc, bDel, COL_RED, 6);
            draw_text_c(hdc, "Elimina", bDel, COL_TEXT, g_font_bold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        if (g_cfg.profile_count == 0) {
            RECT empty = {cx + 16, 160, w - 28, 220};
            draw_text_c(hdc, "Nessun profilo salvato. Clicca '+ Salva Profilo' per crearne uno.", empty, COL_MUTED, g_font_norm, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    // =======================================================================
    // PAGINA 5: IMPOSTAZIONI
    // =======================================================================
    else if (g_page == PAGE_SETTINGS) {
        RECT hdr = {cx, 12, w - 12, 48};
        draw_text_c(hdc, "Impostazioni", hdr, COL_TEXT, g_font_title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT card = {cx, 56, w - 12, h - 12};
        fill_round_rect(hdc, card, COL_BG_CARD, 10);

        RECT s1 = {cx + 20, 74, w - 30, 98};
        draw_text_c(hdc, "Intervallo Aggiornamento Sensori", s1, COL_PURPLE, g_font_bold, DT_LEFT | DT_SINGLELINE);
        const int intervals[3] = {1000, 2000, 5000};
        const char *int_lbl[3] = {"1000 ms (Veloce)", "2000 ms (Normale)", "5000 ms (Risparmio)"};
        for (int i = 0; i < 3; i++) {
            RECT ib = {cx + 20 + i * 175, 108, cx + 185 + i * 175, 142};
            fill_round_rect(hdc, ib, (g_cfg.refresh_ms == intervals[i]) ? COL_ACCENT : COL_BG_BTN, 8);
            draw_text_c(hdc, int_lbl[i], ib, COL_TEXT, g_font_norm, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        RECT s2 = {cx + 20, 174, w - 30, 198};
        draw_text_c(hdc, "Motori Hardware Integrati (C11 Nativo)", s2, COL_PURPLE, g_font_bold, DT_LEFT | DT_SINGLELINE);
        const char *info[4] = {
            "- Schede Madri HID: Gigabyte (IT5702/IT8297), ASUS Aura, MSI Mystic Light, ASRock",
            "- RAM SMBus DDR4/DDR5: Kingston HyperX, ENE/Aura/G.Skill/Kingston DDR5, Corsair",
            "- Periferiche HID++: Tastiere Logitech (G213, G513, G610, G413, G815)",
            "- Sensori & Ventole: SensorBridge nativo + Thermalright AIO USB LCD (0416:5302)"
        };
        for (int i = 0; i < 4; i++) {
            RECT ir = {cx + 24, 210 + i * 32, w - 30, 238 + i * 32};
            draw_text_c(hdc, info[i], ir, COL_TEXT, g_font_norm, DT_LEFT | DT_SINGLELINE);
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
    if (ang < 0) ang += 2.0 * M_PI_D;
    double h = ang / (2.0 * M_PI_D);
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
        render_ui(mem, w, h);
        BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
        for (int i = 0; i < 5; i++) {
            if (pt_in(x, y, 10, 70 + i * 46, 204, 110 + i * 46)) {
                g_page = (AppPage)i;
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
        }
        int cx = 228;
        if (g_page == PAGE_RGB) {
            if (pt_in(x, y, cx + 12, 48, cx + 223, 82)) { apply_rgb_action(true); return 0; }
            if (pt_in(x, y, cx + 12, 88, cx + 223, 120)) {
                reload_all_devices(); apply_rgb_action(true);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            for (int i = 0; i < g_rgb_count && i < 10; i++) {
                if (pt_in(x, y, cx + 12, 134 + i * 44, cx + 223, 170 + i * 44)) {
                    g_selected_dev = i;
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
            }
            int ex = cx + 248, rx = ex + 242;
            if (hypot(x - g_wheel_cx, y - g_wheel_cy) <= g_wheel_size / 2.0) {
                g_dragging_wheel = true; SetCapture(hwnd);
                pick_wheel_color(x, y);
                return 0;
            }
            if (pt_in(x, y, rx, 106, rx + 220, 138)) {
                g_effect_idx = (g_effect_idx + 1) % 7;
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            if (pt_in(x, y, rx, 168, rx + 240, 194)) {
                g_dragging_bright = true; SetCapture(hwnd);
                g_cfg.brightness = ((x - rx) * 100) / 240;
                if (g_cfg.brightness < 0) g_cfg.brightness = 0;
                if (g_cfg.brightness > 100) g_cfg.brightness = 100;
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            for (int i = 0; i < 11; i++) {
                if (pt_in(x, y, rx + i * 28, 226, rx + i * 28 + 22, 248)) {
                    g_cfg.r = GetRValue(PALETTE[i]); g_cfg.g = GetGValue(PALETTE[i]); g_cfg.b = GetBValue(PALETTE[i]);
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
            }
            if (pt_in(x, y, rx, 266, rx + 320, 296)) {
                g_cfg.sync_all = !g_cfg.sync_all;
                settings_save(g_base_dir, &g_cfg);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            if (pt_in(x, y, rx, 308, rx + 210, 350)) { apply_rgb_action(false); return 0; }
            if (g_selected_dev < g_rgb_count && g_rgb_devs[g_selected_dev].driver == DRV_IT5702) {
                RECT rc; GetClientRect(hwnd, &rc);
                int w = rc.right;
                for (int z = 0; z < 6; z++) {
                    int zy = 398 + z * 42;
                    if (pt_in(x, y, w - 230, zy + 4, w - 34, zy + 32)) {
                        hid_apply_zone(&g_rgb_devs[g_selected_dev], z, EFFECTS[g_effect_idx], g_cfg.r, g_cfg.g, g_cfg.b, g_cfg.brightness);
                        return 0;
                    }
                }
            }
        }
        else if (g_page == PAGE_COOLER) {
            int rx = cx + 356;
            for (int i = 0; i < 4; i++) {
                if (pt_in(x, y, rx + 18 + i * 130, 102, rx + 140 + i * 130, 136)) {
                    g_cfg.lcd_preset = i; settings_save(g_base_dir, &g_cfg);
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
            }
            if (pt_in(x, y, rx + 18, 188, rx + 150, 222)) {
                g_cfg.lcd_circle = true; settings_save(g_base_dir, &g_cfg);
                InvalidateRect(hwnd, NULL, FALSE); return 0;
            }
            if (pt_in(x, y, rx + 162, 188, rx + 294, 222)) {
                g_cfg.lcd_circle = false; settings_save(g_base_dir, &g_cfg);
                InvalidateRect(hwnd, NULL, FALSE); return 0;
            }
            bool *ptrs[5] = {&g_cfg.lcd_show_cpu, &g_cfg.lcd_show_gpu, &g_cfg.lcd_show_load, &g_cfg.lcd_show_rpm, &g_cfg.lcd_show_clock};
            for (int i = 0; i < 5; i++) {
                if (pt_in(x, y, rx + 18, 280 + i * 40, rx + 300, 312 + i * 40)) {
                    *ptrs[i] = !(*ptrs[i]); settings_save(g_base_dir, &g_cfg);
                    InvalidateRect(hwnd, NULL, FALSE); return 0;
                }
            }
        }
        else if (g_page == PAGE_FANS) {
            if (pt_in(x, y, cx + 340, 14, cx + 470, 46)) { g_fan_subtab = 0; InvalidateRect(hwnd, NULL, FALSE); return 0; }
            if (pt_in(x, y, cx + 480, 14, cx + 620, 46)) { g_fan_subtab = 1; InvalidateRect(hwnd, NULL, FALSE); return 0; }
            if (g_fan_subtab == 0) {
                EnterCriticalSection(&g_snap_cs);
                SensorSnapshot snap = g_snap;
                LeaveCriticalSection(&g_snap_cs);
                for (int i = 0; i < snap.fan_count && i < 10; i++) {
                    if (!snap.fans[i].controllable) continue;
                    int fy = 104 + i * 50;
                    if (pt_in(x, y, cx + 410, fy + 10, cx + 570, fy + 34)) {
                        g_dragging_fan = i; SetCapture(hwnd);
                        int d = ((x - (cx + 410)) * 100) / 160;
                        g_fan_duty_ui[i] = (d < 0) ? 0 : (d > 100 ? 100 : d);
                        g_fan_manual_ui[i] = true;
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    }
                    if (pt_in(x, y, cx + 645, fy + 7, cx + 735, fy + 37)) {
                        g_fan_manual_ui[i] = true;
                        bridge_set_fan_duty(snap.fans[i].control_id, g_fan_duty_ui[i]);
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    }
                    if (pt_in(x, y, cx + 745, fy + 7, cx + 825, fy + 37)) {
                        g_fan_manual_ui[i] = false;
                        bridge_set_fan_auto(snap.fans[i].control_id);
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    }
                }
            }
        }
        else if (g_page == PAGE_PROFILES) {
            RECT rc; GetClientRect(hwnd, &rc);
            int w = rc.right;
            if (pt_in(x, y, w - 220, 80, w - 42, 114) && g_cfg.profile_count < MAX_PROFILES) {
                SavedProfile *pr = &g_cfg.profiles[g_cfg.profile_count];
                snprintf(pr->name, sizeof(pr->name), "Profilo %d", g_cfg.profile_count + 1);
                pr->r = g_cfg.r; pr->g = g_cfg.g; pr->b = g_cfg.b;
                pr->brightness = g_cfg.brightness;
                snprintf(pr->effect, sizeof(pr->effect), "%s", EFFECTS[g_effect_idx]);
                pr->lcd_preset = g_cfg.lcd_preset;
                g_cfg.profile_count++;
                settings_save(g_base_dir, &g_cfg);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            for (int i = 0; i < g_cfg.profile_count; i++) {
                int py = 140 + i * 50;
                if (pt_in(x, y, w - 340, py + 6, w - 245, py + 36)) {
                    const SavedProfile *pr = &g_cfg.profiles[i];
                    g_cfg.r = pr->r; g_cfg.g = pr->g; g_cfg.b = pr->b;
                    g_cfg.brightness = pr->brightness; g_cfg.lcd_preset = pr->lcd_preset;
                    for (int e = 0; e < 7; e++) if (strcmp(EFFECTS[e], pr->effect) == 0) g_effect_idx = e;
                    apply_rgb_action(true);
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
                if (pt_in(x, y, w - 235, py + 6, w - 135, py + 36)) {
                    g_cfg.startup_profile = (g_cfg.startup_profile == i) ? -1 : i;
                    settings_save(g_base_dir, &g_cfg);
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
                if (pt_in(x, y, w - 125, py + 6, w - 40, py + 36)) {
                    for (int k = i; k + 1 < g_cfg.profile_count; k++) g_cfg.profiles[k] = g_cfg.profiles[k + 1];
                    g_cfg.profile_count--;
                    if (g_cfg.startup_profile == i) g_cfg.startup_profile = -1;
                    else if (g_cfg.startup_profile > i) g_cfg.startup_profile--;
                    settings_save(g_base_dir, &g_cfg);
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
            }
        }
        else if (g_page == PAGE_SETTINGS) {
            const int intervals[3] = {1000, 2000, 5000};
            for (int i = 0; i < 3; i++) {
                if (pt_in(x, y, cx + 20 + i * 175, 108, cx + 185 + i * 175, 142)) {
                    g_cfg.refresh_ms = intervals[i];
                    settings_save(g_base_dir, &g_cfg);
                    SetTimer(hwnd, TIMER_REFRESH_ID, (UINT)g_cfg.refresh_ms, NULL);
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
            }
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
        if (g_dragging_wheel) {
            pick_wheel_color(x, y);
        } else if (g_dragging_bright) {
            int rx = 228 + 248 + 242;
            g_cfg.brightness = ((x - rx) * 100) / 240;
            if (g_cfg.brightness < 0) g_cfg.brightness = 0;
            if (g_cfg.brightness > 100) g_cfg.brightness = 100;
            InvalidateRect(hwnd, NULL, FALSE);
        } else if (g_dragging_fan >= 0) {
            int d = ((x - (228 + 410)) * 100) / 160;
            g_fan_duty_ui[g_dragging_fan] = (d < 0) ? 0 : (d > 100 ? 100 : d);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if (g_dragging_wheel || g_dragging_bright || g_dragging_fan >= 0) {
            g_dragging_wheel = false;
            g_dragging_bright = false;
            g_dragging_fan = -1;
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

    g_rgb_count = hid_detect_rgb_devices(g_rgb_devs, MAX_RGB_DEVICES - 1, &g_cooler);

    g_font_title = CreateFontA(22, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    g_font_bold  = CreateFontA(15, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    g_font_norm  = CreateFontA(14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    g_font_small = CreateFontA(12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    g_font_big   = CreateFontA(46, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    g_wheel_bmp  = build_wheel_bitmap(g_wheel_size);

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "RGBControllerNativeWnd";
    RegisterClassA(&wc);

    g_hwnd = CreateWindowExA(0, wc.lpszClassName, "RGBController - PC Lighting & Cooling",
                             WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
                             CW_USEDEFAULT, CW_USEDEFAULT, 1180, 720,
                             NULL, NULL, hInst, NULL);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(g_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    ShowWindow(g_hwnd, nShow);
    UpdateWindow(g_hwnd);

    HANDLE wt = CreateThread(NULL, 0, worker_init_thread, NULL, 0, NULL);
    if (wt) CloseHandle(wt);
    SetTimer(g_hwnd, TIMER_REFRESH_ID, (UINT)g_cfg.refresh_ms, NULL);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}
