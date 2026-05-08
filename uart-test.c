#include "uart.h"

int main(void) {
    const char* uart_file_path = "/dev/ttyUSB0";
    int port = uart_start(uart_file_path, B115200);
    if (port < 0) {
        log_error("Failed to open serial port\n");
        return 1;
    }

    log_print("Serial port %s opened\n", uart_file_path);

    // Set stdin to non blocking
    int stdin_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (stdin_flags < 0) {
        log_error("fcntl(F_GETFL) failed: %s\n", strerror(errno));
        return 1;
    }
    if (fcntl(STDIN_FILENO, F_SETFL, stdin_flags | O_NONBLOCK) < 0) {
        log_error("fcntl(F_SETFL) failed: %s\n", strerror(errno));
        return 1;
    }

    while (1) {
        char buf[1024];
        ssize_t bytes_read = read(port, buf, sizeof(buf));
        if (bytes_read < 0) {
            log_error("read(port) failed: %s\n", strerror(errno));
            break;
        }
        if (bytes_read > 0) {
            ssize_t bytes_written = write(STDOUT_FILENO, buf, bytes_read);
            assert(bytes_written > 0);
        }

        bytes_read = read(STDIN_FILENO, buf, sizeof(buf));
        if (bytes_read < 0) {
            if (errno != EAGAIN) {
                log_error("read(stdin) failed: %s\n", strerror(errno));
                break;
            }
        }
        if (bytes_read > 0) {
            ssize_t bytes_written = write(port, buf, bytes_read);
            if (bytes_written < 0) {
                log_error("write(port) failed: %s\n", strerror(errno));
                break;
            }
        }

        usleep(200000); // 200ms
    }

    close(port);

    return 0;
}
