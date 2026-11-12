#include "spectra/platform.h"
#include <stdio.h>
#include <string.h>

int platform_scan_sensors(spectra_sensor_event_t *out, int max)
{
    (void)out; (void)max;
    fprintf(stderr, "Sensor scanning not implemented on Windows.\n");
    return 0;
}

int platform_scan_network(spectra_net_event_t *out, int max)
{
    (void)out; (void)max;
    fprintf(stderr, "Network scanning not implemented on Windows.\n");
    return 0;
}

int platform_get_process_info(const char *package, spectra_process_info_t *out)
{
    (void)package;
    memset(out, 0, sizeof(*out));
    fprintf(stderr, "Process info not implemented on Windows.\n");
    return -1;
}

int platform_is_known_package(const char *pkg_or_path)
{
    (void)pkg_or_path;
    return 0;
}

const char *platform_name(void)
{
    return "windows";
}

int platform_check_connection(void)
{
    return 0;
}
