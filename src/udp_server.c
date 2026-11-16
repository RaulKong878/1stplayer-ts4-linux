#include "udp_server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static const uint8_t k_frame_marker[4] = {0xFF, 0xFE, 0xFF, 0xFD};

static size_t encode_udp_frame(uint8_t cmd_type,
                               uint16_t cmd_no,
                               const char *cmd,
                               const char *data,
                               uint8_t *out,
                               size_t out_sz) {
    const char *safe_data = (data && data[0] != '\0') ? data : " ";
    size_t cmd_len = strlen(cmd);
    size_t data_len = strlen(safe_data);
    size_t body_len = cmd_len + 1 + data_len;
    size_t total = 4 + 3 + body_len + 4;
    if (total > out_sz) return 0;

    size_t pos = 0;
    memcpy(out + pos, k_frame_marker, 4);
    pos += 4;
    out[pos++] = cmd_type;
    out[pos++] = (uint8_t)((cmd_no >> 8) & 0xFFu);
    out[pos++] = (uint8_t)(cmd_no & 0xFFu);
    memcpy(out + pos, cmd, cmd_len);
    pos += cmd_len;
    out[pos++] = '|';
    memcpy(out + pos, safe_data, data_len);
    pos += data_len;
    memcpy(out + pos, k_frame_marker, 4);
    pos += 4;
    return pos;
}

static bool decode_udp_frame(const uint8_t *buf,
                             size_t len,
                             uint8_t *out_type,
                             uint16_t *out_no,
                             char *out_cmd,
                             size_t out_cmd_sz,
                             char *out_data,
                             size_t out_data_sz) {
    if (len < 13) return false;
    if (memcmp(buf, k_frame_marker, 4) != 0) return false;
    if (memcmp(buf + len - 4, k_frame_marker, 4) != 0) return false;

    *out_type = buf[4];
    *out_no = (uint16_t)(((uint16_t)buf[5] << 8) | buf[6]);

    const char *body = (const char *)(buf + 7);
    size_t body_len = len - 11;
    const char *sep = memchr(body, '|', body_len);
    if (!sep || (size_t)(sep - body) < 2) return false;

    size_t clen = (size_t)(sep - body);
    size_t dlen = body_len - clen - 1;
    if (clen >= out_cmd_sz) clen = out_cmd_sz - 1;
    if (dlen >= out_data_sz) dlen = out_data_sz - 1;

    memcpy(out_cmd, body, clen);
    out_cmd[clen] = '\0';
    memcpy(out_data, sep + 1, dlen);
    out_data[dlen] = '\0';
    return true;
}

static void *udp_worker(void *arg) {
    stm_udp_ctx_t *ctx = (stm_udp_ctx_t *)arg;
    uint8_t rx_buf[4096];
    uint8_t tx_buf[16384];
    char json_buf[14000];

    while (ctx->running) {
        struct pollfd pfd = {.fd = ctx->sock_fd, .events = POLLIN, .revents = 0};
        int pr = poll(&pfd, 1, 200);
        if (pr <= 0 || !(pfd.revents & POLLIN)) continue;

        struct sockaddr_in peer;
        socklen_t peer_len = sizeof(peer);
        ssize_t n = recvfrom(ctx->sock_fd, rx_buf, sizeof(rx_buf), 0,
                             (struct sockaddr *)&peer, &peer_len);
        if (n <= 0) continue;

        uint8_t cmd_type = 0;
        uint16_t cmd_no = 0;
        char cmd[64];
        char data[512];
        if (!decode_udp_frame(rx_buf, (size_t)n, &cmd_type, &cmd_no,
                              cmd, sizeof(cmd), data, sizeof(data))) {
            continue;
        }

        json_buf[0] = '\0';
        bool should_reply = false;

        if (strcmp(cmd, "rundata") == 0) {
            pthread_mutex_lock(&ctx->lock);
            stm_telemetry_t snap = ctx->latest_telemetry;
            stm_sensors_format_rundata_json(ctx->sensors, &snap, json_buf, sizeof(json_buf));
            pthread_mutex_unlock(&ctx->lock);
            should_reply = true;
        } else if (strcmp(cmd, "staticdata") == 0) {
            pthread_mutex_lock(&ctx->lock);
            stm_sensors_format_staticdata_json(ctx->sensors, json_buf, sizeof(json_buf));
            pthread_mutex_unlock(&ctx->lock);
            should_reply = true;
        } else if (strcmp(cmd, "open") == 0) {
            if (ctx->pause_sending) *ctx->pause_sending = false;
            snprintf(json_buf, sizeof(json_buf), "{\"status\":\"open\"}");
            should_reply = true;
        } else if (strcmp(cmd, "stop") == 0) {
            if (ctx->pause_sending) *ctx->pause_sending = true;
            snprintf(json_buf, sizeof(json_buf), "{\"status\":\"stopped\"}");
            should_reply = true;
        } else if (strcmp(cmd, "status") == 0) {
            pthread_mutex_lock(&ctx->lock);
            size_t online = ctx->online_devices;
            stm_telemetry_t snap = ctx->latest_telemetry;
            int mode = ctx->cfg->display_mode;
            unsigned int sub = ctx->cfg->display_submode;
            pthread_mutex_unlock(&ctx->lock);
            snprintf(json_buf, sizeof(json_buf),
                     "{\"online_devices\":%zu,\"paused\":%s,\"display_mode\":%d,"
                     "\"display_submode\":%u,\"mode_byte\":%u,\"cpu_temp\":%d,"
                     "\"gpu_temp\":%d,\"cpu_fan_rpm\":%u}",
                     online,
                     (ctx->pause_sending && *ctx->pause_sending) ? "true" : "false",
                     mode, sub, (unsigned int)snap.display_mode,
                     snap.cpu_temp_c, snap.gpu_temp_c, snap.cpu_fan_rpm);
            should_reply = true;
        } else if (strcmp(cmd, "setmode") == 0) {
            int m = 1, s = 0;
            if (sscanf(data, "%d,%d", &m, &s) >= 1) {
                pthread_mutex_lock(&ctx->lock);
                ctx->cfg->display_mode = m;
                if (s >= 0 && s <= 255) ctx->cfg->display_submode = (uint8_t)s;
                pthread_mutex_unlock(&ctx->lock);
                snprintf(json_buf, sizeof(json_buf),
                         "{\"display_mode\":%d,\"display_submode\":%d}", m, s);
                should_reply = true;
            }
        } else if (strcmp(cmd, "reload") == 0) {
            if (ctx->reload_requested) *ctx->reload_requested = true;
            snprintf(json_buf, sizeof(json_buf), "{\"reload\":true}");
            should_reply = true;
        }

        if (should_reply) {
            size_t tlen = encode_udp_frame(1, cmd_no, cmd, json_buf,
                                           tx_buf, sizeof(tx_buf));
            if (tlen > 0) {
                sendto(ctx->sock_fd, tx_buf, tlen, 0,
                       (const struct sockaddr *)&peer, peer_len);
            }
        }
    }
    return NULL;
}

int stm_udp_start(stm_udp_ctx_t *ctx,
                  stm_config_t *cfg,
                  const stm_sensor_ctx_t *sensors,
                  volatile bool *pause_sending,
                  volatile bool *reload_requested) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->sock_fd = -1;
    pthread_mutex_init(&ctx->lock, NULL);

    ctx->cfg = cfg;
    ctx->sensors = sensors;
    ctx->pause_sending = pause_sending;
    ctx->reload_requested = reload_requested;

    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -errno;

    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)cfg->local_port);
    if (inet_pton(AF_INET, cfg->bind_address, &addr.sin_addr) != 1) {
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    }

    if (bind(fd, (const struct sockaddr *)&addr, sizeof(addr)) < 0) {
        int e = -errno;
        close(fd);
        return e;
    }

    ctx->sock_fd = fd;
    ctx->running = true;
    if (pthread_create(&ctx->thread, NULL, udp_worker, ctx) != 0) {
        close(fd);
        ctx->sock_fd = -1;
        ctx->running = false;
        return -errno;
    }
    ctx->started = true;
    return 0;
}

void stm_udp_update_snapshot(stm_udp_ctx_t *ctx,
                             const stm_telemetry_t *t,
                             size_t online_devices) {
    if (!ctx || !ctx->started) return;
    pthread_mutex_lock(&ctx->lock);
    ctx->latest_telemetry = *t;
    ctx->online_devices = online_devices;
    pthread_mutex_unlock(&ctx->lock);
}

void stm_udp_stop(stm_udp_ctx_t *ctx) {
    if (!ctx) return;
    if (ctx->started) {
        ctx->running = false;
        pthread_join(ctx->thread, NULL);
        ctx->started = false;
    }
    if (ctx->sock_fd >= 0) {
        close(ctx->sock_fd);
        ctx->sock_fd = -1;
    }
    pthread_mutex_destroy(&ctx->lock);
}

int stm_udp_client_query(const char *host,
                         int port,
                         const char *cmd,
                         const char *data,
                         char *out_cmd,
                         size_t out_cmd_sz,
                         char *out_data,
                         size_t out_data_sz,
                         int timeout_ms) {
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -errno;

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &dst.sin_addr) != 1) {
        close(fd);
        return -EINVAL;
    }

    uint8_t tx[1024];
    size_t tx_len = encode_udp_frame(0, 1, cmd, data, tx, sizeof(tx));
    if (tx_len == 0) {
        close(fd);
        return -EMSGSIZE;
    }

    if (sendto(fd, tx, tx_len, 0, (const struct sockaddr *)&dst, sizeof(dst)) < 0) {
        int e = -errno;
        close(fd);
        return e;
    }

    struct pollfd pfd = {.fd = fd, .events = POLLIN, .revents = 0};
    int pr = poll(&pfd, 1, timeout_ms);
    if (pr <= 0) {
        close(fd);
        return pr == 0 ? -ETIMEDOUT : -errno;
    }

    uint8_t rx[16384];
    ssize_t rn = recvfrom(fd, rx, sizeof(rx), 0, NULL, NULL);
    close(fd);
    if (rn < 0) return -errno;

    uint8_t rtype = 0;
    uint16_t rno = 0;
    if (!decode_udp_frame(rx, (size_t)rn, &rtype, &rno,
                          out_cmd, out_cmd_sz, out_data, out_data_sz)) {
        return -EBADMSG;
    }
    return 0;
}
