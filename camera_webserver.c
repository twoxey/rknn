#include "http_server.h"
#include "camera.h"

bool connections_write_data(Connection* connections[], size_t connection_count, const char* buf, size_t len) {
    bool result = true;
    for (size_t i = 0; i < connection_count; ++i) {
        Connection* connection = connections[i];
        if (connection) {
            if (!connection_write(connection, buf, len)) {
                connections[i] = NULL;
                result = false;
            }
        }
    }
    return result;
}

bool streamed_clients_write_jpeg_data(Connection* connections[], size_t connection_count, const char* buf, size_t len) {
    _Static_assert(false, "TODO");
    return connections_write_data(connections, connection_count, buf, len);
}

void on_new_connection(Connection* connection, uint32_t addr, uint16_t port, void* data) {
}

void on_get_request(Connection* connection, string url, void* data) {
    log_print("[INFO] Connection %p: GET %.*s\n", connection, (int)url.len, url.data);

    http_write_response_text(connection, 404, "Page not found");
}

void on_close(Connection* connection, void* data) {
}


void* server_thread_porc(void* data) {
    return NULL;
}

void* camera_thread_porc(void* data) {

    struct camera cam;
    if (!camera_init(&cam, "/dev/video0")) {
        log_error("Failed to start camera\n");
        goto end;
    }

    log_print("[INFO] camera stream started\n");

    while (true) {
        struct mapped_buffer buffer;
        int index = camera_dequeue_buffer(&cam, &buffer);
        if (index < 0) goto end;

        streamed_clients_write_jpeg_data(streaming_connections, stream_connection_count, buffer.start, buffer.length);

        if (!camera_queue_buffer(&cam, index)) goto end;
    }

end:
    camera_deinit(&cam);
    return NULL;
}

int main(void) {
    Http_Handlers handlers = {
        .on_new_connection = on_new_connection,
        .on_get_request = on_get_request,
        .on_close = on_close,
    };
    http_server_run(INADDR_ANY, 8080, &handlers);

    Thread* thread_start(void* thread_porc(void* data), void* data);
    bool thread_join(Thread* thread, void** ret);
}

