#ifndef STM_PROTOCOL_H
#define STM_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STM_REPORT_BYTES 65
#define STM_PAYLOAD_BYTES 64

#define STM_CMD_HANDSHAKE 0x01
#define STM_CMD_TELEMETRY 0x02

typedef struct {
    uint16_t vid;
    uint16_t pid;
    const char *profile_name;
    const char *device_name;
} stm_vidpid_t;

typedef struct {
    int cpu_temp_c;
    int cpu_load_pct;
    uint16_t cpu_clock_mhz;
    uint16_t cpu_fan_rpm;
    double cpu_voltage_v;
    double cpu_watt;

    int gpu_temp_c;
    int gpu_load_pct;
    uint16_t gpu_clock_mhz;
    uint16_t gpu_fan;
    double gpu_voltage_v;
    double gpu_watt;

    int ram_temp_c;
    int ram_load_pct;
    uint16_t ram_clock_mhz;

    int disk_temp_c;
    int disk_load_pct;

    uint32_t net_up_bps;
    uint32_t net_down_bps;

    int year;        /* e.g. 2026 */
    int month;       /* 1..12 */
    int day;         /* 1..31 */
    int day_of_week; /* 0=Sunday..6=Saturday */
    int hour;        /* 0..23 */
    int minute;      /* 0..59 */
    int second;      /* 0..59 */
    int centisecond; /* 0..99 */

    uint8_t display_mode;
} stm_telemetry_t;

const stm_vidpid_t *stm_known_devices(size_t *count);
const stm_vidpid_t *stm_match_vidpid(uint16_t vid, uint16_t pid);

uint8_t stm_encode_display_mode(int mode, uint8_t submode);
void stm_compute_checksums(uint8_t payload[STM_PAYLOAD_BYTES]);

/*
 * Builds the 65-byte HID output report (Report ID 0x00 + 64-byte payload) for
 * the 0x01 handshake packet. If seed_bytes is NULL, reads 64 random bytes from
 * /dev/urandom (falling back to PRNG if unavailable).
 */
void stm_build_handshake(uint8_t out_report[STM_REPORT_BYTES],
                         uint16_t *expected_reply_key,
                         const uint8_t seed_bytes[STM_PAYLOAD_BYTES]);

/*
 * Verifies the 64-byte HID input payload returned by the device in response
 * to command 0x01.
 */
bool stm_verify_handshake_reply(const uint8_t in_payload[STM_PAYLOAD_BYTES],
                                uint16_t expected_reply_key);

/*
 * Packs all telemetry fields and wall-clock timestamp into a 65-byte HID
 * output report (command 0x02), matching TempComm.dll SetDeviceTime 1:1.
 */
void stm_build_telemetry(const stm_telemetry_t *t,
                         uint8_t out_report[STM_REPORT_BYTES]);

#endif /* STM_PROTOCOL_H */
