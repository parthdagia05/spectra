#ifndef SPECTRA_PLATFORM_H
#define SPECTRA_PLATFORM_H

#include "event.h"
#include "process.h"

int platform_scan_sensors(spectra_sensor_event_t *out, int max);
int platform_scan_network(spectra_net_event_t *out, int max);
int platform_get_process_info(const char *package, spectra_process_info_t *out);
int platform_is_known_package(const char *pkg_or_path);
const char *platform_name(void);
int platform_check_connection(void);

#endif
