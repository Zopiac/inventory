#ifndef INVENTORY_DB_H
#define INVENTORY_DB_H

#include <sqlite3.h>
#include <stddef.h>

#define FIELD_LEN 256

typedef struct {
    int id;
    char campus[FIELD_LEN];
    char location[FIELD_LEN];
    char device_type[FIELD_LEN];
    char make[FIELD_LEN];
    char model[FIELD_LEN];
    char serial_number[FIELD_LEN];
    char role_name[FIELD_LEN];
    char inputs[FIELD_LEN];
    char ip_address[FIELD_LEN];
    int is_static;
    char mac_address[FIELD_LEN];
    char vlan[FIELD_LEN];
    char status[FIELD_LEN];
} Device;

int db_open(const char *path, sqlite3 **db);
int db_init(sqlite3 *db);

int db_add_device(sqlite3 *db, const Device *device, int *new_id);
int db_update_device(sqlite3 *db, const Device *device);
int db_delete_device(sqlite3 *db, int id);

int db_get_device(sqlite3 *db, int id, Device *device);

int db_count_campuses(sqlite3 *db);
int db_get_campuses(sqlite3 *db, char **campuses, int max_campuses);

int db_get_devices(
    sqlite3 *db,
    const char *campus,
    Device **devices,
    int *count
);

void db_free_devices(Device *devices);

const char *db_error(sqlite3 *db);

#endif
