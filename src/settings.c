#include "settings.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void settings_load(const char *base_dir, AppSettings *s) {
    memset(s, 0, sizeof(*s));
    s->r = 0; s->g = 180; s->b = 255;
    s->brightness = 100;
    snprintf(s->effect, sizeof(s->effect), "Static");
    s->sync_all = true;
    s->lcd_preset = 0;
    s->lcd_circle = true;
    s->lcd_show_cpu = true;
    s->lcd_show_gpu = true;
    s->lcd_show_load = true;
    s->lcd_show_rpm = true;
    s->lcd_show_clock = true;
    snprintf(s->lcd_label, sizeof(s->lcd_label), "LIQUID COOLING");
    s->refresh_ms = 2000;
    s->startup_profile = -1;

    char path[MAX_PATH + 32];
    snprintf(path, sizeof(path), "%s\\config\\native_settings.ini", base_dir);
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        int val = 0;
        if (sscanf(line, "r=%d", &val) == 1) s->r = (uint8_t)val;
        else if (sscanf(line, "g=%d", &val) == 1) s->g = (uint8_t)val;
        else if (sscanf(line, "b=%d", &val) == 1) s->b = (uint8_t)val;
        else if (sscanf(line, "brightness=%d", &val) == 1) s->brightness = val;
        else if (sscanf(line, "sync_all=%d", &val) == 1) s->sync_all = (val != 0);
        else if (sscanf(line, "lcd_preset=%d", &val) == 1) s->lcd_preset = val;
        else if (sscanf(line, "lcd_circle=%d", &val) == 1) s->lcd_circle = (val != 0);
        else if (sscanf(line, "lcd_show_cpu=%d", &val) == 1) s->lcd_show_cpu = (val != 0);
        else if (sscanf(line, "lcd_show_gpu=%d", &val) == 1) s->lcd_show_gpu = (val != 0);
        else if (sscanf(line, "lcd_show_load=%d", &val) == 1) s->lcd_show_load = (val != 0);
        else if (sscanf(line, "lcd_show_rpm=%d", &val) == 1) s->lcd_show_rpm = (val != 0);
        else if (sscanf(line, "lcd_show_clock=%d", &val) == 1) s->lcd_show_clock = (val != 0);
        else if (sscanf(line, "refresh_ms=%d", &val) == 1) s->refresh_ms = (val >= 500 && val <= 10000) ? val : 2000;
        else if (sscanf(line, "startup_profile=%d", &val) == 1) s->startup_profile = val;
        else if (strncmp(line, "effect=", 7) == 0) {
            char *p = line + 7;
            p[strcspn(p, "\r\n")] = '\0';
            snprintf(s->effect, sizeof(s->effect), "%.*s", (int)(sizeof(s->effect) - 1), p);
        }
        else if (strncmp(line, "lcd_label=", 10) == 0) {
            char *p = line + 10;
            p[strcspn(p, "\r\n")] = '\0';
            snprintf(s->lcd_label, sizeof(s->lcd_label), "%.*s", (int)(sizeof(s->lcd_label) - 1), p);
        }
        else if (strncmp(line, "profile=", 8) == 0 && s->profile_count < MAX_PROFILES) {
            SavedProfile *pr = &s->profiles[s->profile_count];
            int pr_r = 0, pr_g = 180, pr_b = 255, pr_br = 100, pr_lcd = 0;
            char pr_name[32] = {0}, pr_eff[32] = {0};
            if (sscanf(line + 8, "%31[^|]|%d|%d|%d|%d|%31[^|]|%d",
                       pr_name, &pr_r, &pr_g, &pr_b, &pr_br, pr_eff, &pr_lcd) == 7) {
                snprintf(pr->name, sizeof(pr->name), "%s", pr_name);
                pr->r = (uint8_t)pr_r; pr->g = (uint8_t)pr_g; pr->b = (uint8_t)pr_b;
                pr->brightness = pr_br;
                snprintf(pr->effect, sizeof(pr->effect), "%s", pr_eff);
                pr->lcd_preset = pr_lcd;
                s->profile_count++;
            }
        }
    }
    fclose(f);
}

void settings_save(const char *base_dir, const AppSettings *s) {
    char dir[MAX_PATH + 16], path[MAX_PATH + 40];
    snprintf(dir, sizeof(dir), "%s\\config", base_dir);
    CreateDirectoryA(dir, NULL);
    snprintf(path, sizeof(path), "%s\\native_settings.ini", dir);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f,
            "r=%u\ng=%u\nb=%u\nbrightness=%d\neffect=%s\nsync_all=%d\n"
            "lcd_preset=%d\nlcd_circle=%d\nlcd_show_cpu=%d\nlcd_show_gpu=%d\n"
            "lcd_show_load=%d\nlcd_show_rpm=%d\nlcd_show_clock=%d\nlcd_label=%s\n"
            "refresh_ms=%d\nstartup_profile=%d\n",
            s->r, s->g, s->b, s->brightness, s->effect, s->sync_all ? 1 : 0,
            s->lcd_preset, s->lcd_circle ? 1 : 0, s->lcd_show_cpu ? 1 : 0, s->lcd_show_gpu ? 1 : 0,
            s->lcd_show_load ? 1 : 0, s->lcd_show_rpm ? 1 : 0, s->lcd_show_clock ? 1 : 0, s->lcd_label,
            s->refresh_ms, s->startup_profile);
    for (int i = 0; i < s->profile_count; i++) {
        const SavedProfile *pr = &s->profiles[i];
        fprintf(f, "profile=%s|%u|%u|%u|%d|%s|%d\n",
                pr->name, pr->r, pr->g, pr->b, pr->brightness, pr->effect, pr->lcd_preset);
    }
    fclose(f);
}
