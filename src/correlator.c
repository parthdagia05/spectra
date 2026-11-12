#include "spectra/correlator.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>

/* ------------------------------------------------------------------------
 * Explainable risk model.
 *
 * Every app gets a category (what kind of app it is), every sensor use is
 * judged against what that kind of app is expected to do, and every point
 * added to the score is recorded with a one-line reason. The score is the
 * sum of the reasons, so it can always be explained back to the user.
 * ---------------------------------------------------------------------- */

static const char *allowlist[] = {
    "whatsapp.com", "web.whatsapp.com", "whatsapp.net",
    "signal.org", "chat.signal.org",
    "zoom.us", "zoom.com",
    "meet.google.com", "duo.google.com",
    "facetime.apple.com",
    NULL
};

static int is_allowlisted(const char *host)
{
    for (int i = 0; allowlist[i]; i++)
        if (strstr(host, allowlist[i]) != NULL) return 1;
    return 0;
}

/* ---- categories ------------------------------------------------------ */

typedef struct { const char *prefix; const char *category; } cat_rule_t;

/* prefix match on the package id; first hit wins */
static const cat_rule_t cat_rules[] = {
    {"android",                          "system"},
    {"com.android.",                     "system"},
    {"com.google.android.gms",           "system"},
    {"com.google.android.as",            "system"},
    {"com.google.android.gsf",           "system"},
    {"com.google.android.ext.",          "system"},
    {"com.miui.",                        "system"},
    {"com.xiaomi.",                      "system"},
    {"com.mi.",                          "system"},
    {"com.mediatek.",                    "system"},
    {"com.qualcomm.",                    "system"},
    {"com.samsung.android.",             "system"},
    {"com.sec.android.",                 "system"},
    {"com.oneplus.",                     "system"},
    {"com.oppo.",                        "system"},
    {"com.coloros.",                     "system"},
    {"com.vivo.",                        "system"},
    {"com.whatsapp",                     "messaging"},
    {"org.telegram.",                    "messaging"},
    {"org.thoughtcrime.securesms",       "messaging"},
    {"com.google.android.apps.messaging","messaging"},
    {"com.google.android.apps.tachyon",  "messaging"},
    {"com.google.android.gm",            "messaging"},
    {"com.facebook.orca",                "messaging"},
    {"com.discord",                      "messaging"},
    {"com.Slack",                        "messaging"},
    {"com.microsoft.teams",              "messaging"},
    {"us.zoom.",                         "messaging"},
    {"com.skype.",                       "messaging"},
    {"com.truecaller",                   "callerid"},
    {"com.instagram.",                   "social"},
    {"com.facebook.",                    "social"},
    {"com.snapchat.",                    "social"},
    {"com.zhiliaoapp.musically",         "social"},
    {"com.twitter.",                     "social"},
    {"com.linkedin.",                    "social"},
    {"com.reddit.",                      "social"},
    {"com.pinterest",                    "social"},
    {"com.application.zomato",           "commerce"},
    {"in.swiggy.",                       "commerce"},
    {"com.zeptoconsumerapp",             "commerce"},
    {"com.bigbasket.",                   "commerce"},
    {"com.grofers.",                     "commerce"},
    {"com.amazon.",                      "commerce"},
    {"com.flipkart.",                    "commerce"},
    {"com.myntra.",                      "commerce"},
    {"com.ubercab",                      "commerce"},
    {"com.olacabs.",                     "commerce"},
    {"com.rapido.",                      "commerce"},
    {"com.google.android.apps.maps",     "travel"},
    {"com.ixigo.",                       "travel"},
    {"com.makemytrip",                   "travel"},
    {"cris.org.in.prs.ima",              "travel"},
    {"in.redbus.",                       "travel"},
    {"com.google.android.apps.nbu.paisa","finance"},
    {"com.phonepe.",                     "finance"},
    {"net.one97.paytm",                  "finance"},
    {"com.myairtelapp",                  "utility"},
    {"in.amazon.mShop",                  "commerce"},
    {"com.noisefit",                     "health"},
    {"com.google.android.apps.fitness",  "health"},
    {"com.google.android.youtube",       "media"},
    {"com.spotify.",                     "media"},
    {"com.google.android.apps.photos",   "media"},
    {"com.android.chrome",               "browser"},
    {"org.mozilla.",                     "browser"},
    {"com.brave.",                       "browser"},
    {"com.google.android.googlequicksearchbox", "assistant"},
    {"com.google.android.apps.googleassistant", "assistant"},
    {NULL, NULL}
};

const char *spectra_app_category(const char *package, const spectra_process_info_t *info)
{
    for (int i = 0; cat_rules[i].prefix; i++) {
        if (strncmp(package, cat_rules[i].prefix, strlen(cat_rules[i].prefix)) == 0)
            return cat_rules[i].category;
    }
    if (info && info->is_system_app) return "system";
    return "unknown";
}

/* 0 = expected, even in the background
 * 1 = expected only while the app is in use
 * 2 = not expected for this kind of app */
static int expectation(const char *cat, const char *sensor)
{
    int mic = 2, cam = 2, loc = 2, con = 2;
    /* unknown apps: foreground use of mic/camera/location is not a finding by
     * itself (most legitimate apps are not in the catalogue); background use
     * and contacts still are */
    if      (!strcmp(cat, "unknown"))   { mic = 1; cam = 1; loc = 1; con = 2; }
    else if (!strcmp(cat, "system"))    { mic = 0; cam = 0; loc = 0; con = 0; }
    else if (!strcmp(cat, "messaging")) { mic = 0; cam = 0; loc = 1; con = 0; }
    else if (!strcmp(cat, "callerid"))  { mic = 1; cam = 2; loc = 1; con = 0; }
    else if (!strcmp(cat, "social"))    { mic = 1; cam = 1; loc = 1; con = 2; }
    else if (!strcmp(cat, "commerce"))  { mic = 1; cam = 1; loc = 1; con = 2; }
    else if (!strcmp(cat, "travel"))    { mic = 2; cam = 1; loc = 0; con = 2; }
    else if (!strcmp(cat, "finance"))   { mic = 2; cam = 1; loc = 1; con = 2; }
    else if (!strcmp(cat, "utility"))   { mic = 2; cam = 1; loc = 1; con = 2; }
    else if (!strcmp(cat, "health"))    { mic = 2; cam = 1; loc = 1; con = 2; }
    else if (!strcmp(cat, "media"))     { mic = 1; cam = 1; loc = 1; con = 2; }
    else if (!strcmp(cat, "browser"))   { mic = 1; cam = 1; loc = 1; con = 2; }
    else if (!strcmp(cat, "assistant")) { mic = 0; cam = 1; loc = 1; con = 1; }
    if (!strcmp(sensor, "mic")) return mic;
    if (!strcmp(sensor, "camera")) return cam;
    if (!strcmp(sensor, "location")) return loc;
    if (!strcmp(sensor, "contacts")) return con;
    return 2;
}

static const char *sensor_label(const char *s)
{
    if (!strcmp(s, "mic")) return "microphone";
    return s;
}

static const char *category_label(const char *c)
{
    if (!strcmp(c, "callerid")) return "caller-ID";
    return c;
}

/* "a travel app" / "an assistant app" */
static const char *article(const char *word)
{
    if (!strncmp(word, "uti", 3) || !strncmp(word, "uni", 3) || !strncmp(word, "use", 3)) return "a";
    return strchr("aeiouAEIOU", word[0]) ? "an" : "a";
}

/* "Microphone use" / "Location reads" */
static const char *use_noun(const char *sensor)
{
    if (!strcmp(sensor, "mic")) return "Microphone use";
    if (!strcmp(sensor, "camera")) return "Camera use";
    if (!strcmp(sensor, "location")) return "Location reads";
    if (!strcmp(sensor, "contacts")) return "Contact reads";
    return "Sensor use";
}

/* "Reads your location" / "Uses the microphone" */
static const char *use_phrase(const char *sensor)
{
    if (!strcmp(sensor, "mic")) return "Uses the microphone";
    if (!strcmp(sensor, "camera")) return "Uses the camera";
    if (!strcmp(sensor, "location")) return "Reads your location";
    if (!strcmp(sensor, "contacts")) return "Reads your contacts";
    return "Uses a sensor";
}

/* ---- reason accumulation --------------------------------------------- */

typedef struct {
    int score;
    char *buf;
    size_t cap;
    size_t len;
} reasons_t;

static void add_reason(reasons_t *r, int points, const char *fmt, ...)
{
    char text[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    r->score += points;
    if (r->len + strlen(text) + 8 >= r->cap) return;
    int n = snprintf(r->buf + r->len, r->cap - r->len, "%s%+d %s",
                     r->len ? "\n" : "", points, text);
    if (n > 0) r->len += (size_t)n;
}

static int has_permission(const spectra_process_info_t *info, const char *perm)
{
    if (!info || !info->permissions[0]) return 0;
    const char *p = info->permissions;
    size_t plen = strlen(perm);
    while ((p = strstr(p, perm)) != NULL) {
        int start_ok = (p == info->permissions || *(p - 1) == ' ' || *(p - 1) == ',');
        int end_ok = (p[plen] == '\0' || p[plen] == ',' || p[plen] == ' ');
        if (start_ok && end_ok) return 1;
        p += plen;
    }
    return 0;
}

/* ---- main entry ------------------------------------------------------- */

int spectra_correlate(
    spectra_sensor_event_t *sensors, int n_sensors,
    spectra_net_event_t *net, int n_net,
    spectra_correlation_t *results, int max_results)
{
    return spectra_correlate_ex(sensors, n_sensors, net, n_net, NULL, 0, results, max_results);
}

int spectra_correlate_ex(
    spectra_sensor_event_t *sensors, int n_sensors,
    spectra_net_event_t *net, int n_net,
    spectra_process_info_t *apps, int n_apps,
    spectra_correlation_t *results, int max_results)
{
    int count = 0;

    /* one row per package + sensor_type, keeping the latest event */
    typedef struct {
        char package[256];
        char sensor_type[16];
        spectra_sensor_event_t *ev;
        int access_count;
    } seen_t;
    static seen_t seen[SPECTRA_MAX_EVENTS];
    int n_seen = 0;

    for (int i = 0; i < n_sensors && n_seen < SPECTRA_MAX_EVENTS; i++) {
        int found = -1;
        for (int j = 0; j < n_seen; j++) {
            if (strcmp(seen[j].package, sensors[i].package) == 0 &&
                strcmp(seen[j].sensor_type, sensors[i].sensor_type) == 0) { found = j; break; }
        }
        int ac = sensors[i].access_count > 0 ? sensors[i].access_count : 1;
        if (found >= 0) {
            seen[found].access_count += ac;
            if (sensors[i].last_access > seen[found].ev->last_access)
                seen[found].ev = &sensors[i];
        } else {
            snprintf(seen[n_seen].package, 256, "%s", sensors[i].package);
            snprintf(seen[n_seen].sensor_type, 16, "%s", sensors[i].sensor_type);
            seen[n_seen].ev = &sensors[i];
            seen[n_seen].access_count = ac;
            n_seen++;
        }
    }

    static char done_pkg[SPECTRA_MAX_EVENTS][256];
    int n_done = 0;

    for (int s = 0; s < n_seen && count < max_results; s++) {
        spectra_sensor_event_t *sev = seen[s].ev;

        int already = 0;
        for (int d = 0; d < n_done; d++)
            if (strcmp(done_pkg[d], sev->package) == 0) { already = 1; break; }
        if (already) continue;
        snprintf(done_pkg[n_done++], 256, "%s", sev->package);

        const spectra_process_info_t *info = NULL;
        for (int a = 0; a < n_apps; a++)
            if (strcmp(apps[a].package, sev->package) == 0) { info = &apps[a]; break; }

        const char *cat = spectra_app_category(sev->package, info);
        int is_system = (strcmp(cat, "system") == 0);
        int is_unknown = (strcmp(cat, "unknown") == 0);
        const char *app = sev->app_name[0] ? sev->app_name : sev->package;

        spectra_correlation_t *c = &results[count];
        memset(c, 0, sizeof(*c));
        c->sensor = *sev;
        snprintf(c->category, sizeof(c->category), "%s", category_label(cat));

        reasons_t R = { 0, c->reasons, sizeof(c->reasons), 0 };

        /* ---- network aggregate for this package ---- */
        uint64_t total_sent = 0;
        int n_conn = 0, has_unknown_dest = 0, has_allowlisted = 0;
        char top_dest[256] = {0};
        uint64_t top_bytes = 0;
        for (int n = 0; n < n_net; n++) {
            if (strcmp(net[n].package, sev->package) != 0) continue;
            if (n_conn == 0) c->net = net[n];
            n_conn++;
            /* bytes are per-app totals repeated on each connection */
            if (net[n].bytes_sent > total_sent) total_sent = net[n].bytes_sent;
            if (is_allowlisted(net[n].remote_host)) has_allowlisted = 1;
            else if (strcmp(net[n].remote_host, "unknown") == 0) has_unknown_dest = 1;
            if (net[n].bytes_sent >= top_bytes) {
                top_bytes = net[n].bytes_sent;
                snprintf(top_dest, sizeof(top_dest), "%s",
                         strcmp(net[n].remote_host, "unknown") != 0 ? net[n].remote_host : net[n].remote_ip);
            }
        }

        /* ---- per-sensor judgement ---- */
        int used_mic = 0, used_cam = 0, used_loc = 0, used_con = 0;
        int any_bg_unexpected = 0, any_active_unexpected = 0;
        int loc_count = 0;
        const char *active_sensor = NULL, *bg_sensor = NULL;
        int contacts_unexpected = 0, loc_bg_unexpected = 0, avcam_bg_unexpected = 0;

        for (int i = 0; i < n_seen; i++) {
            if (strcmp(seen[i].package, sev->package) != 0) continue;
            const spectra_sensor_event_t *e = seen[i].ev;
            const char *st = e->sensor_type;
            int exp = expectation(cat, st);
            int ac = seen[i].access_count;

            if (!strcmp(st, "mic")) used_mic = 1;
            if (!strcmp(st, "camera")) used_cam = 1;
            if (!strcmp(st, "location")) { used_loc = 1; loc_count = ac; }
            if (!strcmp(st, "contacts")) used_con = 1;

            if (exp == 2) {
                add_reason(&R, 15, "%s, which %s %s app does not need",
                           use_phrase(st), article(category_label(cat)), category_label(cat));
                if (!strcmp(st, "contacts")) contacts_unexpected = 1;
            } else if (!strcmp(cat, "callerid") && !strcmp(st, "contacts")) {
                add_reason(&R, 15, "Caller-ID apps upload your address book to build their phone directory");
            } else if (exp == 0 && ac > 1 && !is_system) {
                add_reason(&R, 0, "%s (%d times) %s normal for %s %s app",
                           use_noun(st), ac, !strcmp(st, "mic") || !strcmp(st, "camera") ? "is" : "are",
                           article(category_label(cat)), category_label(cat));
            }

            if (e->background) {
                if (exp == 0) {
                    if (!is_system)
                        add_reason(&R, 0, "Background %s access is expected for %s %s app",
                                   sensor_label(st), article(category_label(cat)), category_label(cat));
                } else {
                    int pts = exp == 1 ? 25 : 30;
                    add_reason(&R, pts, "Read %s while you were not using the app",
                               !strcmp(st, "mic") ? "the microphone" : !strcmp(st, "camera") ? "the camera" :
                               !strcmp(st, "location") ? "your location" : "your contacts");
                    any_bg_unexpected = 1;
                    if (!bg_sensor) bg_sensor = st;
                    if (!strcmp(st, "location")) loc_bg_unexpected = 1;
                    if (!strcmp(st, "mic") || !strcmp(st, "camera")) avcam_bg_unexpected = 1;
                }
            }

            if (e->currently_active && (!strcmp(st, "mic") || !strcmp(st, "camera"))) {
                int pts = is_system ? 0 : exp == 0 ? 10 : exp == 1 ? 30 : 40;
                add_reason(&R, pts, "%s is active right now",
                           !strcmp(st, "mic") ? "Microphone" : "Camera");
                if (pts >= 30) { any_active_unexpected = 1; if (!active_sensor) active_sensor = st; }
            }
        }

        /* ---- frequency ---- */
        if (used_loc && !is_system) {
            if (loc_count >= 200)
                add_reason(&R, 15, "Read your location %d times in 24 h", loc_count);
            else if (loc_count >= 50)
                add_reason(&R, 10, "Read your location %d times in 24 h", loc_count);
        }

        /* ---- breadth ---- */
        if (!is_system && used_mic && used_cam && used_loc && used_con)
            add_reason(&R, 10, "Touched every sensor: microphone, camera, location and contacts");
        else if (is_unknown && used_mic && used_cam)
            add_reason(&R, 20, "Unrecognised app used both microphone and camera");

        if (is_unknown) {
            if (R.score > 0)
                add_reason(&R, 15, "App is not in spectra's catalogue, so its behaviour can't be vouched for");
            else
                add_reason(&R, 5, "App is not in spectra's catalogue; nothing unusual seen");
        }

        /* ---- network ---- */
        if (n_conn > 0) {
            char sent_str[32];
            if (total_sent >= 1048576) snprintf(sent_str, sizeof(sent_str), "%.1f MB", total_sent / 1048576.0);
            else if (total_sent >= 1024) snprintf(sent_str, sizeof(sent_str), "%.1f KB", total_sent / 1024.0);
            else snprintf(sent_str, sizeof(sent_str), "%llu bytes", (unsigned long long)total_sent);

            if (has_allowlisted)
                add_reason(&R, 0, "Traffic goes to a known service (%s)", top_dest);
            else if (has_unknown_dest && !is_system)
                add_reason(&R, 10, "Sent %s to a server with no hostname (%s)", sent_str, top_dest);
            else if ((used_mic || used_cam) && total_sent >= 1048576 && !is_system)
                add_reason(&R, 10, "Uploaded %s today while also using %s", sent_str,
                           used_mic && used_cam ? "microphone and camera" : used_mic ? "the microphone" : "the camera");
            else if (!is_system)
                add_reason(&R, 0, "Sent %s to %s", sent_str, top_dest);
        }

        /* ---- install source & permissions ---- */
        if (info && !is_system) {
            if (strncmp(info->install_source, "sideloaded", 10) == 0)
                add_reason(&R, 20, "Installed outside an app store (%s)", info->install_source);
            else if (strcmp(info->install_source, "unknown") == 0)
                add_reason(&R, 5, "Installer unknown");

            if (has_permission(info, "read_sms") &&
                strcmp(cat, "messaging") != 0 && strcmp(cat, "callerid") != 0)
                add_reason(&R, 10, "Holds permission to read your SMS");
            if (has_permission(info, "read_call_log") &&
                strcmp(cat, "messaging") != 0 && strcmp(cat, "callerid") != 0)
                add_reason(&R, 10, "Holds permission to read your call log");
        }

        if (R.score == 0)
            add_reason(&R, 0, "Everything seen is normal for %s %s app", article(category_label(cat)), category_label(cat));

        int risk = R.score > 100 ? 100 : R.score;
        c->risk_score = risk;

        snprintf(c->verdict, sizeof(c->verdict), "%s",
                 risk >= 50 ? "high" : risk >= 25 ? "review" : risk >= 15 ? "low" : "expected");

        /* ---- one-line summary ---- */
        {
            const char *names[4]; int nn = 0;
            if (used_mic) names[nn++] = "microphone";
            if (used_cam) names[nn++] = "camera";
            if (used_loc) names[nn++] = "location";
            if (used_con) names[nn++] = "contacts";
            if (nn == 0) names[nn++] = sev->sensor_type;
            char list[128] = {0};
            for (int i = 0; i < nn; i++) {
                size_t l = strlen(list);
                snprintf(list + l, sizeof(list) - l, "%s%s",
                         i == 0 ? "" : (i == nn - 1 ? " and " : ", "), names[i]);
            }
            if (list[0] >= 'a' && list[0] <= 'z') list[0] -= 32;
            snprintf(c->explanation, sizeof(c->explanation), "%s used by this %s app.%s%s",
                     list, category_label(cat),
                     any_bg_unexpected ? " Background access detected." : "",
                     any_active_unexpected ? " Active right now." : "");
        }

        /* ---- recommended action, highest priority first ---- */
        if (any_active_unexpected)
            snprintf(c->action, sizeof(c->action),
                     "Close %s now and remove its %s permission (Settings > Apps > %s > Permissions).",
                     app, sensor_label(active_sensor), app);
        else if (loc_bg_unexpected)
            snprintf(c->action, sizeof(c->action),
                     "Set %s's Location permission to \"Allow only while using the app\" (Settings > Apps > %s > Permissions > Location).",
                     app, app);
        else if (avcam_bg_unexpected)
            snprintf(c->action, sizeof(c->action),
                     "Remove %s's %s permission; it used it while you were not using the app.",
                     app, sensor_label(bg_sensor));
        else if (contacts_unexpected)
            snprintf(c->action, sizeof(c->action),
                     "Remove %s's Contacts permission; %s %s app does not need your address book.",
                     app, article(category_label(cat)), category_label(cat));
        else if (info && !is_system && strncmp(info->install_source, "sideloaded", 10) == 0)
            snprintf(c->action, sizeof(c->action),
                     "Uninstall %s if you do not recognise it; it was not installed from an app store.", app);
        else if (used_loc && loc_count >= 50 && !is_system)
            snprintf(c->action, sizeof(c->action),
                     "Consider \"Allow only while using the app\" for %s's location.", app);
        else if (has_unknown_dest && !is_system)
            snprintf(c->action, sizeof(c->action),
                     "Review where %s sends data; its top destination has no hostname.", app);
        else if (is_unknown)
            snprintf(c->action, sizeof(c->action),
                     "Check %s's permissions and remove any it does not need.", app);
        else
            snprintf(c->action, sizeof(c->action), "No action needed.");

        count++;
    }

    /* sort by risk score descending */
    for (int i = 0; i < count - 1; i++)
        for (int j = i + 1; j < count; j++)
            if (results[j].risk_score > results[i].risk_score) {
                spectra_correlation_t tmp = results[i];
                results[i] = results[j];
                results[j] = tmp;
            }

    return count;
}
