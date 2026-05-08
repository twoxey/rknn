#include "base_utils.h"

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>

int uart_start(const char* file_path, speed_t baud_rate) {
    log_print("Opening serial port: %s\n", file_path);
    int fd = open(file_path, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        log_error("open() failed: %s\n", strerror(errno));
        goto error;
    }

    struct termios term;
    if (tcgetattr(fd, &term) != 0) {
        log_error("tcgetattr() failed: %s\n", strerror(errno));
        goto error;
    }

    if (cfsetispeed(&term, baud_rate) < 0) {
        log_error("cfsetispeed() failed: %s\n", strerror(errno));
        goto error;
    }
    if (cfsetospeed(&term, baud_rate) < 0) {
        log_error("cfsetospeed() failed: %s\n", strerror(errno));
        goto error;
    }

    term.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG); // Enable NON CANONICAL Mode for Serial Port Comm
    term.c_iflag &= ~(IXON | IXOFF | IXANY);         // Turn OFF software based flow control (XON/XOFF).
    term.c_cflag &= ~CRTSCTS;                        // Turn OFF Hardware based flow control RTS/CTS 

    term.c_cflag |= CREAD | CLOCAL; // Turn on READ & ignore ctrl lines

    // 8N1
    term.c_cflag &= ~PARENB;      // No parity
    term.c_cflag &= ~CSTOPB;      // One stop bit
    term.c_cflag &= ~CSIZE;       // Clear the CSIZE so we can set databits later
    term.c_cflag |=  CS8;         // 8 bits

    // set non blocking read
    term.c_cc[VMIN] = 0;  // Minimum characters to read
    term.c_cc[VTIME] = 0; // Time to wait in deciseconds

    if (tcsetattr(fd, TCSANOW, &term) != 0) {
        log_error("tcsetattr() failed: %s\n", strerror(errno));
        goto error;
    }

    return fd;
error:
    if (fd >= 0) close(fd);
    return -1;
}
