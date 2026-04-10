/**
 * @file avb_test_common.c
 * @brief Implementation of shared test helpers (see avb_test_common.h)
 *
 * SSOT: #include "../../include/avb_ioctl.h" (never the external/ copy)
 */

#include "avb_test_common.h"

/* =========================================================
 * AvbEnumerateAdapters
 * ========================================================= */

int AvbEnumerateAdapters(AvbAdapterInfo *out, int max_count)
{
    if (!out || max_count <= 0) return 0;

    HANDLE hDevice = CreateFileA(
        AVB_DEVICE_PATH,
        GENERIC_READ | GENERIC_WRITE,
        0, NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (hDevice == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[AvbEnumerateAdapters] Cannot open %s (error %lu)\n",
                AVB_DEVICE_PATH, GetLastError());
        return 0;
    }

    int count = 0;

    for (int idx = 0; idx < max_count; idx++) {
        AVB_ENUM_REQUEST req;
        ZeroMemory(&req, sizeof(req));
        req.index = (avb_u32)idx;

        DWORD bytes = 0;
        BOOL ok = DeviceIoControl(hDevice,
                                  IOCTL_AVB_ENUM_ADAPTERS,
                                  &req, sizeof(req),
                                  &req, sizeof(req),
                                  &bytes, NULL);
        if (!ok) break;

        /* req.count is only valid on the first call; clamp on the first pass */
        if (idx == 0) {
            count = (int)req.count;
            if (count == 0) break;
            if (count > max_count) count = max_count;
        }

        out[idx].vendor_id    = req.vendor_id;
        out[idx].device_id    = req.device_id;
        out[idx].capabilities = req.capabilities;
        out[idx].global_index = (avb_u32)idx;

        /* Human-readable name */
        strncpy(out[idx].device_name,
                AvbDeviceName(req.device_id),
                sizeof(out[idx].device_name) - 1);
        out[idx].device_name[sizeof(out[idx].device_name) - 1] = '\0';

        if (idx + 1 >= count) break;
    }

    CloseHandle(hDevice);
    return count;
}

/* =========================================================
 * AvbOpenAdapter
 * ========================================================= */

HANDLE AvbOpenAdapter(const AvbAdapterInfo *info)
{
    if (!info) return INVALID_HANDLE_VALUE;

    HANDLE h = CreateFileA(
        AVB_DEVICE_PATH,
        GENERIC_READ | GENERIC_WRITE,
        0, NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (h == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[AvbOpenAdapter] Cannot open device (error %lu)\n",
                GetLastError());
        return INVALID_HANDLE_VALUE;
    }

    AVB_OPEN_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.vendor_id = info->vendor_id;
    req.device_id = info->device_id;
    req.index     = info->global_index;  /* global position — see SSOT header */

    DWORD bytes = 0;
    BOOL ok = DeviceIoControl(h,
                              IOCTL_AVB_OPEN_ADAPTER,
                              &req, sizeof(req),
                              &req, sizeof(req),
                              &bytes, NULL);

    if (!ok) {
        DWORD err = GetLastError();
        fprintf(stderr, "[AvbOpenAdapter] IOCTL_AVB_OPEN_ADAPTER failed "
                "(VID=0x%04X DID=0x%04X idx=%u error=%lu)\n",
                info->vendor_id, info->device_id, info->global_index, err);
        CloseHandle(h);
        SetLastError(err);
        return INVALID_HANDLE_VALUE;
    }

    return h;
}

/* =========================================================
 * AvbDeviceName
 * ========================================================= */

const char *AvbDeviceName(avb_u16 device_id)
{
    switch (device_id) {
        /* Intel I210 family */
        case 0x1533: return "I210";
        case 0x1534: return "I210-OEM1";
        case 0x1535: return "I210-IT";
        case 0x1536: return "I210-Fiber";
        case 0x1537: return "I210-Serdes";
        case 0x1538: return "I210-SGMII";

        /* Intel I225 family */
        case 0x15F2: return "I225-LM";
        case 0x15F3: return "I225-V";
        case 0x15F4: return "I225-IT";

        /* Intel I226 family */
        case 0x125B: return "I226-LM";
        case 0x125C: return "I226-V";
        case 0x125D: return "I226-IT";
        case 0x125E: return "I226-LMVP";

        /* Intel I219 family — representative + all known SKUs */
        case 0x15B7: return "I219-LM";
        case 0x15B8: return "I219-V";
        case 0x15BB: return "I219-LM4";
        case 0x15BC: return "I219-V4";
        case 0x15BD: return "I219-LM5";
        case 0x15BE: return "I219-V5";
        case 0x15DF: return "I219-LM13";
        case 0x15E0: return "I219-V13";
        case 0x15E1: return "I219-LM9";
        case 0x15E2: return "I219-V9";
        case 0x15E3: return "I219-LM10";
        case 0x0D4F: return "I219-LM14";
        case 0x0D4E: return "I219-V14";
        case 0x0D4C: return "I219-LM15";
        case 0x0D4D: return "I219-V15";
        case 0x0D4A: return "I219-LM16";
        case 0x550F: return "I219-LM18";
        case 0x5510: return "I219-V18";
        case 0x5511: return "I219-LM19";
        case 0x5512: return "I219-V19";
        case 0x5513: return "I219-LM20";
        case 0x5514: return "I219-V20";

        /* Intel I217 family */
        case 0x153A: return "I217-LM";
        case 0x153B: return "I217-V";

        default:     return "Intel-NIC";
    }
}

/* =========================================================
 * AvbCapabilityString
 * ========================================================= */

char *AvbCapabilityString(avb_u32 caps, char *buf, size_t sz)
{
    if (!buf || sz == 0) return buf;
    buf[0] = '\0';

    static const struct { avb_u32 bit; const char *name; } kCaps[] = {
        { INTEL_CAP_BASIC_1588,  "B1588"  },
        { INTEL_CAP_ENHANCED_TS, "ENH_TS" },
        { INTEL_CAP_TSN_TAS,     "TAS"    },
        { INTEL_CAP_TSN_FP,      "FP"     },
        { INTEL_CAP_PCIe_PTM,    "PTM"    },
        { INTEL_CAP_2_5G,        "2.5G"   },
        { INTEL_CAP_MDIO,        "MDIO"   },
        { INTEL_CAP_MMIO,        "MMIO"   },
        { INTEL_CAP_EEE,         "EEE"    },
    };

    int first = 1;
    for (size_t i = 0; i < sizeof(kCaps) / sizeof(kCaps[0]); i++) {
        if (caps & kCaps[i].bit) {
            size_t used = strlen(buf);
            size_t remain = sz - used;
            if (remain < 2) break;
            if (!first) {
                strncat(buf, ",", remain - 1);
                remain--;
            }
            strncat(buf, kCaps[i].name, remain - 1);
            first = 0;
        }
    }

    if (buf[0] == '\0') strncpy(buf, "(none)", sz - 1);
    buf[sz - 1] = '\0';
    return buf;
}

/* =========================================================
 * Register access helpers
 * ========================================================= */

int AvbReadReg(HANDLE h, avb_u32 offset, avb_u32 *value)
{
    if (!value) return ERROR_INVALID_PARAMETER;
    AVB_REGISTER_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.offset = offset;
    DWORD bytes = 0;
    if (!DeviceIoControl(h, IOCTL_AVB_READ_REGISTER,
                         &req, sizeof(req), &req, sizeof(req),
                         &bytes, NULL)) {
        return (int)GetLastError();
    }
    *value = req.value;
    return 0;
}

int AvbWriteReg(HANDLE h, avb_u32 offset, avb_u32 value)
{
    AVB_REGISTER_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.offset = offset;
    req.value  = value;
    DWORD bytes = 0;
    if (!DeviceIoControl(h, IOCTL_AVB_WRITE_REGISTER,
                         &req, sizeof(req), &req, sizeof(req),
                         &bytes, NULL)) {
        return (int)GetLastError();
    }
    return 0;
}

int AvbWriteRegVerified(HANDLE h, avb_u32 offset, avb_u32 value, const char *tag)
{
    int rc = AvbWriteReg(h, offset, value);
    if (rc != 0) {
        printf("  [FAIL] %s: write reg 0x%08X failed (error %d)\n",
               tag, (unsigned)offset, rc);
        return rc;
    }
    avb_u32 readback = 0;
    rc = AvbReadReg(h, offset, &readback);
    if (rc != 0) {
        printf("  [FAIL] %s: readback reg 0x%08X failed (error %d)\n",
               tag, (unsigned)offset, rc);
        return rc;
    }
    if (readback != value) {
        printf("  [FAIL] %s: reg 0x%08X wrote 0x%08X read 0x%08X\n",
               tag, (unsigned)offset, (unsigned)value, (unsigned)readback);
        return -1;
    }
    printf("  [PASS] %s: reg 0x%08X = 0x%08X\n", tag, (unsigned)offset, (unsigned)value);
    return 0;
}

/* =========================================================
 * AvbPrintSummary
 * ========================================================= */

int AvbPrintSummary(const AvbTestStats *stats)
{
    if (!stats) return 1;
    printf("\n========================================\n");
    printf("Total:   %d\n", stats->total);
    printf("Passed:  %d\n", stats->passed);
    printf("Failed:  %d\n", stats->failed);
    printf("Skipped: %d\n", stats->skipped);
    if (stats->total > 0) {
        printf("Rate:    %.1f%%\n",
               100.0 * stats->passed / stats->total);
    }
    printf("========================================\n");
    if (stats->failed > 0) {
        printf("RESULT: FAILED (%d/%d test points failed)\n",
               stats->failed, stats->total);
        return 1;
    }
    printf("RESULT: SUCCESS\n");
    return 0;
}
