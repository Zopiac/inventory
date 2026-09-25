#define _XOPEN_SOURCE 700

#include <ncurses.h>
#include <sqlite3.h>

#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DB_PATH "inventory.db"
#define FIELD_SIZE 256
#define MAX_DEVICES 4096
#define MAX_CAMPUSES 256

typedef struct {
    int id;

    char campus[FIELD_SIZE];
    char location[FIELD_SIZE];
    char device_type[FIELD_SIZE];
    char make[FIELD_SIZE];
    char model[FIELD_SIZE];
    char serial_number[FIELD_SIZE];
    char role_name[FIELD_SIZE];
    char inputs[FIELD_SIZE];
    char ip_address[FIELD_SIZE];
    int is_static;
    char mac_address[FIELD_SIZE];
    char vlan[FIELD_SIZE];
    char status[FIELD_SIZE];
} Device;

typedef struct {
    const char *label;
    char *value;
    size_t value_size;
} Field;

typedef struct {
    int index;
    int minimum_width;
    int width;
    const char *title;
} ListColumn;

static sqlite3 *g_db = NULL;

static Device *g_devices = NULL;
static int g_device_count = 0;
static int g_selected = 0;

static char g_campuses[MAX_CAMPUSES][FIELD_SIZE];
static int g_campus_count = 0;
static int g_current_campus = 0;

static int g_first_column = 0;

static ListColumn columns[] = {
    { 0, 12, 0, "Location" },
    { 1, 14, 0, "Device type" },
    { 2, 12, 0, "Make" },
    { 3, 16, 0, "Model" },
    { 4, 16, 0, "Role/name" },
    { 5, 15, 0, "IP" }
};

#define COLUMN_COUNT ((int)(sizeof(columns) / sizeof(columns[0])))

/* ---------- Utility functions ---------- */

static void copy_string(char *destination, size_t size, const char *source)
{
    if (!source)
        source = "";

    snprintf(destination, size, "%s", source);
}

static const char *device_field(const Device *device, int index)
{
    switch (index) {
        case 0: return device->location;
        case 1: return device->device_type;
        case 2: return device->make;
        case 3: return device->model;
        case 4: return device->role_name;
        case 5: return device->ip_address;
        default: return "";
    }
}

static void draw_text_clipped(
    int y,
    int x,
    int width,
    const char *text,
    bool selected
)
{
    if (width <= 0)
        return;

    char *buffer = calloc((size_t)width + 1, 1);

    if (!buffer)
        return;

    snprintf(buffer, (size_t)width + 1, "%-*.*s", width, width, text);

    if (selected)
        attron(A_REVERSE);

    mvaddnstr(y, x, buffer, width);

    if (selected)
        attroff(A_REVERSE);

    free(buffer);
}

static void draw_centered(int y, const char *text)
{
    int width = getmaxx(stdscr);
    int x = (width - (int)strlen(text)) / 2;

    if (x < 0)
        x = 0;

    mvaddnstr(y, x, text, width - x);
}

static void show_message(const char *message)
{
    int height, width;
    getmaxyx(stdscr, height, width);

    int box_width = (int)strlen(message) + 6;

    if (box_width < 30)
        box_width = 30;

    if (box_width > width - 2)
        box_width = width - 2;

    WINDOW *window = newwin(5, box_width, (height - 5) / 2,
                            (width - box_width) / 2);

    if (!window)
        return;

    box(window, 0, 0);
    mvwaddnstr(window, 2, 2, message, box_width - 4);
    mvwaddstr(window, 3, 2, "Press any key");
    wrefresh(window);

    wgetch(window);
    delwin(window);
}

/* ---------- Database initialization ---------- */

static int initialize_database(void)
{
    const char *sql =
        "PRAGMA journal_mode = WAL;"

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

    char *error = NULL;

    int rc = sqlite3_exec(g_db, sql, NULL, NULL, &error);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Database initialization failed: %s\n",
                error ? error : "unknown error");
        sqlite3_free(error);
        return 0;
    }

    sqlite3_busy_timeout(g_db, 5000);

    return 1;
}

/* ---------- Campus handling ---------- */

static int reload_campuses(void)
{
    const char *sql =
        "SELECT DISTINCT campus "
        "FROM devices "
        "WHERE campus <> '' "
        "ORDER BY campus COLLATE NOCASE";

    sqlite3_stmt *statement = NULL;

    g_campus_count = 0;

    int rc = sqlite3_prepare_v2(g_db, sql, -1, &statement, NULL);

    if (rc != SQLITE_OK)
        return 0;

    while (sqlite3_step(statement) == SQLITE_ROW &&
           g_campus_count < MAX_CAMPUSES) {
        const char *campus =
            (const char *)sqlite3_column_text(statement, 0);

        copy_string(
            g_campuses[g_campus_count],
            sizeof(g_campuses[g_campus_count]),
            campus
        );

        g_campus_count++;
    }

    sqlite3_finalize(statement);

    if (g_campus_count == 0) {
        copy_string(g_campuses[0], sizeof(g_campuses[0]), "Default");
        g_campus_count = 1;
        g_current_campus = 0;
    } else if (g_current_campus >= g_campus_count) {
        g_current_campus = g_campus_count - 1;
    }

    return 1;
}

static const char *current_campus(void)
{
    if (g_campus_count <= 0)
        return "Default";

    return g_campuses[g_current_campus];
}

static void next_campus(void)
{
    if (g_campus_count <= 0)
        return;

    g_current_campus++;

    if (g_current_campus >= g_campus_count)
        g_current_campus = 0;

    g_selected = 0;
}

static void previous_campus(void)
{
    if (g_campus_count <= 0)
        return;

    g_current_campus--;

    if (g_current_campus < 0)
        g_current_campus = g_campus_count - 1;

    g_selected = 0;
}

/* ---------- Device database operations ---------- */

static void clear_devices(void)
{
    free(g_devices);
    g_devices = NULL;
    g_device_count = 0;
}

static int load_devices(void)
{
    const char *sql =
        "SELECT "
        "id, campus, location, device_type, make, model, "
        "serial_number, role_name, inputs, ip_address, is_static, "
        "mac_address, vlan, status "
        "FROM devices "
        "WHERE campus = ? "
        "ORDER BY location, device_type, make, model";

    sqlite3_stmt *statement = NULL;

    clear_devices();

    int rc = sqlite3_prepare_v2(g_db, sql, -1, &statement, NULL);

    if (rc != SQLITE_OK)
        return 0;

    sqlite3_bind_text(
        statement,
        1,
        current_campus(),
        -1,
        SQLITE_TRANSIENT
    );

    int capacity = 32;

    g_devices = calloc((size_t)capacity, sizeof(Device));

    if (!g_devices) {
        sqlite3_finalize(statement);
        return 0;
    }

    while ((rc = sqlite3_step(statement)) == SQLITE_ROW) {
        if (g_device_count >= capacity) {
            capacity *= 2;

            Device *new_devices = realloc(
                g_devices,
                (size_t)capacity * sizeof(Device)
            );

            if (!new_devices) {
                sqlite3_finalize(statement);
                clear_devices();
                return 0;
            }

            g_devices = new_devices;
        }

        Device *device = &g_devices[g_device_count];

        device->id = sqlite3_column_int(statement, 0);

        copy_string(
            device->campus,
            sizeof(device->campus),
            (const char *)sqlite3_column_text(statement, 1)
        );

        copy_string(
            device->location,
            sizeof(device->location),
            (const char *)sqlite3_column_text(statement, 2)
        );

        copy_string(
            device->device_type,
            sizeof(device->device_type),
            (const char *)sqlite3_column_text(statement, 3)
        );

        copy_string(
            device->make,
            sizeof(device->make),
            (const char *)sqlite3_column_text(statement, 4)
        );

        copy_string(
            device->model,
            sizeof(device->model),
            (const char *)sqlite3_column_text(statement, 5)
        );

        copy_string(
            device->serial_number,
            sizeof(device->serial_number),
            (const char *)sqlite3_column_text(statement, 6)
        );

        copy_string(
            device->role_name,
            sizeof(device->role_name),
            (const char *)sqlite3_column_text(statement, 7)
        );

        copy_string(
            device->inputs,
            sizeof(device->inputs),
            (const char *)sqlite3_column_text(statement, 8)
        );

        copy_string(
            device->ip_address,
            sizeof(device->ip_address),
            (const char *)sqlite3_column_text(statement, 9)
        );

        device->is_static = sqlite3_column_int(statement, 10);

        copy_string(
            device->mac_address,
            sizeof(device->mac_address),
            (const char *)sqlite3_column_text(statement, 11)
        );

        copy_string(
            device->vlan,
            sizeof(device->vlan),
            (const char *)sqlite3_column_text(statement, 12)
        );

        copy_string(
            device->status,
            sizeof(device->status),
            (const char *)sqlite3_column_text(statement, 13)
        );

        g_device_count++;
    }

    sqlite3_finalize(statement);

    if (g_selected >= g_device_count)
        g_selected = g_device_count - 1;

    if (g_selected < 0)
        g_selected = 0;

    return rc == SQLITE_DONE;
}

static int insert_device(const Device *device)
{
    const char *sql =
        "INSERT INTO devices "
        "(campus, location, device_type, make, model, serial_number, "
        "role_name, inputs, ip_address, is_static, mac_address, vlan, status) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";

    sqlite3_stmt *statement = NULL;

    int rc = sqlite3_prepare_v2(g_db, sql, -1, &statement, NULL);

    if (rc != SQLITE_OK)
        return 0;

    sqlite3_bind_text(statement, 1, device->campus, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, device->location, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, device->device_type, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 4, device->make, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 5, device->model, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 6, device->serial_number, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 7, device->role_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 8, device->inputs, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 9, device->ip_address, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(statement, 10, device->is_static);
    sqlite3_bind_text(statement, 11, device->mac_address, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 12, device->vlan, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 13, device->status, -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(statement);

    sqlite3_finalize(statement);

    return rc == SQLITE_DONE;
}

static int update_device(const Device *device)
{
    const char *sql =
        "UPDATE devices SET "
        "campus=?, location=?, device_type=?, make=?, model=?, "
        "serial_number=?, role_name=?, inputs=?, ip_address=?, "
        "is_static=?, mac_address=?, vlan=?, status=?, "
        "updated_at=CURRENT_TIMESTAMP "
        "WHERE id=?";

    sqlite3_stmt *statement = NULL;

    int rc = sqlite3_prepare_v2(g_db, sql, -1, &statement, NULL);

    if (rc != SQLITE_OK)
        return 0;

    sqlite3_bind_text(statement, 1, device->campus, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, device->location, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, device->device_type, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 4, device->make, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 5, device->model, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 6, device->serial_number, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 7, device->role_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 8, device->inputs, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 9, device->ip_address, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(statement, 10, device->is_static);
    sqlite3_bind_text(statement, 11, device->mac_address, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 12, device->vlan, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 13, device->status, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(statement, 14, device->id);

    rc = sqlite3_step(statement);

    sqlite3_finalize(statement);

    return rc == SQLITE_DONE;
}

static int delete_device(int id)
{
    const char *sql = "DELETE FROM devices WHERE id = ?";

    sqlite3_stmt *statement = NULL;

    int rc = sqlite3_prepare_v2(g_db, sql, -1, &statement, NULL);

    if (rc != SQLITE_OK)
        return 0;

    sqlite3_bind_int(statement, 1, id);

    rc = sqlite3_step(statement);

    sqlite3_finalize(statement);

    return rc == SQLITE_DONE;
}

/* ---------- Layout ---------- */

static void calculate_column_widths(void)
{
    int height, width;
    getmaxyx(stdscr, height, width);

    (void)height;

    int total_minimum = 0;

    for (int i = 0; i < COLUMN_COUNT; i++)
        total_minimum += columns[i].minimum_width;

    int separators = COLUMN_COUNT - 1;
    int available = width - separators - 2;

    for (int i = 0; i < COLUMN_COUNT; i++)
        columns[i].width = columns[i].minimum_width;

    int extra = available - total_minimum;

    if (extra <= 0)
        return;

    int visible_count = COLUMN_COUNT - g_first_column;

    if (visible_count <= 0)
        visible_count = 1;

    int extra_each = extra / visible_count;

    for (int i = g_first_column; i < COLUMN_COUNT; i++)
        columns[i].width += extra_each;
}

static int visible_column_count(void)
{
    int height, width;
    getmaxyx(stdscr, height, width);

    (void)height;

    int used = 0;
    int count = 0;

    for (int i = g_first_column; i < COLUMN_COUNT; i++) {
        int required = columns[i].minimum_width + 1;

        if (used + required > width - 1)
            break;

        used += required;
        count++;
    }

    return count;
}

static void draw_header(void)
{
    int height, width;
    getmaxyx(stdscr, height, width);

    calculate_column_widths();

    attron(A_BOLD | COLOR_PAIR(2));

    mvhline(0, 0, ' ', width);

    mvprintw(
        0,
        2,
        "Tech Inventory - Campus: %s",
        current_campus()
    );

    attroff(A_BOLD | COLOR_PAIR(2));

    attron(A_BOLD);

    int x = 1;
    int shown = 0;

    for (int i = g_first_column; i < COLUMN_COUNT; i++) {
        if (shown >= visible_column_count())
            break;

        draw_text_clipped(2, x, columns[i].width,
                          columns[i].title, false);

        x += columns[i].width + 1;
        shown++;
    }

    attroff(A_BOLD);

    mvhline(3, 0, ACS_HLINE, width);

    (void)height;
}

static void draw_footer(void)
{
    int height, width;
    getmaxyx(stdscr, height, width);

    attron(A_REVERSE);

    mvhline(height - 1, 0, ' ', width);

    mvaddnstr(
        height - 1,
        1,
        "Enter: Details | e: Edit | +: Add | d/Del: Delete | r: Reload | Tab: Change campus | ?: Help | q: Quit",
        width - 2
    );

    attroff(A_REVERSE);
}

static void draw_list(void)
{
    int height, width;
    getmaxyx(stdscr, height, width);

    erase();

    draw_header();

    int first_row = 4;
    int last_row = height - 2;

    if (g_device_count == 0) {
        draw_centered(first_row + 2, "No devices in this campus.");
        draw_footer();
        refresh();
        return;
    }

    int x;
    int shown;

    for (int row = 0; row < last_row - first_row + 1; row++) {
        int device_index = row;

        if (device_index >= g_device_count)
            break;

        Device *device = &g_devices[device_index];

        x = 1;
        shown = 0;

        for (int i = g_first_column; i < COLUMN_COUNT; i++) {
            if (shown >= visible_column_count())
                break;

            draw_text_clipped(
                first_row + row,
                x,
                columns[i].width,
                device_field(device, columns[i].index),
                device_index == g_selected
            );

            x += columns[i].width + 1;
            shown++;
        }
    }

    draw_footer();
    refresh();
}

/* ---------- Details view ---------- */

static void draw_detail_line(
    int y,
    int label_x,
    int value_x,
    const char *label,
    const char *value
)
{
    attron(A_BOLD);
    mvprintw(y, label_x, "%s:", label);
    attroff(A_BOLD);

    mvprintw(y, value_x, "%s", value ? value : "");
}

static void draw_details(const Device *device)
{
    int height, width;
    getmaxyx(stdscr, height, width);

    erase();

    attron(A_BOLD | COLOR_PAIR(2));
    mvhline(0, 0, ' ', width);
    mvprintw(0, 2, "Device Details");
    attroff(A_BOLD | COLOR_PAIR(2));

    int y = 2;

    draw_detail_line(y++, 2, 22, "ID", "");
    mvprintw(y - 1, 22, "%d", device->id);

    draw_detail_line(y++, 2, 22, "Campus", device->campus);
    draw_detail_line(y++, 2, 22, "Location", device->location);
    draw_detail_line(y++, 2, 22, "Device type", device->device_type);
    draw_detail_line(y++, 2, 22, "Make", device->make);
    draw_detail_line(y++, 2, 22, "Model", device->model);
    draw_detail_line(y++, 2, 22, "S/N", device->serial_number);
    draw_detail_line(y++, 2, 22, "Role/name", device->role_name);
    draw_detail_line(y++, 2, 22, "Input(s)", device->inputs);
    draw_detail_line(y++, 2, 22, "IP", device->ip_address);
    draw_detail_line(
        y++,
        2,
        22,
        "Static",
        device->is_static ? "Yes" : "No"
    );
    draw_detail_line(y++, 2, 22, "MAC", device->mac_address);
    draw_detail_line(y++, 2, 22, "VLAN", device->vlan);
    draw_detail_line(y++, 2, 22, "Status", device->status);

    attron(A_REVERSE);
    mvhline(height - 1, 0, ' ', width);
    mvaddnstr(
        height - 1,
        1,
        "Backspace/Esc Return  e Edit  d Delete",
        width - 2
    );
    attroff(A_REVERSE);

    refresh();
}

/* ---------- Input dialog ---------- */

static void draw_dialog_background(
    WINDOW *window,
    const char *title
)
{
    int height, width;
    getmaxyx(window, height, width);

    werase(window);
    box(window, 0, 0);

    wattron(window, A_BOLD);
    mvwprintw(window, 0, 2, " %s ", title);
    wattroff(window, A_BOLD);

    mvwhline(window, height - 2, 1, ACS_HLINE, width - 2);
}

static int edit_device(Device *device, bool is_new)
{
    int height, width;
    getmaxyx(stdscr, height, width);

    int dialog_height = 20;
    int dialog_width = 76;

    if (dialog_height > height - 2)
        dialog_height = height - 2;

    if (dialog_width > width - 2)
        dialog_width = width - 2;

    WINDOW *window = newwin(
        dialog_height,
        dialog_width,
        (height - dialog_height) / 2,
        (width - dialog_width) / 2
    );

    if (!window)
        return 0;

    keypad(window, TRUE);
    curs_set(1);
    echo();

    char *values[] = {
        device->campus,
        device->location,
        device->device_type,
        device->make,
        device->model,
        device->serial_number,
        device->role_name,
        device->inputs,
        device->ip_address,
        device->mac_address,
        device->vlan,
        device->status
    };

    const char *labels[] = {
        "Campus",
        "Location",
        "Device type",
        "Make",
        "Model",
        "S/N",
        "Role/name",
        "Input(s)",
        "IP",
        "MAC",
        "VLAN",
        "Status"
    };

    const int field_count = 12;

    Field fields[12];

    for (int i = 0; i < field_count; i++) {
        fields[i].label = labels[i];
        fields[i].value = values[i];
        fields[i].value_size = FIELD_SIZE;
    }

    int active_field = 0;
    bool finished = false;
    bool accepted = false;

    while (!finished) {
        draw_dialog_background(
            window,
            is_new ? "Add Device" : "Edit Device"
        );

        int label_width = 14;

        for (int i = 0; i < field_count; i++) {
            int row = 2 + i;

            if (i == active_field)
                wattron(window, A_REVERSE);

            mvwhline(window, row, 1, ' ', dialog_width - 2);

            if (i == active_field)
                wattroff(window, A_REVERSE);

            if (i == active_field) {
                wattron(window, A_REVERSE);
                mvwprintw(
                    window,
                    row,
                    1,
                    "%-*s:",
                    label_width,
                    fields[i].label
                );
                wattroff(window, A_REVERSE);
            }

            mvwprintw(
                window,
                row,
                label_width + 3,
                "%-*.*s",
                dialog_width - label_width - 6,
                dialog_width - label_width - 6,
                fields[i].value
            );
        }

        int static_row = 14;

        if (active_field == field_count) {
            wattron(window, A_REVERSE);
        }

        mvwprintw(
            window,
            static_row,
            1,
            "Static: [%c]",
            device->is_static ? 'X' : ' '
        );

        if (active_field == field_count)
            wattroff(window, A_REVERSE);

        int help_row = dialog_height - 1;

        mvwprintw(
            window,
            help_row,
            2,
            "Tab/↑↓ Navigate  Enter Save  Esc Cancel"
        );

        wrefresh(window);

        int ch = wgetch(window);

        if (ch == 27) {
            finished = true;
            accepted = false;
            break;
        }

        if (ch == KEY_F(2) || ch == KEY_ENTER || ch == '\n') {
            finished = true;
            accepted = true;
            break;
        }

        if (ch == KEY_UP || ch == KEY_BTAB) {
            active_field--;

            if (active_field < 0)
                active_field = field_count;
        } else if (ch == KEY_DOWN || ch == '\t') {
            active_field++;

            if (active_field > field_count)
                active_field = 0;
        } else if (active_field == field_count) {
            if (ch == ' ' || ch == 'x' || ch == 'X')
                device->is_static = !device->is_static;
        } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            size_t length = strlen(fields[active_field].value);

            if (length > 0)
                fields[active_field].value[length - 1] = '\0';
        } else if (ch >= 32 && ch <= 126) {
            size_t length = strlen(fields[active_field].value);

            if (length + 1 < fields[active_field].value_size) {
                fields[active_field].value[length] = (char)ch;
                fields[active_field].value[length + 1] = '\0';
            }
        }
    }

    noecho();
    curs_set(0);

    delwin(window);

    if (!accepted)
        return 0;

    if (device->campus[0] == '\0') {
        show_message("Campus is required.");
        return 0;
    }

    if (device->location[0] == '\0') {
        show_message("Location is required.");
        return 0;
    }

    if (device->device_type[0] == '\0') {
        show_message("Device type is required.");
        return 0;
    }

    if (device->status[0] == '\0')
        copy_string(device->status, sizeof(device->status), "Active");

    return 1;
}

/* ---------- Confirmation ---------- */

static int confirm_delete(const Device *device)
{
    int height, width;
    getmaxyx(stdscr, height, width);

    int dialog_height = 9;
    int dialog_width = 64;

    if (dialog_width > width - 2)
        dialog_width = width - 2;

    WINDOW *window = newwin(
        dialog_height,
        dialog_width,
        (height - dialog_height) / 2,
        (width - dialog_width) / 2
    );

    if (!window)
        return 0;

    keypad(window, TRUE);

    box(window, 0, 0);

    wattron(window, A_BOLD);
    mvwprintw(window, 1, 2, "Delete device?");
    wattroff(window, A_BOLD);

    mvwprintw(window, 3, 2, "%s / %s / %s",
              device->location,
              device->device_type,
              device->model);

    mvwprintw(window, 4, 2, "S/N: %s", device->serial_number);

    mvwprintw(window, 6, 2, "Press y to delete, any other key to cancel.");

    wrefresh(window);

    int ch = wgetch(window);

    delwin(window);

    return ch == 'y' || ch == 'Y';
}

/* ---------- Commands ---------- */

static void add_device(void)
{
    Device device;
    memset(&device, 0, sizeof(device));

    copy_string(
        device.campus,
        sizeof(device.campus),
        current_campus()
    );

    copy_string(
        device.status,
        sizeof(device.status),
        "Active"
    );

    if (!edit_device(&device, true))
        return;

    if (!insert_device(&device)) {
        show_message(sqlite3_errmsg(g_db));
        return;
    }

    reload_campuses();
    load_devices();

    for (int i = 0; i < g_device_count; i++) {
        if (g_devices[i].serial_number[0] != '\0' &&
            strcmp(g_devices[i].serial_number,
                   device.serial_number) == 0) {
            g_selected = i;
            break;
        }
    }
}

static void edit_selected_device(void)
{
    if (g_device_count == 0)
        return;

    Device copy = g_devices[g_selected];

    if (!edit_device(&copy, false))
        return;

    if (!update_device(&copy)) {
        show_message(sqlite3_errmsg(g_db));
        return;
    }

    reload_campuses();
    load_devices();
}

static void delete_selected_device(void)
{
    if (g_device_count == 0)
        return;

    Device *device = &g_devices[g_selected];

    if (!confirm_delete(device))
        return;

    if (!delete_device(device->id)) {
        show_message(sqlite3_errmsg(g_db));
        return;
    }

    load_devices();
}

/* ---------- Main loop ---------- */

static void setup_colors(void)
{
    if (!has_colors())
        return;

    start_color();
    use_default_colors();

    init_pair(1, COLOR_WHITE, -1);
    init_pair(2, COLOR_BLACK, COLOR_CYAN);
}

static void show_help(void)
{
    int height, width;
    getmaxyx(stdscr, height, width);

    int dialog_height = 18;
    int dialog_width = 64;

    if (dialog_height > height - 2)
        dialog_height = height - 2;

    if (dialog_width > width - 2)
        dialog_width = width - 2;

    WINDOW *window = newwin(
        dialog_height,
        dialog_width,
        (height - dialog_height) / 2,
        (width - dialog_width) / 2
    );

    if (!window)
        return;

    keypad(window, TRUE);

    box(window, 0, 0);

    wattron(window, A_BOLD);
    mvwprintw(window, 1, 2, "Keyboard Help");
    wattroff(window, A_BOLD);

    const char *help[] = {
        "Up/Down or j/k       Select device",
        "Home/End             First/last device",
        "Page Up/Page Down    Move by page",
        "Left/Right or h/l    Scroll columns",
        "Enter                View details",
        "e                    Edit device",
        "+                    Add device",
        "Delete or d          Delete device",
        "Tab                  Next campus",
        "Shift+Tab            Previous campus",
        "r                    Reload database",
        "Backspace            Return from details",
        "?                    This help",
        "q                    Quit"
    };

    int count = (int)(sizeof(help) / sizeof(help[0]));

    for (int i = 0; i < count && i + 3 < dialog_height - 1; i++)
        mvwaddnstr(window, i + 3, 2, help[i], dialog_width - 4);

    mvwprintw(window, dialog_height - 2, 2, "Press any key to close.");

    wrefresh(window);
    wgetch(window);

    delwin(window);
}

static void list_loop(void)
{
    int ch;

    while (1) {
        draw_list();

        ch = getch();

        switch (ch) {
            case 'q':
            case 'Q':
                return;

            case KEY_UP:
            case 'k':
                if (g_selected > 0)
                    g_selected--;
                break;

            case KEY_DOWN:
            case 'j':
                if (g_selected < g_device_count - 1)
                    g_selected++;
                break;

            case KEY_HOME:
                g_selected = 0;
                break;

            case KEY_END:
                if (g_device_count > 0)
                    g_selected = g_device_count - 1;
                break;

            case KEY_PPAGE:
                g_selected -= LINES - 5;

                if (g_selected < 0)
                    g_selected = 0;

                break;

            case KEY_NPAGE:
                g_selected += LINES - 5;

                if (g_selected >= g_device_count)
                    g_selected = g_device_count - 1;

                if (g_selected < 0)
                    g_selected = 0;

                break;

            case KEY_LEFT:
            case 'h':
                if (g_first_column > 0)
                    g_first_column--;

                break;

            case KEY_RIGHT:
            case 'l':
                if (g_first_column < COLUMN_COUNT - 1)
                    g_first_column++;

                break;

            case '\t':
                next_campus();
                load_devices();
                break;

            case KEY_BTAB:
                previous_campus();
                load_devices();
                break;

            case 'r':
            case 'R':
                reload_campuses();
                load_devices();
                break;

            case '+':
                add_device();
                break;

            case 'e':
            case 'E':
                edit_selected_device();
                break;

            case KEY_DC:
            case 'd':
            case 'D':
                delete_selected_device();
                break;

            case '\n':
            case KEY_ENTER:
                if (g_device_count > 0) {
                    bool in_details = true;

                    while (in_details) {
                        draw_details(&g_devices[g_selected]);

                        int detail_key = getch();

                        switch (detail_key) {
                            case KEY_BACKSPACE:
                            case 127:
                            case 8:
                            case 27:
                                in_details = false;
                                break;

                            case 'e':
                            case 'E':
                                edit_selected_device();
                                in_details = false;
                                break;

                            case 'd':
                            case 'D':
                            case KEY_DC:
                                delete_selected_device();
                                in_details = false;
                                break;
                        }
                    }
                }

                break;

            case '?':
                show_help();
                break;

            case KEY_RESIZE:
                clearok(stdscr, TRUE);
                break;
        }
    }
}

int main(int argc, char **argv)
{
    const char *database_path = DB_PATH;

    if (argc >= 2)
        database_path = argv[1];

    int rc = sqlite3_open(database_path, &g_db);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Could not open database: %s\n",
                sqlite3_errmsg(g_db));

        if (g_db)
            sqlite3_close(g_db);

        return EXIT_FAILURE;
    }

    if (!initialize_database()) {
        sqlite3_close(g_db);
        return EXIT_FAILURE;
    }

    if (!reload_campuses() || !load_devices()) {
        fprintf(stderr, "Could not load inventory data: %s\n",
                sqlite3_errmsg(g_db));

        sqlite3_close(g_db);
        return EXIT_FAILURE;
    }

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    setup_colors();

    if (has_colors())
        attron(COLOR_PAIR(1));

    list_loop();

    if (has_colors())
        attroff(COLOR_PAIR(1));

    endwin();

    clear_devices();
    sqlite3_close(g_db);

    return EXIT_SUCCESS;
}

