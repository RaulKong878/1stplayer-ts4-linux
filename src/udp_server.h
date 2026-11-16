#ifndef STM_UDP_SERVER_H
#define STM_UDP_SERVER_H

#include "config.h"
#include "sensors.h"

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int sock_fd;
    pthread_t thread;
    bool running;
    bool started;

    pthread_mutex_t lock;
    stm_config_t *cfg;
    const stm_sensor_ctx_t *sensors;
    stm_telemetry_t latest_telemetry;
    size_t online_devices;
    volatile bool *pause_sending;
    volatile bool *reload_requested;
} stm_udp_ctx_t;

int stm_udp_start(stm_udp_ctx_t *ctx,
                  stm_config_t *cfg,
                  const stm_sensor_ctx_t *sensors,
                  volatile bool *pause_sending,
                  volatile bool *reload_requested);

void stm_udp_update_snapshot(stm_udp_ctx_t *ctx,
                             const stm_telemetry_t *t,
                             size_t online_devices);

void stm_udp_stop(stm_udp_ctx_t *ctx);

/* Client helper used by stmctl to query a running daemon via XKW UDP framing. */
int stm_udp_client_query(const char *host,
                         int port,
                         const char *cmd,
                         const char *data,
                         char *out_cmd,
                         size_t out_cmd_sz,
                         char *out_data,
                         size_t out_data_sz,
                         int timeout_ms);

#endif /* STM_UDP_SERVER_H */
