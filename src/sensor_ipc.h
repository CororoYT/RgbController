#ifndef SENSOR_IPC_H
#define SENSOR_IPC_H

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_FANS  32
#define MAX_TEMPS 48

typedef struct {
    char id[96];
    char name[64];
    char hardware[64];
    char control_id[96];
    int  rpm;
    int  duty;
    bool controllable;
} FanInfo;

typedef struct {
    char  id[96];
    char  name[64];
    char  hardware[64];
    float celsius;
} TempInfo;

typedef struct {
    int   fan_count;
    FanInfo fans[MAX_FANS];
    int   temp_count;
    TempInfo temps[MAX_TEMPS];
    float cpu_load;
    float gpu_load;
    float ram_load;
    float cpu_temp;
    float gpu_temp;
    bool  ram_rgb;
    char  ram_model[96];
    bool  bridge_ready;
} SensorSnapshot;

bool bridge_init(const char *base_dir);
bool bridge_refresh(SensorSnapshot *out_snap);
bool bridge_set_ram_rgb(const char *effect, uint8_t r, uint8_t g, uint8_t b, int brightness);
bool bridge_set_fan_duty(const char *control_id, int duty);
bool bridge_set_fan_auto(const char *control_id);
void bridge_stop(void);

#endif
