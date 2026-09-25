#include "db.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COPY_FIELD(dst, src) \
    snprintf((dst), sizeof(dst), "%s", (src) ? (src) : "")

static void fill_device(sqlite3_stmt *stmt, Device *d)
{
    memset(d, 0, sizeof(*d));

    d->id = sqlite3_column_int(stmt, 0);

    COPY_FIELD(d->campus,
               (const char *)sqlite3_column_text(stmt, 1));
    COPY_FIELD(d->location,
               (const char *)sqlite3_column_text(stmt, 2));
    COPY_FIELD(d->device_type,
               (const char *)sqlite3_column_text(stmt, 3));
    COPY_FIELD(d->make,
               (const char *)sqlite3_column_text(stmt, 4));
    COPY_FIELD(d->model,
               (const char *)sqlite3_column_text(stmt, 5));
    COPY_FIELD(d->serial_number,
               (const char *)sqlite3_column_text(stmt, 6));
    COPY_FIELD(d->role_name,
               (const char *)sqlite3_column_text(stmt, 7));
    COPY_FIELD(d->inputs,
               (const char *)sqlite3_column_text(stmt, 8));
    COPY_FIELD(d->ip_address,
               (const char *)sqlite3_column_text(stmt, 9));

    d->is_static = sqlite3_column_int(stmt, 10);

    COPY_FIELD(d->mac_address,
               (const char *)sqlite3_column_text(stmt, 11));
    COPY_FIELD(d->vlan,
               (const char *)sqlite3_column_text(stmt, 12));
    COPY_FIELD(d->status,
               (const char *)sqlite3_column_text(stmt, 13));
}

int db_open(const char *path, sqlite3 **db)
{
    int rc = sqlite3_open_v2(
        path,
        db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
        NULL
    );

    if (rc != SQLITE_OK)
        return rc;

    sqlite3_busy_timeout(*db, 5000);

    sqlite3_exec(*db, "PRAGMA foreign_keys = ON", NULL, NULL, NULL);
    sqlite3_exec(*db, "PRAGMA journal_mode = WAL", NULL, NULL, NULL);

    return SQLITE_OK;
}

int db_init(sqlite3 *db)
{
    const char *sql =
        "CREATE TABLE IF NOT EXISTS devices ("
        "id INTEGER PRIMARY KEY,"
        "campus TEXT NOT NULL,"
        "location TEXT NOT NULL DEFAULT '',"
        "device_type TEXT NOT NULL DEFAULT '',"
        "make TEXT NOT NULL DEFAULT '',"
        "model TEXT NOT NULL DEFAULT '',"
        "serial_number TEXT NOT NULL DEFAULT '',"
        "role_name TEXT NOT NULL DEFAULT '',"
        "inputs TEXT NOT NULL DEFAULT '',"
        "ip_address TEXT NOT NULL DEFAULT '',"
        "is_static INTEGER NOT NULL DEFAULT 0,"
        "mac_address TEXT NOT NULL DEFAULT '',"
        "vlan TEXT NOT NULL DEFAULT '',"
        "status TEXT NOT NULL DEFAULT 'Active',"
        "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_devices_campus "
        "ON devices(campus);";

    return sqlite3_exec(db, sql, NULL, NULL, NULL);
}

int db_add_device(sqlite3 *db, const Device *d, int *new_id)
{
    const char *sql =
        "INSERT INTO devices "
        "(campus, location, device_type, make, model, serial_number,"
        " role_name, inputs, ip_address, is_static, mac_address, vlan, status)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

    if (rc != SQLITE_OK)
        return rc;

    sqlite3_bind_text(stmt, 1, d->campus, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, d->location, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, d->device_type, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, d->make, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, d->model, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, d->serial_number, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, d->role_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, d->inputs, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, d->ip_address, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 10, d->is_static);
    sqlite3_bind_text(stmt, 11, d->mac_address, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 12, d->vlan, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 13, d->status, -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);

    if (rc == SQLITE_DONE && new_id != NULL)
        *new_id = (int)sqlite3_last_insert_rowid(db);

    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE ? SQLITE_OK : rc;
}

int db_update_device(sqlite3 *db, const Device *d)
{
    const char *sql =
        "UPDATE devices SET "
        "campus=?, location=?, device_type=?, make=?, model=?,"
        "serial_number=?, role_name=?, inputs=?, ip_address=?,"
        "is_static=?, mac_address=?, vlan=?, status=?,"
        "updated_at=CURRENT_TIMESTAMP "
        "WHERE id=?";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

    if (rc != SQLITE_OK)
        return rc;

    sqlite3_bind_text(stmt, 1, d->campus, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, d->location, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, d->device_type, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, d->make, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, d->model, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, d->serial_number, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, d->role_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, d->inputs, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, d->ip_address, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 10, d->is_static);
    sqlite3_bind_text(stmt, 11, d->mac_address, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 12, d->vlan, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 13, d->status, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 14, d->id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE ? SQLITE_OK : rc;
}

int db_delete_device(sqlite3 *db, int id)
{
    const char *sql = "DELETE FROM devices WHERE id=?";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

    if (rc != SQLITE_OK)
        return rc;

    sqlite3_bind_int(stmt, 1, id);
    rc = sqlite3_step(stmt);

    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE ? SQLITE_OK : rc;
}

int db_get_devices(
    sqlite3 *db,
    const char *campus,
    Device **devices,
    int *count
)
{
    const char *sql_all =
        "SELECT id, campus, location, device_type, make, model,"
        " serial_number, role_name, inputs, ip_address, is_static,"
        " mac_address, vlan, status "
        "FROM devices "
        "ORDER BY campus, location, device_type, make, model";

    const char *sql_campus =
        "SELECT id, campus, location, device_type, make, model,"
        " serial_number, role_name, inputs, ip_address, is_static,"
        " mac_address, vlan, status "
        "FROM devices "
        "WHERE campus=? "
        "ORDER BY location, device_type, make, model";

    int filter_by_campus =
        campus != NULL && campus[0] != '\0';

    const char *sql = filter_by_campus ? sql_campus : sql_all;

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

    if (rc != SQLITE_OK)
        return rc;

    if (filter_by_campus) {
        sqlite3_bind_text(
            stmt,
            1,
            campus,
            -1,
            SQLITE_TRANSIENT
        );
    }

    int capacity = 32;
    int used = 0;

    Device *result = malloc(sizeof(Device) * capacity);

    if (result == NULL) {
        sqlite3_finalize(stmt);
        return SQLITE_NOMEM;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (used == capacity) {
            capacity *= 2;

            Device *tmp = realloc(
                result,
                sizeof(Device) * capacity
            );

            if (tmp == NULL) {
                free(result);
                sqlite3_finalize(stmt);
                return SQLITE_NOMEM;
            }

            result = tmp;
        }

        fill_device(stmt, &result[used]);
        used++;
    }

    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        free(result);
        return rc;
    }

    *devices = result;
    *count = used;

    return SQLITE_OK;
}

void db_free_devices(Device *devices)
{
    free(devices);
}

const char *db_error(sqlite3 *db)
{
    return sqlite3_errmsg(db);
}

