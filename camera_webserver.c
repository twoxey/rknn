#include "http_server.h"
#include "camera.h"

#define BOUNDARY_STRING "ExampleBoundaryString"

typedef struct {
    volatile bool should_close;
    volatile bool server_started;
    volatile bool camera_started;

    Connection* sse_clients[10];
    Connection* mjpg_streaming_clients[10];
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
        if (connections[i] == connection) {
            if (found) {
                log_error("Sus!! Dupilcated connection found when removing from the list.\n");
            }
            connections[i] = NULL;
            found = true;
        }
    }
    return found;
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

bool state_add_sse_client(App_State* state, Connection* connection) {
    return connections_add(state->sse_clients, ARRAY_LEN(state->sse_clients), connection);
}

bool state_add_mjpg_streaming_client(App_State* state, Connection* connection) {
    return connections_add(state->mjpg_streaming_clients, ARRAY_LEN(state->mjpg_streaming_clients), connection);
}

void state_remove_client(App_State* state, Connection* connection) {
    if (connections_remove(state->sse_clients, ARRAY_LEN(state->sse_clients), connection)) {
        log_print("[INFO] sse connection %p closed\n", connection);
    }
    if (connections_remove(state->mjpg_streaming_clients, ARRAY_LEN(state->mjpg_streaming_clients), connection)) {
        log_print("[INFO] mpeg stream connection %p closed\n", connection);
    }
}

int state_sse_write_message(App_State* state, string data) {
    Arena arena = TEMP_ARENA(8192);

    string buf = { arena.base + arena.used, 0 };
    while (data.len > 0) {
        string line = string_split_by_line(&data);
        buf.len += arena_printf(&arena, "data: %.*s\r\n", (int)line.len, line.data).len;
    }
    buf.len += arena_printf(&arena, "\r\n").len;

    int result = connections_write_data(state->sse_clients, ARRAY_LEN(state->sse_clients), buf.data, buf.len);

    return result;
}

int state_mjpg_stream_write_data(App_State* state, string jpeg_data) {
    string multipart_header = arena_printf(&TEMP_ARENA(1024),
        "--"BOUNDARY_STRING"\r\n"
        "Content-Type: image/jpeg\r\n"
        "Content-Length: %zu\r\n"
        "\r\n",
        jpeg_data.len
    );

    int header_count = connections_write_data(state->mjpg_streaming_clients, ARRAY_LEN(state->mjpg_streaming_clients), multipart_header.data, multipart_header.len);
    int data_count = connections_write_data(state->mjpg_streaming_clients, ARRAY_LEN(state->mjpg_streaming_clients), jpeg_data.data, jpeg_data.len);
    if (header_count != data_count) {
        log_error("Error when writing stream jpeg data: written multipart header to %d clients, while written image data to %d clients\n", header_count, data_count);
    }
    return data_count;
}

void on_new_connection(Connection* connection, uint32_t addr, uint16_t port, void* data) {
}

void on_get_request(Connection* connection, string url, void* data) {
    App_State* state = data;
    if (string_eq(url, string_from_cstr("/"))) {
        string body = read_entire_file("index.html", arena_get_allocator(&TEMP_ARENA(40960)));
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

    } else if (string_eq(url, string_from_cstr("/camera"))) {
        if (!state->camera_started) {
            http_write_response_text(connection, 500, "Camera not started");
            return;
        }
        if (!state_add_mjpg_streaming_client(state, connection)) {
            log_error("Failed to add sse client\n");
            http_write_response_text(connection, 500, "Server failed to register streaming client");
            return;
        }

        http_write_headers(connection, 200, string_from_cstr("Content-Type: multipart/x-mixed-replace; boundary="BOUNDARY_STRING"\r\n"));
        return;

    } else if (string_eq(url, string_from_cstr("/broadcast"))) {
        int write_count = state_sse_write_message(state, string_from_cstr(
            "This is a sse broadcast message\n"
            "Here's the second line.\n"
        ));
        http_write_response_text(connection, 200, arena_printf(&TEMP_ARENA(1024), "Broadcasted message to %d sse clients", write_count).data);
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

    state->should_close = true;

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

    while (!state->should_close) {
        struct mapped_buffer buffer;
        int index = camera_dequeue_buffer(&cam, &buffer);

        if (index < 0) goto end;

        state_mjpg_stream_write_data(state, (string){buffer.start, buffer.length});

        if (!camera_queue_buffer(&cam, index)) goto end;
    }
end:
    log_print("[INFO] camera stream ended\n");
    state->camera_started = false;
    camera_deinit(&cam);
    return NULL;
}

void* message_thread_proc(void* data) {
    App_State* state = data;

    struct timespec start_time = {};
    if (clock_gettime(CLOCK_MONOTONIC, &start_time) < 0) {
        log_error("clock_gettime() failed: %s\n", strerror(errno));
    }
    int message_id = 0;
    while (!state->should_close) {
        float secs = 0;
        struct timespec current_time = {};
        if (clock_gettime(CLOCK_MONOTONIC, &current_time) < 0) {
            log_error("clock_gettime() failed: %s\n", strerror(errno));
        } else {
            secs = (current_time.tv_sec - start_time.tv_sec) + (float)(current_time.tv_nsec - start_time.tv_nsec) / 1e9f;
        }
        state_sse_write_message(state, arena_printf(&TEMP_ARENA(1024), "Message id: %d, time elapsed: %f\n", message_id, secs));
        ++message_id;
        sleep(2);
    }

    return NULL;
}

int main(void) {
    App_State state = {};

    Thread* server_thread = thread_start(server_thread_porc, &state);
    if (!server_thread) {
        log_error("Failed to start server thread\n");
        return 1;
    }
    Thread* camera_thread = thread_start(camera_thread_porc, &state);
    if (!camera_thread) {
        log_error("Failed to start camera thread\n");
        return 1;
    }
    Thread* message_thread = thread_start(message_thread_proc, &state);
    if (!server_thread) {
        log_error("Failed to start message thread\n");
        return 1;
    }

    thread_join(camera_thread, NULL);
    thread_join(server_thread, NULL);
    thread_join(message_thread, NULL);

    return 0;
}

