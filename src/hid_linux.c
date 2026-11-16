#include "hid_linux.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool parse_hidraw_uevent(const char *sys_name, stm_hid_dev_info_t *info) {
    char path[512];
    snprintf(path, sizeof(path), "/sys/class/hidraw/%s/device/uevent", sys_name);
    FILE *fp = fopen(path, "r");
    if (!fp) return false;

    memset(info, 0, sizeof(*info));
    snprintf(info->devnode, sizeof(info->devnode), "/dev/%.240s", sys_name);

    bool has_id = false;
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (strncmp(line, "HID_ID=", 7) == 0) {
            unsigned int bus = 0, vid = 0, pid = 0;
            if (sscanf(line + 7, "%x:%x:%x", &bus, &vid, &pid) == 3) {
                info->vid = (uint16_t)vid;
                info->pid = (uint16_t)pid;
                has_id = true;
            }
        } else if (strncmp(line, "HID_NAME=", 9) == 0) {
            snprintf(info->hid_name, sizeof(info->hid_name), "%.255s", line + 9);
        } else if (strncmp(line, "HID_PHYS=", 9) == 0) {
            snprintf(info->hid_phys, sizeof(info->hid_phys), "%.255s", line + 9);
        }
    }
    fclose(fp);
    if (!has_id) return false;

    info->matched = stm_match_vidpid(info->vid, info->pid);
    return info->matched != NULL;
}

static int cmp_devnode(const void *a, const void *b) {
    const stm_hid_dev_info_t *da = (const stm_hid_dev_info_t *)a;
    const stm_hid_dev_info_t *db = (const stm_hid_dev_info_t *)b;
    return strcmp(da->devnode, db->devnode);
}

size_t stm_hid_scan_devices(stm_hid_dev_info_t *out_devs, size_t max_devs) {
    DIR *dir = opendir("/sys/class/hidraw");
    if (!dir) return 0;

    size_t count = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && count < max_devs) {
        if (strncmp(ent->d_name, "hidraw", 6) != 0) continue;
        stm_hid_dev_info_t info;
        if (parse_hidraw_uevent(ent->d_name, &info)) {
            out_devs[count++] = info;
        }
    }
    closedir(dir);

    if (count > 1) {
        qsort(out_devs, count, sizeof(out_devs[0]), cmp_devnode);
    }
    return count;
}

static void drain_input(int fd) {
    uint8_t trash[128];
    struct pollfd pfd = {.fd = fd, .events = POLLIN, .revents = 0};
    while (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
        ssize_t n = read(fd, trash, sizeof(trash));
        if (n <= 0) break;
        pfd.revents = 0;
    }
}

static int write_full_report(int fd, const uint8_t report[STM_REPORT_BYTES]) {
    size_t off = 0;
    while (off < STM_REPORT_BYTES) {
        ssize_t w = write(fd, report + off, STM_REPORT_BYTES - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -errno;
        }
        if (w == 0) return -EIO;
        off += (size_t)w;
    }
    return 0;
}

static int read_reply_payload(int fd, uint8_t out_payload[STM_PAYLOAD_BYTES], int timeout_ms) {
    struct pollfd pfd = {.fd = fd, .events = POLLIN, .revents = 0};
    int pr;
    do {
        pr = poll(&pfd, 1, timeout_ms);
    } while (pr < 0 && errno == EINTR);

    if (pr < 0) return -errno;
    if (pr == 0) return -ETIMEDOUT;
    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) return -EIO;

    uint8_t buf[STM_REPORT_BYTES];
    ssize_t r;
    do {
        r = read(fd, buf, sizeof(buf));
    } while (r < 0 && errno == EINTR);

    if (r < 0) return -errno;
    if (r == STM_PAYLOAD_BYTES) {
        memcpy(out_payload, buf, STM_PAYLOAD_BYTES);
        return 0;
    }
    if (r == STM_REPORT_BYTES && buf[0] == 0x00) {
        memcpy(out_payload, buf + 1, STM_PAYLOAD_BYTES);
        return 0;
    }
    return -EBADMSG;
}

int stm_hid_open_and_handshake(stm_hid_conn_t *conn,
                               const stm_hid_dev_info_t *info,
                               int timeout_ms,
                               bool verbose) {
    memset(conn, 0, sizeof(*conn));
    conn->fd = -1;
    conn->info = *info;

    int fd = open(info->devnode, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        return -errno;
    }

    int last_err = -EPROTO;
    for (int attempt = 0; attempt < 3; attempt++) {
        drain_input(fd);

        uint8_t req[STM_REPORT_BYTES];
        uint16_t expected_key = 0;
        stm_build_handshake(req, &expected_key, NULL);

        int werr = write_full_report(fd, req);
        if (werr < 0) {
            last_err = werr;
            if (verbose) {
                fprintf(stderr, "[hid] write handshake to %s failed: %s\n",
                        info->devnode, strerror(-werr));
            }
            usleep(20000);
            continue;
        }

        uint8_t reply[STM_PAYLOAD_BYTES];
        int rerr = read_reply_payload(fd, reply, timeout_ms);
        if (rerr < 0) {
            last_err = rerr;
            if (verbose) {
                fprintf(stderr, "[hid] read handshake from %s (attempt %d) failed: %s\n",
                        info->devnode, attempt + 1, strerror(-rerr));
            }
            usleep(20000);
            continue;
        }

        if (stm_verify_handshake_reply(reply, expected_key)) {
            conn->fd = fd;
            conn->online = true;
            return 0;
        }

        last_err = -EBADMSG;
        if (verbose) {
            fprintf(stderr, "[hid] handshake challenge mismatch on %s (attempt %d)\n",
                    info->devnode, attempt + 1);
        }
        usleep(20000);
    }

    close(fd);
    return last_err;
}

void stm_hid_close(stm_hid_conn_t *conn) {
    if (conn->fd >= 0) {
        close(conn->fd);
        conn->fd = -1;
    }
    conn->online = false;
}

int stm_hid_write_report(stm_hid_conn_t *conn,
                         const uint8_t report[STM_REPORT_BYTES]) {
    if (!conn->online || conn->fd < 0) {
        return -ENOTCONN;
    }
    int err = write_full_report(conn->fd, report);
    if (err < 0) {
        conn->online = false;
    }
    return err;
}
