#ifndef STM_CONFIG_H
#define STM_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    STM_GPU_FAN_PWM = 0,
    STM_GPU_FAN_RPM = 1
} stm_gpu_fan_mode_t;

typedef struct {
    int refresh_delay_ms;        /* Windows INI key: fefresh_delay (default 500) */
    int send_delay_ms;           /* Windows INI key: send_delay (default 500) */
    bool is_log;                 /* Windows INI key: IsLog (default false) */
    int local_port;              /* Windows INI key: localPort (default 8890) */
    bool is_udp;                 /* Windows INI key: IsUdp (default true) */
    char log_path[512];          /* Windows INI key: LogPath */
    char bind_address[64];       /* Default 127.0.0.1 */

    int display_mode;            /* SetDisplayMode arg0 (default 1) */
    uint8_t display_submode;     /* SetDisplayMode arg1 (default 0) */

    char cpu_temp_sensor[256];   /* "auto" or sysfs path */
    char cpu_fan_channel[256];   /* "auto", "fan1", "fan2", ... or sysfs path */
    char cpu_volt_sensor[256];   /* "auto" or sysfs path */
    char hidraw_device[256];     /* "auto" or /dev/hidrawX */

    stm_gpu_fan_mode_t gpu_fan_mode;
    bool strict_windows_fields;
} stm_config_t;

void stm_config_defaults(stm_config_t *cfg);
bool stm_config_load(stm_config_t *cfg, const char *path);
bool stm_config_save(const stm_config_t *cfg, const char *path);

#endif /* STM_CONFIG_H */
