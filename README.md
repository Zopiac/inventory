**THE SOFTWARE:**

Simply compile the main program with `make`. Requires libsqlite3, libmicrohttpd, libcjson, libncurses development files.

This compiles the main server (`inventory-server <database.db>`) which runs on port 9142 for the included web page, as well as an ncurses interface (`inventory-tui <database.db>`) which only runs locally to the database file but does not need the server to be running.

The web page can be used to perform basic device addition/removal. I use an nginx instance with a proxy_pass to <server_ip>:9142

**THE PROCESS:**

The "version 1.0" code was completed in three hours, using the Luna model that duck.ai defaulted to, just barely fitting within one provided session/content window. The TUI needs to be polished quite a bit, and the fields probably need major tweaking for other use cases.
