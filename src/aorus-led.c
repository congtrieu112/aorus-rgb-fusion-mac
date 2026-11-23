#include "aorus-led.h"

static int parse_mode(const char *s) {
    if (strcasecmp(s, "off") == 0) return 0;
    if (strcasecmp(s, "static") == 0 || strcasecmp(s, "on") == 0) return 1;
    if (strcasecmp(s, "pulse") == 0 || strcasecmp(s, "breath") == 0) return 2;
    if (strcasecmp(s, "flash") == 0) return 3;
    if (strcasecmp(s, "doubleflash") == 0 || strcasecmp(s, "double") == 0) return 4;
    if (strcasecmp(s, "cycle") == 0 || strcasecmp(s, "rainbow") == 0) return 5;
    if (strcasecmp(s, "wave") == 0) return 6;
    return atoi(s);
}

static void parse_obj_fields(const char *start, const char *end, MbZoneConfig *out) {
    const char *p;
    int v;
    if ((p = strstr(start, "\"r\"")) && p < end && sscanf(p, "\"r\"%*[^0-9]%d", &v) == 1) out->r = (uint8_t)v;
    if ((p = strstr(start, "\"g\"")) && p < end && sscanf(p, "\"g\"%*[^0-9]%d", &v) == 1) out->g = (uint8_t)v;
    if ((p = strstr(start, "\"b\"")) && p < end && sscanf(p, "\"b\"%*[^0-9]%d", &v) == 1) out->b = (uint8_t)v;
    if ((p = strstr(start, "\"mode\"")) && p < end && sscanf(p, "\"mode\"%*[^0-9]%d", &v) == 1) out->mode = v;
    if ((p = strstr(start, "\"brightness\"")) && p < end && sscanf(p, "\"brightness\"%*[^0-9]%d", &v) == 1) out->brightness = v;
    if ((p = strstr(start, "\"speed\"")) && p < end && sscanf(p, "\"speed\"%*[^0-9]%d", &v) == 1) out->speed = v;
}

static int apply_saved_config(void) {
    const char *home = getenv("HOME");
    if (!home) return -1;
    char path[1024];
    snprintf(path, sizeof(path), "%s/.config/aorus-led/config.json", home);
    FILE *f = fopen(path, "r");
    if (!f) {
        return aorus_apply(0xFF, 0xAA, 0x00, 1, 10, 5, 1);
    }
    char buf[16384];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';

    MbZoneConfig caseCfg = {255, 170, 0, 1, 10, 5};
    const char *casePos = strstr(buf, "\"case\"");
    if (casePos) {
        const char *brace = strchr(casePos, '{');
        const char *end = brace ? strchr(brace, '}') : NULL;
        if (brace && end) parse_obj_fields(brace, end, &caseCfg);
    } else {
        parse_obj_fields(buf, buf + n, &caseCfg);
    }
    int rc1 = aorus_apply(caseCfg.r, caseCfg.g, caseCfg.b, caseCfg.mode, caseCfg.brightness, caseCfg.speed, 1);

    const char *zonesPos = strstr(buf, "\"mbZones\"");
    if (zonesPos) {
        MbZoneConfig zones[ITE_ZONE_COUNT];
        for (int i = 0; i < ITE_ZONE_COUNT; i++) zones[i] = caseCfg;
        const char *cur = strchr(zonesPos, '[');
        for (int i = 0; i < ITE_ZONE_COUNT && cur; i++) {
            const char *ob = strchr(cur, '{');
            const char *cb = ob ? strchr(ob, '}') : NULL;
            if (!ob || !cb) break;
            parse_obj_fields(ob, cb, &zones[i]);
            cur = cb + 1;
        }
        ite_apply_zones(zones, 0x7F, 1);
    }
    return rc1;
}

static void print_usage(const char *prog) {
    printf("AORUS RGB Fusion CLI for macOS (HOLTEK Case + GIGABYTE IT5702/IT8297 Motherboard)\n");
    printf("Usage: %s [options]\n", prog);
    printf("  --target TARGET     case | mb | all (default: all)\n");
    printf("  --zone 0..6 | all   Motherboard zone index (default: all)\n");
    printf("  --color RRGGBB      Hex color (default: FFAA00)\n");
    printf("  --mode MODE         off | static | pulse | flash | doubleflash | cycle | wave\n");
    printf("  --brightness 1..10  Brightness level (default: 10)\n");
    printf("  --speed 1..10       Effect speed (default: 5)\n");
    printf("  --off               Turn LED off\n");
    printf("  --apply-saved       Apply config saved by AORUS RGB Fusion.app\n");
    printf("  --info              Show connected devices & firmware info\n");
}

int main(int argc, char **argv) {
    uint8_t r = 0xFF, g = 0xAA, b = 0x00;
    int mode = 1, brightness = 10, speed = 5;
    int do_info = 0, do_apply_saved = 0;
    const char *target = "all";
    int mb_zone = -1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--apply-saved") == 0) {
            do_apply_saved = 1;
        } else if (strcmp(argv[i], "--target") == 0 && i + 1 < argc) {
            target = argv[++i];
        } else if (strcmp(argv[i], "--zone") == 0 && i + 1 < argc) {
            const char *zs = argv[++i];
            mb_zone = (strcasecmp(zs, "all") == 0) ? -1 : atoi(zs);
        } else if (strcmp(argv[i], "--color") == 0 && i + 1 < argc) {
            const char *hex = argv[++i];
            if (hex[0] == '#') hex++;
            unsigned int val = 0;
            if (sscanf(hex, "%x", &val) == 1) {
                r = (val >> 16) & 0xFF;
                g = (val >> 8) & 0xFF;
                b = val & 0xFF;
            }
        } else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            mode = parse_mode(argv[++i]);
        } else if (strcmp(argv[i], "--brightness") == 0 && i + 1 < argc) {
            brightness = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--speed") == 0 && i + 1 < argc) {
            speed = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--off") == 0) {
            mode = 0; r = g = b = 0;
        } else if (strcmp(argv[i], "--info") == 0) {
            do_info = 1;
        }
    }

    if (do_info) {
        uint8_t maj = 0, min = 0;
        int cpid = 0;
        const char *cname = NULL;
        if (aorus_get_info(&maj, &min, &cpid, &cname) == 0)
            printf("[Case] %s (1044:%04X) Firmware v%d.%d\n", cname, cpid, maj, min);
        else
            printf("[Case] No supported AORUS/HOLTEK USB-HID case found\n");

        char prod[32] = {0};
        uint32_t fw = 0;
        int mpid = 0;
        if (ite_get_info(prod, sizeof(prod), &fw, &mpid) == 0)
            printf("[MB]   %s (048D:%04X) FW=0x%08X\n", prod, mpid, fw);
        else
            printf("[MB]   No GIGABYTE IT5702/IT8297 controller found\n");
        if (argc == 2) return 0;
    }

    if (do_apply_saved) {
        return apply_saved_config() == 0 ? 0 : 1;
    }

    int rc = 0;
    if (strcasecmp(target, "case") == 0 || strcasecmp(target, "all") == 0) {
        int cMode = (mode == 6) ? 5 : mode;
        int r1 = aorus_apply(r, g, b, cMode, brightness, speed, 1);
        printf("Case: %s (#%02X%02X%02X mode=%d)\n", r1 == 0 ? "OK" : "FAIL", r, g, b, cMode);
        if (r1 != 0 && strcasecmp(target, "case") == 0) rc = 1;
    }
    if (strcasecmp(target, "mb") == 0 || strcasecmp(target, "all") == 0) {
        MbZoneConfig zones[ITE_ZONE_COUNT];
        for (int i = 0; i < ITE_ZONE_COUNT; i++) {
            zones[i] = (MbZoneConfig){r, g, b, mode, brightness, speed};
        }
        uint8_t mask = (mb_zone >= 0 && mb_zone < ITE_ZONE_COUNT) ? (uint8_t)(1u << mb_zone) : 0x7F;
        int r2 = ite_apply_zones(zones, mask, 1);
        printf("Motherboard (mask 0x%02X): %s (#%02X%02X%02X mode=%d)\n", mask, r2 == 0 ? "OK" : "FAIL", r, g, b, mode);
        if (r2 != 0 && strcasecmp(target, "mb") == 0) rc = 1;
    }
    return rc;
}
