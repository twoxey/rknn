#include "http_server.h"
#include "camera.h"

typedef struct {
    volatile bool server_started;
    volatile bool camera_started;

    Connection* sse_clients[10];
    Connection* mpeg_streaming_clients[10];

    Arena arena;
} App_State;

bool connections_add(Connection* connections[], size_t connection_count, Connection* connection) {
    Connection** result = NULL;
    for (size_t i = 0; i < connection_count; ++i) {
        if (connections[i] == connection) {
            log_error("Sus!! Connection has already been added when trying to add a new client.\n");
            return false;
        }
        if (!result && !connection_is_open(connections[i])) {
            result = &connections[i];
        }
    }
    if (!result) {
        log_error("Maximum client count excceeded\n");
        return false;
    }
    *result = connection;
    return true;
}

bool connections_remove(Connection* connections[], size_t connection_count, Connection* connection) {
    bool found = false;
    for (size_t i = 0; i < connection_count; ++i) {
        if (found) {
            log_error("Sus!! Dupilcated connection found when removing from the list.\n");
        }
        if (connections[i] == connection) {
            connections[i] = NULL;
            found = true;
        }
    }
    return true;
}

int connections_write_data(Connection* connections[], size_t connection_count, const char* buf, size_t len) {
    int write_count = 0;
    for (size_t i = 0; i < connection_count; ++i) {
        Connection* connection = connections[i];
        if (connection) {
            if (!connection_write(connection, buf, len)) {
                log_error("Connectoin %p: failed to write data\n", connection);
                connections[i] = NULL;
            }
            ++write_count;
        }
    }
    return write_count;
}

/*
int streamed_clients_write_jpeg_data(Connection* connections[], size_t connection_count, const char* buf, size_t len) {
    //_Static_assert(false, "TODO");
    return connections_write_data(connections, connection_count, buf, len);
}
*/

bool state_add_sse_client(App_State* state, Connection* connection) {
    return connections_add(state->sse_clients, ARRAY_LEN(state->sse_clients), connection);
}

void state_remove_client(App_State* state, Connection* connection) {
    if (connections_remove(state->sse_clients, ARRAY_LEN(state->sse_clients), connection)) {
        log_print("[INFO] sse connection %p closed\n", connection);
    }
    if (connections_remove(state->mpeg_streaming_clients, ARRAY_LEN(state->mpeg_streaming_clients), connection)) {
        log_print("[INFO] mpeg stream connection %p closed\n", connection);
    }
}

int state_sse_write_message(App_State* state, string data) {
    size_t start = state->arena.used;

    string buf = { state->arena.base + state->arena.used, 0 };
    while (data.len > 0) {
        string line = string_split_by_line(&data);
        buf.len += arena_printf(&state->arena, "data: %.*s\r\n", (int)line.len, line.data).len;
    }
    buf.len += arena_printf(&state->arena, "\r\n").len;

    int result = connections_write_data(state->sse_clients, ARRAY_LEN(state->sse_clients), buf.data, buf.len);

    state->arena.used = start;

    return result;
}

void on_new_connection(Connection* connection, uint32_t addr, uint16_t port, void* data) {
}

void on_get_request(Connection* connection, string url, void* data) {
    App_State* state = data;
    Allocator allocator = arena_get_allocator(&TEMP_ARENA(8192));

    if (string_eq(url, string_from_cstr("/"))) {
        string body = read_entire_file("index.html", allocator);
        if (!body.data) {
            log_error("Failed to load index page\n");
            http_write_response_text(connection, 500, "Failed to load index page");
            return;
        }

        http_write_response(connection, 200, "text/html", body);
        return;

    } else if (string_eq(url, string_from_cstr("/events"))) {
        if (!state_add_sse_client(state, connection)) {
            log_error("Failed to add sse client\n");
            http_write_response_text(connection, 500, "Server failed to register sse client");
            return;
        }

        log_print("[INFO] new sse client registered\n");
        http_write_headers(connection, 200, string_from_cstr("Content-Type: text/event-stream\r\n"));
        return;

    } else if (string_eq(url, string_from_cstr("/broadcast"))) {
        int write_count = state_sse_write_message(state, string_from_cstr("This is a sse broadcast message"));
        http_write_response_text(connection, 200, arena_printf(&state->arena, "Broadcasted message to %d sse clients", write_count).data);
        return;
    }

    http_write_response_text(connection, 404, "Page not found");
}

void on_close(Connection* connection, void* data) {
    App_State* state = data;
    state_remove_client(state, connection);
}

void* server_thread_porc(void* data) {
    App_State* state = data;

    Arena arena = TEMP_ARENA(4096);

    Server* server = arena_push(&arena, Server);
    Http_Handlers handlers = {
        .on_new_connection = on_new_connection,
        .on_get_request = on_get_request,
        .on_close = on_close,
        .data = state,
    };
    if (http_server_init(server, INADDR_ANY, 8080, &handlers)) {
        state->server_started = true;

        while (http_server_run_loop(server)) {};

        state->server_started = false;
        http_server_deinit(server);
    }

    return NULL;
}

void* camera_thread_porc(void* data) {
    App_State* state = data;

    struct camera cam;
    if (!camera_init(&cam, "/dev/video0")) {
        log_error("Failed to start camera\n");
        goto end;
    }

    log_print("[INFO] camera stream started\n");

    state->camera_started = true;

    while (true) {
        struct mapped_buffer buffer;
        int index = camera_dequeue_buffer(&cam, &buffer);
        if (index < 0) goto end;

        if (state->server_started) {
            //streamed_clients_write_jpeg_data(streaming_connections, stream_connection_count, buffer.start, buffer.length);
        }

        if (!camera_queue_buffer(&cam, index)) goto end;
    }
end:
    state->camera_started = false;
    camera_deinit(&cam);
    return NULL;
}

int main1(void) {

    Thread* camera_thread = thread_start(camera_thread_porc, NULL);
    if (!camera_thread) {
        log_error("Failed to start camera thread\n");
        return 1;
    }

    Thread* server_thread = thread_start(server_thread_porc, NULL);
    if (!server_thread) {
        log_error("Failed to start server thread\n");
        return 1;
    }

    thread_join(camera_thread, NULL);
    thread_join(server_thread, NULL);

    return 0;
}

