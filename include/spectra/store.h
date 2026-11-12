#ifndef SPECTRA_STORE_H
#define SPECTRA_STORE_H

#include "report.h"

int store_init(void);
int store_save_scan(const char *device_id, const char *device_name,
                    const char *report_json);
int store_list_scans(const char *device_filter);
int store_get_scan_json(int id, char *buf, size_t bufsize);
void store_close(void);

#endif
