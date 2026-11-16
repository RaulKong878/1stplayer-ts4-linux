#include "protocol.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const stm_vidpid_t k_known_devices[] = {
    {0x0145, 0x1001, "SystemTemperatureMonitoring", "1stPlayer TS4 / HWCX USB Display"},
    {0x12b7, 0x1001, "XKWHardwareServices", "XKW Hardware Display"},
    {0x1c57, 0x3ed1, "ZALMAN-CTM", "ZALMAN CTM Display"},
    {0x1c57, 0x4b8f, "ZALMAN-CTM", "ZALMAN CTM Display v2"},
    {0x1c57, 0x3a7f, "ZALMAN-CTM", "ZALMAN CTM Display v3"},
    {0x1c57, 0x3ed2, "ZALMAN-TDM", "ZALMAN TDM Display"},
    {0x1c57, 0x3ed6, "ZALMAN-TDM", "ZALMAN TDM Display v2"},
    {0x2c65, 0x1001, "SLAYERCPULiveTemperature", "SLAYER CPU Live Display"},
    {0x2c65, 0x1000, "RM-Hardware", "RM Hardware Display"},
    {0x0145, 0x1005, "LK Digital Display", "LK Digital Display"},
};

const stm_vidpid_t *stm_known_devices(size_t *count) {
    if (count) {
        *count = sizeof(k_known_devices) / sizeof(k_known_devices[0]);
    }
    return k_known_devices;
}

const stm_vidpid_t *stm_match_vidpid(uint16_t vid, uint16_t pid) {
    size_t n = sizeof(k_known_devices) / sizeof(k_known_devices[0]);
    for (size_t i = 0; i < n; i++) {
        if (k_known_devices[i].vid == vid && k_known_devices[i].pid == pid) {
            return &k_known_devices[i];
        }
    }
    return NULL;
}

static uint8_t clamp_u8(int val) {
    if (val < 0) return 0;
    if (val > 255) return 255;
    return (uint8_t)val;
}

static void fill_random_bytes(uint8_t *buf, size_t len) {
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        size_t off = 0;
        while (off < len) {
            ssize_t r = read(fd, buf + off, len - off);
            if (r <= 0) break;
            off += (size_t)r;
        }
        close(fd);
        if (off == len) return;
    }

    unsigned int s = (unsigned int)time(NULL) ^ (unsigned int)getpid();
    for (size_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)(rand_r(&s) & 0xFF);
    }
}

static void compute_payload_checksum_pair(const uint8_t payload[STM_PAYLOAD_BYTES],
                                          uint8_t *out_xor,
                                          uint8_t *out_add) {
    /*
     * Mirrors TempComm.dll 0x1800012cb..0x18000130a:
     *   payload[62] = XOR_{i=0..59} ((i * payload[i]) & 0xFF)
     *   payload[63] = SUM_{i=0..62} ((i * payload[i]) & 0xFF) & 0xFF
     * Note: payload[62] is included in the ADD sum loop (i=0..62).
     */
    uint8_t xor_chk = 0;
    for (unsigned int i = 0; i < 60; i++) {
        xor_chk ^= (uint8_t)((i * (unsigned int)payload[i]) & 0xFFu);
    }
    if (out_xor) *out_xor = xor_chk;

    uint8_t add_chk = 0;
    for (unsigned int i = 0; i < 63; i++) {
        uint8_t v = (i == 62) ? xor_chk : payload[i];
        add_chk = (uint8_t)((add_chk + ((i * (unsigned int)v) & 0xFFu)) & 0xFFu);
    }
    if (out_add) *out_add = add_chk;
}

void stm_compute_checksums(uint8_t payload[STM_PAYLOAD_BYTES]) {
    uint8_t x = 0;
    uint8_t a = 0;
    compute_payload_checksum_pair(payload, &x, &a);
    payload[62] = x;
    payload[63] = a;
}

uint8_t stm_encode_display_mode(int mode, uint8_t submode) {
    /* Mirrors TempComm.dll SetDisplayMode (0x180001960). */
    return (mode == 0) ? (uint8_t)(submode | 0x80u) : submode;
}

void stm_build_handshake(uint8_t out_report[STM_REPORT_BYTES],
                         uint16_t *expected_reply_key,
                         const uint8_t seed_bytes[STM_PAYLOAD_BYTES]) {
    out_report[0] = 0x00;
    uint8_t *p = &out_report[1];

    if (seed_bytes) {
        memcpy(p, seed_bytes, STM_PAYLOAD_BYTES);
    } else {
        fill_random_bytes(p, STM_PAYLOAD_BYTES);
    }

    p[0] = STM_CMD_HANDSHAKE;
    p[1] = 0x00;
    p[2] = 0x00;
    p[3] = 0x00;

    uint16_t k1 = (uint16_t)(((uint16_t)p[4] << 8) | p[5]);
    uint16_t k2 = (uint16_t)(((uint16_t)p[6] << 8) | p[7]);
    uint16_t k3 = (uint16_t)(((uint16_t)p[8] << 8) | p[9]);

    uint16_t auth_out = (uint16_t)(k1 ^ (uint16_t)(k2 + k3));
    uint16_t auth_in = (uint16_t)(k3 ^ (uint16_t)(k1 + k2));

    p[10] = (uint8_t)((auth_out >> 8) & 0xFFu);
    p[11] = (uint8_t)(auth_out & 0xFFu);

    if (expected_reply_key) {
        *expected_reply_key = auth_in;
    }

    stm_compute_checksums(p);
}

bool stm_verify_handshake_reply(const uint8_t in_payload[STM_PAYLOAD_BYTES],
                                uint16_t expected_reply_key) {
    uint16_t got_key = (uint16_t)(((uint16_t)in_payload[4] << 8) | in_payload[5]);
    if (got_key != expected_reply_key) {
        return false;
    }
    uint8_t expected_xor = 0;
    uint8_t expected_add = 0;
    compute_payload_checksum_pair(in_payload, &expected_xor, &expected_add);
    return (in_payload[62] == expected_xor) && (in_payload[63] == expected_add);
}

static void split_voltage(double v, uint8_t *out_int, uint8_t *out_frac) {
    if (v < 0.0) v = 0.0;
    if (v > 255.99) v = 255.99;
    int centivolts = (int)(v * 100.0 + 0.5);
    *out_int = clamp_u8(centivolts / 100);
    *out_frac = clamp_u8(centivolts % 100);
}

static uint16_t to_deci_watts(double w) {
    if (w < 0.0) return 0;
    double dw = w * 10.0 + 0.5;
    if (dw > 65535.0) return 65535;
    return (uint16_t)dw;
}

void stm_build_telemetry(const stm_telemetry_t *t,
                         uint8_t out_report[STM_REPORT_BYTES]) {
    memset(out_report, 0, STM_REPORT_BYTES);
    out_report[0] = 0x00;

    uint8_t *p = &out_report[1];
    p[0] = STM_CMD_TELEMETRY;
    p[1] = clamp_u8(t->cpu_temp_c);
    p[2] = clamp_u8(t->gpu_temp_c);

    p[3] = clamp_u8(t->hour);
    p[4] = clamp_u8(t->minute);
    p[5] = clamp_u8(t->second);
    p[6] = clamp_u8(t->centisecond);
    p[7] = clamp_u8(t->year / 100);
    p[8] = clamp_u8(t->year % 100);
    p[9] = clamp_u8(t->month);
    p[10] = clamp_u8(t->day);
    p[11] = clamp_u8(t->day_of_week);

    p[12] = clamp_u8(t->cpu_load_pct);
    p[13] = (uint8_t)((t->cpu_clock_mhz >> 8) & 0xFFu);
    p[14] = (uint8_t)(t->cpu_clock_mhz & 0xFFu);

    p[15] = clamp_u8(t->gpu_load_pct);
    p[16] = (uint8_t)((t->gpu_clock_mhz >> 8) & 0xFFu);
    p[17] = (uint8_t)(t->gpu_clock_mhz & 0xFFu);

    p[18] = clamp_u8(t->ram_temp_c);
    p[19] = clamp_u8(t->ram_load_pct);
    p[20] = (uint8_t)((t->ram_clock_mhz >> 8) & 0xFFu);
    p[21] = (uint8_t)(t->ram_clock_mhz & 0xFFu);

    p[22] = clamp_u8(t->disk_temp_c);
    p[23] = clamp_u8(t->disk_load_pct);

    p[24] = (uint8_t)((t->cpu_fan_rpm >> 8) & 0xFFu);
    p[25] = (uint8_t)(t->cpu_fan_rpm & 0xFFu);

    split_voltage(t->cpu_voltage_v, &p[26], &p[27]);

    uint16_t cpu_dw = to_deci_watts(t->cpu_watt);
    p[28] = (uint8_t)((cpu_dw >> 8) & 0xFFu);
    p[29] = (uint8_t)(cpu_dw & 0xFFu);

    p[30] = (uint8_t)((t->gpu_fan >> 8) & 0xFFu);
    p[31] = (uint8_t)(t->gpu_fan & 0xFFu);

    split_voltage(t->gpu_voltage_v, &p[32], &p[33]);

    uint16_t gpu_dw = to_deci_watts(t->gpu_watt);
    p[34] = (uint8_t)((gpu_dw >> 8) & 0xFFu);
    p[35] = (uint8_t)(gpu_dw & 0xFFu);

    /* p[36..39] remain 0x00, matching TempComm.dll 0x180001bec..0x180001c10 */

    p[40] = (uint8_t)((t->net_up_bps >> 24) & 0xFFu);
    p[41] = (uint8_t)((t->net_up_bps >> 16) & 0xFFu);
    p[42] = (uint8_t)((t->net_up_bps >> 8) & 0xFFu);
    p[43] = (uint8_t)(t->net_up_bps & 0xFFu);

    p[44] = (uint8_t)((t->net_down_bps >> 24) & 0xFFu);
    p[45] = (uint8_t)((t->net_down_bps >> 16) & 0xFFu);
    p[46] = (uint8_t)((t->net_down_bps >> 8) & 0xFFu);
    p[47] = (uint8_t)(t->net_down_bps & 0xFFu);

    p[48] = t->display_mode;
}
