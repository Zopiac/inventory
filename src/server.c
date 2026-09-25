#define _GNU_SOURCE

#include "db.h"

#include <microhttpd.h>
#include <cjson/cJSON.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

#define LISTEN_PORT 9142
#define MAX_BODY_SIZE 65536
#define POST_BUFFER_SIZE 4096

typedef struct {
    char *body;
    size_t length;
} RequestContext;

static sqlite3 *g_db = NULL;

static struct MHD_Response *
response_text(const char *body, unsigned int status, const char *content_type)
{
    struct MHD_Response *response;

    response = MHD_create_response_from_buffer(
        strlen(body),
        (void *)body,
        MHD_RESPMEM_MUST_COPY
    );

    if (response == NULL)
        return NULL;

    MHD_add_response_header(response, "Content-Type", content_type);
    MHD_add_response_header(response, "Cache-Control", "no-store");
    MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
    MHD_add_response_header(response, "Access-Control-Allow-Headers",
                            "Content-Type");
    MHD_add_response_header(response, "Access-Control-Allow-Methods",
                            "GET, POST, OPTIONS");

    return response;
}

static enum MHD_Result
send_text(struct MHD_Connection *connection,
          const char *body,
          unsigned int status,
          const char *content_type)
{
    struct MHD_Response *response;
    enum MHD_Result result;

    response = response_text(body, status, content_type);
    if (response == NULL)
        return MHD_NO;

    result = MHD_queue_response(connection, status, response);
    MHD_destroy_response(response);

    return result;
}

static enum MHD_Result
send_json(struct MHD_Connection *connection,
          const char *json,
          unsigned int status)
{
    return send_text(connection, json, status, "application/json");
}

static cJSON *
json_string(cJSON *object, const char *name)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);

    if (item == NULL || !cJSON_IsString(item))
        return NULL;

    return item;
}

static int
copy_json_string(cJSON *object,
                 const char *name,
                 char *destination,
                 size_t destination_size,
                 int required)
{
    cJSON *item = json_string(object, name);

    if (item == NULL) {
        if (required)
            return -1;

        destination[0] = '\0';
        return 0;
    }

    if (strlen(item->valuestring) >= destination_size)
        return -1;

    snprintf(destination, destination_size, "%s", item->valuestring);
    return 0;
}

static int
json_to_device(cJSON *root, Device *device)
{
    cJSON *static_item;

    memset(device, 0, sizeof(*device));

    if (!cJSON_IsObject(root))
        return -1;

    if (copy_json_string(root, "campus",
                         device->campus, sizeof(device->campus), 1) < 0)
        return -1;

    if (copy_json_string(root, "location",
                         device->location, sizeof(device->location), 1) < 0)
        return -1;

    if (copy_json_string(root, "device_type",
                         device->device_type, sizeof(device->device_type), 1) < 0)
        return -1;

    if (copy_json_string(root, "make",
                         device->make, sizeof(device->make), 0) < 0)
        return -1;

    if (copy_json_string(root, "model",
                         device->model, sizeof(device->model), 0) < 0)
        return -1;

    if (copy_json_string(root, "serial_number",
                         device->serial_number, sizeof(device->serial_number), 0) < 0)
        return -1;

    if (copy_json_string(root, "role_name",
                         device->role_name, sizeof(device->role_name), 0) < 0)
        return -1;

    if (copy_json_string(root, "inputs",
                         device->inputs, sizeof(device->inputs), 0) < 0)
        return -1;

    if (copy_json_string(root, "ip_address",
                         device->ip_address, sizeof(device->ip_address), 0) < 0)
        return -1;

    if (copy_json_string(root, "mac_address",
                         device->mac_address, sizeof(device->mac_address), 0) < 0)
        return -1;

    if (copy_json_string(root, "vlan",
                         device->vlan, sizeof(device->vlan), 0) < 0)
        return -1;

    if (copy_json_string(root, "status",
                         device->status, sizeof(device->status), 0) < 0)
        return -1;

    if (device->status[0] == '\0')
        snprintf(device->status, sizeof(device->status), "Active");

    static_item = cJSON_GetObjectItemCaseSensitive(root, "is_static");

    if (static_item != NULL) {
        if (!cJSON_IsBool(static_item))
            return -1;

        device->is_static = cJSON_IsTrue(static_item) ? 1 : 0;
    }

    return 0;
}

static cJSON *
device_to_json(const Device *device)
{
    cJSON *object = cJSON_CreateObject();

    if (object == NULL)
        return NULL;

    cJSON_AddNumberToObject(object, "id", device->id);
    cJSON_AddStringToObject(object, "campus", device->campus);
    cJSON_AddStringToObject(object, "location", device->location);
    cJSON_AddStringToObject(object, "device_type", device->device_type);
    cJSON_AddStringToObject(object, "make", device->make);
    cJSON_AddStringToObject(object, "model", device->model);
    cJSON_AddStringToObject(object, "serial_number", device->serial_number);
    cJSON_AddStringToObject(object, "role_name", device->role_name);
    cJSON_AddStringToObject(object, "inputs", device->inputs);
    /*cJSON_AddStringToObject(object, "ip_address");
    cJSON_ReplaceItemInObject(
        object,
        "ip_address",
        cJSON_CreateString(device->ip_address)
    );*/
    cJSON_AddStringToObject(object, "ip_address", device->ip_address);
    cJSON_AddBoolToObject(object, "is_static", device->is_static);
    cJSON_AddStringToObject(object, "mac_address", device->mac_address);
    cJSON_AddStringToObject(object, "vlan", device->vlan);
    cJSON_AddStringToObject(object, "status", device->status);

    return object;
}

static char *
devices_json(const char *campus)
{
    Device *devices = NULL;
    int count = 0;

    if (db_get_devices(g_db, campus, &devices, &count) != SQLITE_OK)
        return NULL;

    cJSON *array = cJSON_CreateArray();

    if (array == NULL) {
        db_free_devices(devices);
        return NULL;
    }

    for (int i = 0; i < count; i++) {
        cJSON *object = device_to_json(&devices[i]);

        if (object == NULL) {
            cJSON_Delete(array);
            db_free_devices(devices);
            return NULL;
        }

        cJSON_AddItemToArray(array, object);
    }

    char *output = cJSON_PrintUnformatted(array);

    cJSON_Delete(array);
    db_free_devices(devices);

    return output;
}

static enum MHD_Result
handle_post_device(struct MHD_Connection *connection, RequestContext *request)
{
    cJSON *root;
    Device device;
    int new_id;
    char response[128];

    root = cJSON_ParseWithLength(request->body, request->length);

    if (root == NULL)
        return send_json(
            connection,
            "{\"error\":\"invalid JSON\"}",
            MHD_HTTP_BAD_REQUEST
        );

    if (json_to_device(root, &device) < 0) {
        cJSON_Delete(root);

        return send_json(
            connection,
            "{\"error\":\"missing or invalid field\"}",
            MHD_HTTP_BAD_REQUEST
        );
    }

    cJSON_Delete(root);

    if (db_add_device(g_db, &device, &new_id) != SQLITE_OK) {
        fprintf(stderr, "database insert failed: %s\n", db_error(g_db));

        return send_json(
            connection,
            "{\"error\":\"database error\"}",
            MHD_HTTP_INTERNAL_SERVER_ERROR
        );
    }

    snprintf(response, sizeof(response),
             "{\"ok\":true,\"id\":%d}", new_id);

    return send_json(connection, response, MHD_HTTP_CREATED);
}

static int
parse_device_id(const char *url, int *id)
{
    const char *prefix = "/api/devices/";
    char *end;
    long value;

    if (strncmp(url, prefix, strlen(prefix)) != 0)
        return -1;

    if (url[strlen(prefix)] == '\0')
        return -1;

    errno = 0;
    value = strtol(url + strlen(prefix), &end, 10);

    if (errno != 0 ||
        end == url + strlen(prefix) ||
        *end != '\0' ||
        value < 1 ||
        value > INT_MAX) {
        return -1;
    }

    *id = (int)value;
    return 0;
}

static enum MHD_Result
handle_delete_device(struct MHD_Connection *connection, const char *url)
{
    int id;
    char response[128];

    if (parse_device_id(url, &id) < 0) {
        return send_json(
            connection,
            "{\"error\":\"invalid device ID\"}",
            MHD_HTTP_BAD_REQUEST
        );
    }

    if (db_delete_device(g_db, id) != SQLITE_OK) {
        fprintf(stderr, "database delete failed: %s\n",
                db_error(g_db));

        return send_json(
            connection,
            "{\"error\":\"database error\"}",
            MHD_HTTP_INTERNAL_SERVER_ERROR
        );
    }

    /*
     * sqlite3_changes() tells us whether a row actually existed.
     */
    if (sqlite3_changes(g_db) == 0) {
        return send_json(
            connection,
            "{\"error\":\"device not found\"}",
            MHD_HTTP_NOT_FOUND
        );
    }

    snprintf(response, sizeof(response),
             "{\"ok\":true,\"deleted_id\":%d}", id);

    return send_json(connection, response, MHD_HTTP_OK);
}

static enum MHD_Result
request_handler(
    void *cls,
    struct MHD_Connection *connection,
    const char *url,
    const char *method,
    const char *version,
    const char *upload_data,
    size_t *upload_data_size,
    void **con_cls
)
{
    RequestContext *request = *con_cls;

    (void)cls;
    (void)version;

    if (request == NULL) {
        request = calloc(1, sizeof(*request));

        if (request == NULL)
            return MHD_NO;

        *con_cls = request;

        if (strcmp(method, "POST") == 0 &&
            strcmp(url, "/api/devices") == 0) {
            const char *content_type =
                MHD_lookup_connection_value(
                    connection,
                    MHD_HEADER_KIND,
                    "Content-Type"
                );

            if (content_type == NULL ||
                strncmp(content_type, "application/json", 16) != 0) {
                return send_json(
                    connection,
                    "{\"error\":\"Content-Type must be application/json\"}",
                    MHD_HTTP_UNSUPPORTED_MEDIA_TYPE
                );
            }
        }

        return MHD_YES;
    }

    if (strcmp(method, "OPTIONS") == 0) {
        *upload_data_size = 0;
        return send_text(connection, "", MHD_HTTP_NO_CONTENT, "text/plain");
    }

    if (strcmp(method, "GET") == 0 &&
        strcmp(url, "/api/health") == 0) {
        *upload_data_size = 0;
        return send_json(connection, "{\"ok\":true}", MHD_HTTP_OK);
    }

    if (strcmp(method, "GET") == 0 &&
        strcmp(url, "/api/devices") == 0) {
        const char *campus;
        char *json;
        enum MHD_Result result;

        campus = MHD_lookup_connection_value(
            connection,
            MHD_GET_ARGUMENT_KIND,
            "campus"
        );

        if (campus == NULL)
            campus = "";

        json = devices_json(campus);

        if (json == NULL)
            return send_json(
                connection,
                "{\"error\":\"database error\"}",
                MHD_HTTP_INTERNAL_SERVER_ERROR
            );

        result = send_json(connection, json, MHD_HTTP_OK);
        free(json);

        *upload_data_size = 0;
        return result;
    }

    if (strcmp(method, "POST") == 0 &&
        strcmp(url, "/api/devices") == 0) {
        if (*upload_data_size > 0) {
            if (request->length + *upload_data_size > MAX_BODY_SIZE)
                return send_json(
                    connection,
                    "{\"error\":\"request body too large\"}",
                    MHD_HTTP_REQUEST_ENTITY_TOO_LARGE
                );

            char *new_body = realloc(
                request->body,
                request->length + *upload_data_size + 1
            );

            if (new_body == NULL)
                return MHD_NO;

            request->body = new_body;

            memcpy(
                request->body + request->length,
                upload_data,
                *upload_data_size
            );

            request->length += *upload_data_size;
            request->body[request->length] = '\0';

            *upload_data_size = 0;
            return MHD_YES;
        }

        return handle_post_device(connection, request);
    }

    if (strcmp(method, "DELETE") == 0 &&
        strncmp(url, "/api/devices/", strlen("/api/devices/")) == 0) {
        *upload_data_size = 0;
        return handle_delete_device(connection, url);
    }

    return send_json(
        connection,
        "{\"error\":\"not found\"}",
        MHD_HTTP_NOT_FOUND
    );
}

static void
request_completed(
    void *cls,
    struct MHD_Connection *connection,
    void **con_cls,
    enum MHD_RequestTerminationCode toe
)
{
    RequestContext *request = *con_cls;

    (void)cls;
    (void)connection;
    (void)toe;

    if (request != NULL) {
        free(request->body);
        free(request);
        *con_cls = NULL;
    }
}

int
main(int argc, char **argv)
{
    const char *database_path = "inventory.db";
    struct MHD_Daemon *daemon;

    if (argc >= 2)
        database_path = argv[1];

    if (db_open(database_path, &g_db) != SQLITE_OK) {
        fprintf(stderr, "cannot open database: %s\n", database_path);
        return EXIT_FAILURE;
    }

    if (db_init(g_db) != SQLITE_OK) {
        fprintf(stderr, "cannot initialize database: %s\n",
                db_error(g_db));
        sqlite3_close(g_db);
        return EXIT_FAILURE;
    }

    daemon = MHD_start_daemon(
        MHD_USE_INTERNAL_POLLING_THREAD |
        MHD_USE_DEBUG,
        LISTEN_PORT,
        NULL,
        NULL,
        &request_handler,
        NULL,
        MHD_OPTION_NOTIFY_COMPLETED,
        &request_completed,
        NULL,
        MHD_OPTION_CONNECTION_TIMEOUT,
        30,
        MHD_OPTION_END
    );

    if (daemon == NULL) {
        fprintf(stderr, "cannot listen on port %d\n", LISTEN_PORT);
        sqlite3_close(g_db);
        return EXIT_FAILURE;
    }

    printf("inventory server listening on http://127.0.0.1:%d\n",
           LISTEN_PORT);
    printf("database: %s\n", database_path);
    fflush(stdout);

    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);

    while (1)
        pause();

    MHD_stop_daemon(daemon);
    sqlite3_close(g_db);

    return EXIT_SUCCESS;
}
