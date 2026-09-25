CC := cc

CFLAGS := -Wall -Wextra -Wpedantic -std=c11 \
          $(shell pkg-config --cflags sqlite3 libmicrohttpd libcjson)

LDLIBS := $(shell pkg-config --libs sqlite3 libmicrohttpd libcjson)

all: inventory-server inventory-tui

inventory-server: src/server.o src/db.o
	$(CC) $^ -o $@ $(LDLIBS)

src/server.o: src/server.c src/db.h
	$(CC) $(CFLAGS) -c src/server.c -o src/server.o

src/db.o: src/db.c src/db.h
	$(CC) $(CFLAGS) -c src/db.c -o src/db.o

inventory-tui: src/inventory_tui.c
	cc -std=c11 -Wall -Wextra -pedantic src/inventory_tui.c -o inventory-tui -lsqlite3 -lncurses

clean:
	rm -f src/*.o inventory-server inventory-tui

