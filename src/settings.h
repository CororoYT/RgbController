#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_PROFILES 8

typedef struct {
    char    name[32];
    uint8_t r, g, b;
    int     brightness;
    char    effect[32];
    int     lcd_preset;
} SavedProfile;

typedef struct {
    uint8_t r, g, b;
    int     brightness;
    char    effect[32];
    bool    sync_all;
    int     lcd_preset;      // 0=Cyberpunk, 1=Minimal, 2=Ice Blue, 3=Thermal Alert
    bool    lcd_circle;
    bool    lcd_show_cpu;
    bool    lcd_show_gpu;
    bool    lcd_show_load;
    bool    lcd_show_rpm;
    bool    lcd_show_clock;
    char    lcd_label[48];
    int     refresh_ms;
    int     startup_profile; // -1 = nessuno
    int     profile_count;
    SavedProfile profiles[MAX_PROFILES];
} AppSettings;

void settings_load(const char *base_dir, AppSettings *s);
void settings_save(const char *base_dir, const AppSettings *s);

#endif
