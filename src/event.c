#include "spectra/event.h"
#include <string.h>

void sensor_event_init(spectra_sensor_event_t *ev)
{
    memset(ev, 0, sizeof(*ev));
}

void net_event_init(spectra_net_event_t *ev)
{
    memset(ev, 0, sizeof(*ev));
}
