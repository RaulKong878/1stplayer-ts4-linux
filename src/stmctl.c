#include "config.h"
#include "hid_linux.h"
#include "protocol.h"
#include "sensors.h"
#include "udp_server.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(void) {
    printf("stmctl - Native controller for 1stPlayer TS4 / HWCX USB Display\n\n"
           "Usage:\n"
           "  stmctl status                    Show service, USB device, and live daemon status\n"
           "  stmctl sensors                   Show detected Linux sensors and live telemetry\n"
           "  stmctl probe                     Scan /dev/hidraw* and test 0x01 USB handshake\n"
           "  stmctl send-once                 Send one live telemetry frame immediately\n"
           "  stmctl mode <mode> [submode]     Set display mode/submode (live + config.ini)\n"
           "  stmctl pause                     Pause USB screen updates via UDP\n"
           "  stmctl resume                    Resume USB screen updates via UDP\n"
           "  stmctl udp <rundata|staticdata>  Query daemon UDP port (default 8890)\n");
}

static int cmd_probe(void) {
    stm_hid_dev_info_t devs[STM_MAX_HID_DEVICES];
    size_t n = stm_hid_scan_devices(devs, STM_MAX_HID_DEVICES);
    if (n == 0) {
        printf("No supported USB display HID devices found in /sys/class/hidraw.\n");
        return 1;
    }

    int ok_count = 0;
    for (size_t i = 0; i < n; i++) {
        printf("Found %s: %04x:%04x (%s) [%s]\n",
               devs[i].devnode, devs[i].vid, devs[i].pid,
               devs[i].matched ? devs[i].matched->device_name : devs[i].hid_name,
               devs[i].hid_phys);
        stm_hid_conn_t conn;
        int err = stm_hid_open_and_handshake(&conn, &devs[i], 1000, true);
        if (err == 0) {
            printf("  -> Handshake 0x01 OK!\n");
            stm_hid_close(&conn);
            ok_count++;
        } else {
            printf("  -> Handshake failed: %s\n", strerror(-err));
        }
    }
    return ok_count > 0 ? 0 : 1;
}

static int cmd_status(const stm_config_t *cfg) {
    stm_hid_dev_info_t devs[STM_MAX_HID_DEVICES];
    size_t n = stm_hid_scan_devices(devs, STM_MAX_HID_DEVICES);
    printf("USB Display Hardware:\n");
    if (n == 0) {
        printf("  (none detected)\n");
    } else {
        for (size_t i = 0; i < n; i++) {
            printf("  %s -> %04x:%04x %s (%s)\n",
                   devs[i].devnode, devs[i].vid, devs[i].pid,
                   devs[i].hid_name,
                   devs[i].matched ? devs[i].matched->profile_name : "unknown");
        }
    }

    char rcmd[64] = {0}, rdata[4096] = {0};
    int err = stm_udp_client_query(cfg->bind_address, cfg->local_port,
                                   "status", "", rcmd, sizeof(rcmd),
                                   rdata, sizeof(rdata), 1000);
    printf("\nDaemon Status (%s:%d):\n", cfg->bind_address, cfg->local_port);
    if (err == 0) {
        printf("  %s\n", rdata);
    } else {
        printf("  Not reachable (%s). Check: systemctl status system-temperature-monitoring\n",
               strerror(-err));
    }
    return 0;
}

static int cmd_sensors(const stm_config_t *cfg) {
    stm_sensor_ctx_t sensors;
    stm_sensors_init(&sensors, cfg);
    stm_telemetry_t t;
    stm_sensors_sample_once(&sensors, cfg, &t);
    stm_sensors_print_diagnostics(&sensors, &t);
    return 0;
}

static int cmd_send_once(const stm_config_t *cfg) {
    stm_hid_dev_info_t devs[STM_MAX_HID_DEVICES];
    size_t n = stm_hid_scan_devices(devs, STM_MAX_HID_DEVICES);
    if (n == 0) {
        fprintf(stderr, "No compatible USB Display device found.\n");
        return 1;
    }
    stm_sensor_ctx_t sensors;
    stm_sensors_init(&sensors, cfg);
    stm_telemetry_t t;
    stm_sensors_sample_once(&sensors, cfg, &t);

    uint8_t pkt[STM_REPORT_BYTES];
    stm_build_telemetry(&t, pkt);

    int sent = 0;
    for (size_t i = 0; i < n; i++) {
        stm_hid_conn_t c;
        int err = stm_hid_open_and_handshake(&c, &devs[i], 1000, true);
        if (err < 0) {
            fprintf(stderr, "Handshake failed on %s: %s\n",
                    devs[i].devnode, strerror(-err));
            continue;
        }
        err = stm_hid_write_report(&c, pkt);
        stm_hid_close(&c);
        if (err == 0) {
            printf("Sent telemetry frame to %s.\n", devs[i].devnode);
            sent++;
        } else {
            fprintf(stderr, "Write failed on %s: %s\n",
                    devs[i].devnode, strerror(-err));
        }
    }
    return sent > 0 ? 0 : 1;
}

static int cmd_udp(const stm_config_t *cfg, const char *cmd, const char *arg) {
    char rcmd[64] = {0}, rdata[14000] = {0};
    int err = stm_udp_client_query(cfg->bind_address, cfg->local_port,
                                   cmd, arg ? arg : "",
                                   rcmd, sizeof(rcmd), rdata, sizeof(rdata), 1500);
    if (err < 0) {
        fprintf(stderr, "UDP query '%s' failed: %s\n", cmd, strerror(-err));
        return 1;
    }
    printf("%s\n", rdata);
    return 0;
}

static int cmd_set_mode(stm_config_t *cfg, const char *cfg_path, int mode, int submode) {
    cfg->display_mode = mode;
    cfg->display_submode = (uint8_t)submode;

    char arg[32];
    snprintf(arg, sizeof(arg), "%d,%d", mode, submode);
    char rcmd[64] = {0}, rdata[1024] = {0};
    int uerr = stm_udp_client_query(cfg->bind_address, cfg->local_port,
                                    "setmode", arg, rcmd, sizeof(rcmd),
                                    rdata, sizeof(rdata), 1000);
    if (uerr == 0) {
        printf("Live daemon updated: %s\n", rdata);
    } else {
        printf("Note: live daemon not reachable (%s)\n", strerror(-uerr));
    }

    if (cfg_path && stm_config_save(cfg, cfg_path)) {
        printf("Saved configuration to %s\n", cfg_path);
    } else {
        printf("Note: could not write %s (run with sudo to persist across reboots)\n",
               cfg_path ? cfg_path : "/etc/system-temperature-monitoring/config.ini");
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    stm_config_t cfg;
    stm_config_defaults(&cfg);
    const char *cfg_path = "/etc/system-temperature-monitoring/config.ini";
    if (!stm_config_load(&cfg, cfg_path)) {
        if (stm_config_load(&cfg, "./config.ini")) {
            cfg_path = "./config.ini";
        }
    }

    const char *sub = argv[1];
    if (strcmp(sub, "status") == 0) return cmd_status(&cfg);
    if (strcmp(sub, "sensors") == 0) return cmd_sensors(&cfg);
    if (strcmp(sub, "probe") == 0) return cmd_probe();
    if (strcmp(sub, "send-once") == 0) return cmd_send_once(&cfg);
    if (strcmp(sub, "pause") == 0) return cmd_udp(&cfg, "stop", "");
    if (strcmp(sub, "resume") == 0) return cmd_udp(&cfg, "open", "");
    if (strcmp(sub, "udp") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: stmctl udp <rundata|staticdata|open|stop|status>\n");
            return 2;
        }
        return cmd_udp(&cfg, argv[2], argc > 3 ? argv[3] : "");
    }
    if (strcmp(sub, "mode") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: stmctl mode <mode> [submode]\n");
            return 2;
        }
        int m = atoi(argv[2]);
        int s = (argc >= 4) ? atoi(argv[3]) : 0;
        return cmd_set_mode(&cfg, cfg_path, m, s);
    }

    print_usage();
    return 2;
}
