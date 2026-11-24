#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t r, g, b;
    int     brightness;
    char    effect[32];
    bool    sync_all;
    char    lcd_label[48];
    int     lcd_preset;
} AppSettings;

void settings_load(const char *base_dir, AppSettings *s);
void settings_save(const char *base_dir, const AppSettings *s);

#endif
