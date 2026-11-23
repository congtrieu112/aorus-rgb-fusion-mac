#ifndef AORUS_LED_H
#define AORUS_LED_H

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ==================== Device 1: GIGABYTE / AORUS HOLTEK USB-HID Peripherals ==================== */
#define AORUS_VID 0x1044
#define AORUS_USAGE_PAGE 0xFF01

typedef struct {
    int pid;
    const char *name;
} AorusPidEntry;

static const AorusPidEntry AORUS_CASE_PIDS[] = {
    { 0x7A30, "AORUS AC300W / C300" },
    { 0x7A3C, "AORUS AC300W Lite / XC700W" },
    { 0x7A27, "GIGABYTE XC300W / XTC700" },
    { 0x7A29, "AORUS ATC700" },
    { 0x7A32, "AORUS P7" },
    { 0x7A34, "AORUS H5" },
    { 0x7A3D, "AORUS Case RGB" },
    { 0x7A3E, "AORUS RGB Peripheral" },
};
#define AORUS_CASE_PID_COUNT ((int)(sizeof(AORUS_CASE_PIDS) / sizeof(AORUS_CASE_PIDS[0])))

/* ==================== Device 2: GIGABYTE Motherboard IT5702 / IT8297 ==================== */
#define ITE_VID 0x048D
#define ITE_PID_5702 0x5702
#define ITE_PID_8297 0x8297
#define ITE_USAGE_PAGE 0xFF89
#define ITE_REPORT_ID 0xCC
#define ITE_ZONE_COUNT 7

typedef struct {
    IOHIDManagerRef mgr;
    CFSetRef devSet;
    IOHIDDeviceRef *devs;
    IOHIDDeviceRef target;
    int matchedPid;
    const char *matchedName;
} HidDeviceHandle;

static inline int hid_get_int_prop(IOHIDDeviceRef dev, CFStringRef key) {
    CFTypeRef ref = IOHIDDeviceGetProperty(dev, key);
    int val = 0;
    if (ref && CFGetTypeID(ref) == CFNumberGetTypeID()) {
        CFNumberGetValue((CFNumberRef)ref, kCFNumberIntType, &val);
    }
    return val;
}

static inline int hid_open_match(HidDeviceHandle *d, int vid, int pid, int upage) {
    memset(d, 0, sizeof(*d));
    d->mgr = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (!d->mgr) return -1;

    CFMutableDictionaryRef match = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    CFNumberRef vidNum = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vid);
    CFNumberRef pidNum = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &pid);
    CFDictionarySetValue(match, CFSTR(kIOHIDVendorIDKey), vidNum);
    CFDictionarySetValue(match, CFSTR(kIOHIDProductIDKey), pidNum);
    CFRelease(vidNum);
    CFRelease(pidNum);

    if (upage > 0) {
        CFNumberRef upNum = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &upage);
        CFDictionarySetValue(match, CFSTR(kIOHIDPrimaryUsagePageKey), upNum);
        CFRelease(upNum);
    }

    IOHIDManagerSetDeviceMatching(d->mgr, match);
    CFRelease(match);
    IOHIDManagerOpen(d->mgr, kIOHIDOptionsTypeNone);

    d->devSet = IOHIDManagerCopyDevices(d->mgr);
    if (!d->devSet || CFSetGetCount(d->devSet) == 0) {
        if (d->devSet) CFRelease(d->devSet);
        IOHIDManagerClose(d->mgr, kIOHIDOptionsTypeNone);
        CFRelease(d->mgr);
        memset(d, 0, sizeof(*d));
        return -2;
    }

    CFIndex count = CFSetGetCount(d->devSet);
    d->devs = (IOHIDDeviceRef *)calloc((size_t)count, sizeof(IOHIDDeviceRef));
    CFSetGetValues(d->devSet, (const void **)d->devs);

    for (CFIndex i = 0; i < count; i++) {
        int up = hid_get_int_prop(d->devs[i], CFSTR(kIOHIDPrimaryUsagePageKey));
        if (upage == 0 || up == upage) {
            d->target = d->devs[i];
            break;
        }
    }
    if (!d->target) d->target = d->devs[0];

    IOReturn kr = IOHIDDeviceOpen(d->target, kIOHIDOptionsTypeNone);
    if (kr != kIOReturnSuccess) {
        free(d->devs);
        CFRelease(d->devSet);
        IOHIDManagerClose(d->mgr, kIOHIDOptionsTypeNone);
        CFRelease(d->mgr);
        memset(d, 0, sizeof(*d));
        return -3;
    }
    d->matchedPid = pid;
    return 0;
}

static inline void hid_close_match(HidDeviceHandle *d) {
    if (!d) return;
    if (d->target) IOHIDDeviceClose(d->target, kIOHIDOptionsTypeNone);
    if (d->devs) free(d->devs);
    if (d->devSet) CFRelease(d->devSet);
    if (d->mgr) {
        IOHIDManagerClose(d->mgr, kIOHIDOptionsTypeNone);
        CFRelease(d->mgr);
    }
    memset(d, 0, sizeof(*d));
}

/* ==================== AORUS / HOLTEK CASE CONTROLLER ==================== */

static inline int aorus_open(HidDeviceHandle *d) {
    for (int i = 0; i < AORUS_CASE_PID_COUNT; i++) {
        if (hid_open_match(d, AORUS_VID, AORUS_CASE_PIDS[i].pid, AORUS_USAGE_PAGE) == 0) {
            d->matchedName = AORUS_CASE_PIDS[i].name;
            return 0;
        }
    }
    return -1;
}

static inline int aorus_get_info(uint8_t *maj, uint8_t *min, int *pidOut, const char **nameOut) {
    HidDeviceHandle d;
    if (aorus_open(&d) != 0) return -1;
    if (pidOut) *pidOut = d.matchedPid;
    if (nameOut) *nameOut = d.matchedName;

    uint8_t cmd[8] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    IOReturn kr = IOHIDDeviceSetReport(d.target, kIOHIDReportTypeFeature, 0, cmd, 8);
    int ret = -1;
    if (kr == kIOReturnSuccess) {
        usleep(5000);
        uint8_t resp[8] = {0};
        CFIndex len = 8;
        kr = IOHIDDeviceGetReport(d.target, kIOHIDReportTypeFeature, 0, resp, &len);
        if (kr == kIOReturnSuccess) {
            if (maj) *maj = resp[1];
            if (min) *min = resp[2];
            ret = 0;
        }
    }
    hid_close_match(&d);
    return ret;
}

static inline int aorus_apply(uint8_t r, uint8_t g, uint8_t b, int mode, int brightness, int speed, int save) {
    if (brightness < 1) brightness = 1;
    if (brightness > 10) brightness = 10;
    if (speed < 1) speed = 1;
    if (speed > 10) speed = 10;
    if (mode < 0) mode = 0;
    if (mode > 5) mode = 5;

    HidDeviceHandle d;
    int rc = aorus_open(&d);
    if (rc != 0) return rc;

    uint8_t pkt_color[8] = {0x01, 0xC8, r, g, b, 0x08, 0x01, 0x00};
    IOReturn kr = IOHIDDeviceSetReport(d.target, kIOHIDReportTypeFeature, 0, pkt_color, 8);
    usleep(100000);

    uint8_t bri_byte = (mode == 4 || mode == 5) ? (uint8_t)(brightness * 10) : (uint8_t)brightness;
    uint8_t spd_byte = (uint8_t)(11 - speed);
    uint8_t pkt_mode[8] = {0x01, 0xC9, (uint8_t)mode, bri_byte, spd_byte, 0x01, 0x08, 0x00};
    if (kr == kIOReturnSuccess) {
        kr = IOHIDDeviceSetReport(d.target, kIOHIDReportTypeFeature, 0, pkt_mode, 8);
        usleep(10000);
    }

    if (save && kr == kIOReturnSuccess) {
        uint8_t pkt_save[8] = {0x01, 0xB6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        kr = IOHIDDeviceSetReport(d.target, kIOHIDReportTypeFeature, 0, pkt_save, 8);
    }

    hid_close_match(&d);
    return (kr == kIOReturnSuccess) ? 0 : -4;
}

/* ==================== GIGABYTE MOTHERBOARD IT5702 / IT8297 ==================== */

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    int mode;       /* 0=Off, 1=Static, 2=Pulse, 3=Flash, 4=DoubleFlash, 5=ColorCycle, 6=Wave */
    int brightness; /* 1..10 */
    int speed;      /* 1..10 */
} MbZoneConfig;

static inline int ite_open(HidDeviceHandle *d) {
    if (hid_open_match(d, ITE_VID, ITE_PID_5702, ITE_USAGE_PAGE) == 0) {
        d->matchedName = "IT5702";
        return 0;
    }
    if (hid_open_match(d, ITE_VID, ITE_PID_8297, ITE_USAGE_PAGE) == 0) {
        d->matchedName = "IT8297";
        return 0;
    }
    return -1;
}

static inline int ite_get_info(char *prodOut, size_t prodCap, uint32_t *fwVerOut, int *pidOut) {
    HidDeviceHandle d;
    if (ite_open(&d) != 0) return -1;
    if (pidOut) *pidOut = d.matchedPid;

    uint8_t pkt[64];
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = ITE_REPORT_ID;
    pkt[1] = 0x60;
    IOReturn kr = IOHIDDeviceSetReport(d.target, kIOHIDReportTypeFeature, ITE_REPORT_ID, pkt, 64);
    int ret = -1;
    if (kr == kIOReturnSuccess) {
        usleep(5000);
        uint8_t resp[64];
        memset(resp, 0, sizeof(resp));
        resp[0] = ITE_REPORT_ID;
        CFIndex len = 64;
        kr = IOHIDDeviceGetReport(d.target, kIOHIDReportTypeFeature, ITE_REPORT_ID, resp, &len);
        if (kr == kIOReturnSuccess) {
            if (fwVerOut) {
                *fwVerOut = (uint32_t)resp[4] | ((uint32_t)resp[5] << 8) |
                            ((uint32_t)resp[6] << 16) | ((uint32_t)resp[7] << 24);
            }
            if (prodOut && prodCap > 0) {
                size_t copyLen = 28 < prodCap - 1 ? 28 : prodCap - 1;
                memcpy(prodOut, resp + 12, copyLen);
                prodOut[copyLen] = '\0';
            }
            ret = 0;
        }
    }
    hid_close_match(&d);
    return ret;
}

static inline void ite_build_zone_packet(uint8_t pkt[64], int zoneIdx, const MbZoneConfig *cfg) {
    static const uint8_t ledBrightness[10] = {15, 20, 30, 40, 50, 60, 70, 80, 90, 100};
    static const uint16_t pulseT[10][3] = {
        {1600,1600,200},{1400,1400,200},{1200,1200,200},{1000,1000,200},{900,900,200},
        {800,800,200},{700,700,200},{600,600,200},{500,500,200},{400,400,200}
    };
    static const uint16_t flashT[10][3] = {
        {100,100,2400},{100,100,2200},{100,100,2000},{100,100,1800},{100,100,1600},
        {100,100,1400},{100,100,1200},{100,100,1000},{100,100,800},{100,100,600}
    };
    static const uint16_t dflashT[10][3] = {
        {100,100,2600},{100,100,2400},{100,100,2200},{100,100,2000},{100,100,1800},
        {100,100,1600},{100,100,1400},{100,100,1200},{100,100,1000},{100,100,800}
    };
    static const uint16_t cycleT[10][2] = {
        {2400,2200},{1100,900},{1000,800},{900,700},{800,600},
        {700,500},{600,400},{500,300},{400,200},{300,100}
    };

    memset(pkt, 0, 64);
    pkt[0] = ITE_REPORT_ID;
    pkt[1] = (uint8_t)(0x20 + zoneIdx);

    uint32_t zoneMask = (1u << zoneIdx);
    pkt[2] = (uint8_t)(zoneMask & 0xFF);
    pkt[3] = (uint8_t)((zoneMask >> 8) & 0xFF);
    pkt[4] = (uint8_t)((zoneMask >> 16) & 0xFF);
    pkt[5] = (uint8_t)((zoneMask >> 24) & 0xFF);

    int briIdx = cfg->brightness - 1;
    if (briIdx < 0) briIdx = 0;
    if (briIdx > 9) briIdx = 9;
    int spdIdx = cfg->speed - 1;
    if (spdIdx < 0) spdIdx = 0;
    if (spdIdx > 9) spdIdx = 9;

    uint8_t r = cfg->r, g = cfg->g, b = cfg->b;
    uint8_t modeSel = 1;
    uint8_t maxBri = ledBrightness[briIdx];
    uint16_t t0 = 0, t1 = 0, t2 = 0;
    uint8_t ctrl0 = 0, ctrl1 = 0, oem0 = 0;

    switch (cfg->mode) {
        case 0: /* Off */
            modeSel = 1;
            r = g = b = 0;
            maxBri = 0;
            break;
        case 1: /* Static */
            modeSel = 1;
            if (briIdx > 8) maxBri = ledBrightness[8];
            break;
        case 2: /* Pulse */
            modeSel = 2;
            t0 = pulseT[spdIdx][0];
            t1 = pulseT[spdIdx][1];
            t2 = pulseT[spdIdx][2];
            ctrl1 = 1;
            break;
        case 3: /* Flash */
            modeSel = 3;
            t0 = flashT[spdIdx][0];
            t1 = flashT[spdIdx][1];
            t2 = flashT[spdIdx][2];
            ctrl1 = 1;
            oem0 = 1;
            break;
        case 4: /* Double Flash */
            modeSel = 3;
            t0 = dflashT[spdIdx][0];
            t1 = dflashT[spdIdx][1];
            t2 = dflashT[spdIdx][2];
            ctrl1 = 1;
            oem0 = 2;
            break;
        case 5: /* Color Cycle */
            modeSel = 4;
            t0 = cycleT[spdIdx][0];
            t1 = cycleT[spdIdx][1];
            ctrl0 = 7;
            break;
        case 6: /* Wave / Rainbow */
            modeSel = 6;
            t0 = (uint16_t)(1200 - spdIdx * 100);
            ctrl0 = 7;
            break;
        default:
            modeSel = 1;
            break;
    }

    pkt[11] = modeSel;
    pkt[12] = maxBri;
    pkt[13] = 0;

    /* dwColor0 (0x00RRGGBB little-endian -> B, G, R, 0) */
    pkt[14] = b;
    pkt[15] = g;
    pkt[16] = r;
    pkt[17] = 0;

    pkt[22] = (uint8_t)(t0 & 0xFF);
    pkt[23] = (uint8_t)((t0 >> 8) & 0xFF);
    pkt[24] = (uint8_t)(t1 & 0xFF);
    pkt[25] = (uint8_t)((t1 >> 8) & 0xFF);
    pkt[26] = (uint8_t)(t2 & 0xFF);
    pkt[27] = (uint8_t)((t2 >> 8) & 0xFF);

    pkt[30] = ctrl0;
    pkt[31] = ctrl1;
    pkt[32] = oem0;
    pkt[33] = 0;
}

static inline int ite_apply_zones(const MbZoneConfig zones[ITE_ZONE_COUNT], uint8_t zoneMask, int save) {
    HidDeviceHandle d;
    if (ite_open(&d) != 0) return -1;

    uint8_t pkt[64];
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = ITE_REPORT_ID;
    pkt[1] = 0x32;
    pkt[2] = 0x00;
    IOHIDDeviceSetReport(d.target, kIOHIDReportTypeFeature, ITE_REPORT_ID, pkt, 64);
    usleep(5000);

    IOReturn kr = kIOReturnSuccess;
    for (int i = 0; i < ITE_ZONE_COUNT; i++) {
        if (!(zoneMask & (1u << i))) continue;
        ite_build_zone_packet(pkt, i, &zones[i]);
        kr = IOHIDDeviceSetReport(d.target, kIOHIDReportTypeFeature, ITE_REPORT_ID, pkt, 64);
        usleep(4000);
        if (kr != kIOReturnSuccess) break;
    }

    if (kr == kIOReturnSuccess) {
        memset(pkt, 0, sizeof(pkt));
        pkt[0] = ITE_REPORT_ID;
        pkt[1] = 0x28;
        pkt[2] = zoneMask;
        kr = IOHIDDeviceSetReport(d.target, kIOHIDReportTypeFeature, ITE_REPORT_ID, pkt, 64);
        usleep(10000);
    }

    if (save && kr == kIOReturnSuccess) {
        memset(pkt, 0, sizeof(pkt));
        pkt[0] = ITE_REPORT_ID;
        pkt[1] = 0x5E;
        IOHIDDeviceSetReport(d.target, kIOHIDReportTypeFeature, ITE_REPORT_ID, pkt, 64);
    }

    hid_close_match(&d);
    return (kr == kIOReturnSuccess) ? 0 : -4;
}

#endif
