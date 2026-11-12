#include "spectra/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include <netdb.h>

static int run_cmd(const char *cmd, char *buf, size_t bufsize)
{
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;
    size_t total = 0, n;
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

    /* camera: scan /proc/*/fd for symlinks to /dev/video* */
    DIR *proc = opendir("/proc");
    if (proc) {
        struct dirent *ent;
        while ((ent = readdir(proc)) != NULL && count < max) {
            if (ent->d_name[0] < '0' || ent->d_name[0] > '9') continue;
            int pid = atoi(ent->d_name);
            char fddir[256];
            snprintf(fddir, sizeof(fddir), "/proc/%d/fd", pid);
            DIR *fds = opendir(fddir);
            if (!fds) continue;

            struct dirent *fdent;
            while ((fdent = readdir(fds)) != NULL) {
                char link_path[512], target[512];
                snprintf(link_path, sizeof(link_path), "%s/%s", fddir, fdent->d_name);
                ssize_t len = readlink(link_path, target, sizeof(target) - 1);
                if (len > 0) {
                    target[len] = '\0';
                    if (strstr(target, "/dev/video") != NULL) {
                        spectra_sensor_event_t *ev = &out[count];
                        memset(ev, 0, sizeof(*ev));
                        ev->pid = pid;
                        /* read comm */
                        char comm_path[256], comm[256] = {0};
                        snprintf(comm_path, sizeof(comm_path), "/proc/%d/comm", pid);
                        FILE *cf = fopen(comm_path, "r");
                        if (cf) { fgets(comm, sizeof(comm), cf); fclose(cf); }
                        char *nl = strchr(comm, '\n');
                        if (nl) *nl = '\0';
                        snprintf(ev->app_name, sizeof(ev->app_name), "%s", comm);
                        snprintf(ev->package, sizeof(ev->package), "pid:%d", pid);
                        snprintf(ev->sensor_type, sizeof(ev->sensor_type), "camera");
                        snprintf(ev->device, sizeof(ev->device), "%s", target);
                        ev->last_access = time(NULL);
                        ev->currently_active = 1;
                        ev->detected_at = time(NULL);
                        count++;
                        break;
                    }
                }
            }
            closedir(fds);
        }
        closedir(proc);
    }

    /* microphone: try PipeWire first, fall back to PulseAudio */
    static char buf[65536];
    int n = run_cmd("pw-dump 2>/dev/null | grep -B5 -A5 '\"Audio/Source\"'", buf, sizeof(buf));
    if (n <= 0)
        n = run_cmd("pactl list source-outputs 2>/dev/null", buf, sizeof(buf));

    if (n > 0) {
        char *line = strtok(buf, "\n");
        while (line && count < max) {
            char *pidstr = strstr(line, "pid");
            if (!pidstr) pidstr = strstr(line, "PID");
            if (pidstr) {
                int pid = 0;
                char *p = pidstr;
                while (*p && (*p < '0' || *p > '9')) p++;
                if (*p) pid = atoi(p);
                if (pid > 0) {
                    spectra_sensor_event_t *ev = &out[count];
                    memset(ev, 0, sizeof(*ev));
                    ev->pid = pid;
                    char comm_path[256], comm[256] = {0};
                    snprintf(comm_path, sizeof(comm_path), "/proc/%d/comm", pid);
                    FILE *cf = fopen(comm_path, "r");
                    if (cf) { fgets(comm, sizeof(comm), cf); fclose(cf); }
                    char *nl = strchr(comm, '\n');
                    if (nl) *nl = '\0';
                    snprintf(ev->app_name, sizeof(ev->app_name), "%s", comm);
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
    FILE *fp = fopen("/proc/net/tcp", "r");
    if (!fp) return 0;

    char line[512];
    fgets(line, sizeof(line), fp); /* skip header */

    while (fgets(line, sizeof(line), fp) && count < max) {
        unsigned int idx, local_port, remote_port, state, uid;
        char local_hex[16], remote_hex[16];

        int matched = sscanf(line,
            " %u: %8[0-9A-Fa-f]:%X %8[0-9A-Fa-f]:%X %X %*s %*s %*s %u",
            &idx, local_hex, &local_port, remote_hex, &remote_port,
            &state, &uid);

        if (matched >= 7 && state == 0x01) {
            spectra_net_event_t *ev = &out[count];
            memset(ev, 0, sizeof(*ev));
            ev->uid = uid;

            /* convert hex IP */
            unsigned int addr;
            sscanf(remote_hex, "%X", &addr);
            struct in_addr in;
            in.s_addr = addr;
            inet_ntop(AF_INET, &in, ev->remote_ip, sizeof(ev->remote_ip));
            ev->remote_port = remote_port;

            /* reverse DNS */
            struct sockaddr_in sa;
            memset(&sa, 0, sizeof(sa));
            sa.sin_family = AF_INET;
            sa.sin_addr = in;
            char hostbuf[256];
            if (getnameinfo((struct sockaddr *)&sa, sizeof(sa),
                            hostbuf, sizeof(hostbuf), NULL, 0, NI_NAMEREQD) == 0)
                snprintf(ev->remote_host, sizeof(ev->remote_host), "%s", hostbuf);
            else
                snprintf(ev->remote_host, sizeof(ev->remote_host), "unknown");

            /* find process name from uid */
            snprintf(ev->package, sizeof(ev->package), "uid:%d", uid);
            snprintf(ev->app_name, sizeof(ev->app_name), "uid:%d", uid);
            ev->detected_at = time(NULL);
            count++;
        }
    }
    fclose(fp);
    return count;
}

int platform_get_process_info(const char *package, spectra_process_info_t *out)
{
    memset(out, 0, sizeof(*out));
    snprintf(out->package, sizeof(out->package), "%s", package);

    int pid = 0;
    if (strncmp(package, "pid:", 4) == 0)
        pid = atoi(package + 4);

    if (pid > 0) {
        char path[256], buf[4096];

        snprintf(path, sizeof(path), "/proc/%d/comm", pid);
        FILE *f = fopen(path, "r");
        if (f) {
            if (fgets(buf, sizeof(buf), f)) {
                char *nl = strchr(buf, '\n');
                if (nl) *nl = '\0';
                snprintf(out->app_name, sizeof(out->app_name), "%s", buf);
            }
            fclose(f);
        }

        /* check if from a known package */
        snprintf(path, sizeof(path), "/proc/%d/exe", pid);
        ssize_t len = readlink(path, buf, sizeof(buf) - 1);
        if (len > 0) {
            buf[len] = '\0';
            char cmd[1024];
            static char dpkg_buf[1024];
            snprintf(cmd, sizeof(cmd), "dpkg -S '%s' 2>/dev/null", buf);
            int n = run_cmd(cmd, dpkg_buf, sizeof(dpkg_buf));
            if (n > 0) {
                char *colon = strchr(dpkg_buf, ':');
                if (colon) {
                    *colon = '\0';
                    snprintf(out->install_source, sizeof(out->install_source), "dpkg: %s", dpkg_buf);
                }
            }
        }
    } else {
        snprintf(out->app_name, sizeof(out->app_name), "%s", package);
    }

    return 0;
}

int platform_is_known_package(const char *pkg_or_path)
{
    if (strstr(pkg_or_path, "/usr/") != NULL) return 1;
    static char buf[1024];
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "dpkg -S '%s' 2>/dev/null", pkg_or_path);
    int n = run_cmd(cmd, buf, sizeof(buf));
    return (n > 0) ? 1 : 0;
}

const char *platform_name(void)
{
    return "linux";
}

int platform_check_connection(void)
{
    return 1;
}
