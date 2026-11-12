#include "spectra/platform.h"
#include "spectra/report.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <time.h>
#include <arpa/inet.h>
#include <netdb.h>

#define ADB_TIMEOUT_SEC 5
#define ADB_BUF_SIZE    (2 * 1024 * 1024)

static char uid_pkg_cache[1024][2][256];
static int uid_pkg_count = 0;

static int adb_exec_timeout(const char *cmd, char *buf, size_t bufsize, int timeout_sec);

int adb_exec(const char *cmd, char *buf, size_t bufsize)
{
    return adb_exec_timeout(cmd, buf, bufsize, ADB_TIMEOUT_SEC);
}

static int adb_exec_timeout(const char *cmd, char *buf, size_t bufsize, int timeout_sec)
{
    int pipefd[2];
    if (pipe(pipefd) < 0)
        return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execlp("adb", "adb", "shell", cmd, NULL);
        _exit(127);
    }

    close(pipefd[1]);

    size_t total = 0;
    ssize_t n;
    alarm(timeout_sec);
    while (total < bufsize - 1 &&
           (n = read(pipefd[0], buf + total, bufsize - 1 - total)) > 0) {
        total += n;
    }
    alarm(0);
    buf[total] = '\0';
    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status) && WEXITSTATUS(status) == 127)
        return -1;

    return (int)total;
}

/* List attached devices via "adb devices". Fills `serial` with the first one
 * in state "device" (or the one already selected through ANDROID_SERIAL) and
 * returns the count of usable devices; *unauthorized is set if any device is
 * waiting for the "Allow USB debugging" tap. */
int android_pick_device(char *serial, size_t size, int *unauthorized)
{
    static char buf[8192];
    if (unauthorized) *unauthorized = 0;
    if (serial && size) serial[0] = '\0';

    int pipefd[2];
    if (pipe(pipefd) < 0) return 0;
    pid_t pid = fork();
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); return 0; }
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        execlp("adb", "adb", "devices", NULL);
        _exit(127);
    }
    close(pipefd[1]);
    size_t total = 0; ssize_t n;
    while (total < sizeof(buf) - 1 && (n = read(pipefd[0], buf + total, sizeof(buf) - 1 - total)) > 0)
        total += n;
    buf[total] = '\0';
    close(pipefd[0]);
    int status; waitpid(pid, &status, 0);

    const char *want = getenv("ANDROID_SERIAL");
    int usable = 0;
    char *line = strtok(buf, "\n");
    while (line) {
        char id[128] = {0}, state[32] = {0};
        if (sscanf(line, "%127s %31s", id, state) == 2 && strcmp(id, "List") != 0) {
            if (strcmp(state, "device") == 0) {
                usable++;
                int take = (want && *want) ? strcmp(id, want) == 0 : (serial && !serial[0]);
                if (take && serial) snprintf(serial, size, "%s", id);
            } else if (strcmp(state, "unauthorized") == 0 && unauthorized) {
                *unauthorized = 1;
            }
        }
        line = strtok(NULL, "\n");
    }
    return usable;
}

int android_check_connection(void)
{
    char buf[512];
    int pipefd[2];
    if (pipe(pipefd) < 0)
        return 0;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return 0;
    }
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        execlp("adb", "adb", "get-state", NULL);
        _exit(127);
    }
    close(pipefd[1]);

    size_t total = 0;
    ssize_t n;
    while (total < sizeof(buf) - 1 &&
           (n = read(pipefd[0], buf + total, sizeof(buf) - 1 - total)) > 0)
        total += n;
    buf[total] = '\0';
    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);

    return (strstr(buf, "device") != NULL) ? 1 : 0;
}

static void build_uid_pkg_map(void)
{
    static char buf[ADB_BUF_SIZE];
    int n = adb_exec("pm list packages -U", buf, sizeof(buf));
    if (n <= 0) return;

    uid_pkg_count = 0;
    char *line = strtok(buf, "\n");
    while (line && uid_pkg_count < 1024) {
        char *pkg_start = strstr(line, "package:");
        char *uid_start = strstr(line, "uid:");
        if (pkg_start && uid_start) {
            pkg_start += 8;
            uid_start += 4;
            char *pkg_end = strchr(pkg_start, ' ');
            if (pkg_end) *pkg_end = '\0';
            char *uid_end = strchr(uid_start, '\r');
            if (!uid_end) uid_end = strchr(uid_start, '\n');
            if (uid_end) *uid_end = '\0';

            snprintf(uid_pkg_cache[uid_pkg_count][0], 256, "%s", uid_start);
            snprintf(uid_pkg_cache[uid_pkg_count][1], 256, "%s", pkg_start);
            uid_pkg_count++;
        }
        line = strtok(NULL, "\n");
    }
}

static const char *uid_to_package(int uid)
{
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%d", uid);
    for (int i = 0; i < uid_pkg_count; i++) {
        if (strcmp(uid_pkg_cache[i][0], uid_str) == 0)
            return uid_pkg_cache[i][1];
    }
    return NULL;
}

/* ---- per-package dump cache ------------------------------------------
 * "dumpsys package X" is ~200 KB and one adb round trip each; a scan touches
 * 30-40 packages twice (display name + app details). Fetch them all in one
 * adb shell call and keep the text for the rest of the scan. */
#define PKG_CACHE_BYTES (16 * 1024 * 1024)
#define PKG_CACHE_MAX   512

static char pkg_arena[PKG_CACHE_BYTES];
static size_t pkg_arena_used = 0;
static struct { char pkg[256]; const char *dump; const char *install; } pkg_cache[PKG_CACHE_MAX];
static int pkg_cache_n = 0;

static int pkg_cache_find(const char *pkg)
{
    for (int i = 0; i < pkg_cache_n; i++)
        if (strcmp(pkg_cache[i].pkg, pkg) == 0) return i;
    return -1;
}

static const char *arena_store(const char *src, size_t len)
{
    if (pkg_arena_used + len + 1 > sizeof(pkg_arena)) return NULL;
    char *dst = pkg_arena + pkg_arena_used;
    memcpy(dst, src, len);
    dst[len] = '\0';
    pkg_arena_used += len + 1;
    return dst;
}

static void pkg_cache_add(const char *pkg, const char *dump, size_t dlen,
                          const char *install, size_t ilen)
{
    if (pkg_cache_n >= PKG_CACHE_MAX || pkg_cache_find(pkg) >= 0) return;
    const char *d = arena_store(dump, dlen);
    const char *in = arena_store(install ? install : "", install ? ilen : 0);
    if (!d || !in) return;
    snprintf(pkg_cache[pkg_cache_n].pkg, 256, "%s", pkg);
    pkg_cache[pkg_cache_n].dump = d;
    pkg_cache[pkg_cache_n].install = in;
    pkg_cache_n++;
}

static int pkg_name_safe(const char *p)
{
    if (!*p) return 0;
    for (; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '.' || *p == '_'))
            return 0;
    return 1;
}

/* fetch dumps for every package in the list that isn't cached yet, in one call */
static void prefetch_packages(const char **pkgs, int n)
{
    static char fetch[PKG_CACHE_BYTES];
    size_t cmdcap = 128 + (size_t)n * 272;
    char *cmd = malloc(cmdcap);
    if (!cmd) return;
    size_t off = (size_t)snprintf(cmd, cmdcap, "for p in");
    int todo = 0;
    for (int i = 0; i < n; i++) {
        if (!pkg_name_safe(pkgs[i]) || pkg_cache_find(pkgs[i]) >= 0) continue;
        int dup = 0;
        for (int j = 0; j < i; j++) if (strcmp(pkgs[j], pkgs[i]) == 0) { dup = 1; break; }
        if (dup) continue;
        off += (size_t)snprintf(cmd + off, cmdcap - off, " %s", pkgs[i]);
        todo++;
    }
    if (todo == 0) { free(cmd); return; }
    snprintf(cmd + off, cmdcap - off,
             "; do echo \"===PKG $p\"; dumpsys package $p; done");

    int got = adb_exec_timeout(cmd, fetch, sizeof(fetch), 60);
    free(cmd);
    if (got <= 0) return;

    char *p = fetch;
    while ((p = strstr(p, "===PKG ")) != NULL) {
        char *name = p + 7;
        char *nl = strchr(name, '\n');
        if (!nl) break;
        char pkg[256] = {0};
        size_t nlen = (size_t)(nl - name);
        if (nlen > 0 && name[nlen - 1] == '\r') nlen--;
        if (nlen >= sizeof(pkg)) nlen = sizeof(pkg) - 1;
        memcpy(pkg, name, nlen);

        char *dump = nl + 1;
        char *next = strstr(dump, "===PKG ");
        char *inst = strstr(dump, "===INSTALL");
        if (inst && next && inst > next) inst = NULL;
        size_t dlen = inst ? (size_t)(inst - dump) : (next ? (size_t)(next - dump) : strlen(dump));
        const char *itext = NULL; size_t ilen = 0;
        if (inst) {
            itext = strchr(inst, '\n');
            itext = itext ? itext + 1 : inst + 10;
            ilen = next ? (size_t)(next - itext) : strlen(itext);
        }
        pkg_cache_add(pkg, dump, dlen, itext, ilen);
        p = next ? next : dump + strlen(dump);
    }
}

/* cached "dumpsys package" text (and install-source text) for one package;
 * falls back to a direct call if it wasn't prefetched */
static const char *pkg_dump_get(const char *package, const char **install_out)
{
    int idx = pkg_cache_find(package);
    if (idx < 0) {
        const char *one[1] = { package };
        prefetch_packages(one, 1);
        idx = pkg_cache_find(package);
    }
    if (idx < 0) { if (install_out) *install_out = NULL; return NULL; }
    if (install_out) *install_out = pkg_cache[idx].install;
    return pkg_cache[idx].dump;
}

/* Display names. "dumpsys package" stopped printing applicationLabel on
 * recent Android versions and resources can't be read over plain adb, so:
 * known packages come from a table, the rest get a readable name derived
 * from the package id ("com.ixigo.train.ixitrain" -> "Ixitrain"). */
static const struct { const char *pkg; const char *name; } known_names[] = {
    {"android", "Android System"},
    {"com.whatsapp", "WhatsApp"},
    {"com.whatsapp.w4b", "WhatsApp Business"},
    {"org.telegram.messenger", "Telegram"},
    {"com.truecaller", "Truecaller"},
    {"com.google.android.gms", "Google Play Services"},
    {"com.google.android.googlequicksearchbox", "Google"},
    {"com.android.vending", "Play Store"},
    {"com.google.android.apps.messaging", "Messages"},
    {"com.google.android.gm", "Gmail"},
    {"com.google.android.apps.tachyon", "Google Meet"},
    {"com.google.android.apps.nbu.paisa.user", "Google Pay"},
    {"com.google.android.as", "Android System Intelligence"},
    {"com.google.android.youtube", "YouTube"},
    {"com.google.android.apps.maps", "Google Maps"},
    {"com.google.android.apps.photos", "Google Photos"},
    {"com.android.chrome", "Chrome"},
    {"com.instagram.android", "Instagram"},
    {"com.facebook.katana", "Facebook"},
    {"com.facebook.orca", "Messenger"},
    {"com.facebook.services", "Facebook Services"},
    {"com.snapchat.android", "Snapchat"},
    {"com.zhiliaoapp.musically", "TikTok"},
    {"com.twitter.android", "X"},
    {"com.linkedin.android", "LinkedIn"},
    {"com.discord", "Discord"},
    {"com.spotify.music", "Spotify"},
    {"com.microsoft.teams", "Microsoft Teams"},
    {"us.zoom.videomeetings", "Zoom"},
    {"com.application.zomato", "Zomato"},
    {"in.swiggy.android", "Swiggy"},
    {"com.zeptoconsumerapp", "Zepto"},
    {"com.bigbasket.mobileapp", "BigBasket"},
    {"com.ixigo.train.ixitrain", "ixigo Trains"},
    {"com.myairtelapp", "Airtel Thanks"},
    {"com.noisefit", "NoiseFit"},
    {"com.phonepe.app", "PhonePe"},
    {"net.one97.paytm", "Paytm"},
    {"com.miui.face", "Face Unlock"},
    {"com.android.camera", "Camera"},
    {"com.android.contacts", "Contacts"},
    {"com.android.providers.contacts", "Contacts Storage"},
    {"com.android.phone", "Phone"},
    {"com.android.server.telecom", "Call Management"},
    {"com.android.systemui", "System UI"},
    {"com.android.settings", "Settings"},
    {"com.android.bluetooth", "Bluetooth"},
    {"com.xiaomi.bluetooth", "Xiaomi Bluetooth"},
    {"com.xiaomi.xmsf", "Xiaomi Service Framework"},
    {"com.xiaomi.mi_connect_service", "Mi Connect"},
    {"com.miui.analytics", "MIUI Analytics"},
    {"com.miui.home", "MIUI Launcher"},
    {"com.miui.securitycenter", "Security"},
    {"com.mediatek.ims", "IMS Calling Service"},
    {"com.microsoft.appmanager", "Link to Windows"},
    {NULL, NULL}
};

static void name_from_package(const char *package, char *out, size_t outsize)
{
    const char *seg = strrchr(package, '.');
    seg = seg ? seg + 1 : package;
    /* skip generic trailing segments */
    static const char *generic[] = {"android", "app", "apps", "mobile", "mobileapp", "user", "main", "client", NULL};
    for (int g = 0; generic[g]; g++) {
        if (strcmp(seg, generic[g]) == 0 && seg > package) {
            const char *end = seg - 1;        /* the '.' */
            const char *start = end;
            while (start > package && *(start - 1) != '.') start--;
            size_t len = (size_t)(end - start);
            if (len > 0 && len < outsize) {
                memcpy(out, start, len);
                out[len] = '\0';
                if (out[0] >= 'a' && out[0] <= 'z') out[0] -= 32;
                return;
            }
        }
    }
    snprintf(out, outsize, "%s", seg);
    if (out[0] >= 'a' && out[0] <= 'z') out[0] -= 32;
}

static void resolve_app_name(const char *package, char *out, size_t outsize)
{
    for (int i = 0; known_names[i].pkg; i++) {
        if (strcmp(known_names[i].pkg, package) == 0) {
            snprintf(out, outsize, "%s", known_names[i].name);
            return;
        }
    }

    const char *buf = pkg_dump_get(package, NULL);
    if (!buf) {
        name_from_package(package, out, outsize);
        return;
    }

    const char *label = strstr(buf, "applicationLabel=");
    if (label) {
        label += 17;
        const char *end = strchr(label, '\n');
        if (!end) end = strchr(label, '\r');
        if (end) {
            size_t len = end - label;
            if (len > 0 && label[len - 1] == '\r') len--;
            if (len >= outsize) len = outsize - 1;
            memcpy(out, label, len);
            out[len] = '\0';
            return;
        }
    }
    name_from_package(package, out, outsize);
}

static time_t parse_appops_timestamp(const char *ts)
{
    struct tm tm = {0};
    if (sscanf(ts, "%d-%d-%d %d:%d:%d",
               &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
               &tm.tm_hour, &tm.tm_min, &tm.tm_sec) == 6) {
        tm.tm_year -= 1900;
        tm.tm_mon -= 1;
        tm.tm_isdst = -1;
        return mktime(&tm);
    }
    return 0;
}

/* "+827ms", "+8s218ms", "+7m55s816ms", "+1h2m3s4ms", "+1d2h..." -> milliseconds */
static int parse_duration(const char *dur)
{
    long long total = 0;
    const char *p = dur;
    if (*p == '+') p++;
    while (*p) {
        char *end;
        long v = strtol(p, &end, 10);
        if (end == p) break;
        p = end;
        if (strncmp(p, "ms", 2) == 0)      { total += v;                  p += 2; }
        else if (*p == 's')                 { total += v * 1000LL;         p += 1; }
        else if (*p == 'm')                 { total += v * 60000LL;        p += 1; }
        else if (*p == 'h')                 { total += v * 3600000LL;      p += 1; }
        else if (*p == 'd')                 { total += v * 86400000LL;     p += 1; }
        else break;
    }
    if (total > 2147483647LL) total = 2147483647LL;
    return (int)total;
}

static int parse_appops_dump(char *buf, const char *op, const char *sensor,
                             const char *device_name,
                             spectra_sensor_event_t *out, int count, int max)
{
    size_t oplen = strlen(op);
    char current_pkg[256] = {0};
    int current_uid = 0;
    time_t now = time(NULL);
    time_t cutoff = now - 86400;

    char *line = buf;
    while (*line && count < max) {
        char *eol = strchr(line, '\n');
        if (eol) *eol = '\0';

        /* trim \r */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\r') line[len - 1] = '\0';

        /* package header: "  Uid 10234 (com.example.app):" or "Uid 10234:" */
        char *uid_match = strstr(line, "Uid ");
        if (uid_match && (uid_match == line || *(uid_match - 1) == ' ')) {
            int uid_val = -1, user_id = 0, app_id = 0;
            char pkg[256] = {0};
            /* "Uid 1000:" (numeric) or "Uid u0a293:" (user 0, app id 293 -> 10293) */
            if (sscanf(uid_match, "Uid u%da%d", &user_id, &app_id) == 2)
                uid_val = user_id * 100000 + 10000 + app_id;
            else
                sscanf(uid_match, "Uid %d (%255[^)])", &uid_val, pkg);
            if (uid_val >= 0) {
                current_uid = uid_val;
                if (pkg[0]) {
                    snprintf(current_pkg, sizeof(current_pkg), "%s", pkg);
                } else {
                    const char *resolved = uid_to_package(uid_val);
                    if (resolved)
                        snprintf(current_pkg, sizeof(current_pkg), "%s", resolved);
                    else
                        snprintf(current_pkg, sizeof(current_pkg), "uid:%d", uid_val);
                }
            }
        }

        /* package line (Android 10+): "    Package com.example.app:" */
        char *pkg_match = strstr(line, "Package ");
        if (pkg_match && (pkg_match == line || *(pkg_match - 1) == ' ')) {
            char pkg[256] = {0};
            if (sscanf(pkg_match, "Package %255[^:]:", pkg) == 1 && pkg[0])
                snprintf(current_pkg, sizeof(current_pkg), "%s", pkg);
        }

        /* sensor op line: "      CAMERA (allow): " */
        const char *tl = line;
        while (*tl == ' ') tl++;
        int is_op = (strncmp(tl, op, oplen) == 0 && strncmp(tl + oplen, " (", 2) == 0);

        if (is_op && current_pkg[0]) {

            /* read subsequent Access: lines */
            char *next = eol ? eol + 1 : NULL;
            while (next && *next && count < max) {
                char *neol = strchr(next, '\n');
                if (neol) *neol = '\0';

                len = strlen(next);
                if (len > 0 && next[len - 1] == '\r') next[len - 1] = '\0';

                if (strstr(next, "Access:") == NULL) {
                    /* skip attribution wrappers: "null=[", "tag=[", "]", and Reject: lines */
                    const char *t = next;
                    while (*t == ' ') t++;
                    size_t tl = strlen(t);
                    int wrapper = (tl >= 2 && strcmp(t + tl - 2, "=[") == 0) ||
                                  strcmp(t, "]") == 0 ||
                                  strncmp(t, "Reject:", 7) == 0;
                    if (!wrapper) {
                        /* restore the newline so the outer loop re-reads this line intact */
                        if (neol) *neol = '\n';
                        break;
                    }
                    if (neol) next = neol + 1;
                    else break;
                    continue;
                }

                int bg = (strstr(next, "[bg") != NULL);

                char *ts_start = strchr(next, ']');
                if (ts_start) {
                    ts_start++;
                    while (*ts_start == ' ') ts_start++;

                    time_t access_time = parse_appops_timestamp(ts_start);
                    if (access_time < cutoff) {
                        if (neol) next = neol + 1;
                        else break;
                        continue;
                    }

                    int dur_ms = 0;
                    char *dur_str = strstr(next, "duration=");
                    if (dur_str) {
                        dur_str += 9;
                        dur_ms = parse_duration(dur_str);
                    }

                    int active = 0;
                    if (dur_ms > 0 && access_time + (dur_ms / 1000) + 2 >= now)
                        active = 1;
                    if (strstr(next, "running") != NULL)
                        active = 1;

                    spectra_sensor_event_t *ev = &out[count];
                    memset(ev, 0, sizeof(*ev));
                    ev->uid = current_uid;
                    snprintf(ev->package, sizeof(ev->package), "%s", current_pkg);
                    resolve_app_name(current_pkg, ev->app_name, sizeof(ev->app_name));
                    snprintf(ev->sensor_type, sizeof(ev->sensor_type), "%s", sensor);
                    snprintf(ev->device, sizeof(ev->device), "%s", device_name);
                    ev->last_access = access_time;
                    ev->duration_ms = dur_ms;
                    ev->currently_active = active;
                    ev->background = bg;
                    ev->access_count = 1;
                    ev->detected_at = now;
                    count++;
                }

                if (neol) next = neol + 1;
                else break;
            }
            if (next) {
                line = next;
                continue;
            }
        }

        line = eol ? eol + 1 : line + strlen(line);
    }
    return count;
}

/* Parse the "Discrete accesses:" section (Android 11+): every access in the
 * retention window, minute-quantised. Format:
 *   Uid: 10175
 *     Package: com.whatsapp
 *       CAMERA
 *         Attribution: null
 *           Access [top-s] at 2026-11-11 19:51:00.000
 *           Access [top-s] at 2026-11-11 19:51:00.000 for 60000 milliseconds
 * The start record and the "for N milliseconds" record describe the same
 * access, so a duration line directly following an identical start line is
 * merged into it. */
static int parse_discrete_dump(char *buf, const char *want_op, const char *sensor,
                               const char *device_name,
                               spectra_sensor_event_t *out, int count, int max)
{
    time_t now = time(NULL);
    time_t cutoff = now - 86400;
    int cur_uid = -1;
    char cur_pkg[256] = {0};
    int in_op = 0;
    char last_flag[16] = {0};
    time_t last_ts = 0;
    int last_idx = -1;      /* event the previous start record went into */

    char *line = buf;
    while (*line) {
        char *eol = strchr(line, '\n');
        if (eol) *eol = '\0';
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\r') line[len - 1] = '\0';

        const char *t = line;
        while (*t == ' ') t++;

        if (strncmp(t, "Uid:", 4) == 0) {
            sscanf(t, "Uid: %d", &cur_uid);
            in_op = 0;
        } else if (strncmp(t, "Package:", 8) == 0) {
            cur_pkg[0] = '\0';
            sscanf(t, "Package: %255s", cur_pkg);
            in_op = 0;
        } else if (strncmp(t, "Access", 6) != 0 && strncmp(t, "Attribution", 11) != 0 &&
                   *t && strchr(t, ' ') == NULL) {
            /* bare op name line */
            in_op = (strcmp(t, want_op) == 0);
            last_idx = -1;
        } else if (in_op && cur_pkg[0] && strncmp(t, "Access", 6) == 0) {
            char flag[16] = {0};
            int y, mo, d, h, mi, se;
            long dur_ms = 0;
            if (sscanf(t, "Access [%15[^]]] at %d-%d-%d %d:%d:%d",
                       flag, &y, &mo, &d, &h, &mi, &se) == 7) {
                struct tm tm = {0};
                tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d;
                tm.tm_hour = h; tm.tm_min = mi; tm.tm_sec = se; tm.tm_isdst = -1;
                time_t ts = mktime(&tm);
                const char *forp = strstr(t, " for ");
                if (forp) sscanf(forp, " for %ld", &dur_ms);

                if (ts >= cutoff) {
                    if (forp && last_idx >= 0 && last_ts == ts &&
                        strcmp(last_flag, flag) == 0) {
                        /* duration record for the access we just counted */
                        if (out[last_idx].last_access == ts)
                            out[last_idx].duration_ms = (int)dur_ms;
                        last_idx = -1;
                    } else {
                        /* one event per (package, sensor); merge into it */
                        int k = -1;
                        for (int j = 0; j < count; j++) {
                            if (strcmp(out[j].package, cur_pkg) == 0 &&
                                strcmp(out[j].sensor_type, sensor) == 0) { k = j; break; }
                        }
                        if (k < 0) {
                            if (count >= max) { line = eol ? eol + 1 : line + strlen(line); continue; }
                            k = count++;
                            spectra_sensor_event_t *ev = &out[k];
                            memset(ev, 0, sizeof(*ev));
                            ev->uid = cur_uid;
                            snprintf(ev->package, sizeof(ev->package), "%s", cur_pkg);
                            snprintf(ev->sensor_type, sizeof(ev->sensor_type), "%s", sensor);
                            snprintf(ev->device, sizeof(ev->device), "%s", device_name);
                            ev->last_access = ts;
                            ev->duration_ms = (int)dur_ms;
                            ev->detected_at = now;
                            ev->access_count = 0;
                        }
                        out[k].access_count++;
                        if (ts >= out[k].last_access) {
                            out[k].last_access = ts;
                            out[k].duration_ms = (int)dur_ms;
                        }
                        if (strncmp(flag, "bg", 2) == 0) out[k].background = 1;
                        last_idx = k;
                        last_ts = ts;
                        snprintf(last_flag, sizeof(last_flag), "%s", flag);
                    }
                }
            }
        }

        line = eol ? eol + 1 : line + strlen(line);
    }
    return count;
}

int android_scan_sensors(spectra_sensor_event_t *out, int max)
{
    static char buf[ADB_BUF_SIZE];
    static spectra_sensor_event_t legacy[SPECTRA_MAX_EVENTS];
    int count = 0;

    /* Per-op dumps are ~100-250 KB; the full "dumpsys appops" is multi-MB.
     * The number after --include-discrete caps entries per attribution, so
     * pass a large one and filter by time ourselves. */
    const char *ops[] = {"CAMERA", "RECORD_AUDIO", "FINE_LOCATION", "COARSE_LOCATION", "READ_CONTACTS"};
    const char *sensor_names[] = {"camera", "mic", "location", "location", "contacts"};
    const char *device_names[] = {"camera", "microphone", "location", "location", "contacts"};
    const int n_ops = 5;

    for (int i = 0; i < n_ops && count < max; i++) {
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "dumpsys appops --op %s --include-discrete 100000", ops[i]);
        int n = adb_exec(cmd, buf, sizeof(buf));
        if (n <= 0) continue;

        /* split: [legacy state dump][Discrete accesses: ...] */
        char *disc = strstr(buf, "\nDiscrete accesses:");
        if (disc) { *disc = '\0'; disc++; }

        /* legacy section: last access per app state, precise timestamps,
         * durations and the "running" marker */
        int n_legacy = parse_appops_dump(buf, ops[i], sensor_names[i], device_names[i],
                                         legacy, 0, SPECTRA_MAX_EVENTS);

        if (!disc) {
            /* no discrete history (Android < 11): legacy entries are all we have */
            for (int j = 0; j < n_legacy && count < max; j++)
                if (strcmp(legacy[j].sensor_type, sensor_names[i]) == 0)
                    out[count++] = legacy[j];
            continue;
        }

        int before = count;
        count = parse_discrete_dump(disc, ops[i], sensor_names[i], device_names[i],
                                    out, count, max);
        (void)before;

        /* enrich with the precise legacy data: exact time and duration of the
         * latest access, plus the currently-active flag */
        for (int j = 0; j < n_legacy; j++) {
            if (strcmp(legacy[j].sensor_type, sensor_names[i]) != 0) continue;
            int k = -1;
            for (int m = 0; m < count; m++)
                if (strcmp(out[m].package, legacy[j].package) == 0 &&
                    strcmp(out[m].sensor_type, sensor_names[i]) == 0) { k = m; break; }
            if (k < 0) {
                /* legacy access with no discrete counterpart */
                if (count < max) { out[count] = legacy[j]; out[count].access_count = 1; count++; }
                continue;
            }
            long diff = (long)legacy[j].last_access - (long)out[k].last_access;
            if (diff >= -5 && diff < 120) {
                /* same access as the latest discrete one: take the precise values */
                out[k].last_access = legacy[j].last_access;
                out[k].duration_ms = legacy[j].duration_ms;
            } else if (diff >= 120) {
                /* newer than anything in discrete history (not flushed yet) */
                out[k].last_access = legacy[j].last_access;
                out[k].duration_ms = legacy[j].duration_ms;
                out[k].access_count++;
            }
            if (legacy[j].currently_active) out[k].currently_active = 1;
            if (legacy[j].background) out[k].background = 1;
        }

        /* discrete entries are minute-quantised (durations are multiples of
         * 60 s); keep only the precise durations we matched above */
        for (int k = 0; k < count; k++)
            if (out[k].duration_ms > 0 && out[k].duration_ms % 60000 == 0)
                out[k].duration_ms = 0;

    }
    int n;

    /* collapse to one event per (package, sensor): the discrete history can
     * hold hundreds of location reads per app per day */
    for (int i = 0; i < count; i++) {
        if (out[i].access_count <= 0) out[i].access_count = 1;
        for (int j = 0; j < i; j++) {
            if (strcmp(out[j].package, out[i].package) != 0 ||
                strcmp(out[j].sensor_type, out[i].sensor_type) != 0)
                continue;
            out[j].access_count += out[i].access_count;
            if (out[i].last_access > out[j].last_access) {
                out[j].last_access = out[i].last_access;
                out[j].duration_ms = out[i].duration_ms;
            } else if (out[j].duration_ms == 0 && out[i].duration_ms > 0) {
                out[j].duration_ms = out[i].duration_ms;
            }
            if (out[i].background) out[j].background = 1;
            if (out[i].currently_active) out[j].currently_active = 1;
            if (!out[j].app_name[0] && out[i].app_name[0])
                snprintf(out[j].app_name, sizeof(out[j].app_name), "%s", out[i].app_name);
            memmove(&out[i], &out[i + 1], sizeof(out[0]) * (count - i - 1));
            count--;
            i--;
            break;
        }
    }

    /* display names: one batched fetch for every package seen */
    {
        const char *pkgs[SPECTRA_MAX_EVENTS];
        for (int k = 0; k < count; k++) pkgs[k] = out[k].package;
        prefetch_packages(pkgs, count);
        for (int k = 0; k < count; k++)
            if (!out[k].app_name[0])
                resolve_app_name(out[k].package, out[k].app_name, sizeof(out[k].app_name));
    }

    /* also check currently active via query-op (camera / mic only) */
    static char qbuf[4096];

    for (int i = 0; i < 2 && count < max; i++) {
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "cmd appops query-op %s", ops[i]);
        n = adb_exec(cmd, qbuf, sizeof(qbuf));
        if (n <= 0) continue;

        char *qline = strtok(qbuf, "\n");
        while (qline && count < max) {
            size_t qlen = strlen(qline);
            if (qlen > 0 && qline[qlen - 1] == '\r') qline[qlen - 1] = '\0';

            if (qline[0] && strcmp(qline, "No operations.") != 0) {
                /* check if we already have an active entry for this pkg */
                int already = 0;
                for (int j = 0; j < count; j++) {
                    if (strcmp(out[j].package, qline) == 0 &&
                        strcmp(out[j].sensor_type, sensor_names[i]) == 0 &&
                        out[j].currently_active)
                        already = 1;
                }
                if (!already) {
                    spectra_sensor_event_t *ev = &out[count];
                    memset(ev, 0, sizeof(*ev));
                    snprintf(ev->package, sizeof(ev->package), "%s", qline);
                    resolve_app_name(qline, ev->app_name, sizeof(ev->app_name));
                    snprintf(ev->sensor_type, sizeof(ev->sensor_type), "%s", sensor_names[i]);
                    snprintf(ev->device, sizeof(ev->device), "%s", device_names[i]);
                    ev->last_access = time(NULL);
                    ev->currently_active = 1;
                    ev->detected_at = time(NULL);
                    count++;
                }
            }
            qline = strtok(NULL, "\n");
        }
    }

    return count;
}

static void hex_to_ip(const char *hex, char *out, size_t outsize, int ipv6)
{
    if (!ipv6) {
        unsigned int addr;
        sscanf(hex, "%X", &addr);
        struct in_addr in;
        in.s_addr = addr;
        inet_ntop(AF_INET, &in, out, outsize);
    } else {
        unsigned char bytes[16];
        for (int i = 0; i < 16; i++) {
            unsigned int b;
            sscanf(hex + i * 2, "%2X", &b);
            bytes[i] = (unsigned char)b;
        }
        /* /proc/net/tcp6 stores in 4-byte groups, each in host order */
        unsigned char reordered[16];
        for (int g = 0; g < 4; g++) {
            reordered[g * 4 + 0] = bytes[g * 4 + 3];
            reordered[g * 4 + 1] = bytes[g * 4 + 2];
            reordered[g * 4 + 2] = bytes[g * 4 + 1];
            reordered[g * 4 + 3] = bytes[g * 4 + 0];
        }
        inet_ntop(AF_INET6, reordered, out, outsize);
    }
}

static void reverse_dns(const char *ip, char *out, size_t outsize)
{
    struct sockaddr_storage ss;
    memset(&ss, 0, sizeof(ss));
    socklen_t slen;

    struct in_addr addr4;
    struct in6_addr addr6;

    if (inet_pton(AF_INET, ip, &addr4) == 1) {
        struct sockaddr_in *s = (struct sockaddr_in *)&ss;
        s->sin_family = AF_INET;
        s->sin_addr = addr4;
        slen = sizeof(struct sockaddr_in);
    } else if (inet_pton(AF_INET6, ip, &addr6) == 1) {
        struct sockaddr_in6 *s = (struct sockaddr_in6 *)&ss;
        s->sin6_family = AF_INET6;
        s->sin6_addr = addr6;
        slen = sizeof(struct sockaddr_in6);
    } else {
        snprintf(out, outsize, "unknown");
        return;
    }

    if (getnameinfo((struct sockaddr *)&ss, slen, out, outsize,
                    NULL, 0, NI_NAMEREQD) != 0)
        snprintf(out, outsize, "unknown");
}

int android_scan_network(spectra_net_event_t *out, int max)
{
    build_uid_pkg_map();

    int count = 0;
    static char buf[ADB_BUF_SIZE];
    const char *tcp_files[] = {"cat /proc/net/tcp", "cat /proc/net/tcp6"};
    int ipv6_flags[] = {0, 1};

    for (int f = 0; f < 2 && count < max; f++) {
        int n = adb_exec(tcp_files[f], buf, sizeof(buf));
        if (n <= 0) continue;

        char *line = strtok(buf, "\n");
        line = strtok(NULL, "\n"); /* skip header */

        while (line && count < max) {
            size_t len = strlen(line);
            if (len > 0 && line[len - 1] == '\r') line[len - 1] = '\0';

            unsigned int idx, local_port, remote_port, state, uid;
            char local_hex[65], remote_hex[65];

            int matched = sscanf(line,
                " %u: %64[0-9A-Fa-f]:%X %64[0-9A-Fa-f]:%X %X %*s %*s %*s %u",
                &idx, local_hex, &local_port, remote_hex, &remote_port,
                &state, &uid);

            if (matched >= 7 && state == 0x01) { /* ESTABLISHED */
                const char *pkg = uid_to_package(uid);
                if (pkg) {
                    spectra_net_event_t *ev = &out[count];
                    memset(ev, 0, sizeof(*ev));
                    ev->uid = uid;
                    snprintf(ev->package, sizeof(ev->package), "%s", pkg);
                    ev->remote_port = remote_port;
                    hex_to_ip(remote_hex, ev->remote_ip, sizeof(ev->remote_ip), ipv6_flags[f]);
                    reverse_dns(ev->remote_ip, ev->remote_host, sizeof(ev->remote_host));
                    ev->detected_at = time(NULL);
                    count++;
                }
            }
            line = strtok(NULL, "\n");
        }
    }

    /* display names: one batched fetch */
    {
        const char *pkgs[SPECTRA_MAX_EVENTS];
        for (int i = 0; i < count; i++) pkgs[i] = out[i].package;
        prefetch_packages(pkgs, count);
        for (int i = 0; i < count; i++)
            resolve_app_name(out[i].package, out[i].app_name, sizeof(out[i].app_name));
    }

    /* get per-uid byte counts from netstats (last 24h, tag 0x0 only).
     * Format (Android 10+):
     *   UID stats:
     *     ident=[{type=1, ...}] uid=10155 set=DEFAULT tag=0x0
     *       NetworkStatsHistory: bucketDuration=7200
     *         st=1794405600 rb=5324 rp=15 tb=5577 tp=12 op=0
     */
    static char nsbuf[ADB_BUF_SIZE];
    int n = adb_exec("dumpsys netstats detail", nsbuf, sizeof(nsbuf));
    if (n > 0) {
        int cur_uid = -1;
        int cur_type = -1;          /* 0 = mobile, 1 = wifi */
        long bucket = 7200;
        time_t cutoff = time(NULL) - 86400;
        int in_uid_section = 0;

        char *nsline = strtok(nsbuf, "\n");
        while (nsline) {
            size_t nslen = strlen(nsline);
            if (nslen > 0 && nsline[nslen - 1] == '\r') nsline[nslen - 1] = '\0';

            if (strncmp(nsline, "UID stats:", 10) == 0) { in_uid_section = 1; cur_uid = -1; }
            else if (nsline[0] != ' ' && nsline[0] != '\0') { in_uid_section = 0; cur_uid = -1; }

            if (!in_uid_section) { nsline = strtok(NULL, "\n"); continue; }

            char *identp = strstr(nsline, "ident=");
            if (identp) {
                cur_uid = -1;
                char *uidp = strstr(nsline, " uid=");
                char *tagp = strstr(nsline, "tag=");
                int uid_val = -1;
                if (uidp) sscanf(uidp, " uid=%d", &uid_val);
                if (uid_val >= 0 && tagp && strncmp(tagp, "tag=0x0", 7) == 0 &&
                    (tagp[7] == '\0' || tagp[7] == ' '))
                    cur_uid = uid_val;
                char *typep = strstr(nsline, "type=");
                cur_type = -1;
                if (typep) sscanf(typep, "type=%d", &cur_type);
                nsline = strtok(NULL, "\n");
                continue;
            }

            char *bdp = strstr(nsline, "bucketDuration=");
            if (bdp) { sscanf(bdp, "bucketDuration=%ld", &bucket); nsline = strtok(NULL, "\n"); continue; }

            char *stp = strstr(nsline, "st=");
            if (stp && cur_uid >= 0) {
                long st = 0; unsigned long long rb = 0, tb = 0;
                sscanf(stp, "st=%ld", &st);
                char *rbp = strstr(nsline, "rb=");
                char *tbp = strstr(nsline, "tb=");
                if (rbp) sscanf(rbp, "rb=%llu", &rb);
                if (tbp) sscanf(tbp, "tb=%llu", &tb);
                if (st + bucket >= cutoff) {
                    for (int i = 0; i < count; i++) {
                        if (out[i].uid == cur_uid) {
                            out[i].bytes_recv += rb;
                            out[i].bytes_sent += tb;
                            if (cur_type == 1)
                                snprintf(out[i].interface, sizeof(out[i].interface), "wifi");
                            else if (cur_type == 0 && !out[i].interface[0])
                                snprintf(out[i].interface, sizeof(out[i].interface), "mobile");
                        }
                    }
                }
            }
            nsline = strtok(NULL, "\n");
        }
    }

    return count;
}

int android_get_process_info(const char *package, spectra_process_info_t *out)
{
    memset(out, 0, sizeof(*out));
    snprintf(out->package, sizeof(out->package), "%s", package);

    const char *ibuf = NULL;
    const char *buf = pkg_dump_get(package, &ibuf);
    if (!buf) {
        snprintf(out->app_name, sizeof(out->app_name), "%s", package);
        return -1;
    }

    /* app label */
    resolve_app_name(package, out->app_name, sizeof(out->app_name));

    /* system app */
    out->is_system_app = (strstr(buf, "SYSTEM") != NULL ||
                          strstr(buf, "flags=[ SYSTEM") != NULL);

    /* first install time */
    const char *fit = strstr(buf, "firstInstallTime=");
    if (fit) {
        fit += 17;
        const char *end = strchr(fit, '\n');
        if (end) {
            if (end > fit && *(end - 1) == '\r') end--;
            size_t len = end - fit;
            if (len >= sizeof(out->first_install)) len = sizeof(out->first_install) - 1;
            memcpy(out->first_install, fit, len);
            out->first_install[len] = '\0';
        }
    }

    /* install source: "installerPackageName=com.android.vending" in the dump */
    (void)ibuf;
    char installer[128] = {0};
    const char *ip = strstr(buf, "installerPackageName=");
    if (ip) sscanf(ip + 21, "%127[^ \r\n]", installer);
    if (strcmp(installer, "com.android.vending") == 0)
        snprintf(out->install_source, sizeof(out->install_source), "Play Store");
    else if (strcmp(installer, "com.xiaomi.market") == 0 || strcmp(installer, "com.xiaomi.mipicks") == 0)
        snprintf(out->install_source, sizeof(out->install_source), "GetApps (Xiaomi)");
    else if (strcmp(installer, "com.amazon.venezia") == 0)
        snprintf(out->install_source, sizeof(out->install_source), "Amazon Appstore");
    else if (strcmp(installer, "com.sec.android.app.samsungapps") == 0)
        snprintf(out->install_source, sizeof(out->install_source), "Galaxy Store");
    else if (out->is_system_app || !installer[0] || strcmp(installer, "null") == 0)
        snprintf(out->install_source, sizeof(out->install_source), out->is_system_app ? "pre-installed" : "unknown");
    else
        snprintf(out->install_source, sizeof(out->install_source), "sideloaded (%s)", installer);

    /* permissions */
    out->permissions[0] = '\0';
    const char *perm = buf;
    size_t perm_off = 0;
    while ((perm = strstr(perm, "android.permission.")) != NULL && perm_off < sizeof(out->permissions) - 64) {
        const char *end = perm;
        while (*end && *end != ':' && *end != '\n' && *end != ' ' && *end != ',')
            end++;
        const char *granted = strstr(end, "granted=true");
        if (granted && granted - end < 80) {
            /* extract short name */
            const char *short_name = perm + 19; /* skip "android.permission." */
            size_t nlen = end - short_name;
            if (perm_off > 0) {
                out->permissions[perm_off++] = ',';
                out->permissions[perm_off++] = ' ';
            }
            if (perm_off + nlen < sizeof(out->permissions) - 1) {
                /* lowercase */
                for (size_t i = 0; i < nlen; i++) {
                    char c = short_name[i];
                    out->permissions[perm_off++] = (c >= 'A' && c <= 'Z') ? c + 32 : c;
                }
            }
        }
        perm = end;
    }
    out->permissions[perm_off] = '\0';

    return 0;
}

int android_get_device_info(spectra_device_info_t *info)
{
    memset(info, 0, sizeof(*info));
    static char buf[1024];

    if (adb_exec("getprop ro.product.model", buf, sizeof(buf)) > 0) {
        char *end = strchr(buf, '\n');
        if (end) *end = '\0';
        end = strchr(buf, '\r');
        if (end) *end = '\0';
        snprintf(info->device_model, sizeof(info->device_model), "%s", buf);
    }

    if (adb_exec("getprop ro.product.marketname", buf, sizeof(buf)) > 0) {
        char *end = strchr(buf, '\n');
        if (end) *end = '\0';
        end = strchr(buf, '\r');
        if (end) *end = '\0';
        if (buf[0])
            snprintf(info->device_name, sizeof(info->device_name), "%s", buf);
        else
            snprintf(info->device_name, sizeof(info->device_name), "%s", info->device_model);
    }

    if (adb_exec("getprop ro.build.version.release", buf, sizeof(buf)) > 0) {
        char *end = strchr(buf, '\n');
        if (end) *end = '\0';
        end = strchr(buf, '\r');
        if (end) *end = '\0';
        snprintf(info->android_version, sizeof(info->android_version), "%s", buf);
    }

    if (adb_exec("getprop ro.build.version.sdk", buf, sizeof(buf)) > 0) {
        info->api_level = atoi(buf);
    }

    snprintf(info->connection_type, sizeof(info->connection_type), "USB");

    if (adb_exec("id", buf, sizeof(buf)) > 0)
        info->has_root = (strstr(buf, "uid=0") != NULL);

    return 0;
}

int android_is_known_package(const char *pkg)
{
    static char buf[4096];
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "pm path %s", pkg);
    int n = adb_exec(cmd, buf, sizeof(buf));
    return (n > 0 && strstr(buf, "package:") != NULL) ? 1 : 0;
}
