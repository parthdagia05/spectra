#ifndef SPECTRA_PROCESS_H
#define SPECTRA_PROCESS_H

#include <sys/types.h>

typedef struct {
    int uid;
    pid_t pid;
    char app_name[256];
    char package[256];
    char install_source[128];
    char permissions[2048];
    char first_install[64];
    int is_system_app;
} spectra_process_info_t;

int process_get_info_android(const char *package, spectra_process_info_t *out);

#endif
