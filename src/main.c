#include "config.h"
#include "hid_linux.h"
#include "protocol.h"
#include "sensors.h"
#include "udp_server.h"

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop = 0;
static volatile bool g_reload = false;
static volatile bool g_pause = false;

static void on_sigterm(int sig) {
    (void)sig;
    g_stop = 1;
}

static void on_sighup(int sig) {
    (void)sig;
    g_reload = true;
}

static void log_msg(const stm_config_t *cfg, const char *fmt, ...) {
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    fprintf(stderr, "[%02d:%02d:%02d] %s\n",
            tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec, msg);
    fflush(stderr);

    if (cfg && cfg->is_log && cfg->log_path[0] != '\0') {
        FILE *fp = fopen(cfg->log_path, "a");
        if (fp) {
            fprintf(fp, "%04d-%02d-%02d %02d:%02d:%02d\t%s\n",
                    tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
                    tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec, msg);
            fclose(fp);
        }
    }
}

static void print_hex(const char *label, const uint8_t *buf, size_t len) {
    printf("%s (%zu bytes):", label, len);
    for (size_t i = 0; i < len; i++) {
        if (i % 16 == 0) printf("\n  %02zu: ", i);
        printf("%02x ", buf[i]);
    }
    printf("\n");
}

static size_t reconcile_devices(stm_hid_conn_t *conns,
                                size_t *conn_count,
                                const stm_config_t *cfg,
                                bool verbose) {
    stm_hid_dev_info_t found[STM_MAX_HID_DEVICES];
    size_t n_found = 0;

    if (strcmp(cfg->hidraw_device, "auto") != 0) {
        n_found = 1;
        memset(&found[0], 0, sizeof(found[0]));
        snprintf(found[0].devnode, sizeof(found[0].devnode), "%.255s", cfg->hidraw_device);
        snprintf(found[0].hid_name, sizeof(found[0].hid_name), "Configured HID Device");
    } else {
        n_found = stm_hid_scan_devices(found, STM_MAX_HID_DEVICES);
    }

    /* Drop any existing connections that are offline or missing */
    for (size_t i = 0; i < *conn_count;) {
        bool still_present = false;
        if (conns[i].online) {
            for (size_t j = 0; j < n_found; j++) {
                if (strcmp(conns[i].info.devnode, found[j].devnode) == 0) {
                    still_present = true;
                    break;
                }
            }
        }
        if (!still_present) {
            log_msg(cfg, "Device disconnected: %s", conns[i].info.devnode);
            stm_hid_close(&conns[i]);
            conns[i] = conns[*conn_count - 1];
            (*conn_count)--;
        } else {
            i++;
        }
    }

    /* Open newly appeared devices and verify 0x01 handshake */
    for (size_t j = 0; j < n_found && *conn_count < STM_MAX_HID_DEVICES; j++) {
        bool already = false;
        for (size_t i = 0; i < *conn_count; i++) {
            if (strcmp(conns[i].info.devnode, found[j].devnode) == 0) {
                already = true;
                break;
            }
        }
        if (already) continue;

        stm_hid_conn_t c;
        int err = stm_hid_open_and_handshake(&c, &found[j], 1000, verbose);
        if (err == 0) {
            conns[(*conn_count)++] = c;
            log_msg(cfg, "Connected %s (%04x:%04x %s)",
                    c.info.devnode, c.info.vid, c.info.pid,
                    c.info.matched ? c.info.matched->device_name : c.info.hid_name);
        } else if (verbose) {
            log_msg(cfg, "Handshake failed for %s: %s",
                    found[j].devnode, strerror(-err));
        }
    }

    return *conn_count;
}

static void usage(const char *prog) {
    printf("Usage: %s [options]\n"
           "  -c, --config <path>   Path to config.ini\n"
           "  -1, --once            Send one telemetry packet and exit\n"
           "  -n, --dry-run         Sample sensors and print HID reports without opening hardware\n"
           "  -m, --mode <int>      Override display mode (0 or 1)\n"
           "  -s, --submode <int>   Override display submode (0..255)\n"
           "  -v, --verbose         Verbose diagnostic logging\n"
           "  -h, --help            Show help\n",
           prog);
}

int main(int argc, char **argv) {
    const char *cfg_path = NULL;
    bool once = false;
    bool dry_run = false;
    bool verbose = false;
    int override_mode = -1;
    int override_submode = -1;

    static const struct option long_opts[] = {
        {"config", required_argument, NULL, 'c'},
        {"once", no_argument, NULL, '1'},
        {"dry-run", no_argument, NULL, 'n'},
        {"mode", required_argument, NULL, 'm'},
        {"submode", required_argument, NULL, 's'},
        {"verbose", no_argument, NULL, 'v'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "c:1nm:s:vh", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'c': cfg_path = optarg; break;
            case '1': once = true; break;
            case 'n': dry_run = true; break;
            case 'm': override_mode = atoi(optarg); break;
            case 's': override_submode = atoi(optarg); break;
            case 'v': verbose = true; break;
            case 'h': usage(argv[0]); return 0;
            default: usage(argv[0]); return 2;
        }
    }

    stm_config_t cfg;
    stm_config_defaults(&cfg);

    if (cfg_path) {
        if (!stm_config_load(&cfg, cfg_path) && verbose) {
            fprintf(stderr, "Warning: could not read %s, using defaults\n", cfg_path);
        }
    } else if (!stm_config_load(&cfg, "/etc/system-temperature-monitoring/config.ini")) {
        stm_config_load(&cfg, "./config.ini");
    }

    if (override_mode >= 0) cfg.display_mode = override_mode;
    if (override_submode >= 0) cfg.display_submode = (uint8_t)override_submode;

    stm_sensor_ctx_t sensors;
    stm_sensors_init(&sensors, &cfg);

    if (dry_run) {
        stm_telemetry_t t;
        stm_sensors_sample_once(&sensors, &cfg, &t);
        stm_sensors_print_diagnostics(&sensors, &t);

        uint8_t hs[STM_REPORT_BYTES];
        uint16_t exp_key = 0;
        stm_build_handshake(hs, &exp_key, NULL);
        printf("\nHandshake Expected Reply Key: 0x%04x\n", exp_key);
        print_hex("HID Out 0x01 (Handshake)", hs, sizeof(hs));

        uint8_t pkt[STM_REPORT_BYTES];
        stm_build_telemetry(&t, pkt);
        print_hex("HID Out 0x02 (Telemetry)", pkt, sizeof(pkt));
        return 0;
    }

    if (once) {
        stm_hid_conn_t conns[STM_MAX_HID_DEVICES];
        size_t n_conns = 0;
        if (reconcile_devices(conns, &n_conns, &cfg, true) == 0) {
            fprintf(stderr, "No compatible USB Display device accessible.\n");
            return 1;
        }
        stm_telemetry_t t;
        stm_sensors_sample_once(&sensors, &cfg, &t);
        uint8_t pkt[STM_REPORT_BYTES];
        stm_build_telemetry(&t, pkt);
        int rc = 0;
        for (size_t i = 0; i < n_conns; i++) {
            int w = stm_hid_write_report(&conns[i], pkt);
            if (w < 0) {
                fprintf(stderr, "Write failed on %s: %s\n",
                        conns[i].info.devnode, strerror(-w));
                rc = 1;
            } else {
                printf("Sent telemetry frame to %s (CPU %dC %d%% %uMHz, GPU %dC %d%%)\n",
                       conns[i].info.devnode, t.cpu_temp_c, t.cpu_load_pct,
                       t.cpu_clock_mhz, t.gpu_temp_c, t.gpu_load_pct);
            }
            stm_hid_close(&conns[i]);
        }
        return rc;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigterm;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sa.sa_handler = on_sighup;
    sigaction(SIGHUP, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    stm_udp_ctx_t udp;
    memset(&udp, 0, sizeof(udp));
    if (cfg.is_udp) {
        int uerr = stm_udp_start(&udp, &cfg, &sensors, &g_pause, &g_reload);
        if (uerr < 0) {
            log_msg(&cfg, "Warning: UDP bind %s:%d failed: %s",
                    cfg.bind_address, cfg.local_port, strerror(-uerr));
        } else {
            log_msg(&cfg, "UDP control server listening on %s:%d",
                    cfg.bind_address, cfg.local_port);
        }
    }

    stm_hid_conn_t conns[STM_MAX_HID_DEVICES];
    size_t n_conns = 0;
    log_msg(&cfg, "SystemTemperatureMonitoring native Linux daemon started (interval=%dms)",
            cfg.send_delay_ms);

    int rescan_tick = 0;
    while (!g_stop) {
        if (g_reload) {
            g_reload = false;
            const char *reload_target = cfg_path ? cfg_path
                                                 : "/etc/system-temperature-monitoring/config.ini";
            if (stm_config_load(&cfg, reload_target)) {
                log_msg(&cfg, "Reloaded configuration from %s", reload_target);
                stm_sensors_init(&sensors, &cfg);
            }
        }

        if (n_conns == 0 || (rescan_tick++ % 4 == 0)) {
            reconcile_devices(conns, &n_conns, &cfg, verbose);
        }

        stm_telemetry_t t;
        stm_sensors_sample(&sensors, &cfg, &t);

        if (!g_pause && n_conns > 0) {
            uint8_t pkt[STM_REPORT_BYTES];
            stm_build_telemetry(&t, pkt);
            for (size_t i = 0; i < n_conns; i++) {
                int w = stm_hid_write_report(&conns[i], pkt);
                if (w < 0) {
                    log_msg(&cfg, "Write error on %s (%s), will reconnect",
                            conns[i].info.devnode, strerror(-w));
                }
            }
        }

        stm_udp_update_snapshot(&udp, &t, n_conns);

        int sleep_ms = cfg.send_delay_ms;
        if (sleep_ms < 100) sleep_ms = 100;
        usleep((useconds_t)sleep_ms * 1000u);
    }

    log_msg(&cfg, "Shutting down...");
    for (size_t i = 0; i < n_conns; i++) {
        stm_hid_close(&conns[i]);
    }
    stm_udp_stop(&udp);
    return 0;
}
