#include "settings.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void settings_load(const char *base_dir, AppSettings *s) {
    s->r = 0; s->g = 180; s->b = 255;
    s->brightness = 100;
    snprintf(s->effect, sizeof(s->effect), "Static");
    s->sync_all = true;
    snprintf(s->lcd_label, sizeof(s->lcd_label), "LIQUID COOLING");
    s->lcd_preset = 0;

    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\config\\native_settings.ini", base_dir);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        int val;
        if (sscanf(line, "r=%d", &val) == 1) s->r = (uint8_t)val;
        else if (sscanf(line, "g=%d", &val) == 1) s->g = (uint8_t)val;
        else if (sscanf(line, "b=%d", &val) == 1) s->b = (uint8_t)val;
        else if (sscanf(line, "brightness=%d", &val) == 1) s->brightness = val;
        else if (sscanf(line, "sync_all=%d", &val) == 1) s->sync_all = (val != 0);
        else if (sscanf(line, "lcd_preset=%d", &val) == 1) s->lcd_preset = val;
        else if (strncmp(line, "effect=", 7) == 0) {
            char *p = line + 7;
            p[strcspn(p, "\r\n")] = '\0';
            snprintf(s->effect, sizeof(s->effect), "%s", p);
        }
        else if (strncmp(line, "lcd_label=", 10) == 0) {
            char *p = line + 10;
            p[strcspn(p, "\r\n")] = '\0';
            snprintf(s->lcd_label, sizeof(s->lcd_label), "%s", p);
        }
    }
    fclose(f);
}

void settings_save(const char *base_dir, const AppSettings *s) {
    char dir[MAX_PATH], path[MAX_PATH];
    snprintf(dir, sizeof(dir), "%s\\config", base_dir);
    CreateDirectoryA(dir, NULL);
    snprintf(path, sizeof(path), "%s\\native_settings.ini", dir);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "r=%u\ng=%u\nb=%u\nbrightness=%d\neffect=%s\nsync_all=%d\nlcd_preset=%d\nlcd_label=%s\n",
            s->r, s->g, s->b, s->brightness, s->effect, s->sync_all ? 1 : 0, s->lcd_preset, s->lcd_label);
    fclose(f);
}
