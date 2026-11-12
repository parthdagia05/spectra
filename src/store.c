#include "spectra/store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sqlite3.h>

static sqlite3 *db = NULL;

static int ensure_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) return 0;
    return mkdir(path, 0755);
}

int store_init(void)
{
    char dbpath[512];
    const char *home = getenv("HOME");
    if (!home) return -1;

    snprintf(dbpath, sizeof(dbpath), "%s/.spectra", home);
    ensure_dir(dbpath);

    snprintf(dbpath, sizeof(dbpath), "%s/.spectra/scans.db", home);
    if (sqlite3_open(dbpath, &db) != SQLITE_OK) {
        fprintf(stderr, "Failed to open database: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    const char *sql =
        "CREATE TABLE IF NOT EXISTS scans ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  device_id TEXT,"
        "  device_name TEXT,"
        "  timestamp TEXT DEFAULT (datetime('now')),"
        "  report_json TEXT"
        ");";

    char *err = NULL;
    if (sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK) {
        fprintf(stderr, "Failed to create table: %s\n", err);
        sqlite3_free(err);
        return -1;
    }
    return 0;
}

int store_save_scan(const char *device_id, const char *device_name,
                    const char *report_json)
{
    if (!db) return -1;

    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO scans (device_id, device_name, report_json) "
                      "VALUES (?, ?, ?);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return -1;

    sqlite3_bind_text(stmt, 1, device_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, device_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, report_json, -1, SQLITE_TRANSIENT);

    int ret = (sqlite3_step(stmt) == SQLITE_DONE) ? 0 : -1;
    sqlite3_finalize(stmt);
    return ret;
}

int store_list_scans(const char *device_filter)
{
    if (!db) return -1;

    sqlite3_stmt *stmt;
    const char *sql;
    if (device_filter && device_filter[0]) {
        sql = "SELECT id, device_name, timestamp FROM scans "
              "WHERE device_name LIKE ? OR device_id LIKE ? "
              "ORDER BY id DESC LIMIT 50;";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
            return -1;
        char pattern[256];
        snprintf(pattern, sizeof(pattern), "%%%s%%", device_filter);
        sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, pattern, -1, SQLITE_TRANSIENT);
    } else {
        sql = "SELECT id, device_name, timestamp FROM scans "
              "ORDER BY id DESC LIMIT 50;";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
            return -1;
    }

    printf("ID    Device               Timestamp\n");
    printf("----  -------------------  -------------------\n");
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("%-5d %-20s %s\n",
               sqlite3_column_int(stmt, 0),
               (const char *)sqlite3_column_text(stmt, 1),
               (const char *)sqlite3_column_text(stmt, 2));
    }
    sqlite3_finalize(stmt);
    return 0;
}

int store_get_scan_json(int id, char *buf, size_t bufsize)
{
    if (!db) return -1;

    sqlite3_stmt *stmt;
    const char *sql = "SELECT report_json FROM scans WHERE id = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return -1;

    sqlite3_bind_int(stmt, 1, id);
    int ret = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *json = (const char *)sqlite3_column_text(stmt, 0);
        snprintf(buf, bufsize, "%s", json);
        ret = 0;
    }
    sqlite3_finalize(stmt);
    return ret;
}

void store_close(void)
{
    if (db) {
        sqlite3_close(db);
        db = NULL;
    }
}
