#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include "spectra/event.h"
#include "spectra/correlator.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    printf("  %-50s ", #name); \
    tests_run++; \
    if (test_##name()) { printf("PASS\n"); tests_passed++; } \
    else printf("FAIL\n"); \
} while(0)

static int test_no_events(void)
{
    spectra_correlation_t results[16];
    int n = spectra_correlate(NULL, 0, NULL, 0, results, 16);
    return n == 0;
}

static int test_sensor_only(void)
{
    spectra_sensor_event_t s = {0};
    s.uid = 10234;
    snprintf(s.package, sizeof(s.package), "com.example.app");
    snprintf(s.sensor_type, sizeof(s.sensor_type), "mic");
    s.currently_active = 1;
    s.last_access = time(NULL);

    spectra_correlation_t results[16];
    int n = spectra_correlate(&s, 1, NULL, 0, results, 16);
    return n == 1 && results[0].risk_score >= 40;
}

static int test_background_bonus(void)
{
    spectra_sensor_event_t s = {0};
    s.uid = 10234;
    snprintf(s.package, sizeof(s.package), "com.example.app");
    snprintf(s.sensor_type, sizeof(s.sensor_type), "mic");
    s.currently_active = 1;
    s.background = 1;
    s.last_access = time(NULL);

    spectra_correlation_t results[16];
    int n = spectra_correlate(&s, 1, NULL, 0, results, 16);
    return n == 1 && results[0].risk_score >= 60;
}

static int test_full_correlation(void)
{
    spectra_sensor_event_t s = {0};
    s.uid = 10234;
    snprintf(s.package, sizeof(s.package), "com.instagram.android");
    snprintf(s.app_name, sizeof(s.app_name), "Instagram");
    snprintf(s.sensor_type, sizeof(s.sensor_type), "mic");
    s.currently_active = 1;
    s.last_access = time(NULL);

    spectra_net_event_t n_ev = {0};
    n_ev.uid = 10234;
    snprintf(n_ev.package, sizeof(n_ev.package), "com.instagram.android");
    snprintf(n_ev.app_name, sizeof(n_ev.app_name), "Instagram");
    snprintf(n_ev.remote_ip, sizeof(n_ev.remote_ip), "157.240.1.35");
    snprintf(n_ev.remote_host, sizeof(n_ev.remote_host), "graph.facebook.com");
    n_ev.bytes_sent = 4 * 1024 * 1024;
    n_ev.remote_port = 443;

    spectra_correlation_t results[16];
    int count = spectra_correlate(&s, 1, &n_ev, 1, results, 16);
    /* social app with mic active in the foreground while uploading 4 MB:
     * worth reviewing (active mic +30, upload while recording +10), not "high" */
    return count == 1 && results[0].risk_score >= 25 && results[0].risk_score < 50 &&
           strcmp(results[0].verdict, "review") == 0 &&
           strstr(results[0].reasons, "active right now") != NULL &&
           strstr(results[0].reasons, "Uploaded") != NULL;
}

/* WhatsApp syncing contacts in the background is normal for a messaging app */
static int test_expected_messaging_contacts(void)
{
    spectra_sensor_event_t s = {0};
    s.uid = 10293;
    snprintf(s.package, sizeof(s.package), "com.whatsapp");
    snprintf(s.sensor_type, sizeof(s.sensor_type), "contacts");
    s.background = 1;
    s.access_count = 3;
    s.last_access = time(NULL);

    spectra_correlation_t results[16];
    int n = spectra_correlate(&s, 1, NULL, 0, results, 16);
    return n == 1 && results[0].risk_score < 15 &&
           strcmp(results[0].verdict, "expected") == 0 &&
           strcmp(results[0].category, "messaging") == 0 &&
           strcmp(results[0].action, "No action needed.") == 0;
}

/* system services reading location all day is not a finding */
static int test_expected_system_location(void)
{
    spectra_sensor_event_t s = {0};
    s.uid = 1000;
    snprintf(s.package, sizeof(s.package), "com.android.phone");
    snprintf(s.sensor_type, sizeof(s.sensor_type), "location");
    s.background = 1;
    s.access_count = 214;
    s.last_access = time(NULL);

    spectra_correlation_t results[16];
    int n = spectra_correlate(&s, 1, NULL, 0, results, 16);
    return n == 1 && results[0].risk_score == 0 && strcmp(results[0].verdict, "expected") == 0;
}

/* a commerce app reading location in the background gets a concrete fix */
static int test_commerce_background_location(void)
{
    spectra_sensor_event_t s = {0};
    s.uid = 10400;
    snprintf(s.package, sizeof(s.package), "com.zeptoconsumerapp");
    snprintf(s.app_name, sizeof(s.app_name), "Zepto");
    snprintf(s.sensor_type, sizeof(s.sensor_type), "location");
    s.background = 1;
    s.access_count = 60;
    s.last_access = time(NULL);

    spectra_correlation_t results[16];
    int n = spectra_correlate(&s, 1, NULL, 0, results, 16);
    return n == 1 && results[0].risk_score >= 25 &&
           strcmp(results[0].category, "commerce") == 0 &&
           strstr(results[0].action, "Allow only while using") != NULL;
}

/* app details raise the score: a sideloaded unknown app reading contacts */
static int test_sideloaded_unknown_app(void)
{
    spectra_sensor_event_t s = {0};
    s.uid = 10500;
    snprintf(s.package, sizeof(s.package), "com.freegift.cleaner");
    snprintf(s.sensor_type, sizeof(s.sensor_type), "contacts");
    s.background = 1;
    s.last_access = time(NULL);

    spectra_process_info_t info = {0};
    snprintf(info.package, sizeof(info.package), "com.freegift.cleaner");
    snprintf(info.install_source, sizeof(info.install_source), "sideloaded (com.miui.packageinstaller)");
    snprintf(info.permissions, sizeof(info.permissions), "read_contacts, read_sms, internet");

    spectra_correlation_t without[16], with[16];
    int a = spectra_correlate(&s, 1, NULL, 0, without, 16);
    int b = spectra_correlate_ex(&s, 1, NULL, 0, &info, 1, with, 16);
    return a == 1 && b == 1 &&
           with[0].risk_score > without[0].risk_score &&
           with[0].risk_score >= 50 &&
           strcmp(with[0].verdict, "high") == 0 &&
           strstr(with[0].reasons, "outside an app store") != NULL &&
           strstr(with[0].reasons, "SMS") != NULL;
}

static int test_allowlisted_destination(void)
{
    spectra_sensor_event_t s = {0};
    s.uid = 10187;
    snprintf(s.package, sizeof(s.package), "com.whatsapp");
    snprintf(s.sensor_type, sizeof(s.sensor_type), "mic");
    s.currently_active = 1;
    s.last_access = time(NULL);

    spectra_net_event_t n_ev = {0};
    n_ev.uid = 10187;
    snprintf(n_ev.package, sizeof(n_ev.package), "com.whatsapp");
    snprintf(n_ev.remote_host, sizeof(n_ev.remote_host), "web.whatsapp.com");
    n_ev.bytes_sent = 1024 * 1024;
    n_ev.remote_port = 443;

    spectra_correlation_t results[16];
    int count = spectra_correlate(&s, 1, &n_ev, 1, results, 16);
    /* allowlisted dest avoids the +30 unknown penalty, so score should be lower than full_correlation */
    return count == 1 && results[0].risk_score < 80;
}

static int test_dual_sensor(void)
{
    spectra_sensor_event_t sensors[2] = {0};
    sensors[0].uid = 10234;
    snprintf(sensors[0].package, sizeof(sensors[0].package), "com.example.app");
    snprintf(sensors[0].sensor_type, sizeof(sensors[0].sensor_type), "mic");
    sensors[0].currently_active = 1;
    sensors[0].last_access = time(NULL);

    sensors[1].uid = 10234;
    snprintf(sensors[1].package, sizeof(sensors[1].package), "com.example.app");
    snprintf(sensors[1].sensor_type, sizeof(sensors[1].sensor_type), "camera");
    sensors[1].currently_active = 1;
    sensors[1].last_access = time(NULL);

    spectra_correlation_t results[16];
    int count = spectra_correlate(sensors, 2, NULL, 0, results, 16);
    /* should produce one correlation with dual-sensor bonus */
    return count == 1 && results[0].risk_score >= 50;
}

static int test_risk_cap(void)
{
    spectra_sensor_event_t s = {0};
    s.uid = 10234;
    snprintf(s.package, sizeof(s.package), "com.evil.app");
    snprintf(s.sensor_type, sizeof(s.sensor_type), "mic");
    s.currently_active = 1;
    s.background = 1;
    s.last_access = time(NULL);

    spectra_net_event_t n_ev = {0};
    n_ev.uid = 10234;
    snprintf(n_ev.package, sizeof(n_ev.package), "com.evil.app");
    snprintf(n_ev.remote_host, sizeof(n_ev.remote_host), "unknown");
    n_ev.bytes_sent = 100 * 1024 * 1024;
    n_ev.remote_port = 8443;

    spectra_correlation_t results[16];
    int count = spectra_correlate(&s, 1, &n_ev, 1, results, 16);
    return count == 1 && results[0].risk_score <= 100;
}

int main(void)
{
    printf("\nspectra correlator tests\n");
    printf("======================\n\n");

    TEST(no_events);
    TEST(sensor_only);
    TEST(background_bonus);
    TEST(full_correlation);
    TEST(allowlisted_destination);
    TEST(dual_sensor);
    TEST(risk_cap);
    TEST(expected_messaging_contacts);
    TEST(expected_system_location);
    TEST(commerce_background_location);
    TEST(sideloaded_unknown_app);

    printf("\n%d/%d tests passed\n\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
