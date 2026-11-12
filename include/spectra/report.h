#ifndef SPECTRA_REPORT_H
#define SPECTRA_REPORT_H

#include "event.h"
#include "process.h"

typedef struct {
    char device_model[128];
    char device_name[128];
    char android_version[32];
    int api_level;
    char connection_type[16];
    int has_root;
} spectra_device_info_t;

typedef struct {
    spectra_device_info_t device;
    spectra_sensor_event_t *sensors;
    int n_sensors;
    spectra_net_event_t *net;
    int n_net;
    spectra_correlation_t *correlations;
    int n_correlations;
    spectra_process_info_t *apps;
    int n_apps;
    double scan_duration_s;
} spectra_report_t;

void report_print_terminal(const spectra_report_t *r);
int report_write_json(const spectra_report_t *r, const char *path);
int report_write_html(const spectra_report_t *r, const char *path);

#endif
