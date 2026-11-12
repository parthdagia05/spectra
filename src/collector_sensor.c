#include "spectra/platform.h"

int collector_scan_sensors(spectra_sensor_event_t *out, int max)
{
    return platform_scan_sensors(out, max);
}
