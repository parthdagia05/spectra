#ifndef SPECTRA_CORRELATOR_H
#define SPECTRA_CORRELATOR_H

#include "event.h"

#include "process.h"

int spectra_correlate(
    spectra_sensor_event_t *sensors, int n_sensors,
    spectra_net_event_t *net, int n_net,
    spectra_correlation_t *results, int max_results
);

/* same, with app details (installer, permissions, system flag) for better
 * weighting; apps may be NULL */
int spectra_correlate_ex(
    spectra_sensor_event_t *sensors, int n_sensors,
    spectra_net_event_t *net, int n_net,
    spectra_process_info_t *apps, int n_apps,
    spectra_correlation_t *results, int max_results
);

/* category label for a package ("system", "messaging", "unknown", ...) */
const char *spectra_app_category(const char *package, const spectra_process_info_t *info);

#endif
