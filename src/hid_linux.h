#ifndef STM_HID_LINUX_H
#define STM_HID_LINUX_H

#include "protocol.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STM_MAX_HID_DEVICES 20

typedef struct {
    char devnode[256];      /* e.g. /dev/hidraw7 */
    char hid_name[256];     /* e.g. HWCX USB Display */
    char hid_phys[256];     /* e.g. usb-0000:0c:00.0-11/input0 */
    uint16_t vid;
    uint16_t pid;
    const stm_vidpid_t *matched;
} stm_hid_dev_info_t;

typedef struct {
    int fd;
    stm_hid_dev_info_t info;
    bool online;
} stm_hid_conn_t;

size_t stm_hid_scan_devices(stm_hid_dev_info_t *out_devs, size_t max_devs);

/*
 * Opens the device node, performs the 0x01 challenge-response handshake, and
 * populates conn->fd and conn->online. Returns 0 on success or -errno / -1 on failure.
 */
int stm_hid_open_and_handshake(stm_hid_conn_t *conn,
                               const stm_hid_dev_info_t *info,
                               int timeout_ms,
                               bool verbose);

void stm_hid_close(stm_hid_conn_t *conn);

/*
 * Sends a 65-byte report (0x00 + 64-byte payload). Returns 0 on success or
 * negative errno on failure.
 */
int stm_hid_write_report(stm_hid_conn_t *conn,
                         const uint8_t report[STM_REPORT_BYTES]);

#endif /* STM_HID_LINUX_H */
