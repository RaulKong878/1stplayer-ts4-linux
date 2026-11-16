#include "sensors.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static bool read_line_trimmed(const char *path, char *out, size_t out_sz) {
    FILE *fp = fopen(path, "r");
    if (!fp) return false;
    if (!fgets(out, (int)out_sz, fp)) {
        fclose(fp);
        return false;
    }
    fclose(fp);
    size_t len = strlen(out);
    while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r' ||
                       isspace((unsigned char)out[len - 1]))) {
        out[--len] = '\0';
    }
    return true;
}

static bool read_i64(const char *path, int64_t *out) {
    char buf[64];
    if (!read_line_trimmed(path, buf, sizeof(buf))) return false;
    char *end = NULL;
    long long v = strtoll(buf, &end, 10);
    if (end == buf) return false;
    *out = (int64_t)v;
    return true;
}

static bool read_u64(const char *path, uint64_t *out) {
    char buf[64];
    if (!read_line_trimmed(path, buf, sizeof(buf))) return false;
    char *end = NULL;
    unsigned long long v = strtoull(buf, &end, 10);
    if (end == buf) return false;
    *out = (uint64_t)v;
    return true;
}

static double monotonic_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static bool str_contains_ci(const char *hay, const char *needle) {
    if (!hay || !needle) return false;
    size_t nlen = strlen(needle);
    if (nlen == 0) return true;
    for (const char *p = hay; *p; p++) {
        if (strncasecmp(p, needle, nlen) == 0) return true;
    }
    return false;
}

static bool file_exists(const char *path) {
    struct stat st;
    return path && path[0] != '\0' && stat(path, &st) == 0;
}

static void find_hwmon_labeled_file(const char *hwmon_dir,
                                    const char *prefix,
                                    const char *suffix,
                                    const char *label_substr,
                                    char *out_path,
                                    size_t out_sz) {
    for (int i = 0; i <= 20; i++) {
        char lpath[512], ipath[512], label[128];
        snprintf(lpath, sizeof(lpath), "%.460s/%.16s%d_label", hwmon_dir, prefix, i);
        snprintf(ipath, sizeof(ipath), "%.460s/%.16s%d_%.16s", hwmon_dir, prefix, i, suffix);
        if (!file_exists(ipath)) continue;
        if (read_line_trimmed(lpath, label, sizeof(label)) &&
            str_contains_ci(label, label_substr)) {
            snprintf(out_path, out_sz, "%.*s", (int)(out_sz > 0 ? out_sz - 1 : 0), ipath);
            return;
        }
    }
}

static bool is_superio_hwmon(const char *name) {
    return strncmp(name, "nct6", 4) == 0 || strncmp(name, "it87", 4) == 0 ||
           strncmp(name, "f718", 4) == 0 || strcmp(name, "asus") == 0 ||
           strcmp(name, "asus-ec-sensors") == 0 || strcmp(name, "dell_smm") == 0 ||
           strcmp(name, "thinkpad") == 0 || strcmp(name, "lm75") == 0;
}

static void discover_cpu_hwmon(stm_sensor_ctx_t *ctx, const stm_config_t *cfg) {
    if (strcmp(cfg->cpu_temp_sensor, "auto") != 0 && file_exists(cfg->cpu_temp_sensor)) {
        snprintf(ctx->cpu_temp_path, sizeof(ctx->cpu_temp_path), "%s", cfg->cpu_temp_sensor);
    }
    if (strcmp(cfg->cpu_volt_sensor, "auto") != 0 && file_exists(cfg->cpu_volt_sensor)) {
        snprintf(ctx->cpu_volt_path, sizeof(ctx->cpu_volt_path), "%s", cfg->cpu_volt_sensor);
    }
    if (cfg->cpu_fan_channel[0] == '/' && file_exists(cfg->cpu_fan_channel)) {
        snprintf(ctx->cpu_fan_path, sizeof(ctx->cpu_fan_path), "%s", cfg->cpu_fan_channel);
    }

    DIR *dir = opendir("/sys/class/hwmon");
    if (!dir) return;

    char fallback_superio_temp[512] = {0};
    char fallback_fan[512] = {0};

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, "hwmon", 5) != 0) continue;
        char hdir[320], npath[384], hname[64];
        snprintf(hdir, sizeof(hdir), "/sys/class/hwmon/%.255s", ent->d_name);
        snprintf(npath, sizeof(npath), "%s/name", hdir);
        if (!read_line_trimmed(npath, hname, sizeof(hname))) continue;

        /* CPU temperature: k10temp / zenpower / coretemp */
        if (ctx->cpu_temp_path[0] == '\0' &&
            (strcmp(hname, "k10temp") == 0 || strcmp(hname, "zenpower") == 0 ||
             strcmp(hname, "coretemp") == 0)) {
            find_hwmon_labeled_file(hdir, "temp", "input", "Tctl",
                                    ctx->cpu_temp_path, sizeof(ctx->cpu_temp_path));
            if (ctx->cpu_temp_path[0] == '\0') {
                find_hwmon_labeled_file(hdir, "temp", "input", "Tdie",
                                        ctx->cpu_temp_path, sizeof(ctx->cpu_temp_path));
            }
            if (ctx->cpu_temp_path[0] == '\0') {
                find_hwmon_labeled_file(hdir, "temp", "input", "Package",
                                        ctx->cpu_temp_path, sizeof(ctx->cpu_temp_path));
            }
            if (ctx->cpu_temp_path[0] == '\0') {
                char t1[384];
                snprintf(t1, sizeof(t1), "%s/temp1_input", hdir);
                if (file_exists(t1)) {
                    snprintf(ctx->cpu_temp_path, sizeof(ctx->cpu_temp_path), "%s", t1);
                }
            }
        }

        if (ctx->cpu_power_hwmon_path[0] == '\0' &&
            (strcmp(hname, "amd_energy") == 0 || strcmp(hname, "zenergy") == 0 ||
             strcmp(hname, "k10temp") == 0)) {
            char p1[384];
            snprintf(p1, sizeof(p1), "%s/power1_input", hdir);
            if (file_exists(p1)) {
                snprintf(ctx->cpu_power_hwmon_path, sizeof(ctx->cpu_power_hwmon_path), "%s", p1);
            } else {
                snprintf(p1, sizeof(p1), "%s/power1_average", hdir);
                if (file_exists(p1)) {
                    snprintf(ctx->cpu_power_hwmon_path, sizeof(ctx->cpu_power_hwmon_path), "%s", p1);
                }
            }
        }

        if (is_superio_hwmon(hname)) {
            if (fallback_superio_temp[0] == '\0') {
                find_hwmon_labeled_file(hdir, "temp", "input", "CPUTIN",
                                        fallback_superio_temp, sizeof(fallback_superio_temp));
            }

            if (ctx->cpu_volt_path[0] == '\0') {
                find_hwmon_labeled_file(hdir, "in", "input", "Vcore",
                                        ctx->cpu_volt_path, sizeof(ctx->cpu_volt_path));
                if (ctx->cpu_volt_path[0] == '\0') {
                    char v0[384];
                    snprintf(v0, sizeof(v0), "%s/in0_input", hdir);
                    if (file_exists(v0)) {
                        snprintf(ctx->cpu_volt_path, sizeof(ctx->cpu_volt_path), "%s", v0);
                    }
                }
            }

            if (ctx->cpu_fan_path[0] == '\0') {
                if (strcmp(cfg->cpu_fan_channel, "auto") != 0) {
                    char fpath[512];
                    snprintf(fpath, sizeof(fpath), "%s/%.120s_input", hdir, cfg->cpu_fan_channel);
                    if (file_exists(fpath)) {
                        snprintf(ctx->cpu_fan_path, sizeof(ctx->cpu_fan_path), "%s", fpath);
                    }
                } else {
                    find_hwmon_labeled_file(hdir, "fan", "input", "cpu",
                                            ctx->cpu_fan_path, sizeof(ctx->cpu_fan_path));
                    if (ctx->cpu_fan_path[0] == '\0') {
                        /* On ASUS/ASRock/MSI nct67* boards, fan2 is typically CPU_FAN */
                        char f2[384];
                        int64_t rpm = 0;
                        snprintf(f2, sizeof(f2), "%s/fan2_input", hdir);
                        if (read_i64(f2, &rpm) && rpm > 0) {
                            snprintf(ctx->cpu_fan_path, sizeof(ctx->cpu_fan_path), "%s", f2);
                        }
                    }
                    if (ctx->cpu_fan_path[0] == '\0') {
                        for (int fi = 1; fi <= 10; fi++) {
                            char fp_try[384];
                            int64_t rpm = 0;
                            snprintf(fp_try, sizeof(fp_try), "%s/fan%d_input", hdir, fi);
                            if (read_i64(fp_try, &rpm) && rpm > 0) {
                                snprintf(fallback_fan, sizeof(fallback_fan), "%s", fp_try);
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
    closedir(dir);

    if (ctx->cpu_temp_path[0] == '\0' && fallback_superio_temp[0] != '\0') {
        snprintf(ctx->cpu_temp_path, sizeof(ctx->cpu_temp_path), "%s", fallback_superio_temp);
    }
    if (ctx->cpu_temp_path[0] == '\0' &&
        file_exists("/sys/class/thermal/thermal_zone0/temp")) {
        snprintf(ctx->cpu_temp_path, sizeof(ctx->cpu_temp_path),
                 "/sys/class/thermal/thermal_zone0/temp");
    }
    if (ctx->cpu_fan_path[0] == '\0' && fallback_fan[0] != '\0') {
        snprintf(ctx->cpu_fan_path, sizeof(ctx->cpu_fan_path), "%s", fallback_fan);
    }
}

static void discover_rapl(stm_sensor_ctx_t *ctx) {
    const char *ep = "/sys/class/powercap/intel-rapl:0/energy_uj";
    const char *mp = "/sys/class/powercap/intel-rapl:0/max_energy_range_uj";
    uint64_t dummy = 0;
    if (read_u64(ep, &dummy)) {
        snprintf(ctx->rapl_energy_path, sizeof(ctx->rapl_energy_path), "%s", ep);
        if (!read_u64(mp, &ctx->rapl_max_range_uj)) {
            ctx->rapl_max_range_uj = 0;
        }
    }
}

static void discover_gpu(stm_sensor_ctx_t *ctx) {
    DIR *dir = opendir("/sys/class/drm");
    if (!dir) return;

    int best_score = -1;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, "card", 4) != 0) continue;
        if (strchr(ent->d_name, '-') != NULL) continue;

        char vpath[320], vbuf[32];
        snprintf(vpath, sizeof(vpath), "/sys/class/drm/%.240s/device/vendor", ent->d_name);
        if (!read_line_trimmed(vpath, vbuf, sizeof(vbuf))) continue;

        unsigned int vendor = (unsigned int)strtoul(vbuf, NULL, 16);
        int score = 0;
        if (vendor == 0x10de) score = 3;      /* Prefer NVIDIA discrete */
        else if (vendor == 0x1002) score = 2; /* AMD Radeon */
        else if (vendor == 0x8086) score = 1; /* Intel */
        if (score <= best_score) continue;

        best_score = score;
        ctx->gpu_vendor = (int)vendor;
        snprintf(ctx->gpu_busy_path, sizeof(ctx->gpu_busy_path),
                 "/sys/class/drm/%.240s/device/gpu_busy_percent", ent->d_name);

        /* Extract PCI slot name if present */
        ctx->gpu_pci_slot[0] = '\0';
        char uepath[320];
        snprintf(uepath, sizeof(uepath), "/sys/class/drm/%.240s/device/uevent", ent->d_name);
        FILE *ufp = fopen(uepath, "r");
        if (ufp) {
            char uline[256];
            while (fgets(uline, sizeof(uline), ufp)) {
                if (strncmp(uline, "PCI_SLOT_NAME=", 14) == 0) {
                    snprintf(ctx->gpu_pci_slot, sizeof(ctx->gpu_pci_slot), "%.31s", uline + 14);
                    size_t sl = strlen(ctx->gpu_pci_slot);
                    while (sl > 0 && isspace((unsigned char)ctx->gpu_pci_slot[sl - 1])) {
                        ctx->gpu_pci_slot[--sl] = '\0';
                    }
                    break;
                }
            }
            fclose(ufp);
        }

        char hroot[320];
        snprintf(hroot, sizeof(hroot), "/sys/class/drm/%.240s/device/hwmon", ent->d_name);
        DIR *hdir = opendir(hroot);
        if (hdir) {
            struct dirent *hent;
            while ((hent = readdir(hdir)) != NULL) {
                if (strncmp(hent->d_name, "hwmon", 5) != 0) continue;
                char base[448];
                snprintf(base, sizeof(base), "%.319s/%.120s", hroot, hent->d_name);
                snprintf(ctx->gpu_temp_path, sizeof(ctx->gpu_temp_path), "%s/temp1_input", base);
                snprintf(ctx->gpu_freq_path, sizeof(ctx->gpu_freq_path), "%s/freq1_input", base);
                snprintf(ctx->gpu_pwm_path, sizeof(ctx->gpu_pwm_path), "%s/pwm1", base);
                snprintf(ctx->gpu_fan_path, sizeof(ctx->gpu_fan_path), "%s/fan1_input", base);
                snprintf(ctx->gpu_volt_path, sizeof(ctx->gpu_volt_path), "%s/in0_input", base);
                snprintf(ctx->gpu_power_path, sizeof(ctx->gpu_power_path), "%s/power1_average", base);
                if (!file_exists(ctx->gpu_power_path)) {
                    snprintf(ctx->gpu_power_path, sizeof(ctx->gpu_power_path), "%s/power1_input", base);
                }
                break;
            }
            closedir(hdir);
        }

        if (vendor == 0x10de) {
            ctx->has_nvidia_smi = (access("/usr/bin/nvidia-smi", X_OK) == 0);
            snprintf(ctx->gpu_name, sizeof(ctx->gpu_name), "NVIDIA GPU");
        } else if (vendor == 0x1002) {
            snprintf(ctx->gpu_name, sizeof(ctx->gpu_name), "AMD Radeon GPU");
        } else if (vendor == 0x8086) {
            snprintf(ctx->gpu_name, sizeof(ctx->gpu_name), "Intel Graphics");
        }
    }
    closedir(dir);
}

static void read_static_identity(stm_sensor_ctx_t *ctx) {
    /* CPU name */
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "model name", 10) == 0) {
                char *c = strchr(line, ':');
                if (c) {
                    c++;
                    while (*c && isspace((unsigned char)*c)) c++;
                    snprintf(ctx->cpu_name, sizeof(ctx->cpu_name), "%.255s", c);
                    size_t l = strlen(ctx->cpu_name);
                    while (l > 0 && isspace((unsigned char)ctx->cpu_name[l - 1])) {
                        ctx->cpu_name[--l] = '\0';
                    }
                    break;
                }
            }
        }
        fclose(fp);
    }
    if (ctx->cpu_name[0] == '\0') {
        snprintf(ctx->cpu_name, sizeof(ctx->cpu_name), "Linux CPU");
    }

    /* OS name */
    fp = fopen("/etc/os-release", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
                char *v = line + 12;
                if (*v == '"') v++;
                snprintf(ctx->os_name, sizeof(ctx->os_name), "%.255s", v);
                size_t l = strlen(ctx->os_name);
                while (l > 0 && (ctx->os_name[l - 1] == '\n' ||
                                 ctx->os_name[l - 1] == '\r' ||
                                 ctx->os_name[l - 1] == '"')) {
                    ctx->os_name[--l] = '\0';
                }
                break;
            }
        }
        fclose(fp);
    }
    if (ctx->os_name[0] == '\0') {
        snprintf(ctx->os_name, sizeof(ctx->os_name), "Linux");
    }

    /* Motherboard */
    char vendor[64] = {0}, product[64] = {0};
    read_line_trimmed("/sys/class/dmi/id/board_vendor", vendor, sizeof(vendor));
    read_line_trimmed("/sys/class/dmi/id/board_name", product, sizeof(product));
    if (vendor[0] && product[0]) {
        snprintf(ctx->board_name, sizeof(ctx->board_name), "%s %s", vendor, product);
    } else {
        snprintf(ctx->board_name, sizeof(ctx->board_name), "Linux Mainboard");
    }

    /* Total RAM */
    fp = fopen("/proc/meminfo", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            unsigned long long kb = 0;
            if (sscanf(line, "MemTotal: %llu kB", &kb) == 1) {
                ctx->mem_total_kb = (uint64_t)kb;
                break;
            }
        }
        fclose(fp);
    }

    /* NVIDIA precise GPU name or lspci lookup once at init */
    if (ctx->gpu_vendor == 0x10de && ctx->has_nvidia_smi) {
        FILE *np = popen("nvidia-smi --query-gpu=name --format=csv,noheader 2>/dev/null", "r");
        if (np) {
            char gbuf[256] = {0};
            if (fgets(gbuf, sizeof(gbuf), np)) {
                size_t l = strlen(gbuf);
                while (l > 0 && isspace((unsigned char)gbuf[l - 1])) gbuf[--l] = '\0';
                if (l > 0) snprintf(ctx->gpu_name, sizeof(ctx->gpu_name), "%.255s", gbuf);
            }
            pclose(np);
        }
    } else if (ctx->gpu_pci_slot[0] != '\0') {
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "lspci -s %.31s 2>/dev/null", ctx->gpu_pci_slot);
        FILE *lp = popen(cmd, "r");
        if (lp) {
            char gbuf[256] = {0};
            if (fgets(gbuf, sizeof(gbuf), lp)) {
                size_t l = strlen(gbuf);
                while (l > 0 && isspace((unsigned char)gbuf[l - 1])) gbuf[--l] = '\0';
                char *col = strstr(gbuf, ": ");
                if (col) {
                    snprintf(ctx->gpu_name, sizeof(ctx->gpu_name), "%.255s", col + 2);
                }
            }
            pclose(lp);
        }
    }
}

void stm_sensors_init(stm_sensor_ctx_t *ctx, const stm_config_t *cfg) {
    memset(ctx, 0, sizeof(*ctx));
    discover_cpu_hwmon(ctx, cfg);
    discover_rapl(ctx);
    discover_gpu(ctx);
    read_static_identity(ctx);

    /* Prime baseline deltas without blocking */
    stm_telemetry_t dummy;
    stm_sensors_sample(ctx, cfg, &dummy);
}

static int sample_cpu_load(stm_sensor_ctx_t *ctx) {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return 0;

    char line[512];
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return 0;
    }
    fclose(fp);

    unsigned long long user = 0, nice = 0, sys = 0, idle = 0;
    unsigned long long iow = 0, irq = 0, sirq = 0, steal = 0;
    int n = sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                   &user, &nice, &sys, &idle, &iow, &irq, &sirq, &steal);
    if (n < 4) return 0;

    uint64_t cur_idle = (uint64_t)(idle + iow);
    uint64_t cur_total = (uint64_t)(user + nice + sys + idle + iow + irq + sirq + steal);

    int pct = 0;
    if (ctx->has_prev && cur_total > ctx->prev_cpu_total) {
        uint64_t dt = cur_total - ctx->prev_cpu_total;
        uint64_t di = (cur_idle >= ctx->prev_cpu_idle) ? (cur_idle - ctx->prev_cpu_idle) : 0;
        if (di < dt) {
            pct = (int)(((double)(dt - di) * 100.0) / (double)dt + 0.5);
        }
    }

    ctx->prev_cpu_total = cur_total;
    ctx->prev_cpu_idle = cur_idle;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

static uint16_t sample_cpu_clock_mhz(void) {
    uint64_t max_khz = 0;
    for (int cpu = 0; cpu < 256; cpu++) {
        char path[128];
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", cpu);
        uint64_t khz = 0;
        if (!read_u64(path, &khz)) {
            if (cpu == 0) break;
            continue;
        }
        if (khz > max_khz) max_khz = khz;
    }
    if (max_khz > 0) {
        return (uint16_t)((max_khz + 500) / 1000);
    }

    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (fp) {
        char line[256];
        double max_mhz = 0.0;
        while (fgets(line, sizeof(line), fp)) {
            double mhz = 0.0;
            if (sscanf(line, "cpu MHz : %lf", &mhz) == 1 && mhz > max_mhz) {
                max_mhz = mhz;
            }
        }
        fclose(fp);
        if (max_mhz > 0.0) return (uint16_t)(max_mhz + 0.5);
    }
    return 0;
}

static double sample_cpu_watt(stm_sensor_ctx_t *ctx, double dt_sec) {
    if (ctx->rapl_energy_path[0] != '\0') {
        uint64_t cur_uj = 0;
        if (read_u64(ctx->rapl_energy_path, &cur_uj)) {
            double w = 0.0;
            if (ctx->has_prev_rapl && dt_sec > 0.005) {
                uint64_t duj = 0;
                if (cur_uj >= ctx->prev_rapl_uj) {
                    duj = cur_uj - ctx->prev_rapl_uj;
                } else if (ctx->rapl_max_range_uj > ctx->prev_rapl_uj) {
                    duj = (ctx->rapl_max_range_uj - ctx->prev_rapl_uj) + cur_uj;
                }
                w = (double)duj / (dt_sec * 1e6);
            }
            ctx->prev_rapl_uj = cur_uj;
            ctx->has_prev_rapl = true;
            if (w >= 0.0 && w < 2000.0) return w;
        }
    }
    if (ctx->cpu_power_hwmon_path[0] != '\0') {
        uint64_t uw = 0;
        if (read_u64(ctx->cpu_power_hwmon_path, &uw)) {
            return (double)uw / 1e6;
        }
    }
    return 0.0;
}

static int sample_ram_load_pct(void) {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return 0;

    uint64_t total = 0, avail = 0, memfree = 0;
    bool has_avail = false;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        unsigned long long v = 0;
        if (sscanf(line, "MemTotal: %llu kB", &v) == 1) total = v;
        else if (sscanf(line, "MemAvailable: %llu kB", &v) == 1) {
            avail = v;
            has_avail = true;
        } else if (sscanf(line, "MemFree: %llu kB", &v) == 1) {
            memfree = v;
        }
    }
    fclose(fp);
    if (total == 0) return 0;
    if (!has_avail) avail = memfree;
    int avail_pct = (int)(((double)avail * 100.0) / (double)total);
    int used_pct = 100 - avail_pct;
    if (used_pct < 0) used_pct = 0;
    if (used_pct > 100) used_pct = 100;
    return used_pct;
}

static int sample_max_hwmon_temp_by_name(const char *driver_a, const char *driver_b) {
    DIR *dir = opendir("/sys/class/hwmon");
    if (!dir) return 0;
    int64_t max_mc = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, "hwmon", 5) != 0) continue;
        char npath[320], tpath[320], hname[64];
        snprintf(npath, sizeof(npath), "/sys/class/hwmon/%.255s/name", ent->d_name);
        if (!read_line_trimmed(npath, hname, sizeof(hname))) continue;
        if (strcmp(hname, driver_a) != 0 &&
            (driver_b == NULL || strcmp(hname, driver_b) != 0)) {
            continue;
        }
        snprintf(tpath, sizeof(tpath), "/sys/class/hwmon/%.255s/temp1_input", ent->d_name);
        int64_t mc = 0;
        if (read_i64(tpath, &mc) && mc > max_mc && mc < 200000) {
            max_mc = mc;
        }
    }
    closedir(dir);
    return (int)((max_mc + 500) / 1000);
}

static bool is_physical_block_name(const char *name) {
    if (strncmp(name, "nvme", 4) == 0) {
        return strchr(name, 'p') == NULL;
    }
    if (strncmp(name, "sd", 2) == 0 || strncmp(name, "vd", 2) == 0 ||
        strncmp(name, "hd", 2) == 0) {
        size_t l = strlen(name);
        return l >= 3 && !isdigit((unsigned char)name[l - 1]);
    }
    if (strncmp(name, "mmcblk", 6) == 0) {
        return strchr(name, 'p') == NULL;
    }
    return false;
}

static int sample_disk_load_pct(stm_sensor_ctx_t *ctx, double dt_sec) {
    FILE *fp = fopen("/proc/diskstats", "r");
    if (!fp) return 0;

    stm_disk_snap_t cur[STM_MAX_DISKS];
    size_t n = 0;
    int max_pct = 0;

    char line[512];
    while (fgets(line, sizeof(line), fp) && n < STM_MAX_DISKS) {
        unsigned int major = 0, minor = 0;
        char dev[32] = {0};
        unsigned long long r_ios, r_mrg, r_sec, r_ms, w_ios, w_mrg, w_sec, w_ms;
        unsigned long long in_flight, io_ms;
        int matched = sscanf(line,
                             "%u %u %31s %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                             &major, &minor, dev,
                             &r_ios, &r_mrg, &r_sec, &r_ms,
                             &w_ios, &w_mrg, &w_sec, &w_ms,
                             &in_flight, &io_ms);
        if (matched < 13 || !is_physical_block_name(dev)) continue;

        snprintf(cur[n].name, sizeof(cur[n].name), "%.63s", dev);
        cur[n].io_ticks_ms = (uint64_t)io_ms;
        cur[n].total_ios = (uint64_t)(r_ios + w_ios);
        cur[n].total_rw_ms = (uint64_t)(r_ms + w_ms);

        if (ctx->has_prev && dt_sec > 0.005) {
            for (size_t i = 0; i < ctx->prev_disk_count; i++) {
                if (strcmp(ctx->prev_disk[i].name, dev) == 0 &&
                    cur[n].io_ticks_ms >= ctx->prev_disk[i].io_ticks_ms) {
                    /* Guard against stuck in_flight counters on USB-SATA bridges where
                     * io_ticks climbs at 1000ms/s while zero I/Os complete. */
                    bool active = (in_flight == 0) ||
                                  (cur[n].total_ios > ctx->prev_disk[i].total_ios) ||
                                  (cur[n].total_rw_ms > ctx->prev_disk[i].total_rw_ms);
                    if (!active) break;

                    uint64_t dms = cur[n].io_ticks_ms - ctx->prev_disk[i].io_ticks_ms;
                    int pct = (int)(((double)dms * 100.0) / (dt_sec * 1000.0) + 0.5);
                    if (pct > 100) pct = 100;
                    if (pct > max_pct) max_pct = pct;
                    break;
                }
            }
        }
        n++;
    }
    fclose(fp);

    memcpy(ctx->prev_disk, cur, n * sizeof(cur[0]));
    ctx->prev_disk_count = n;
    return max_pct;
}

static void sample_net_speed(stm_sensor_ctx_t *ctx,
                             double dt_sec,
                             uint32_t *out_up_bps,
                             uint32_t *out_down_bps) {
    *out_up_bps = 0;
    *out_down_bps = 0;

    FILE *fp = fopen("/proc/net/dev", "r");
    if (!fp) return;

    stm_net_snap_t cur[STM_MAX_NET_IFACES];
    size_t n = 0;
    uint32_t max_up = 0, max_down = 0;

    char line[512];
    int line_idx = 0;
    while (fgets(line, sizeof(line), fp) && n < STM_MAX_NET_IFACES) {
        if (line_idx++ < 2) continue;
        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = '\0';

        char *ifname = line;
        while (*ifname && isspace((unsigned char)*ifname)) ifname++;
        if (strcmp(ifname, "lo") == 0) continue;

        unsigned long long rx_b = 0, rx_p, rx_e, rx_d, rx_f, rx_fr, rx_c, rx_m;
        unsigned long long tx_b = 0;
        if (sscanf(colon + 1, "%llu %llu %llu %llu %llu %llu %llu %llu %llu",
                   &rx_b, &rx_p, &rx_e, &rx_d, &rx_f, &rx_fr, &rx_c, &rx_m, &tx_b) < 9) {
            continue;
        }

        snprintf(cur[n].name, sizeof(cur[n].name), "%.63s", ifname);
        cur[n].rx_bytes = (uint64_t)rx_b;
        cur[n].tx_bytes = (uint64_t)tx_b;

        if (ctx->has_prev && dt_sec > 0.005) {
            for (size_t i = 0; i < ctx->prev_net_count; i++) {
                if (strcmp(ctx->prev_net[i].name, ifname) == 0) {
                    if (cur[n].rx_bytes >= ctx->prev_net[i].rx_bytes) {
                        double bps = (double)(cur[n].rx_bytes - ctx->prev_net[i].rx_bytes) / dt_sec;
                        uint32_t v = (bps > (double)UINT32_MAX) ? UINT32_MAX : (uint32_t)(bps + 0.5);
                        if (v > max_down) max_down = v;
                    }
                    if (cur[n].tx_bytes >= ctx->prev_net[i].tx_bytes) {
                        double bps = (double)(cur[n].tx_bytes - ctx->prev_net[i].tx_bytes) / dt_sec;
                        uint32_t v = (bps > (double)UINT32_MAX) ? UINT32_MAX : (uint32_t)(bps + 0.5);
                        if (v > max_up) max_up = v;
                    }
                    break;
                }
            }
        }
        n++;
    }
    fclose(fp);

    memcpy(ctx->prev_net, cur, n * sizeof(cur[0]));
    ctx->prev_net_count = n;
    *out_up_bps = max_up;
    *out_down_bps = max_down;
}

static void sample_gpu(const stm_sensor_ctx_t *ctx,
                       const stm_config_t *cfg,
                       stm_telemetry_t *out) {
    if (ctx->gpu_vendor == 0x10de && ctx->has_nvidia_smi) {
        FILE *np = popen(
            "nvidia-smi --query-gpu=temperature.gpu,utilization.gpu,"
            "clocks.current.graphics,fan.speed,power.draw "
            "--format=csv,noheader,nounits 2>/dev/null",
            "r");
        if (np) {
            char line[256] = {0};
            if (fgets(line, sizeof(line), np)) {
                double t = 0, u = 0, clk = 0, fan = 0, pwr = 0;
                if (sscanf(line, "%lf, %lf, %lf, %lf, %lf", &t, &u, &clk, &fan, &pwr) >= 3) {
                    out->gpu_temp_c = (int)(t + 0.5);
                    out->gpu_load_pct = (int)(u + 0.5);
                    out->gpu_clock_mhz = (uint16_t)(clk + 0.5);
                    out->gpu_fan = (uint16_t)(fan + 0.5);
                    if (!cfg->strict_windows_fields) {
                        out->gpu_watt = pwr;
                    }
                }
            }
            pclose(np);
            return;
        }
    }

    int64_t val = 0;
    if (ctx->gpu_busy_path[0] && read_i64(ctx->gpu_busy_path, &val)) {
        out->gpu_load_pct = (int)val;
    }
    if (ctx->gpu_temp_path[0] && read_i64(ctx->gpu_temp_path, &val)) {
        out->gpu_temp_c = (int)((val + 500) / 1000);
    }
    if (ctx->gpu_freq_path[0] && read_i64(ctx->gpu_freq_path, &val)) {
        out->gpu_clock_mhz = (uint16_t)(val / 1000000);
    }
    if (cfg->gpu_fan_mode == STM_GPU_FAN_PWM) {
        if (ctx->gpu_pwm_path[0] && read_i64(ctx->gpu_pwm_path, &val)) {
            out->gpu_fan = (uint16_t)((val * 100 + 127) / 255);
        }
    } else {
        if (ctx->gpu_fan_path[0] && read_i64(ctx->gpu_fan_path, &val) && val >= 0) {
            out->gpu_fan = (uint16_t)val;
        }
    }
    if (!cfg->strict_windows_fields) {
        if (ctx->gpu_volt_path[0] && read_i64(ctx->gpu_volt_path, &val) && val >= 0) {
            out->gpu_voltage_v = (double)val / 1000.0;
        }
        if (ctx->gpu_power_path[0] && read_i64(ctx->gpu_power_path, &val) && val >= 0) {
            out->gpu_watt = (double)val / 1e6;
        }
    }
}

void stm_sensors_sample(stm_sensor_ctx_t *ctx,
                        const stm_config_t *cfg,
                        stm_telemetry_t *out) {
    memset(out, 0, sizeof(*out));

    double now_mono = monotonic_sec();
    double dt_sec = ctx->has_prev ? (now_mono - ctx->prev_mono_sec) : 0.0;

    /* Wall-clock timestamp (matching .NET DateTime.Now + DayOfWeek 0=Sun..6=Sat) */
    struct timespec rt;
    clock_gettime(CLOCK_REALTIME, &rt);
    struct tm tm_now;
    localtime_r(&rt.tv_sec, &tm_now);
    out->year = tm_now.tm_year + 1900;
    out->month = tm_now.tm_mon + 1;
    out->day = tm_now.tm_mday;
    out->day_of_week = tm_now.tm_wday;
    out->hour = tm_now.tm_hour;
    out->minute = tm_now.tm_min;
    out->second = tm_now.tm_sec;
    out->centisecond = (int)(rt.tv_nsec / 10000000L);

    /* CPU */
    int64_t val = 0;
    if (ctx->cpu_temp_path[0] && read_i64(ctx->cpu_temp_path, &val)) {
        out->cpu_temp_c = (int)((val + 500) / 1000);
    }
    out->cpu_load_pct = sample_cpu_load(ctx);
    out->cpu_clock_mhz = sample_cpu_clock_mhz();
    out->cpu_watt = sample_cpu_watt(ctx, dt_sec);
    if (ctx->cpu_volt_path[0] && read_i64(ctx->cpu_volt_path, &val) && val >= 0) {
        out->cpu_voltage_v = (double)val / 1000.0;
    }
    if (ctx->cpu_fan_path[0] && read_i64(ctx->cpu_fan_path, &val) && val >= 0) {
        out->cpu_fan_rpm = (uint16_t)(val > 65535 ? 65535 : val);
    }

    /* GPU */
    sample_gpu(ctx, cfg, out);

    /* RAM & Disk */
    out->ram_load_pct = sample_ram_load_pct();
    out->disk_load_pct = sample_disk_load_pct(ctx, dt_sec);
    if (!cfg->strict_windows_fields) {
        out->ram_temp_c = sample_max_hwmon_temp_by_name("spd5118", "jc42");
        out->disk_temp_c = sample_max_hwmon_temp_by_name("nvme", "drivetemp");
    }

    /* Network */
    sample_net_speed(ctx, dt_sec, &out->net_up_bps, &out->net_down_bps);

    /* Display mode */
    out->display_mode = stm_encode_display_mode(cfg->display_mode, cfg->display_submode);

    ctx->prev_mono_sec = now_mono;
    ctx->has_prev = true;
}

void stm_sensors_sample_once(stm_sensor_ctx_t *ctx,
                             const stm_config_t *cfg,
                             stm_telemetry_t *out) {
    stm_sensors_sample(ctx, cfg, out);
    usleep(150000);
    stm_sensors_sample(ctx, cfg, out);
}

void stm_sensors_print_diagnostics(const stm_sensor_ctx_t *ctx,
                                   const stm_telemetry_t *t) {
    printf("System Identity:\n");
    printf("  OS        : %s\n", ctx->os_name);
    printf("  Mainboard : %s\n", ctx->board_name);
    printf("  CPU       : %s\n", ctx->cpu_name);
    printf("  GPU       : %s (vendor=0x%04x)\n",
           ctx->gpu_name[0] ? ctx->gpu_name : "None", ctx->gpu_vendor);
    printf("  RAM Total : %.1f GiB\n", (double)ctx->mem_total_kb / (1024.0 * 1024.0));
    printf("\nSensor Sources:\n");
    printf("  cpu_temp  : %s\n", ctx->cpu_temp_path[0] ? ctx->cpu_temp_path : "(none)");
    printf("  cpu_volt  : %s\n", ctx->cpu_volt_path[0] ? ctx->cpu_volt_path : "(none)");
    printf("  cpu_fan   : %s\n", ctx->cpu_fan_path[0] ? ctx->cpu_fan_path : "(none)");
    printf("  cpu_power : %s\n",
           ctx->rapl_energy_path[0] ? ctx->rapl_energy_path
                                    : (ctx->cpu_power_hwmon_path[0] ? ctx->cpu_power_hwmon_path
                                                                   : "(none/needs root for RAPL)"));
    printf("  gpu_temp  : %s\n", ctx->gpu_temp_path[0] ? ctx->gpu_temp_path : "(none)");
    printf("  gpu_busy  : %s\n", ctx->gpu_busy_path[0] ? ctx->gpu_busy_path : "(none)");
    printf("\nLive Telemetry:\n");
    printf("  Time      : %04d-%02d-%02d (wday=%d) %02d:%02d:%02d.%02d\n",
           t->year, t->month, t->day, t->day_of_week,
           t->hour, t->minute, t->second, t->centisecond);
    printf("  CPU       : %d C | %d%% | %u MHz | %.2f V | %.1f W | Fan %u RPM\n",
           t->cpu_temp_c, t->cpu_load_pct, t->cpu_clock_mhz,
           t->cpu_voltage_v, t->cpu_watt, t->cpu_fan_rpm);
    printf("  GPU       : %d C | %d%% | %u MHz | %.2f V | %.1f W | Fan %u\n",
           t->gpu_temp_c, t->gpu_load_pct, t->gpu_clock_mhz,
           t->gpu_voltage_v, t->gpu_watt, t->gpu_fan);
    printf("  RAM / Disk: RAM %d%% (%d C) | Disk %d%% (%d C)\n",
           t->ram_load_pct, t->ram_temp_c, t->disk_load_pct, t->disk_temp_c);
    printf("  Network   : UP %u B/s | DOWN %u B/s\n",
           t->net_up_bps, t->net_down_bps);
    printf("  Mode Byte : 0x%02x\n", t->display_mode);
}

size_t stm_sensors_format_rundata_json(const stm_sensor_ctx_t *ctx,
                                       const stm_telemetry_t *t,
                                       char *buf,
                                       size_t buf_sz) {
    double total_gb = (double)ctx->mem_total_kb / (1024.0 * 1024.0);
    double used_gb = total_gb * ((double)t->ram_load_pct / 100.0);
    double avail_gb = total_gb - used_gb;

    int n = snprintf(
        buf, buf_sz,
        "[{\"type\":2,\"data\":[{\"name\":\"packge\",\"temperature\":\"%d.00\","
        "\"load\":\"%d.00\",\"clock\":\"%u.00\",\"powers\":%.2f,\"voltage\":%.3f}]},"
        "{\"type\":3,\"data\":[{\"name\":\"%s\",\"temperature\":%d,"
        "\"load\":%d,\"clock\":%u,\"fan\":%u}]},"
        "{\"type\":4,\"data\":[{\"Used Memory\":%.2f,\"Available Memory\":%.2f,\"Memory\":%.2f}]},"
        "{\"type\":7,\"data\":[{\"load\":%d}]},"
        "{\"type\":8,\"data\":[{\"name\":\"CPU Fan\",\"speed\":%u}]},"
        "{\"type\":9,\"data\":[{\"name\":\"net\",\"ReceivedPerSec\":%u,\"SentPerSec\":%u}]}]",
        t->cpu_temp_c, t->cpu_load_pct, t->cpu_clock_mhz, t->cpu_watt, t->cpu_voltage_v,
        ctx->gpu_name[0] ? ctx->gpu_name : "GPU",
        t->gpu_temp_c, t->gpu_load_pct, t->gpu_clock_mhz, t->gpu_fan,
        used_gb, avail_gb, total_gb,
        t->disk_load_pct,
        t->cpu_fan_rpm,
        t->net_down_bps, t->net_up_bps);

    return (n > 0 && (size_t)n < buf_sz) ? (size_t)n : (buf_sz > 0 ? strlen(buf) : 0);
}

size_t stm_sensors_format_staticdata_json(const stm_sensor_ctx_t *ctx,
                                          char *buf,
                                          size_t buf_sz) {
    double total_gb = (double)ctx->mem_total_kb / (1024.0 * 1024.0);
    int n = snprintf(
        buf, buf_sz,
        "[{\"type\":0,\"data\":[{\"Caption\":\"%s\"}]},"
        "{\"type\":1,\"data\":[{\"Product\":\"%s\"}]},"
        "{\"type\":2,\"data\":[{\"Name\":\"%s\"}]},"
        "{\"type\":3,\"data\":[{\"Caption\":\"%s\"}]},"
        "{\"type\":4,\"data\":[{\"CapacityGB\":%.2f}]}]",
        ctx->os_name, ctx->board_name, ctx->cpu_name,
        ctx->gpu_name[0] ? ctx->gpu_name : "None", total_gb);
    return (n > 0 && (size_t)n < buf_sz) ? (size_t)n : (buf_sz > 0 ? strlen(buf) : 0);
}
