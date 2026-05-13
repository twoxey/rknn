#include "http_server.h"
#include "camera.h"
#include "uart.h"

#include "yolo11.h"
#include "stb_image.h"

#include "coco_80_labels_list.h"
#include "esp32-program/haptic-motor-states.h"

#define BOUNDARY_STRING "ExampleBoundaryString"

typedef enum {
    Drive_Stop,
    Drive_Forward,
    Drive_SideLeft,
    Drive_SideRight,
    Drive_TurnLeft,
    Drive_TurnRight,
    Drive_Backward,
};

enum ESP_Output_Value {
    ESP_Output1 = (1<<0),
    ESP_Output2 = (1<<1),
    ESP_Output3 = (1<<2),
    ESP_Output4 = (1<<3),
};

typedef struct {
    volatile bool should_close;
    volatile bool server_started;
    volatile bool camera_started;

    Connection* sse_clients[10];
    Connection* mjpg_streaming_clients[10];
    Connection* esp_connection;

    int drive_train_uart;

    unsigned char esp_output_state;

    Drive_State drive_default_state;
    Drive_State drive_current_state;

    Haptic_States current_haptic_states;

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
    if (string_starts_with(message, string_from_cstr("ESP "))) {
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

void handle_esp_output(Connection* connection, App_State* state, enum ESP_Output_Value value) {
    state->esp_output_state ^= value;
    log_print(
        "current state: %d%d%d%d\n",
        (bool)(state->esp_output_state & ESP_Output1),
        (bool)(state->esp_output_state & ESP_Output2),
        (bool)(state->esp_output_state & ESP_Output3),
        (bool)(state->esp_output_state & ESP_Output4));

    if (state->esp_connection) {
        connection_write(state->esp_connection, (char*)&state->esp_output_state, sizeof(state->esp_output_state));
    }
    http_write_response_text(connection, 200, "OK");
}

bool drive_set_state(Connection* connection, App_State* state, Drive_State drive_state) {
    bool result = false;
    if (state->drive_train_uart) {
        unsigned char data = drive_state;
        ssize_t bytes_written = write(state->drive_train_uart, &data, sizeof(data));
        if (bytes_written < 0) {
            log_error("write(drive_train_uart) failed: %s\n", strerror(errno));
            state->drive_train_uart = 0;
        }
        log_print("Set drive train to state: %d\n", drive_state);
        result = true;
    }
    if (result) {
        http_write_response_text(connection, 200, "OK, successfully set drive train state");
    } else {
        http_write_response_text(connection, 500, "Failed to set drive train state");
    }
    return result;
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
    } else if (string_eq(url, string_from_cstr("/quit"))) {
        state->should_close = true;

        http_write_response_text(connection, 200, "Server closed");
        return;

    } else if (string_eq(url, string_from_cstr("/Drive_Stop"))) {
        drive_set_state(connection, state, Drive_Stop);
    } else if (string_eq(url, string_from_cstr("/Drive_Forward"))) {
        drive_set_state(connection, state, Drive_Forward);
    } else if (string_eq(url, string_from_cstr("/Drive_SideLeft"))) {
        drive_set_state(connection, state, Drive_SideLeft);
    } else if (string_eq(url, string_from_cstr("/Drive_SideRight"))) {
        drive_set_state(connection, state, Drive_SideRight);
    } else if (string_eq(url, string_from_cstr("/Drive_TurnLeft"))) {
        drive_set_state(connection, state, Drive_TurnLeft);
    } else if (string_eq(url, string_from_cstr("/Drive_TurnRight"))) {
        drive_set_state(connection, state, Drive_TurnRight);
    } else if (string_eq(url, string_from_cstr("/1"))) {
        handle_esp_output(connection, state, ESP_Output1);
    } else if (string_eq(url, string_from_cstr("/2"))) {
        handle_esp_output(connection, state, ESP_Output2);
    } else if (string_eq(url, string_from_cstr("/3"))) {
        handle_esp_output(connection, state, ESP_Output3);
    } else if (string_eq(url, string_from_cstr("/3"))) {
        handle_esp_output(connection, state, Output4);
    } else {
        http_write_response_text(connection, 404, "Page not found");
    }
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

        while (!state->should_close && http_server_run_loop(server)) {};

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

long time_diff_ms(struct timespec a, struct timespec b) {
    return (a.tv_sec - b.tv_sec) * 1000 + (a.tv_nsec - b.tv_nsec) / 1e6;
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

typedef struct {
    Haptic_Motor_State haptic_state;
    float pos_x;
    bool should_go_sideway;
} Process_Result;

Process_Result process_result(object_detect_result *det_result) {
    float f = 800;

    float H, W;
    float notify_threshold, warn_theshold;
    switch (det_result->cls_id) {
        case Label_person:      H = ;    W = ;  notify_threshold = ; warn_theshold = ; break;
        case Label_bicycle:     H = ;    W = ;  notify_threshold = ; warn_theshold = ; break;
        case Label_car:         H = ;    W = ;  notify_threshold = ; warn_theshold = ; break;
        case Label_motorcycle:  H = ;    W = ;  notify_threshold = ; warn_theshold = ; break;
        case Label_bus:         H = ;    W = ;  notify_threshold = ; warn_theshold = ; break;
        default: return;  // ignore other objects
    }

    // // reject certain results?
    // float confidence_threshold = ;
    // if (det_result->prop < confidence_threshold) return;

    int minx = det_result->box.left;
    int miny = det_result->box.top;
    int maxx = det_result->box.right;
    int maxy = det_result->box.bottom;

    float w = maxx - minx;
    float h = maxy - miny;

    float pos_x = (minx + maxx) * 0.5f;
    float pos_y = (minx + maxx) * 0.5f;

    float D_h = f * (H / h);    // distance based on width
    float D_w = f * (W / w);    // distance based on height

    float D = D_h;  // TODO: figure out which result to use

    Process_Result result = {};
    if (D < warn_theshold) {
        result.haptic_state = Haptic_Fast;
    } else if (D < notify_threshold) {
        result.haptic_state = Haptic_Slow;
    } else {
        result.haptic_state = Haptic_Stop;
    }
    result.pos_x = pos_x;
    result.should_go_sideway = D < warn_theshold;

    return result;
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

        if (!image_loaded) continue;

        object_detect_result_list od_results = {};
        int ret = inference_yolo11_model(state->rknn_app_ctx, &image, &od_results);
        free(image.virt_addr);

        if (ret != 0) {
            log_error("inference_yolo11_model failed: ret=%d\n", ret);
            continue;
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

        Haptic_Motor_State s0 = Haptic_Stop;
        Haptic_Motor_State s1 = Haptic_Stop;
        Haptic_Motor_State s2 = Haptic_Stop;
        Haptic_Motor_State s3 = Haptic_Stop;

        Drive_State drive_state = state->drive_default_state;

        for (int i = 0; i < od_results.count; i++) {
            object_detect_result *det_result = &od_results.results[i];
            Process_Result result = process_result(det_result);
            float horizontal_position = result.pos_x / image.width;
            if (horizontal_position < 0.33) {
                // left
                if (result.haptic_state > s0) s0 = result.haptic_state;
                if (result.haptic_state > s1) s1 = result.haptic_state;
            } else if (horizontal_position < 0.66) {
                // middle
                if (result.haptic_state > s1) s1 = result.haptic_state;
                if (result.haptic_state > s2) s2 = result.haptic_state;
            } else {
                // right
                if (result.haptic_state > s2) s2 = result.haptic_state;
                if (result.haptic_state > s3) s3 = result.haptic_state;
            }

            if (result.should_go_sideway) {
                drive_state = Drive_SideLeft;
            }

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

        Haptic_States haptic_states = haptic_motor_pack_states(s0, s1, s2, s3);
        if (haptic_states != state->current_haptic_states) {
            // TODO: send states to ESP32
            state->current_haptic_states = haptic_states;
        }

        // TODO: set drive train state

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

    int port = uart_start("/dev/ttyS4", B9600);
    if (port < 0) {
        log_error("Failed to open serial port\n");
        return 1;
    }

    state.drive_train_uart = port;

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
    if (!message_thread) {
        log_error("Failed to start message thread\n");
        return 1;
    }
    */

    thread_join(camera_thread, NULL);
    thread_join(server_thread, NULL);
    //thread_join(message_thread, NULL);

    close(state.drive_train_uart);

    ret = release_yolo11_model(&rknn_app_ctx);
    if (ret != 0) {
        log_error("release_yolo11_model fail! ret=%d\n", ret);
    }

    return 0;
}

