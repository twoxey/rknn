#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

int main(void) {
    const char* device_name = "/dev/video0";
    fprintf(stderr, "Opening device: '%s'\n", device_name);

    int fd = open(device_name, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Failed to open device '%s': %s\n", device_name, strerror(errno));
        return 1;
    }

    struct v4l2_capability caps = {};
    int ret = ioctl(fd, VIDIOC_QUERYCAP, &caps);
    if (ret < 0) {
        fprintf(stderr, "Failed to query device capability: %s\n", strerror(errno));
        goto end;
    }

    fprintf(stderr, "Device driver: %s\n", caps.driver);
    fprintf(stderr, "Device name: %s\n", caps.card);
    fprintf(stderr, "Device location: %s\n", caps.bus_info);
    fprintf(stderr, "Driver version: %u\n", caps.version);

    if (!(caps.capabilities & V4L2_CAP_DEVICE_CAPS)) {
        fprintf(stderr, "V4L2_CAP_DEVICE_CAPS flag not set.\n");
        goto end;
    }

    bool supports_video_capture = caps.device_caps & V4L2_CAP_VIDEO_CAPTURE;
    bool supports_read_write_io = caps.device_caps & V4L2_CAP_READWRITE;
    bool supports_async_io      = caps.device_caps & V4L2_CAP_ASYNCIO;
    bool supports_streaming     = caps.device_caps & V4L2_CAP_STREAMING;

    fprintf(stderr, "  V4L2_CAP_VIDEO_CAPTURE: %u\n", supports_video_capture);
    fprintf(stderr, "  V4L2_CAP_READWRITE:     %u\n", supports_read_write_io);
    fprintf(stderr, "  V4L2_CAP_ASYNCIO:       %u\n", supports_async_io);
    fprintf(stderr, "  V4L2_CAP_STREAMING:     %u\n", supports_streaming);

    if (!supports_video_capture) {
        fprintf(stderr, "'%s' is not a video capture device\n", device_name);
        goto end;
    }

    struct v4l2_format fmt = {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ret = ioctl(fd, VIDIOC_G_FMT, &fmt);
    if (ret < 0) {
        fprintf(stderr, "Failed to get device pixel format: %s\n", strerror(errno));
        goto end;
    }

    __u32 format = fmt.fmt.pix.pixelformat;
    __u32 image_size = fmt.fmt.pix.sizeimage;

    fprintf(stderr, "Format:\n");
    fprintf(stderr, "  type:  %u\n", fmt.type);
    fprintf(stderr, "  width:  %u\n", fmt.fmt.pix.width);
    fprintf(stderr, "  height: %u\n", fmt.fmt.pix.height);
    fprintf(stderr, "  format: %c%c%c%c\n", format, format >> 8, format >> 16, format >> 24);
    fprintf(stderr, "  field: %d\n", fmt.fmt.pix.field);
    fprintf(stderr, "  bytesperline: %d\n", fmt.fmt.pix.bytesperline);
    fprintf(stderr, "  sizeimage: %d\n", fmt.fmt.pix.sizeimage);
    fprintf(stderr, "  colorspace: %d\n", fmt.fmt.pix.colorspace);
    fprintf(stderr, "  priv: %d\n", fmt.fmt.pix.priv == V4L2_PIX_FMT_PRIV_MAGIC);

    if (format != V4L2_PIX_FMT_MJPEG) {
        fprintf(stderr, "Only supports pixel format V4L2_PIX_FMT_MJPEG for now\n");
        goto end;
    }

    struct v4l2_requestbuffers request = {};
    request.count = 1;
    request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    request.memory = V4L2_MEMORY_DMABUF;

    ret = ioctl(fd, VIDIOC_REQBUFS, &request);
    if (ret < 0) {
        fprintf(stderr, "Failed to get DMA buffer from device: %s\n", strerror(errno));
        goto end;
    }

    fprintf(stderr, "Successfully requested DMA buffers from device\n");

end:
    close(fd);

    return 0;
}
