#pragma once

#include "base_utils.h"

#include <stdint.h>

#define ADDR_PRI "%u.%u.%u.%u"
#define ADDR_ARG(a) (a)&0xFF, (a>>8)&0xFF, (a>>16)&0xFF, (a>>24)&0xFF

typedef struct Connection Connection;

bool connection_is_open(Connection* connection);
void connection_close(Connection* connection);
bool connection_read(Connection* connection, char* buf, size_t* len);
bool connection_write(Connection* connection, const char* buf, size_t len);

typedef struct {
    void (*on_new_connection)(Connection* connection, uint32_t addr, uint16_t port, void* data);
    void (*on_get_request)(Connection* connection, string url, void* data);
    void (*on_close)(Connection* connection, void* data);
    void* data;
} Http_Handlers;

bool http_server_run(uint32_t addr, uint16_t port, Http_Handlers* handlers);
bool http_write_headers(Connection* connection, int status, string headers);
bool http_write_response(Connection* connection, int status, const char* content_type, string body);
bool http_write_response_text(Connection* connection, int status, const char* body);

//
// Implementation
//

#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int tcp_socket_listen(uint32_t addr, uint16_t port) {
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

    if (bind(fd, (struct sockaddr *)&addr_in, sizeof(addr_in)) < 0) {
        log_error("Failed to bind address: %s\n", strerror(errno));
        goto error;
    }

    if (listen(fd, SOMAXCONN) < 0) {
        log_error("listen() failed: %s\n", strerror(errno));
        goto error;
    }

    return fd;

error:
    if (fd >= 0) close(fd);
    return -1;
}

bool epoll_add(int epoll_fd, int fd, epoll_data_t data) {
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

struct Connection {
    int _fd; // ~fd, this makes ~0 an invalid fd so zero initializaion will work
    pthread_mutex_t mutex;
};

bool connection_is_open(Connection* connection) {
    return connection && connection->_fd;
}

void connection_close(Connection* connection) {
    pthread_mutex_lock(&connection->mutex);

    if (connection_is_open(connection)) {
        close(~connection->_fd);
        connection->_fd = 0;
    }

    pthread_mutex_unlock(&connection->mutex);
}

bool connection_read(Connection* connection, char* buf, size_t* len) {
    pthread_mutex_lock(&connection->mutex);

    bool result = false;
    if (!connection_is_open(connection)) goto end;

    ssize_t bytes_read = recv(~connection->_fd, buf, *len, 0);
    if (bytes_read < 0) {
        log_error("recv() failed: %s\n", strerror(errno));
        connection_close(connection);
        goto end;
    }

    *len = bytes_read;
    result = true;
end:
    pthread_mutex_unlock(&connection->mutex);
    return result;
}

bool connection_write(Connection* connection, const char* buf, size_t len) {
    pthread_mutex_lock(&connection->mutex);

    bool result = false;
    if (!connection_is_open(connection)) goto end;

    ssize_t bytes_sent = send(~connection->_fd, buf, len, 0);
    if (bytes_sent < 0) {
        log_error("send() failed: %s\n", strerror(errno));
        connection_close(connection);
        goto end;
    }

    result = true;
end:
    pthread_mutex_unlock(&connection->mutex);
    return result;
}

bool http_write_headers(Connection* connection, int status, string headers) {
    if (!connection_is_open(connection)) {
        log_error("Attempt to write response to a closed connection");
        return false;
    }

    char buf[4096];
    if (headers.len + 256 /*max status line length*/ >= sizeof(buf)) {
        log_error("Headers excceeds response buffer size\n");
        return false;
    }
    int len = snprintf(
        buf, sizeof(buf),
        "HTTP/1.1 %d\r\n"
        "%.*s"
        "\r\n",
        status,
        (int)headers.len, headers.data
    );
    if (!connection_write(connection, buf, len)) {
        log_error("Failed to write response headers\n");
        return false;
    }
    log_print("[INFO] Connection %p response status -> %d\n", connection, status);
    return true;
}

bool http_write_response(Connection* connection, int status, const char* content_type, string body) {
    string headers = stack_buffer(1024);
    headers.len = snprintf(
        headers.data, headers.len,
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n",
        content_type,
        body.len
    );
    if (!http_write_headers(connection, status, headers)) {
        return false;
    }
    return connection_write(connection, body.data, body.len);
}

bool http_write_response_text(Connection* connection, int status, const char* body) {
    return http_write_response(connection, status, "text/plain", string_from_cstr(body));
}

bool http_parse_get_request(Connection* connection, const char* buf, size_t len, string* url) {
    string buf_remain = {(char*)buf, len};
    char* url_start = NULL;
    size_t url_len = 0;

    // parse request line
    {
        string request_line = string_split_by_line(&buf_remain);
        if (!buf_remain.data) { // not found
            log_error("Bad request: Incomplete request line\n");
            http_write_response_text(connection, 400, "Bad request: Incomplete request line");
            return false;
        }

        string protocol = string_from_cstr(" HTTP/1.1");
        if (!string_ends_with(request_line, protocol)) {
            log_error("Bad request: Request line does not end with ' HTTP/1.1'\n");
            http_write_response_text(connection, 400, "Bad request: Invalid Protocol");
            return false;
        }
        string method = string_from_cstr("GET ");
        if (!string_starts_with(request_line, method)) {
            log_error("Bad request: Request line does not start with 'GET '\n");
            http_write_response_text(connection, 400, "Bad request: Invalid Method");
            return false;
        }

        url_start = request_line.data + method.len;
        url_len = request_line.len - method.len - protocol.len;
    }

    // parse headers
    for (;;) {
        string header_line = string_split_by_line(&buf_remain);
        if (!buf_remain.data) { // not found
            log_error("Bad request: Incomplete headers\n");
            http_write_response_text(connection, 400, "Bad request: Incomplete headers");
            return false;
        }
        if (header_line.len == 0) break;
    }

    if (buf_remain.len != 0) {
        log_error("Bad request: GET request followed by more data after headers\n");
        http_write_response_text(connection, 400, "Bad request: GET request followed by more data after headers");
        return false;
    }

    url->data = url_start;
    url->len = url_len;

    return true;
}

bool http_server_run(uint32_t addr, uint16_t port, Http_Handlers* handlers) {
    bool result = true;

    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        log_error("epoll_create1() failed: %s\n", strerror(errno));
        result = false;
        goto end;
    }

    int listen_socket = tcp_socket_listen(addr, port);
    if (listen_socket < 0) {
        log_error("Failed to create and listen tcp socket\n");
        result = false;
        goto end;
    }

    if (!epoll_add(epoll_fd, listen_socket, (epoll_data_t){.ptr = &listen_socket})) {
        log_error("Failed to add listen socket to epoll\n");
        result = false;
        goto end;
    }

    Connection connections[20] = {};

    pthread_mutexattr_t mutex_attr;
    int err = pthread_mutexattr_init(&mutex_attr);
    if (err != 0) {
        log_error("pthread_mutexattr_init() failed: %s\n", strerror(err));
        result = false;
        goto end;
    }
    err = pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_RECURSIVE);
    if (err != 0) {
        log_error("pthread_mutexattr_settype() failed: %s\n", strerror(err));
        result = false;
        goto end;
    }
    ARRAY_FOREACH(Connection*, connections, connection) {
        err = pthread_mutex_init(&connection->mutex, &mutex_attr);
        if (err != 0) {
            log_error("pthread_mutex_init() failed: %s\n", strerror(err));
            result = false;
            goto end;
        }
    }

    log_print("[INFO] Server listening on http://"ADDR_PRI":%d\n", ADDR_ARG(addr), port);

    while (true) {
        struct epoll_event events[10];
        int event_count = epoll_wait(epoll_fd, events, ARRAY_LEN(events), -1);
        if (event_count < 0) {
            log_error("epoll_wait() failed: %s\n", strerror(errno));
            result = false;
            goto end;
        }
        for (int i = 0; i < event_count; ++i) {
            epoll_data_t data = events[i].data;
            if (data.ptr == &listen_socket) {
                struct sockaddr_in client_addr;
                socklen_t addrlen = sizeof(client_addr);
                int client_fd = accept(listen_socket, (struct sockaddr*)&client_addr, &addrlen);
                if (client_fd < 0) {
                    log_error("accept() failed: %s\n", strerror(errno));
                    continue;
                }
                Connection* result = NULL;
                ARRAY_FOREACH(Connection*, connections, c){
                    if (~c->_fd == client_fd) {
                        log_error("[Unreachable!!] accept() got a fd that is already existed in recorded connections\n");
                        result = false;
                        goto end;
                    }
                    if (!result && c->_fd == 0) {
                        c->_fd = ~client_fd;
                        result = c;
                    }
                }
                if (!result) {
                    log_error("Maximum connection count excceeded\n");
                    close(client_fd);
                    continue;
                }
                if (!epoll_add(epoll_fd, client_fd, (epoll_data_t){.ptr = result})) {
                    log_error("Failed to add new connection to epoll\n");
                    close(client_fd);
                    continue;
                }
                uint32_t addr = client_addr.sin_addr.s_addr;
                uint16_t port = ntohs(client_addr.sin_port);
                log_print("[INFO] Got new connection %p from: "ADDR_PRI":%d\n", result, ADDR_ARG(addr), port);
                if (handlers->on_new_connection) {
                    handlers->on_new_connection(result, addr, port, handlers->data);
                }
            } else {
                Connection* connection = data.ptr;
                if (!connection_is_open(connection)) {
                    log_error("[Unreachable!!] epoll_wait() returned a data pointer that is not an opening connection\n");
                    result = false;
                    goto end;
                }

                char buf[8192];
                size_t len = sizeof(buf);
                if (!connection_read(connection, buf, &len)) {
                    log_error("Failed to read from connection\n");
                    continue;
                }

                if (len == 0) {
                    if (handlers->on_close) {
                        handlers->on_close(connection, handlers->data);
                    }
                    connection_close(connection);
                    log_print("[INFO] Connection %p closed by remote client\n", connection);
                    continue;
                }

                string url;
                if (!http_parse_get_request(connection, buf, len, &url)) {
                    continue;
                }

                if (handlers->on_get_request) {
                    handlers->on_get_request(connection, url, handlers->data);
                }
            }
        }
    }

end:
    if (listen_socket >= 0) close(listen_socket);
    if (epoll_fd >= 0) close(epoll_fd);

    return result;
}

