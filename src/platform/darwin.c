#include "spectra/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include <arpa/inet.h>
#include <netdb.h>

static int run_cmd(const char *cmd, char *buf, size_t bufsize)
{
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;

    size_t total = 0;
    size_t n;
    while (total < bufsize - 1 &&
           (n = fread(buf + total, 1, bufsize - 1 - total, fp)) > 0)
        total += n;
    buf[total] = '\0';
    pclose(fp);
    return (int)total;
}

int platform_scan_sensors(spectra_sensor_event_t *out, int max)
{
    int count = 0;
    static char buf[65536];

    /* camera: check for VDCAssistant (camera daemon) or AppleCamera processes */
    int n = run_cmd("lsof -c VDCAssistant -c AppleCamera 2>/dev/null | grep -v '^COMMAND'", buf, sizeof(buf));
    if (n > 0) {
        char *line = strtok(buf, "\n");
        while (line && count < max) {
            /* lsof output: COMMAND PID USER ... */
            char cmd_name[256];
            int pid;
            if (sscanf(line, "%255s %d", cmd_name, &pid) == 2) {
                spectra_sensor_event_t *ev = &out[count];
                memset(ev, 0, sizeof(*ev));
                ev->pid = pid;
                snprintf(ev->app_name, sizeof(ev->app_name), "%s", cmd_name);
                snprintf(ev->package, sizeof(ev->package), "pid:%d", pid);
                snprintf(ev->sensor_type, sizeof(ev->sensor_type), "camera");
                snprintf(ev->device, sizeof(ev->device), "camera");
                ev->last_access = time(NULL);
                ev->currently_active = 1;
                ev->detected_at = time(NULL);
                count++;
            }
            line = strtok(NULL, "\n");
        }
    }

    /* microphone: check for coreaudiod clients via lsof */
    n = run_cmd("lsof -c coreaudiod 2>/dev/null | grep -i 'input\\|mic' | head -20", buf, sizeof(buf));
    /* fallback: check if any process has audio input open */
    if (n <= 0) {
        n = run_cmd("lsof 2>/dev/null | grep -i '/dev/audio\\|CoreAudio' | head -20", buf, sizeof(buf));
    }
    if (n > 0) {
        char *line = strtok(buf, "\n");
        while (line && count < max) {
            char cmd_name[256];
            int pid;
            if (sscanf(line, "%255s %d", cmd_name, &pid) == 2) {
                int dup = 0;
                for (int i = 0; i < count; i++)
                    if (out[i].pid == pid && strcmp(out[i].sensor_type, "mic") == 0)
                        { dup = 1; break; }
                if (!dup) {
                    spectra_sensor_event_t *ev = &out[count];
                    memset(ev, 0, sizeof(*ev));
                    ev->pid = pid;
                    snprintf(ev->app_name, sizeof(ev->app_name), "%s", cmd_name);
                    snprintf(ev->package, sizeof(ev->package), "pid:%d", pid);
                    snprintf(ev->sensor_type, sizeof(ev->sensor_type), "mic");
                    snprintf(ev->device, sizeof(ev->device), "microphone");
                    ev->last_access = time(NULL);
                    ev->currently_active = 1;
                    ev->detected_at = time(NULL);
                    count++;
                }
            }
            line = strtok(NULL, "\n");
        }
    }

    return count;
}

int platform_scan_network(spectra_net_event_t *out, int max)
{
    int count = 0;
    static char buf[131072];

    /* use lsof -i -n -P for network connections */
    int n = run_cmd("lsof -i -n -P 2>/dev/null | grep ESTABLISHED", buf, sizeof(buf));
    if (n <= 0) return 0;

    char *line = strtok(buf, "\n");
    while (line && count < max) {
        /* COMMAND PID USER FD TYPE DEVICE SIZE/OFF NODE NAME */
        char cmd_name[256], user[64], fd[32], type[32], device_str[64];
        char size_off[32], node[16], name[512];
        int pid;

        int matched = sscanf(line, "%255s %d %63s %31s %31s %63s %31s %15s %511[^\n]",
                             cmd_name, &pid, user, fd, type, device_str,
                             size_off, node, name);
        if (matched >= 9 && strcmp(node, "TCP") == 0) {
            /* name: local->remote */
            char *arrow = strstr(name, "->");
            if (arrow) {
                char *remote = arrow + 2;
                char *colon = strrchr(remote, ':');
                if (colon) {
                    *colon = '\0';
                    uint16_t port = atoi(colon + 1);

                    spectra_net_event_t *ev = &out[count];
                    memset(ev, 0, sizeof(*ev));
                    ev->pid = pid;
                    snprintf(ev->app_name, sizeof(ev->app_name), "%s", cmd_name);
                    snprintf(ev->package, sizeof(ev->package), "pid:%d", pid);
                    snprintf(ev->remote_ip, sizeof(ev->remote_ip), "%s", remote);
                    ev->remote_port = port;
                    ev->detected_at = time(NULL);

                    /* reverse DNS */
                    struct sockaddr_storage ss;
                    memset(&ss, 0, sizeof(ss));
                    struct in_addr a4;
                    struct in6_addr a6;
                    char hostbuf[256];
                    if (inet_pton(AF_INET, remote, &a4) == 1) {
                        struct sockaddr_in *s4 = (struct sockaddr_in *)&ss;
                        s4->sin_family = AF_INET;
                        s4->sin_addr = a4;
                        if (getnameinfo((struct sockaddr *)s4, sizeof(*s4),
                                        hostbuf, sizeof(hostbuf), NULL, 0, NI_NAMEREQD) == 0)
                            snprintf(ev->remote_host, sizeof(ev->remote_host), "%s", hostbuf);
                        else
                            snprintf(ev->remote_host, sizeof(ev->remote_host), "unknown");
                    } else if (inet_pton(AF_INET6, remote, &a6) == 1) {
                        struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)&ss;
                        s6->sin6_family = AF_INET6;
                        s6->sin6_addr = a6;
                        if (getnameinfo((struct sockaddr *)s6, sizeof(*s6),
                                        hostbuf, sizeof(hostbuf), NULL, 0, NI_NAMEREQD) == 0)
                            snprintf(ev->remote_host, sizeof(ev->remote_host), "%s", hostbuf);
                        else
                            snprintf(ev->remote_host, sizeof(ev->remote_host), "unknown");
                    } else {
                        snprintf(ev->remote_host, sizeof(ev->remote_host), "unknown");
                    }

                    count++;
                }
            }
        }
        line = strtok(NULL, "\n");
    }

    return count;
}

int platform_get_process_info(const char *package, spectra_process_info_t *out)
{
    memset(out, 0, sizeof(*out));
    snprintf(out->package, sizeof(out->package), "%s", package);
    snprintf(out->app_name, sizeof(out->app_name), "%s", package);

    /* try to get more info from pid */
    int pid = 0;
    if (strncmp(package, "pid:", 4) == 0)
        pid = atoi(package + 4);

    if (pid > 0) {
        char cmd[256];
        static char buf[4096];
        snprintf(cmd, sizeof(cmd), "ps -p %d -o comm= 2>/dev/null", pid);
        int n = run_cmd(cmd, buf, sizeof(buf));
        if (n > 0) {
            char *end = strchr(buf, '\n');
            if (end) *end = '\0';
            snprintf(out->app_name, sizeof(out->app_name), "%s", buf);
        }
    }

    return 0;
}

int platform_is_known_package(const char *pkg_or_path)
{
    if (strstr(pkg_or_path, "/Applications/") != NULL) return 1;
    if (strstr(pkg_or_path, "/usr/") != NULL) return 1;
    if (strstr(pkg_or_path, "Homebrew") != NULL) return 1;
    return 0;
}

const char *platform_name(void)
{
    return "darwin";
}

int platform_check_connection(void)
{
    return 1;
}
