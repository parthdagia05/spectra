#include "spectra/report.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <cjson/cJSON.h>

static void print_separator(void)
{
    printf("\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\n");
}

static const char *format_bytes(uint64_t bytes, char *buf, size_t bufsz)
{
    if (bytes >= 1048576)
        snprintf(buf, bufsz, "%.1f MB", bytes / 1048576.0);
    else if (bytes >= 1024)
        snprintf(buf, bufsz, "%.1f KB", bytes / 1024.0);
    else
        snprintf(buf, bufsz, "%llu B", (unsigned long long)bytes);
    return buf;
}

/* one line per app for a given sensor type: latest access, longest duration,
 * flagged active/background if any of its events are */
static void print_sensor_group(const spectra_report_t *r, const char *type,
                               const char *header, const char *none_msg)
{
    int has_any = 0;
    for (int i = 0; i < r->n_sensors; i++) {
        const spectra_sensor_event_t *e = &r->sensors[i];
        if (strcmp(e->sensor_type, type) != 0) continue;

        /* already printed as part of an earlier event's group? */
        int dup = 0;
        for (int j = 0; j < i; j++)
            if (strcmp(r->sensors[j].package, e->package) == 0 &&
                strcmp(r->sensors[j].sensor_type, type) == 0) { dup = 1; break; }
        if (dup) continue;

        int acc_count = 0, max_dur = 0, active = 0, bg = 0;
        time_t latest = 0;
        for (int j = 0; j < r->n_sensors; j++) {
            const spectra_sensor_event_t *o = &r->sensors[j];
            if (strcmp(o->package, e->package) != 0 || strcmp(o->sensor_type, type) != 0) continue;
            acc_count += o->access_count > 0 ? o->access_count : 1;
            if (o->last_access > latest) latest = o->last_access;
            if (o->duration_ms > max_dur) max_dur = o->duration_ms;
            if (o->currently_active) active = 1;
            if (o->background) bg = 1;
        }

        if (!has_any) { printf("%s", header); has_any = 1; }
        struct tm *atm = localtime(&latest);
        char atbuf[16];
        strftime(atbuf, sizeof(atbuf), "%H:%M", atm);

        char durbuf[16] = {0};
        if (max_dur >= 1000) snprintf(durbuf, sizeof(durbuf), " (%ds)", max_dur / 1000);
        else if (max_dur > 0) snprintf(durbuf, sizeof(durbuf), " (%dms)", max_dur);
        printf("     %-20s %d %s, last %s%s",
               e->app_name[0] ? e->app_name : e->package,
               acc_count, acc_count == 1 ? "access" : "accesses",
               atbuf, durbuf);
        if (active) printf(", \033[1;31mACTIVE NOW\033[0m");
        if (bg) printf(", \033[1;33mbackground !!\033[0m");
        printf("\n");
    }
    if (!has_any) printf("%s", none_msg);
}

void report_print_terminal(const spectra_report_t *r)
{
    char timebuf[64];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm);

    printf("\n");
    print_separator();
    printf("  spectra scan report");
    if (r->device.device_name[0])
        printf(" \xe2\x80\x94 %s", r->device.device_name);
    printf(" \xe2\x80\x94 %s\n", timebuf);
    print_separator();

    /* device info */
    if (r->device.device_model[0]) {
        printf("\nDEVICE INFO\n");
        printf("  Model: %s", r->device.device_model);
        if (r->device.device_name[0] &&
            strcmp(r->device.device_name, r->device.device_model) != 0)
            printf(" (%s)", r->device.device_name);
        printf("\n");
        if (r->device.android_version[0])
            printf("  Android: %s (API %d)\n",
                   r->device.android_version, r->device.api_level);
        printf("  Connection: %s\n", r->device.connection_type);
        printf("  ADB root: %s\n", r->device.has_root ? "yes" : "no");
    }

    /* sensor access */
    printf("\nSENSOR ACCESS (last 24 hours)\n");

    print_sensor_group(r, "mic", "  Microphone\n", "  Microphone: no access detected\n");
    print_sensor_group(r, "camera", "\n  Camera\n", "\n  Camera: no access detected\n");
    print_sensor_group(r, "location", "\n  Location\n", "\n  Location: no access detected\n");
    print_sensor_group(r, "contacts", "\n  Contacts\n", "\n  Contacts: no access detected\n");

    /* network activity */
    printf("\nNETWORK ACTIVITY (by app, current connections)\n");
    char printed_pkg[256][256];
    int n_printed = 0;

    for (int i = 0; i < r->n_net; i++) {
        int already = 0;
        for (int p = 0; p < n_printed; p++) {
            if (strcmp(printed_pkg[p], r->net[i].package) == 0) { already = 1; break; }
        }
        if (already) continue;
        snprintf(printed_pkg[n_printed++], 256, "%s", r->net[i].package);

        /* count connections for this app */
        int conn_count = 0;
        for (int j = 0; j < r->n_net; j++) {
            if (strcmp(r->net[j].package, r->net[i].package) == 0)
                conn_count++;
        }

        printf("  %s (uid %d) \xe2\x80\x94 %d %s\n",
               r->net[i].app_name[0] ? r->net[i].app_name : r->net[i].package,
               r->net[i].uid, conn_count,
               conn_count == 1 ? "connection" : "connections");

        for (int j = 0; j < r->n_net; j++) {
            if (strcmp(r->net[j].package, r->net[i].package) != 0) continue;
            char sb[32], rb[32];
            format_bytes(r->net[j].bytes_sent, sb, sizeof(sb));
            format_bytes(r->net[j].bytes_recv, rb, sizeof(rb));

            const char *host = (strcmp(r->net[j].remote_host, "unknown") != 0)
                                   ? r->net[j].remote_host : r->net[j].remote_ip;

            printf("     -> %-35s sent %-10s recv %-10s %s",
                   host, sb, rb, r->net[j].interface);

            if (strcmp(r->net[j].remote_host, "unknown") == 0)
                printf("  \033[1;33m!!\033[0m");
            printf("\n");
        }
        printf("\n");
    }
    if (r->n_net == 0)
        printf("  No active connections detected\n\n");

    /* findings: anything above "expected", with reasons and an action */
    printf("FINDINGS\n");
    int n_expected = 0;
    for (int i = 0; i < r->n_correlations; i++) {
        spectra_correlation_t *c = &r->correlations[i];
        const char *name = c->sensor.app_name[0] ? c->sensor.app_name : c->sensor.package;
        if (strcmp(c->verdict, "expected") == 0) { n_expected++; continue; }

        if (c->risk_score >= 50)
            printf("  \033[1;31mHIGH   %3d/100\033[0m  %s  \033[2m(%s app)\033[0m\n", c->risk_score, name, c->category);
        else if (c->risk_score >= 25)
            printf("  \033[1;33mREVIEW %3d/100\033[0m  %s  \033[2m(%s app)\033[0m\n", c->risk_score, name, c->category);
        else
            printf("  \033[1;32mLOW    %3d/100\033[0m  %s  \033[2m(%s app)\033[0m\n", c->risk_score, name, c->category);

        /* reasons, one per line */
        const char *p = c->reasons;
        while (p && *p) {
            const char *nl = strchr(p, '\n');
            int len = nl ? (int)(nl - p) : (int)strlen(p);
            printf("      %.*s\n", len, p);
            p = nl ? nl + 1 : NULL;
        }
        if (c->action[0] && strcmp(c->action, "No action needed.") != 0)
            printf("      \033[1m-> %s\033[0m\n", c->action);
        printf("\n");
    }
    if (n_expected > 0) {
        printf("  \033[1;32mAS EXPECTED\033[0m  %d app%s behaved normally for their type: ", n_expected, n_expected == 1 ? "" : "s");
        int printed = 0;
        for (int i = 0; i < r->n_correlations; i++) {
            spectra_correlation_t *c = &r->correlations[i];
            if (strcmp(c->verdict, "expected") != 0) continue;
            printf("%s%s", printed ? ", " : "", c->sensor.app_name[0] ? c->sensor.app_name : c->sensor.package);
            printed++;
        }
        printf("\n\n");
    }
    if (r->n_correlations == 0)
        printf("  No apps used a sensor in the last 24 hours\n\n");

    /* app details */
    if (r->n_apps > 0) {
        printf("APP DETAILS\n");
        for (int i = 0; i < r->n_apps; i++) {
            spectra_process_info_t *a = &r->apps[i];
            printf("  %s \xe2\x80\x94 %s\n",
                   a->app_name[0] ? a->app_name : a->package, a->package);
            if (a->install_source[0])
                printf("    Installed from: %s\n", a->install_source);
            if (a->permissions[0])
                printf("    Permissions: %s\n", a->permissions);
            if (a->first_install[0])
                printf("    Install date: %s\n", a->first_install);
            printf("    Category: %s\n\n",
                   a->is_system_app ? "system app" : "user-installed");
        }
    }

    /* scan summary */
    int active_count = 0, bg_count = 0;
    for (int i = 0; i < r->n_sensors; i++) {
        if (r->sensors[i].currently_active) active_count++;
        if (r->sensors[i].background) bg_count++;
    }

    int flagged = 0;
    for (int i = 0; i < r->n_correlations; i++) {
        if (r->correlations[i].risk_score >= 50) flagged++;
    }

    printf("SCAN SUMMARY\n");
    printf("  Apps that accessed sensors: ");
    /* count unique packages */
    char upkg[256][256]; int nupkg = 0;
    for (int i = 0; i < r->n_sensors; i++) {
        int dup = 0;
        for (int j = 0; j < nupkg; j++) {
            if (strcmp(upkg[j], r->sensors[i].package) == 0) { dup = 1; break; }
        }
        if (!dup && nupkg < 256)
            snprintf(upkg[nupkg++], 256, "%s", r->sensors[i].package);
    }
    printf("%d\n", nupkg);
    printf("  Active sensor users right now: %d\n", active_count);
    if (bg_count > 0)
        printf("  \033[1;33mBackground sensor access detected: %d !!\033[0m\n", bg_count);
    printf("  Total app connections: %d\n", r->n_net);
    printf("  Correlations flagged: %d\n", flagged);
    printf("  Scan duration: %.1fs\n", r->scan_duration_s);
    print_separator();
    printf("\n");
}

int report_write_json(const spectra_report_t *r, const char *path)
{
    cJSON *root = cJSON_CreateObject();

    /* device */
    cJSON *dev = cJSON_AddObjectToObject(root, "device");
    cJSON_AddStringToObject(dev, "model", r->device.device_model);
    cJSON_AddStringToObject(dev, "name", r->device.device_name);
    cJSON_AddStringToObject(dev, "android_version", r->device.android_version);
    cJSON_AddNumberToObject(dev, "api_level", r->device.api_level);
    cJSON_AddStringToObject(dev, "connection", r->device.connection_type);
    cJSON_AddBoolToObject(dev, "has_root", r->device.has_root);

    /* sensors */
    cJSON *sarr = cJSON_AddArrayToObject(root, "sensors");
    for (int i = 0; i < r->n_sensors; i++) {
        cJSON *s = cJSON_CreateObject();
        cJSON_AddStringToObject(s, "app_name", r->sensors[i].app_name);
        cJSON_AddStringToObject(s, "package", r->sensors[i].package);
        cJSON_AddStringToObject(s, "sensor_type", r->sensors[i].sensor_type);
        cJSON_AddNumberToObject(s, "uid", r->sensors[i].uid);
        cJSON_AddNumberToObject(s, "last_access", (double)r->sensors[i].last_access);
        cJSON_AddNumberToObject(s, "duration_ms", r->sensors[i].duration_ms);
        cJSON_AddNumberToObject(s, "access_count",
                                r->sensors[i].access_count > 0 ? r->sensors[i].access_count : 1);
        cJSON_AddBoolToObject(s, "currently_active", r->sensors[i].currently_active);
        cJSON_AddBoolToObject(s, "background", r->sensors[i].background);
        cJSON_AddItemToArray(sarr, s);
    }

    /* network */
    cJSON *narr = cJSON_AddArrayToObject(root, "network");
    for (int i = 0; i < r->n_net; i++) {
        cJSON *n = cJSON_CreateObject();
        cJSON_AddStringToObject(n, "app_name", r->net[i].app_name);
        cJSON_AddStringToObject(n, "package", r->net[i].package);
        cJSON_AddStringToObject(n, "remote_ip", r->net[i].remote_ip);
        cJSON_AddStringToObject(n, "remote_host", r->net[i].remote_host);
        cJSON_AddNumberToObject(n, "remote_port", r->net[i].remote_port);
        cJSON_AddNumberToObject(n, "bytes_sent", (double)r->net[i].bytes_sent);
        cJSON_AddNumberToObject(n, "bytes_recv", (double)r->net[i].bytes_recv);
        cJSON_AddStringToObject(n, "interface", r->net[i].interface);
        cJSON_AddNumberToObject(n, "uid", r->net[i].uid);
        cJSON_AddItemToArray(narr, n);
    }

    /* correlations */
    cJSON *carr = cJSON_AddArrayToObject(root, "correlations");
    for (int i = 0; i < r->n_correlations; i++) {
        cJSON *c = cJSON_CreateObject();
        cJSON_AddStringToObject(c, "app_name", r->correlations[i].sensor.app_name);
        cJSON_AddStringToObject(c, "package", r->correlations[i].sensor.package);
        cJSON_AddNumberToObject(c, "risk_score", r->correlations[i].risk_score);
        cJSON_AddStringToObject(c, "explanation", r->correlations[i].explanation);
        cJSON_AddStringToObject(c, "verdict", r->correlations[i].verdict);
        cJSON_AddStringToObject(c, "category", r->correlations[i].category);
        cJSON_AddStringToObject(c, "action", r->correlations[i].action);
        cJSON *rarr = cJSON_AddArrayToObject(c, "reasons");
        {
            const char *p = r->correlations[i].reasons;
            while (p && *p) {
                const char *nl = strchr(p, '\n');
                size_t len = nl ? (size_t)(nl - p) : strlen(p);
                char line[300];
                if (len >= sizeof(line)) len = sizeof(line) - 1;
                memcpy(line, p, len); line[len] = '\0';
                int pts = atoi(line);
                const char *text = strchr(line, ' ');
                cJSON *rj = cJSON_CreateObject();
                cJSON_AddNumberToObject(rj, "points", pts);
                cJSON_AddStringToObject(rj, "text", text ? text + 1 : line);
                cJSON_AddItemToArray(rarr, rj);
                p = nl ? nl + 1 : NULL;
            }
        }
        cJSON_AddItemToArray(carr, c);
    }

    /* apps */
    cJSON *aarr = cJSON_AddArrayToObject(root, "apps");
    for (int i = 0; i < r->n_apps; i++) {
        cJSON *a = cJSON_CreateObject();
        cJSON_AddStringToObject(a, "app_name", r->apps[i].app_name);
        cJSON_AddStringToObject(a, "package", r->apps[i].package);
        cJSON_AddStringToObject(a, "install_source", r->apps[i].install_source);
        cJSON_AddStringToObject(a, "permissions", r->apps[i].permissions);
        cJSON_AddStringToObject(a, "first_install", r->apps[i].first_install);
        cJSON_AddBoolToObject(a, "is_system_app", r->apps[i].is_system_app);
        cJSON_AddItemToArray(aarr, a);
    }

    cJSON_AddNumberToObject(root, "scan_duration_s", r->scan_duration_s);

    char *json_str = cJSON_Print(root);
    int ret = 0;

    if (path) {
        FILE *f = fopen(path, "w");
        if (f) {
            fputs(json_str, f);
            fclose(f);
        } else {
            ret = -1;
        }
    } else {
        puts(json_str);
    }

    free(json_str);
    cJSON_Delete(root);
    return ret;
}

int report_write_html(const spectra_report_t *r, const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;

    fprintf(f, "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n");
    fprintf(f, "<title>spectra scan report</title>\n");
    fprintf(f, "<style>\n");
    fprintf(f, "body{font-family:-apple-system,sans-serif;max-width:900px;"
               "margin:40px auto;padding:0 20px;background:#0d1117;color:#c9d1d9}\n");
    fprintf(f, "h1{color:#58a6ff} h2{color:#58a6ff;border-bottom:1px solid #30363d;"
               "padding-bottom:8px}\n");
    fprintf(f, ".risk-high{color:#f85149;font-weight:bold}\n");
    fprintf(f, ".risk-med{color:#d29922;font-weight:bold}\n");
    fprintf(f, ".risk-low{color:#3fb950;font-weight:bold}\n");
    fprintf(f, ".active{color:#f85149;font-weight:bold}\n");
    fprintf(f, ".bg{color:#d29922;font-weight:bold}\n");
    fprintf(f, "table{border-collapse:collapse;width:100%%} "
               "td,th{padding:8px 12px;text-align:left;border-bottom:1px solid #30363d}\n");
    fprintf(f, "th{color:#8b949e}\n");
    fprintf(f, ".finding{border:1px solid #30363d;border-radius:8px;padding:4px 16px;margin:12px 0}\n");
    fprintf(f, ".finding ul{margin:4px 0 8px;color:#8b949e}\n");
    fprintf(f, ".action{color:#c9d1d9;font-weight:600}\n");
    fprintf(f, ".cat{color:#8b949e;font-weight:normal;font-size:0.9em}\n");
    fprintf(f, ".expected{color:#8b949e}\n");
    fprintf(f, "</style>\n</head>\n<body>\n");

    fprintf(f, "<h1>spectra privacy report</h1>\n");
    if (r->device.device_name[0])
        fprintf(f, "<p>%s &mdash; %s</p>\n", r->device.device_name, r->device.device_model);

    /* findings first: what needs attention and what to do about it */
    {
        int n_high = 0, n_review = 0, n_low = 0, n_exp = 0;
        for (int i = 0; i < r->n_correlations; i++) {
            const char *v = r->correlations[i].verdict;
            if (!strcmp(v, "high")) n_high++;
            else if (!strcmp(v, "review")) n_review++;
            else if (!strcmp(v, "low")) n_low++;
            else n_exp++;
        }
        fprintf(f, "<h2>Findings</h2>\n");
        fprintf(f, "<p><span class=\"risk-high\">%d high</span> &middot; "
                   "<span class=\"risk-med\">%d to review</span> &middot; "
                   "<span class=\"risk-low\">%d low</span> &middot; %d behaving as expected</p>\n",
                n_high, n_review, n_low, n_exp);
        for (int i = 0; i < r->n_correlations; i++) {
            spectra_correlation_t *c = &r->correlations[i];
            if (!strcmp(c->verdict, "expected")) continue;
            const char *cls = c->risk_score >= 50 ? "risk-high" :
                              c->risk_score >= 25 ? "risk-med" : "risk-low";
            fprintf(f, "<div class=\"finding\"><p><span class=\"%s\">%s %d/100</span> &mdash; "
                       "<strong>%s</strong> <span class=\"cat\">%s app</span></p>\n<ul>\n",
                    cls, c->risk_score >= 50 ? "HIGH" : c->risk_score >= 25 ? "REVIEW" : "LOW",
                    c->risk_score,
                    c->sensor.app_name[0] ? c->sensor.app_name : c->sensor.package, c->category);
            const char *p = c->reasons;
            while (p && *p) {
                const char *nl = strchr(p, '\n');
                int len = nl ? (int)(nl - p) : (int)strlen(p);
                fprintf(f, "<li>%.*s</li>\n", len, p);
                p = nl ? nl + 1 : NULL;
            }
            fprintf(f, "</ul>\n<p class=\"action\">What to do: %s</p></div>\n", c->action);
        }
        if (n_exp > 0) {
            fprintf(f, "<p class=\"expected\">Behaving as expected: ");
            int printed = 0;
            for (int i = 0; i < r->n_correlations; i++) {
                spectra_correlation_t *c = &r->correlations[i];
                if (strcmp(c->verdict, "expected") != 0) continue;
                fprintf(f, "%s%s", printed ? ", " : "",
                        c->sensor.app_name[0] ? c->sensor.app_name : c->sensor.package);
                printed++;
            }
            fprintf(f, "</p>\n");
        }
    }

    /* sensors table */
    fprintf(f, "<h2>Sensor Access</h2>\n<table>\n");
    fprintf(f, "<tr><th>App</th><th>Sensor</th><th>Last Access</th>"
               "<th>Duration</th><th>Status</th></tr>\n");
    for (int i = 0; i < r->n_sensors; i++) {
        struct tm *atm = localtime(&r->sensors[i].last_access);
        char atbuf[32];
        strftime(atbuf, sizeof(atbuf), "%Y-%m-%d %H:%M", atm);
        fprintf(f, "<tr><td>%s</td><td>%s</td><td>%s</td><td>%ds</td><td>",
                r->sensors[i].app_name[0] ? r->sensors[i].app_name : r->sensors[i].package,
                r->sensors[i].sensor_type, atbuf,
                r->sensors[i].duration_ms / 1000);
        if (r->sensors[i].currently_active)
            fprintf(f, "<span class=\"active\">ACTIVE</span>");
        if (r->sensors[i].background)
            fprintf(f, " <span class=\"bg\">BACKGROUND</span>");
        fprintf(f, "</td></tr>\n");
    }
    fprintf(f, "</table>\n");

    /* network table */
    fprintf(f, "<h2>Network Activity</h2>\n<table>\n");
    fprintf(f, "<tr><th>App</th><th>Destination</th><th>Port</th>"
               "<th>Sent</th><th>Received</th><th>Interface</th></tr>\n");
    for (int i = 0; i < r->n_net; i++) {
        char sb[32], rb[32];
        format_bytes(r->net[i].bytes_sent, sb, sizeof(sb));
        format_bytes(r->net[i].bytes_recv, rb, sizeof(rb));
        const char *host = (strcmp(r->net[i].remote_host, "unknown") != 0)
                               ? r->net[i].remote_host : r->net[i].remote_ip;
        fprintf(f, "<tr><td>%s</td><td>%s</td><td>%d</td>"
                   "<td>%s</td><td>%s</td><td>%s</td></tr>\n",
                r->net[i].app_name[0] ? r->net[i].app_name : r->net[i].package,
                host, r->net[i].remote_port, sb, rb, r->net[i].interface);
    }
    fprintf(f, "</table>\n");

    /* full score list */
    fprintf(f, "<h2>All Scores</h2>\n<table>\n<tr><th>App</th><th>Type</th><th>Score</th><th>Summary</th></tr>\n");
    for (int i = 0; i < r->n_correlations; i++) {
        spectra_correlation_t *c = &r->correlations[i];
        const char *cls = c->risk_score >= 50 ? "risk-high" :
                          c->risk_score >= 25 ? "risk-med" : "risk-low";
        fprintf(f, "<tr><td>%s</td><td>%s</td><td><span class=\"%s\">%d</span></td><td>%s</td></tr>\n",
                c->sensor.app_name[0] ? c->sensor.app_name : c->sensor.package,
                c->category, cls, c->risk_score, c->explanation);
    }
    fprintf(f, "</table>\n");

    fprintf(f, "<p><em>Scan duration: %.1fs</em></p>\n", r->scan_duration_s);
    fprintf(f, "</body>\n</html>\n");
    fclose(f);
    return 0;
}
