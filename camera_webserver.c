#include "http_server.h"
#include "camera.h"

#include "yolo11.h"
#include "stb_image.h"

#include "coco_80_labels_list.h"

#define BOUNDARY_STRING "ExampleBoundaryString"

typedef struct {
    volatile bool should_close;
    volatile bool server_started;
    volatile bool camera_started;

    Connection* sse_clients[10];
    Connection* mjpg_streaming_clients[10];
    Connection* esp_connection;

    rknn_app_context_t* rknn_app_ctx;
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

int state_sse_write_message(App_State* state, const char* event, string data) {
    Arena builder = TEMP_ARENA(8192);
    if (event) {
        arena_printf(&builder, "event: %s\n", event);
    }
    while (data.len > 0) {
        string line = string_split_by_line(&data);
        arena_printf(&builder, "data: %.*s\n", (int)line.len, line.data);
    }
    arena_push_char(&builder, '\n');

    int result = connections_write_data(state->sse_clients, ARRAY_LEN(state->sse_clients), builder.base, builder.used);

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

bool on_data(Connection* connection, char* buf, size_t len, void* data) {
    App_State* state = data;
    string message = {buf, len};
    if (string_starts_with(message, string_from_cstr("XRP "))) {
        log_print("[INFO] got connection from esp32: %.*s\n", (int)message.len, message.data);
        if (state->esp_connection) {
            log_error("Got new esp32 connection when one is already present\n");
            return false;
        }
        state->esp_connection = connection;
        return true;
    }
    return false;
}

void on_new_connection(Connection* connection, uint32_t addr, uint16_t port, void* data) {
    (void)connection;
    (void)addr;
    (void)port;
    (void)data;
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
        Arena builder = TEMP_ARENA(1024);
        bool ok = false;
        if (state->esp_connection) {
            string message = string_from_cstr("broadcast esp32");
            ok = connection_write(state->esp_connection, message.data, message.len);
        }
        arena_printf(&builder, "Esp connected: %d\n", (bool)state->esp_connection);
        arena_printf(&builder, "Esp write succeeded: %d\n", ok);
        int write_count = state_sse_write_message(state, NULL, (string){builder.base, builder.used});
        arena_printf(&builder, "Broadcasted message to %d sse clients\n", write_count);
        http_write_response_text(connection, 200, builder.base);
        return;
    }

    http_write_response_text(connection, 404, "Page not found");
}

void on_close(Connection* connection, void* data) {
    App_State* state = data;
    if (connection == state->esp_connection) {
        state->esp_connection = NULL;
        log_print("[INFO] esp32 disconnected\n");
        state_sse_write_message(state, NULL, string_from_cstr("esp disconnected"));
    } else {
        state_remove_client(state, connection);
    }
}

void* server_thread_porc(void* data) {
    App_State* state = data;

    Http_Handlers handlers = {
        .on_new_connection = on_new_connection,
        .on_get_request = on_get_request,
        .on_close = on_close,
        .on_data = on_data,
        .data = state,
    };
    Server* server = http_server_init(INADDR_ANY, 8080, &handlers);
    if (server) {
        state->server_started = true;

        while (http_server_run_loop(server)) {};

        state->server_started = false;
        http_server_deinit(server);
    }

    state->should_close = true;

    return NULL;
}

struct timespec get_time() {
    struct timespec time = {};
    if (clock_gettime(CLOCK_MONOTONIC, &time) < 0) {
        log_error("clock_gettime() failed: %s\n", strerror(errno));
    }
    return time;
}

float time_diff(struct timespec a, struct timespec b) {
    return (a.tv_sec - b.tv_sec) + (float)(a.tv_nsec - b.tv_nsec) / 1e9f;
}

bool load_jpeg_image_from_memory(const unsigned char* buf, size_t len, image_buffer_t* image) {
    *image = (image_buffer_t){};

    int w, h, c;
    stbi_uc *pixeldata = stbi_load_from_memory(buf, len, &w, &h, &c, 0);
    if (!pixeldata) {
        log_error("error: failed to load jpeg image from memory\n");
        return false;
    }

    //int size = w * h * c;
    image->virt_addr = pixeldata;
    image->width = w;
    image->height = h;
    if (c == 4) {
        image->format = IMAGE_FORMAT_RGBA8888;
    } else if (c == 1) {
        image->format = IMAGE_FORMAT_GRAY8;
    } else {
        image->format = IMAGE_FORMAT_RGB888;
    }
    return true;
}

void* camera_thread_porc(void* data) {
    App_State* state = data;

    struct camera* cam = camera_init("/dev/video0");
    if (!cam) {
        log_error("Failed to start camera\n");
        goto end;
    }

    log_print("[INFO] camera stream started\n");

    state->camera_started = true;

    size_t frame_count = 0;
    size_t sent_count = 0;

    float frame_times[5] = {};
    int frame_time_idx = 0;

    struct timespec last_time = get_time();

    while (!state->should_close) {
        struct mapped_buffer buffer;
        int index = camera_dequeue_buffer(cam, &buffer);

        struct timespec current_time = get_time();

        if (index < 0) goto end;

        image_buffer_t image = {};
        bool image_loaded = load_jpeg_image_from_memory(buffer.start, buffer.length, &image);
        int clients_written = state_mjpg_stream_write_data(state, (string){buffer.start, buffer.length});

        if (!camera_queue_buffer(cam, index)) goto end;

        object_detect_result_list od_results = {};
        if (image_loaded) {
            int ret = inference_yolo11_model(state->rknn_app_ctx, &image, &od_results);
            if (ret != 0) {
                log_error("init_yolo11_model fail! ret=%d\n", ret);
            }
            free(image.virt_addr);
        }

        ++frame_count;
        sent_count += clients_written;

        frame_times[frame_time_idx++] = time_diff(current_time, last_time);
        frame_time_idx %= ARRAY_LEN(frame_times);

        float fps = 0;
        for (size_t i = 0; i < ARRAY_LEN(frame_times); ++i) {
            fps += frame_times[i];
        }
        fps /= ARRAY_LEN(frame_times);
        fps = 1.0f / fps;

        Arena builder = TEMP_ARENA(8192);
        arena_push_char(&builder, '{');
        arena_printf(&builder,
            "\"frame_count\": %zu,"
            "\"sent_count\": %zu,"
            "\"fps\": %f,",
            frame_count,
            sent_count,
            fps);
        arena_printf(&builder, "\"results\": [");
        for (int i = 0; i < od_results.count; i++) {
            object_detect_result *det_result = &od_results.results[i];
            if (i > 0) arena_push_char(&builder, ',');
            arena_printf(&builder,
                "{\"class\": \"%s\","
                "\"minx\": %d,"
                "\"miny\": %d,"
                "\"maxx\": %d,"
                "\"maxy\": %d,"
                "\"confidence\": %.3f}",
                label_get_name(det_result->cls_id),
                det_result->box.left,
                det_result->box.top,
                det_result->box.right,
                det_result->box.bottom,
                det_result->prop);
        }
        arena_push_char(&builder, ']');
        arena_push_char(&builder, '}');

        state_sse_write_message(state, "stream_info", (string){builder.base, builder.used});

        last_time = current_time;
    }
end:
    log_print("[INFO] camera stream ended\n");
    state->camera_started = false;
    camera_deinit(cam);
    return NULL;
}

void* message_thread_proc(void* data) {
    App_State* state = data;

    struct timespec start_time = get_time();
    int message_id = 0;
    while (!state->should_close) {
        float secs = time_diff(get_time(), start_time);
        state_sse_write_message(state, NULL, arena_printf(&TEMP_ARENA(1024), "Message id: %d, time elapsed: %f\n", message_id, secs));
        ++message_id;
        sleep(2);
    }

    return NULL;
}

int main(void) {
    App_State state = {};

    const char* model_path = "yolo11.rknn";

    rknn_app_context_t rknn_app_ctx = {};
    int ret = init_yolo11_model(model_path, &rknn_app_ctx);
    if (ret != 0) {
        log_error("init_yolo11_model fail! ret=%d model_path=%s\n", ret, model_path);
        return 1;
    }
    state.rknn_app_ctx = &rknn_app_ctx;

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
    /*
    Thread* message_thread = thread_start(message_thread_proc, &state);
    if (!server_thread) {
        log_error("Failed to start message thread\n");
        return 1;
    }
    */

    thread_join(camera_thread, NULL);
    thread_join(server_thread, NULL);
    //thread_join(message_thread, NULL);

    ret = release_yolo11_model(&rknn_app_ctx);
    if (ret != 0) {
        log_error("release_yolo11_model fail! ret=%d\n", ret);
    }

    return 0;
}

