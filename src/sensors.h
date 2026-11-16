#ifndef STM_SENSORS_H
#define STM_SENSORS_H

#include "config.h"
#include "protocol.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STM_MAX_NET_IFACES 32
#define STM_MAX_DISKS 32

typedef struct {
    char name[64];
    uint64_t rx_bytes;
    uint64_t tx_bytes;
} stm_net_snap_t;

typedef struct {
    char name[64];
    uint64_t io_ticks_ms;
    uint64_t total_ios;
    uint64_t total_rw_ms;
} stm_disk_snap_t;

typedef struct {
    char cpu_name[256];
    char gpu_name[256];
    char os_name[256];
    char board_name[256];
    uint64_t mem_total_kb;

    char cpu_temp_path[512];
    char cpu_volt_path[512];
    char cpu_fan_path[512];
    char cpu_power_hwmon_path[512];
    char rapl_energy_path[512];
    uint64_t rapl_max_range_uj;

    int gpu_vendor; /* 0=none, 0x1002=AMD, 0x10de=NVIDIA, 0x8086=Intel */
    char gpu_pci_slot[32];
    char gpu_busy_path[512];
    char gpu_temp_path[512];
    char gpu_freq_path[512];
    char gpu_pwm_path[512];
    char gpu_fan_path[512];
    char gpu_volt_path[512];
    char gpu_power_path[512];
    bool has_nvidia_smi;

    /* Delta state */
    bool has_prev;
    double prev_mono_sec;

    uint64_t prev_cpu_total;
    uint64_t prev_cpu_idle;
    uint64_t prev_rapl_uj;
    bool has_prev_rapl;

    stm_net_snap_t prev_net[STM_MAX_NET_IFACES];
    size_t prev_net_count;

    stm_disk_snap_t prev_disk[STM_MAX_DISKS];
    size_t prev_disk_count;
} stm_sensor_ctx_t;

void stm_sensors_init(stm_sensor_ctx_t *ctx, const stm_config_t *cfg);
void stm_sensors_sample(stm_sensor_ctx_t *ctx,
                        const stm_config_t *cfg,
                        stm_telemetry_t *out);

/* Takes a baseline sample, sleeps 150ms, then samples deltas accurately. */
void stm_sensors_sample_once(stm_sensor_ctx_t *ctx,
                             const stm_config_t *cfg,
                             stm_telemetry_t *out);

void stm_sensors_print_diagnostics(const stm_sensor_ctx_t *ctx,
                                   const stm_telemetry_t *t);

size_t stm_sensors_format_rundata_json(const stm_sensor_ctx_t *ctx,
                                       const stm_telemetry_t *t,
                                       char *buf,
                                       size_t buf_sz);

size_t stm_sensors_format_staticdata_json(const stm_sensor_ctx_t *ctx,
                                          char *buf,
                                          size_t buf_sz);

#endif /* STM_SENSORS_H */
