#include "hid_drivers.h"
#include <setupapi.h>
#include <hidsdi.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static void get_motherboard_name(char *buf, size_t sz) {
    HKEY hKey;
    DWORD len = (DWORD)sz;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExA(hKey, "BaseBoardProduct", NULL, NULL, (LPBYTE)buf, &len) == ERROR_SUCCESS && buf[0]) {
            RegCloseKey(hKey);
            return;
        }
        RegCloseKey(hKey);
    }
    snprintf(buf, sz, "Motherboard RGB");
}

static HANDLE open_hid_rw(const char *path) {
    return CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL, OPEN_EXISTING, 0, NULL);
}

static bool hid_set_feat(HANDLE h, const uint8_t *buf, DWORD len) {
    return HidD_SetFeature(h, (PVOID)buf, len) != FALSE;
}

static bool hid_get_feat(HANDLE h, uint8_t report_id, uint8_t *buf, DWORD len) {
    memset(buf, 0, len);
    buf[0] = report_id;
    return HidD_GetFeature(h, (PVOID)buf, len) != FALSE;
}

static bool hid_write_out(HANDLE h, const uint8_t *buf, DWORD len) {
    DWORD written = 0;
    return WriteFile(h, buf, len, &written, NULL) != FALSE;
}

static uint8_t scale_u8(uint8_t v, int brightness) {
    if (brightness <= 0) return 0;
    if (brightness >= 100) return v;
    int res = (int)((v * brightness + 50) / 100);
    return (uint8_t)(res > 255 ? 255 : res);
}

// ---------------------------------------------------------------------------
// Gigabyte IT5702 / IT8297
// ---------------------------------------------------------------------------

static const RGBZone IT5702_ZONES_DEF[8] = {
    {0, 5, "D_LED1 (ARGB 5V)", 64, true},
    {1, 6, "D_LED2 (ARGB 5V)", 64, true},
    {2, 0, "LED Zona 1 (I/O)", 1, false},
    {3, 1, "LED_C1 (12V)", 1, false},
    {4, 2, "LED Zona 2 (PCIe)", 1, false},
    {5, 3, "LED_C2 (12V)", 1, false},
    {6, 4, "LED Zona 3", 1, false},
    {7, 7, "LED Zona 4", 1, false},
};

static uint8_t it5702_mode_id(const char *eff) {
    if (strcmp(eff, "Breathing") == 0) return 0x02;
    if (strcmp(eff, "Flicker") == 0 || strcmp(eff, "Chase") == 0) return 0x03;
    if (strcmp(eff, "Color Cycle") == 0 || strcmp(eff, "Rainbow") == 0) return 0x04;
    if (strcmp(eff, "Wave") == 0) return 0x06;
    return 0x01;
}

static void it5702_set_led_count(HANDLE h) {
    uint8_t p[64] = {0xCC, 0x34, 0x11};
    hid_set_feat(h, p, 64);
    Sleep(10);
}

static void it5702_set_direct_mask(HANDLE h, uint8_t mask) {
    uint8_t p[64] = {0xCC, 0x32, mask};
    hid_set_feat(h, p, 64);
    Sleep(50);
}

static void it5702_apply_hw(HANDLE h, uint8_t zone_mask) {
    uint8_t p[64] = {0xCC, 0x28, zone_mask};
    hid_set_feat(h, p, 64);
    Sleep(25);
}

static bool it5702_send_zone_eff(HANDLE h, int hw_idx, uint8_t mode_id, uint8_t r, uint8_t g, uint8_t b, int brightness) {
    uint8_t p[64] = {0};
    int b255 = (brightness * 255 + 50) / 100;
    if (b255 < 0) b255 = 0;
    if (b255 > 255) b255 = 255;
    p[0] = 0xCC;
    p[1] = (uint8_t)(0x20 + hw_idx);
    p[2] = (uint8_t)(1 << hw_idx);
    p[11] = mode_id;
    p[12] = (uint8_t)b255;
    p[14] = b;
    p[15] = g;
    p[16] = r;
    p[22] = 0xC8;
    bool ok = hid_set_feat(h, p, 64);
    Sleep(4);
    return ok;
}

static bool it5702_send_direct_argb(HANDLE h, uint8_t cmd, uint8_t r, uint8_t g, uint8_t b, int brightness, int led_count) {
    uint8_t sr = scale_u8(r, brightness);
    uint8_t sg = scale_u8(g, brightness);
    uint8_t sb = scale_u8(b, brightness);
    bool ok = true;
    int offset = 0, rem = led_count;
    while (rem > 0) {
        int chunk = rem > 19 ? 19 : rem;
        uint8_t pd[64] = {0};
        pd[0] = 0xCC;
        pd[1] = cmd;
        pd[2] = (uint8_t)((offset * 3) & 0xFF);
        pd[3] = (uint8_t)(((offset * 3) >> 8) & 0xFF);
        pd[4] = (uint8_t)(chunk * 3);
        for (int i = 0; i < chunk; i++) {
            // WS2812B ARGB: ordine [G, R, B]
            pd[5 + i * 3 + 0] = sg;
            pd[5 + i * 3 + 1] = sr;
            pd[5 + i * 3 + 2] = sb;
        }
        ok &= hid_set_feat(h, pd, 64);
        Sleep(4);
        offset += chunk;
        rem -= chunk;
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Rilevamento unificato in singolo passaggio SetupAPI
// ---------------------------------------------------------------------------

int hid_detect_rgb_devices(RGBDevice *out_devs, int max_devs, CoolerUsbInfo *out_cooler) {
    int count = 0;
    if (out_cooler) {
        memset(out_cooler, 0, sizeof(*out_cooler));
    }

    GUID hid_guid;
    HidD_GetHidGuid(&hid_guid);
    HDEVINFO hDevInfo = SetupDiGetClassDevsA(&hid_guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDevInfo == INVALID_HANDLE_VALUE) return 0;

    SP_DEVICE_INTERFACE_DATA iface;
    iface.cbSize = sizeof(iface);
    char mb_name[96];
    get_motherboard_name(mb_name, sizeof(mb_name));

    bool found_mobo = false, found_kbd = false;

    for (DWORD idx = 0; SetupDiEnumDeviceInterfaces(hDevInfo, NULL, &hid_guid, idx, &iface); idx++) {
        DWORD req = 0;
        SetupDiGetDeviceInterfaceDetailA(hDevInfo, &iface, NULL, 0, &req, NULL);
        if (req == 0) continue;

        PSP_DEVICE_INTERFACE_DETAIL_DATA_A detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)malloc(req);
        if (!detail) continue;
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);

        if (SetupDiGetDeviceInterfaceDetailA(hDevInfo, &iface, detail, req, NULL, NULL)) {
            HANDLE h = CreateFileA(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                HIDD_ATTRIBUTES attr;
                attr.Size = sizeof(attr);
                PHIDP_PREPARSED_DATA pp = NULL;
                HIDP_CAPS caps;
                if (HidD_GetAttributes(h, &attr) && HidD_GetPreparsedData(h, &pp)) {
                    HidP_GetCaps(pp, &caps);
                    HidD_FreePreparsedData(pp);

                    // Thermalright / AIO LCD USB
                    if (out_cooler && !out_cooler->connected &&
                        ((attr.VendorID == 0x0416 && (attr.ProductID == 0x5302 || attr.ProductID == 0x5406)) ||
                         (attr.VendorID == 0x0418 && attr.ProductID == 0x5303) ||
                         (attr.VendorID == 0x1E71 && attr.ProductID == 0x3008))) {
                        out_cooler->connected = true;
                        out_cooler->vid = attr.VendorID;
                        out_cooler->pid = attr.ProductID;
                        snprintf(out_cooler->name, sizeof(out_cooler->name),
                                 "Thermalright AIO LCD (%04X:%04X)", attr.VendorID, attr.ProductID);
                    }

                    if (count < max_devs) {
                        // 1) Gigabyte IT5702 / IT8297
                        if (!found_mobo && attr.VendorID == 0x048D &&
                            (attr.ProductID == 0x5702 || attr.ProductID == 0x8297) &&
                            caps.FeatureReportByteLength == 64) {
                            found_mobo = true;
                            RGBDevice *d = &out_devs[count];
                            memset(d, 0, sizeof(*d));
                            d->id = count;
                            d->driver = DRV_IT5702;
                            d->vid = attr.VendorID;
                            d->pid = attr.ProductID;
                            d->feat_len = caps.FeatureReportByteLength;
                            snprintf(d->hid_path, sizeof(d->hid_path), "%s", detail->DevicePath);
                            snprintf(d->category, sizeof(d->category), "MOBO");
                            snprintf(d->name, sizeof(d->name), "%s (IT%04X)", mb_name, attr.ProductID);
                            d->zone_count = 8;
                            memcpy(d->zones, IT5702_ZONES_DEF, sizeof(IT5702_ZONES_DEF));
                            count++;
                        }
                        // 2) ASUS Aura Sync USB
                        else if (!found_mobo && attr.VendorID == 0x0B05 &&
                                 (attr.ProductID == 0x18F3 || attr.ProductID == 0x1939 || attr.ProductID == 0x19AF ||
                                  attr.ProductID == 0x1872 || attr.ProductID == 0x1867 || attr.ProductID == 0x18A3) &&
                                 (caps.OutputReportByteLength == 64 || caps.OutputReportByteLength == 65)) {
                            found_mobo = true;
                            RGBDevice *d = &out_devs[count];
                            memset(d, 0, sizeof(*d));
                            d->id = count;
                            d->driver = DRV_ASUS_AURA;
                            d->vid = attr.VendorID;
                            d->pid = attr.ProductID;
                            d->out_len = caps.OutputReportByteLength;
                            snprintf(d->hid_path, sizeof(d->hid_path), "%s", detail->DevicePath);
                            snprintf(d->category, sizeof(d->category), "MOBO");
                            snprintf(d->name, sizeof(d->name), "%s (ASUS Aura)", mb_name);
                            d->zone_count = 1;
                            snprintf(d->zones[0].name, 48, "Aura Mainboard");
                            d->zones[0].leds = 32;
                            count++;
                        }
                        // 3) MSI Mystic Light USB
                        else if (!found_mobo && attr.VendorID == 0x1462 &&
                                 attr.ProductID >= 0x7B00 && attr.ProductID <= 0x7F00 &&
                                 caps.FeatureReportByteLength >= 64) {
                            found_mobo = true;
                            RGBDevice *d = &out_devs[count];
                            memset(d, 0, sizeof(*d));
                            d->id = count;
                            d->driver = DRV_MSI_MYSTIC;
                            d->vid = attr.VendorID;
                            d->pid = attr.ProductID;
                            d->feat_len = caps.FeatureReportByteLength;
                            snprintf(d->hid_path, sizeof(d->hid_path), "%s", detail->DevicePath);
                            snprintf(d->category, sizeof(d->category), "MOBO");
                            snprintf(d->name, sizeof(d->name), "%s (MSI Mystic Light)", mb_name);
                            d->zone_count = 1;
                            snprintf(d->zones[0].name, 48, "Mystic Light");
                            d->zones[0].leds = 32;
                            count++;
                        }
                        // 4) ASRock Polychrome USB
                        else if (!found_mobo && attr.VendorID == 0x26CE &&
                                 (attr.ProductID == 0x01A2 || attr.ProductID == 0x01A4 || attr.ProductID == 0x01A6) &&
                                 caps.OutputReportByteLength >= 64) {
                            found_mobo = true;
                            RGBDevice *d = &out_devs[count];
                            memset(d, 0, sizeof(*d));
                            d->id = count;
                            d->driver = DRV_ASROCK;
                            d->vid = attr.VendorID;
                            d->pid = attr.ProductID;
                            d->out_len = caps.OutputReportByteLength;
                            snprintf(d->hid_path, sizeof(d->hid_path), "%s", detail->DevicePath);
                            snprintf(d->category, sizeof(d->category), "MOBO");
                            snprintf(d->name, sizeof(d->name), "%s (ASRock Polychrome)", mb_name);
                            d->zone_count = 1;
                            snprintf(d->zones[0].name, 48, "Polychrome RGB");
                            d->zones[0].leds = 32;
                            count++;
                        }
                        // 5) Logitech HID++ 2.0 Keyboards
                        else if (!found_kbd && attr.VendorID == 0x046D &&
                                 (attr.ProductID == 0xC336 || attr.ProductID == 0xC33C ||
                                  attr.ProductID == 0xC333 || attr.ProductID == 0xC33A || attr.ProductID == 0xC33F) &&
                                 caps.UsagePage == 0xFF43 && caps.OutputReportByteLength == 20) {
                            found_kbd = true;
                            RGBDevice *d = &out_devs[count];
                            memset(d, 0, sizeof(*d));
                            d->id = count;
                            d->driver = DRV_LOGITECH_HIDPP;
                            d->vid = attr.VendorID;
                            d->pid = attr.ProductID;
                            d->out_len = 20;
                            snprintf(d->hid_path, sizeof(d->hid_path), "%s", detail->DevicePath);
                            snprintf(d->category, sizeof(d->category), "PERIPHERAL");
                            const char *kname = (attr.ProductID == 0xC336) ? "Logitech G213 Prodigy" : "Tastiera Logitech RGB";
                            snprintf(d->name, sizeof(d->name), "%s", kname);
                            d->zone_count = 1;
                            snprintf(d->zones[0].name, 48, "Tastiera (5 zone)");
                            d->zones[0].leds = 5;
                            count++;
                        }
                    }
                }
                CloseHandle(h);
            }
        }
        free(detail);
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
    return count;
}

// ---------------------------------------------------------------------------
// Applicazione colori / effetti
// ---------------------------------------------------------------------------

bool hid_apply_device(const RGBDevice *dev, const char *effect, uint8_t r, uint8_t g, uint8_t b, int brightness) {
    if (!dev || !dev->hid_path[0]) return false;
    HANDLE h = open_hid_rw(dev->hid_path);
    if (h == INVALID_HANDLE_VALUE) return false;

    bool ok = false;
    if (dev->driver == DRV_IT5702) {
        it5702_set_led_count(h);
        if (strcmp(effect, "Static") == 0) {
            it5702_set_direct_mask(h, 0x00);
            ok = true;
            for (int i = 0; i < dev->zone_count; i++) {
                ok &= it5702_send_zone_eff(h, dev->zones[i].hw_idx, 0x01, r, g, b, brightness);
            }
            it5702_apply_hw(h, 0xFF);
            it5702_set_direct_mask(h, 0x03);
            ok &= it5702_send_direct_argb(h, 0x58, r, g, b, brightness, 64);
            ok &= it5702_send_direct_argb(h, 0x59, r, g, b, brightness, 64);
        } else {
            it5702_set_direct_mask(h, 0x00);
            uint8_t mid = it5702_mode_id(effect);
            ok = true;
            for (int i = 0; i < dev->zone_count; i++) {
                ok &= it5702_send_zone_eff(h, dev->zones[i].hw_idx, mid, r, g, b, brightness);
            }
            it5702_apply_hw(h, 0xFF);
        }
    }
    else if (dev->driver == DRV_LOGITECH_HIDPP) {
        uint8_t sr = scale_u8(r, brightness), sg = scale_u8(g, brightness), sb = scale_u8(b, brightness);
        uint8_t pkt[20] = {0x11, 0xFF, 0x0C, 0x3A, 0x00};
        if (strcmp(effect, "Color Cycle") == 0 || strcmp(effect, "Rainbow") == 0 || strcmp(effect, "Wave") == 0) {
            pkt[5] = 0x02; pkt[10] = 0x0F; pkt[11] = 0xA0; pkt[12] = (uint8_t)(brightness < 1 ? 1 : brightness);
        } else if (strcmp(effect, "Breathing") == 0) {
            pkt[5] = 0x03; pkt[6] = sr; pkt[7] = sg; pkt[8] = sb; pkt[10] = 0x0F; pkt[11] = 0xA0;
            pkt[12] = (uint8_t)(brightness < 1 ? 1 : brightness);
        } else {
            pkt[5] = 0x01; pkt[6] = sr; pkt[7] = sg; pkt[8] = sb; pkt[9] = 0x02;
        }
        ok = hid_write_out(h, pkt, 20);
    }
    else if (dev->driver == DRV_ASUS_AURA) {
        uint8_t sr = scale_u8(r, brightness), sg = scale_u8(g, brightness), sb = scale_u8(b, brightness);
        uint8_t pkt[65] = {0};
        pkt[0] = 0xEC; pkt[1] = 0x35; pkt[5] = 0x01; pkt[6] = sr; pkt[7] = sg; pkt[8] = sb;
        ok = hid_write_out(h, pkt, dev->out_len);
        uint8_t cmt[65] = {0}; cmt[0] = 0xEC; cmt[1] = 0x3F; cmt[2] = 0x55;
        hid_write_out(h, cmt, dev->out_len);
    }
    else if (dev->driver == DRV_MSI_MYSTIC) {
        uint8_t sr = scale_u8(r, brightness), sg = scale_u8(g, brightness), sb = scale_u8(b, brightness);
        uint8_t pkt[64] = {0};
        pkt[0] = 0x52; pkt[1] = 0x01; pkt[2] = 0x01; pkt[3] = sr; pkt[4] = sg; pkt[5] = sb; pkt[6] = 0x0A;
        pkt[7] = sr; pkt[8] = sg; pkt[9] = sb;
        ok = hid_set_feat(h, pkt, dev->feat_len);
    }
    else if (dev->driver == DRV_ASROCK) {
        uint8_t sr = scale_u8(r, brightness), sg = scale_u8(g, brightness), sb = scale_u8(b, brightness);
        uint8_t pkt[64] = {0};
        pkt[0] = 0x00; pkt[1] = 0x11; pkt[2] = 0xFF; pkt[3] = 0x01; pkt[4] = sr; pkt[5] = sg; pkt[6] = sb;
        pkt[7] = 0x03; pkt[8] = (uint8_t)((brightness * 255 + 50) / 100);
        ok = hid_write_out(h, pkt, dev->out_len);
    }

    CloseHandle(h);
    return ok;
}

bool hid_apply_zone(const RGBDevice *dev, int zone_idx, const char *effect, uint8_t r, uint8_t g, uint8_t b, int brightness) {
    if (!dev || zone_idx < 0 || zone_idx >= dev->zone_count) return false;
    if (dev->driver != DRV_IT5702) {
        return hid_apply_device(dev, effect, r, g, b, brightness);
    }
    HANDLE h = open_hid_rw(dev->hid_path);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = false;
    it5702_set_led_count(h);
    if (strcmp(effect, "Static") == 0 && (zone_idx == 0 || zone_idx == 1)) {
        it5702_set_direct_mask(h, 0x03);
        ok = it5702_send_direct_argb(h, zone_idx == 0 ? 0x58 : 0x59, r, g, b, brightness, dev->zones[zone_idx].leds);
    } else {
        it5702_set_direct_mask(h, 0x00);
        ok = it5702_send_zone_eff(h, dev->zones[zone_idx].hw_idx, it5702_mode_id(effect), r, g, b, brightness);
        it5702_apply_hw(h, (uint8_t)(1 << dev->zones[zone_idx].hw_idx));
    }
    CloseHandle(h);
    return ok;
}
