#ifndef SPECTRA_EVENT_H
#define SPECTRA_EVENT_H

#include <stdint.h>
#include <time.h>
#include <sys/types.h>
#include <netinet/in.h>

#define SPECTRA_MAX_EVENTS 256

typedef struct {
    int uid;
    pid_t pid;
    char app_name[256];
    char package[256];
    char device[64];
    char sensor_type[16];
    time_t last_access;
    int duration_ms;
    int currently_active;
    int background;
    int access_count;      /* accesses merged into this event (0 = 1) */
    time_t detected_at;
} spectra_sensor_event_t;

typedef struct {
    int uid;
    pid_t pid;
    char app_name[256];
    char package[256];
    char remote_ip[INET6_ADDRSTRLEN];
    char remote_host[256];
    uint16_t remote_port;
    uint64_t bytes_sent;
    uint64_t bytes_recv;
    char interface[16];
    time_t detected_at;
} spectra_net_event_t;

typedef struct {
    spectra_sensor_event_t sensor;
    spectra_net_event_t net;
    int risk_score;
    char explanation[512];
    char verdict[16];       /* expected | low | review | high */
    char category[24];      /* system, messaging, social, commerce, ... */
    char reasons[1024];     /* one per line: "+25 Location read in the background" */
    char action[256];       /* what the user should do about it */
} spectra_correlation_t;

#endif
