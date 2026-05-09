#include "base_utils.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <unistd.h>
#include <sys/epoll.h>

int tcp_socket_connect(uint32_t addr, uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        log_error("socket() failed: %s\n", strerror(errno));
        goto error;
    }
    struct sockaddr_in addr_in = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = addr,
    };
    if (connect(fd, (struct sockaddr*)&addr_in, sizeof(addr_in)) < 0) {
        log_error("connect() failed: %s\n", strerror(errno));
        goto error;
    }
    return fd;

error:
    if (fd >= 0) close(fd);
    return -1;
}

bool epoll_add(int epoll_fd, int fd, void* ptr) {
    epoll_data_t data = { .ptr = ptr };
    struct epoll_event event = {
        .events = EPOLLIN,
        .data = data,
    };
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) < 0) {
        log_error("epoll_ctl(EPOLL_CTL_ADD) failed: %s\n", strerror(errno));
        return false;
    }
    return true;
}

bool buffer_read(int fd, string* buf) {
    ssize_t bytes_read = read(fd, buf->data, buf->len);
    if (bytes_read < 0) {
        log_error("read() failed: %s\n", strerror(errno));
        return false;
    }
    buf->len = bytes_read;
    return true;
}

int main(void) {
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        log_error("epoll_create1() failed: %s\n", strerror(errno));
        return 1;
    }

    int esp_connection = -1;

    if (!epoll_add(epoll_fd, STDIN_FILENO,  main))   return 1;

    while (1) {
        struct epoll_event events[10];
        int event_count = epoll_wait(epoll_fd, events, ARRAY_LEN(events), -1);
        if (event_count < 0) {
            log_error("epoll_wait() failed: %s\n", strerror(errno));
            return false;
        }
        for (int i = 0; i < event_count; ++i) {
            void* ptr = events[i].data.ptr;

            string buf = stack_buffer(1024);
            if (ptr == main) {
                if (!buffer_read(STDIN_FILENO, &buf)) {
                    log_error("Failed to read stdin\n");
                    return 1;
                }
                string data = buf;
                if (string_starts_with(data, string_from_cstr("set"))) {
                    string_advance(&data, 3);
                    char id = -1;
                    if (data.len > 0) id = data.data[0];
                    if (!(id >= '1' && id <= '4')) {
                        log_error("set cmd: expects an index of 1-4, got %c\n", id);
                        continue;
                    }
                    if (esp_connection >= 0) {
                        write(esp_connection, &id, sizeof(id));
                    }
                } else if (string_starts_with(data, string_from_cstr("connect"))) {
                    log_print("Connecting to esp...\n");

                    uint32_t esp_addr = *(uint32_t*)(uint8_t[4]){192, 168, 4, 1};
                    int fd = tcp_socket_connect(esp_addr, 8080);
                    if (fd < 0) {
                        log_error("Failed to connect to esp32\n");
                        continue;;
                    }
                    if (!epoll_add(epoll_fd, fd, &esp_connection)) {
                        log_error("Failed to add esp to epoll\n");
                        return 1;
                    }
                    esp_connection = fd;
                    log_print("Successfully connected to esp32\n");
                }
            } else if (ptr == &esp_connection) {
                if (!buffer_read(esp_connection, &buf)) {
                    log_error("Failed to read from esp\n");
                    return 1;
                }
                if (buf.len == 0) {
                    log_print("esp connection closed\n");
                    close(esp_connection);
                    esp_connection = -1;
                    return 1;
                }
                log_print("Data from esp: %.*s\n", (int)buf.len, buf.data);
            }
        }
    }

    // Http_Handlers handlers = {
    //     .on_new_connection = on_new_connection,
    // };
    // bool ok = http_server_run(INADDR_ANY, 8080, &handlers);
    // if (!ok) {
    //     log_error("Failed to start server");
    //     return 1;
    // }

    return 0;
}