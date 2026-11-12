#include "spectra/platform.h"

int collector_scan_network(spectra_net_event_t *out, int max)
{
    return platform_scan_network(out, max);
}
