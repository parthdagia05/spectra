#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <cjson/cJSON.h>

#include "spectra/event.h"
#include "spectra/platform.h"
#include "spectra/correlator.h"
#include "spectra/report.h"
#include "spectra/store.h"

extern int android_scan_sensors(spectra_sensor_event_t *out, int max);
extern int android_pick_device(char *serial, size_t size, int *unauthorized);
extern int android_scan_network(spectra_net_event_t *out, int max);
extern int android_get_process_info(const char *package, spectra_process_info_t *out);
extern int android_get_device_info(spectra_device_info_t *info);
extern int android_check_connection(void);

static void usage(void)
{
    fprintf(stderr,
        "Usage: spectra <command> [options]\n\n"
        "Commands:\n"
        "  scan              Scan for sensor access and network activity\n"
        "  history           List past scans\n"
        "  diff <id1> <id2>  Compare two scans\n"
        "  devices           List connected Android devices\n"
        "  allowlist         Manage domain allowlist\n"
        "  ui                Launch the dashboard (python3 tools/serve.py)\n"
        "\n"
        "Scan options:\n"
        "  --android         Scan connected Android phone (default: local machine)\n"
        "  --last <hours>    How far back to look (default: 24)\n"
        "  --json [file]     JSON output\n"
        "  --html <file>     HTML report\n"
        "  --live            Re-scan every 2 seconds\n"
        "  --device <serial> Phone to scan when several are plugged in (see: spectra devices)\n"
        "  --no-store        Do not save this scan to history (guest scans)\n"
        "\n"
        "History options:\n"
        "  --device <name>   Filter by device name\n"
        "\n");
}

static int cmd_scan(int argc, char **argv)
{
    int android_mode = 0;
    int last_hours = 24;
    int json_mode = 0;
    int live_mode = 0;
    int no_store = 0;
    const char *device_serial = NULL;
    const char *json_file = NULL;
    const char *html_file = NULL;

    static struct option long_opts[] = {
        {"android", no_argument, NULL, 'a'},
        {"last",    required_argument, NULL, 'l'},
        {"json",    optional_argument, NULL, 'j'},
        {"html",    required_argument, NULL, 'h'},
        {"live",    no_argument, NULL, 'L'},
        {"device",  required_argument, NULL, 'd'},
        {"no-store", no_argument, NULL, 'n'},
        {NULL, 0, NULL, 0}
    };

    optind = 1;
    int c;
    while ((c = getopt_long(argc, argv, "al:j::h:Ld:n", long_opts, NULL)) != -1) {
        switch (c) {
        case 'a': android_mode = 1; break;
        case 'd': device_serial = optarg; break;
        case 'n': no_store = 1; break;
        case 'l': last_hours = atoi(optarg); break;
        case 'j':
            json_mode = 1;
            json_file = optarg;
            /* allow "--json file" as well as "--json=file" */
            if (!json_file && optind < argc && argv[optind][0] != '-')
                json_file = argv[optind++];
            break;
        case 'h': html_file = optarg; break;
        case 'L': live_mode = 1; break;
        default:  usage(); return 1;
        }
    }

    (void)last_hours;

    if (android_mode) {
        /* pick the phone: --device, else ANDROID_SERIAL, else the first one */
        if (device_serial && *device_serial)
            setenv("ANDROID_SERIAL", device_serial, 1);
        char serial[128];
        int unauthorized = 0;
        int usable = android_pick_device(serial, sizeof(serial), &unauthorized);
        if (usable > 0 && serial[0])
            setenv("ANDROID_SERIAL", serial, 1);

        if (usable == 0 || !serial[0] || !android_check_connection()) {
            if (unauthorized)
                fprintf(stderr,
                    "Phone found but not authorized yet.\n"
                    "Unlock the phone and tap \"Allow\" on the USB debugging prompt, then retry.\n");
            else if (device_serial)
                fprintf(stderr, "No device with serial %s (see: spectra devices).\n", device_serial);
            else
                fprintf(stderr,
                    "No Android device found.\n"
                    "Connect via USB and enable USB debugging:\n"
                    "  Settings > Developer Options > USB Debugging\n"
                    "  and set the USB mode to File transfer if the phone is not detected.\n");
            return 1;
        }
    }

    do {
        struct timespec ts_start, ts_end;
        clock_gettime(CLOCK_MONOTONIC, &ts_start);

        /* collect sensor events */
        spectra_sensor_event_t sensors[SPECTRA_MAX_EVENTS];
        int n_sensors;
        if (android_mode)
            n_sensors = android_scan_sensors(sensors, SPECTRA_MAX_EVENTS);
        else
            n_sensors = platform_scan_sensors(sensors, SPECTRA_MAX_EVENTS);

        /* collect network events */
        spectra_net_event_t net[SPECTRA_MAX_EVENTS];
        int n_net;
        if (android_mode)
            n_net = android_scan_network(net, SPECTRA_MAX_EVENTS);
        else
            n_net = platform_scan_network(net, SPECTRA_MAX_EVENTS);

        if (n_sensors < 0) n_sensors = 0;
        if (n_net < 0) n_net = 0;

        /* gather app details for every app that touched a sensor */
        static spectra_process_info_t apps[SPECTRA_MAX_EVENTS];
        int n_apps = 0;
        static char seen_pkg[SPECTRA_MAX_EVENTS][256];
        int n_seen_pkg = 0;

        for (int i = 0; i < n_sensors && n_apps < SPECTRA_MAX_EVENTS; i++) {
            int dup = 0;
            for (int j = 0; j < n_seen_pkg; j++) {
                if (strcmp(seen_pkg[j], sensors[i].package) == 0) { dup = 1; break; }
            }
            if (dup) continue;
            snprintf(seen_pkg[n_seen_pkg++], 256, "%s", sensors[i].package);

            if (android_mode)
                android_get_process_info(sensors[i].package, &apps[n_apps]);
            else
                platform_get_process_info(sensors[i].package, &apps[n_apps]);
            n_apps++;
        }

        /* correlate, with app details for weighting */
        static spectra_correlation_t correlations[SPECTRA_MAX_EVENTS];
        int n_corr = spectra_correlate_ex(sensors, n_sensors, net, n_net,
                                        apps, n_apps,
                                        correlations, SPECTRA_MAX_EVENTS);

        clock_gettime(CLOCK_MONOTONIC, &ts_end);
        double duration = (ts_end.tv_sec - ts_start.tv_sec) +
                          (ts_end.tv_nsec - ts_start.tv_nsec) / 1e9;

        /* build report */
        spectra_report_t report = {0};
        if (android_mode)
            android_get_device_info(&report.device);
        report.sensors = sensors;
        report.n_sensors = n_sensors;
        report.net = net;
        report.n_net = n_net;
        report.correlations = correlations;
        report.n_correlations = n_corr;
        report.apps = apps;
        report.n_apps = n_apps;
        report.scan_duration_s = duration;

        if (json_mode) {
            report_write_json(&report, json_file);
        } else if (html_file) {
            report_write_html(&report, html_file);
            printf("HTML report written to %s\n", html_file);
        } else {
            report_print_terminal(&report);
        }

        if (no_store) {
            if (live_mode) {
                printf("\033[2J\033[H");
                struct timespec ts = {2, 0};
                nanosleep(&ts, NULL);
            }
            continue;
        }

        /* save to history */
        store_init();
        char device_id[128] = "local";
        if (android_mode) {
            static char serial[128];
            char buf[256];
            int pipefd[2];
            if (pipe(pipefd) == 0) {
                pid_t pid = fork();
                if (pid == 0) {
                    close(pipefd[0]);
                    dup2(pipefd[1], STDOUT_FILENO);
                    close(pipefd[1]);
                    execlp("adb", "adb", "get-serialno", NULL);
                    _exit(127);
                }
                close(pipefd[1]);
                ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
                if (n > 0) {
                    buf[n] = '\0';
                    char *end = strchr(buf, '\n');
                    if (end) *end = '\0';
                    snprintf(serial, sizeof(serial), "%s", buf);
                    snprintf(device_id, sizeof(device_id), "%s", serial);
                }
                close(pipefd[0]);
                int status;
                waitpid(pid, &status, 0);
            }
        }

        /* save JSON to store */
        char *json_buf = NULL;
        cJSON *root = cJSON_CreateObject();
        /* minimal JSON for store */
        cJSON_AddNumberToObject(root, "n_sensors", n_sensors);
        cJSON_AddNumberToObject(root, "n_net", n_net);
        cJSON_AddNumberToObject(root, "n_correlations", n_corr);
        cJSON_AddNumberToObject(root, "scan_duration_s", duration);
        json_buf = cJSON_PrintUnformatted(root);
        store_save_scan(device_id,
                        android_mode ? report.device.device_name : "local",
                        json_buf);
        free(json_buf);
        cJSON_Delete(root);
        store_close();

        if (live_mode) {
            printf("\033[2J\033[H");
            struct timespec ts = {2, 0};
            nanosleep(&ts, NULL);
        }
    } while (live_mode);

    return 0;
}

static int cmd_history(int argc, char **argv)
{
    const char *device_filter = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--device") == 0 && i + 1 < argc)
            device_filter = argv[++i];
    }

    store_init();
    store_list_scans(device_filter);
    store_close();
    return 0;
}

static int cmd_diff(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "Usage: spectra diff <id1> <id2>\n");
        return 1;
    }

    int id1 = atoi(argv[1]);
    int id2 = atoi(argv[2]);

    store_init();

    static char buf1[65536], buf2[65536];
    if (store_get_scan_json(id1, buf1, sizeof(buf1)) < 0) {
        fprintf(stderr, "Scan %d not found\n", id1);
        store_close();
        return 1;
    }
    if (store_get_scan_json(id2, buf2, sizeof(buf2)) < 0) {
        fprintf(stderr, "Scan %d not found\n", id2);
        store_close();
        return 1;
    }

    printf("Scan %d:\n%s\n\nScan %d:\n%s\n", id1, buf1, id2, buf2);
    store_close();
    return 0;
}

static int cmd_devices(void)
{
    printf("Connected Android devices:\n\n");
    int pipefd[2];
    if (pipe(pipefd) < 0) return 1;

    pid_t pid = fork();
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        execlp("adb", "adb", "devices", "-l", NULL);
        _exit(127);
    }
    close(pipefd[1]);

    char buf[4096];
    ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        printf("%s", buf);
    }
    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);
    return 0;
}

static const char *allowlist_path(void)
{
    static char path[512];
    const char *home = getenv("HOME");
    if (!home) return NULL;
    snprintf(path, sizeof(path), "%s/.spectra/allowlist.txt", home);
    return path;
}

static int cmd_allowlist(int argc, char **argv)
{
    const char *path = allowlist_path();
    if (!path) {
        fprintf(stderr, "Cannot determine HOME directory\n");
        return 1;
    }

    if (argc < 1) {
        fprintf(stderr,
            "Usage: spectra allowlist <command>\n\n"
            "Commands:\n"
            "  list              Show all trusted domains\n"
            "  add <domain>      Add a domain to the allowlist\n"
            "  remove <domain>   Remove a domain from the allowlist\n\n");
        return 1;
    }

    const char *subcmd = argv[0];

    if (strcmp(subcmd, "list") == 0 || strcmp(subcmd, "ls") == 0) {
        FILE *f = fopen(path, "r");
        if (!f) {
            printf("Allowlist is empty (no file at %s)\n", path);
            return 0;
        }
        char line[256];
        int count = 0;
        printf("Trusted domains:\n");
        while (fgets(line, sizeof(line), f)) {
            char *end = strchr(line, '\n');
            if (end) *end = '\0';
            end = strchr(line, '\r');
            if (end) *end = '\0';
            if (line[0] && line[0] != '#') {
                printf("  %s\n", line);
                count++;
            }
        }
        fclose(f);
        if (count == 0)
            printf("  (empty)\n");
        printf("\n%d domain%s\n", count, count == 1 ? "" : "s");
        return 0;
    }

    if (strcmp(subcmd, "add") == 0) {
        if (argc < 2) {
            fprintf(stderr, "Usage: spectra allowlist add <domain>\n");
            return 1;
        }
        const char *domain = argv[1];

        /* ensure directory exists */
        char dir[512];
        snprintf(dir, sizeof(dir), "%s/.spectra", getenv("HOME"));
        mkdir(dir, 0755);

        /* check if already present */
        FILE *f = fopen(path, "r");
        if (f) {
            char line[256];
            while (fgets(line, sizeof(line), f)) {
                char *end = strchr(line, '\n');
                if (end) *end = '\0';
                end = strchr(line, '\r');
                if (end) *end = '\0';
                if (strcmp(line, domain) == 0) {
                    printf("%s is already in the allowlist\n", domain);
                    fclose(f);
                    return 0;
                }
            }
            fclose(f);
        }

        f = fopen(path, "a");
        if (!f) {
            fprintf(stderr, "Cannot write to %s\n", path);
            return 1;
        }
        fprintf(f, "%s\n", domain);
        fclose(f);
        printf("Added %s to the allowlist\n", domain);
        return 0;
    }

    if (strcmp(subcmd, "remove") == 0 || strcmp(subcmd, "rm") == 0) {
        if (argc < 2) {
            fprintf(stderr, "Usage: spectra allowlist remove <domain>\n");
            return 1;
        }
        const char *domain = argv[1];

        FILE *f = fopen(path, "r");
        if (!f) {
            printf("%s is not in the allowlist\n", domain);
            return 0;
        }

        char lines[512][256];
        int n_lines = 0;
        int found = 0;
        char line[256];
        while (fgets(line, sizeof(line), f) && n_lines < 512) {
            char *end = strchr(line, '\n');
            if (end) *end = '\0';
            end = strchr(line, '\r');
            if (end) *end = '\0';
            if (strcmp(line, domain) == 0) {
                found = 1;
                continue;
            }
            snprintf(lines[n_lines++], 256, "%s", line);
        }
        fclose(f);

        if (!found) {
            printf("%s is not in the allowlist\n", domain);
            return 0;
        }

        f = fopen(path, "w");
        if (!f) {
            fprintf(stderr, "Cannot write to %s\n", path);
            return 1;
        }
        for (int i = 0; i < n_lines; i++) {
            if (lines[i][0])
                fprintf(f, "%s\n", lines[i]);
        }
        fclose(f);
        printf("Removed %s from the allowlist\n", domain);
        return 0;
    }

    fprintf(stderr, "Unknown allowlist command: %s\n", subcmd);
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage();
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "scan") == 0) {
        return cmd_scan(argc - 1, argv + 1);
    } else if (strcmp(cmd, "history") == 0) {
        return cmd_history(argc - 1, argv + 1);
    } else if (strcmp(cmd, "diff") == 0) {
        return cmd_diff(argc - 1, argv + 1);
    } else if (strcmp(cmd, "devices") == 0) {
        return cmd_devices();
    } else if (strcmp(cmd, "allowlist") == 0) {
        return cmd_allowlist(argc - 2, argv + 2);
    } else if (strcmp(cmd, "ui") == 0) {
        printf("Launching Spectra dashboard...\n");
        execlp("python3", "python3", "tools/serve.py", "--open", NULL);
        fprintf(stderr, "Failed to launch dashboard server\n");
        return 1;
    } else if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        usage();
        return 0;
    } else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        usage();
        return 1;
    }
}
