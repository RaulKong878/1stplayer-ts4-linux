#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

void stm_config_defaults(stm_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->refresh_delay_ms = 500;
    cfg->send_delay_ms = 500;
    cfg->is_log = false;
    cfg->local_port = 8890;
    cfg->is_udp = true;
    snprintf(cfg->log_path, sizeof(cfg->log_path),
             "/var/log/system-temperature-monitoring/log.txt");
    snprintf(cfg->bind_address, sizeof(cfg->bind_address), "127.0.0.1");

    cfg->display_mode = 1;
    cfg->display_submode = 0;

    snprintf(cfg->cpu_temp_sensor, sizeof(cfg->cpu_temp_sensor), "auto");
    snprintf(cfg->cpu_fan_channel, sizeof(cfg->cpu_fan_channel), "auto");
    snprintf(cfg->cpu_volt_sensor, sizeof(cfg->cpu_volt_sensor), "auto");
    snprintf(cfg->hidraw_device, sizeof(cfg->hidraw_device), "auto");

    cfg->gpu_fan_mode = STM_GPU_FAN_PWM;
    cfg->strict_windows_fields = false;
}

static char *trim_ws(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }
    return s;
}

static bool parse_bool(const char *v, bool fallback) {
    if (strcasecmp(v, "true") == 0 || strcmp(v, "1") == 0 ||
        strcasecmp(v, "yes") == 0) {
        return true;
    }
    if (strcasecmp(v, "false") == 0 || strcmp(v, "0") == 0 ||
        strcasecmp(v, "no") == 0) {
        return false;
    }
    return fallback;
}

static int clamp_int(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

bool stm_config_load(stm_config_t *cfg, const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) return false;

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        char *p = trim_ws(line);
        if (*p == '\0' || *p == '#' || *p == ';' || *p == '[') continue;

        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';

        char *k = trim_ws(p);
        char *v = trim_ws(eq + 1);

        if (strcasecmp(k, "fefresh_delay") == 0 ||
            strcasecmp(k, "refresh_delay") == 0) {
            cfg->refresh_delay_ms = clamp_int(atoi(v), 100, 60000);
        } else if (strcasecmp(k, "send_delay") == 0) {
            cfg->send_delay_ms = clamp_int(atoi(v), 100, 60000);
        } else if (strcasecmp(k, "IsLog") == 0) {
            cfg->is_log = parse_bool(v, cfg->is_log);
        } else if (strcasecmp(k, "localPort") == 0) {
            cfg->local_port = clamp_int(atoi(v), 1, 65535);
        } else if (strcasecmp(k, "IsUdp") == 0) {
            cfg->is_udp = parse_bool(v, cfg->is_udp);
        } else if (strcasecmp(k, "LogPath") == 0 && *v != '\0') {
            snprintf(cfg->log_path, sizeof(cfg->log_path), "%s", v);
        } else if (strcasecmp(k, "bind_address") == 0 && *v != '\0') {
            snprintf(cfg->bind_address, sizeof(cfg->bind_address), "%s", v);
        } else if (strcasecmp(k, "display_mode") == 0) {
            cfg->display_mode = atoi(v);
        } else if (strcasecmp(k, "display_submode") == 0) {
            cfg->display_submode = (uint8_t)clamp_int(atoi(v), 0, 255);
        } else if (strcasecmp(k, "cpu_temp_sensor") == 0 && *v != '\0') {
            snprintf(cfg->cpu_temp_sensor, sizeof(cfg->cpu_temp_sensor), "%s", v);
        } else if (strcasecmp(k, "cpu_fan_channel") == 0 && *v != '\0') {
            snprintf(cfg->cpu_fan_channel, sizeof(cfg->cpu_fan_channel), "%s", v);
        } else if (strcasecmp(k, "cpu_volt_sensor") == 0 && *v != '\0') {
            snprintf(cfg->cpu_volt_sensor, sizeof(cfg->cpu_volt_sensor), "%s", v);
        } else if (strcasecmp(k, "hidraw_device") == 0 && *v != '\0') {
            snprintf(cfg->hidraw_device, sizeof(cfg->hidraw_device), "%s", v);
        } else if (strcasecmp(k, "gpu_fan_mode") == 0) {
            cfg->gpu_fan_mode = (strcasecmp(v, "rpm") == 0)
                                    ? STM_GPU_FAN_RPM
                                    : STM_GPU_FAN_PWM;
        } else if (strcasecmp(k, "strict_windows_fields") == 0) {
            cfg->strict_windows_fields = parse_bool(v, cfg->strict_windows_fields);
        }
    }

    fclose(fp);
    return true;
}

bool stm_config_save(const stm_config_t *cfg, const char *path) {
    FILE *fp = fopen(path, "w");
    if (!fp) return false;

    fprintf(fp,
            "[config]\n"
            "fefresh_delay = %d\n"
            "send_delay = %d\n"
            "IsLog = %s\n"
            "localPort = %d\n"
            "LogPath = %s\n"
            "IsUdp = %s\n"
            "\n"
            "[linux]\n"
            "bind_address = %s\n"
            "display_mode = %d\n"
            "display_submode = %u\n"
            "cpu_temp_sensor = %s\n"
            "cpu_fan_channel = %s\n"
            "cpu_volt_sensor = %s\n"
            "hidraw_device = %s\n"
            "gpu_fan_mode = %s\n"
            "strict_windows_fields = %s\n",
            cfg->refresh_delay_ms,
            cfg->send_delay_ms,
            cfg->is_log ? "true" : "false",
            cfg->local_port,
            cfg->log_path,
            cfg->is_udp ? "true" : "false",
            cfg->bind_address,
            cfg->display_mode,
            (unsigned int)cfg->display_submode,
            cfg->cpu_temp_sensor,
            cfg->cpu_fan_channel,
            cfg->cpu_volt_sensor,
            cfg->hidraw_device,
            cfg->gpu_fan_mode == STM_GPU_FAN_RPM ? "rpm" : "pwm",
            cfg->strict_windows_fields ? "true" : "false");

    fclose(fp);
    return true;
}
