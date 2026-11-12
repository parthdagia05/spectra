#include "spectra/process.h"
#include "spectra/platform.h"

int process_get_info_android(const char *package, spectra_process_info_t *out)
{
    return platform_get_process_info(package, out);
}
