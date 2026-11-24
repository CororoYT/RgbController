#ifndef HID_DRIVERS_H
#define HID_DRIVERS_H

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_RGB_DEVICES 16
#define MAX_RGB_ZONES   8

typedef enum {
    DRV_NONE = 0,
    DRV_IT5702,
    DRV_ASUS_AURA,
    DRV_MSI_MYSTIC,
    DRV_ASROCK,
    DRV_LOGITECH_HIDPP,
    DRV_RAM_SMBUS
} DriverKind;

typedef struct {
    int index;
    int hw_idx;
    char name[48];
    int leds;
    bool resizable;
} RGBZone;

typedef struct {
    int id;
    DriverKind driver;
    char name[128];
    char category[24];   // "MOBO", "RAM", "PERIPHERAL", "COOLER"
    char hid_path[512];
    uint16_t vid;
    uint16_t pid;
    uint16_t out_len;
    uint16_t feat_len;
    int zone_count;
    RGBZone zones[MAX_RGB_ZONES];
} RGBDevice;

typedef struct {
    bool connected;
    char name[96];
    uint16_t vid;
    uint16_t pid;
} CoolerUsbInfo;

int  hid_detect_rgb_devices(RGBDevice *out_devs, int max_devs, CoolerUsbInfo *out_cooler);
bool hid_apply_device(const RGBDevice *dev, const char *effect, uint8_t r, uint8_t g, uint8_t b, int brightness);
bool hid_apply_zone(const RGBDevice *dev, int zone_idx, const char *effect, uint8_t r, uint8_t g, uint8_t b, int brightness);

#endif
