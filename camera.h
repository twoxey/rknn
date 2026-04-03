#include <stdbool.h>
#include <stddef.h>

#define PIX_FMT_PRI "%c%c%c%c"
#define PIX_FMT_ARG(f) (f), (f)>>8, (f)>>16, (f)>>24

struct mapped_buffer {
    void* start;
    size_t length;
};

struct camera {
    bool started;
    int fd;
    struct mapped_buffer buffers[4];
    unsigned int mapped_buffer_count;
};

bool camera_init(struct camera* cam, const char* device_name);
void camera_deinit(struct camera* cam);
bool camera_start_streaming(struct camera* cam);
bool camera_stop_streaming(struct camera* cam);
int  camera_dequeue_buffer(struct camera* cam, struct mapped_buffer* buffer);
bool camera_queue_buffer(struct camera* cam, int index);

//
// Implementation
//

#include <stdio.h>
#include <errno.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <linux/videodev2.h>

int open_video_capture_device(const char* device_name) {
    int fd = open(device_name, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Failed to open device '%s': %s\n", device_name, strerror(errno));
        goto error;
    }

    struct v4l2_capability capability = {};
    if (ioctl(fd, VIDIOC_QUERYCAP, &capability) < 0) {
        fprintf(stderr, "Failed to query device capability: %s\n", strerror(errno));
        goto error;
    }

    if (!(capability.capabilities & V4L2_CAP_DEVICE_CAPS)) {
        fprintf(stderr, "Unreachable: V4L2_CAP_DEVICE_CAPS flag not set.\n");
        goto error;
    }

    if (!(capability.device_caps & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "'%s' is not a video capture device\n", device_name);
        goto error;
    }

    if (!(capability.device_caps & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "'%s' does not support streaming\n", device_name);
        goto error;
    }

    return fd;

error:
    if (fd >= 0) close(fd);
    return -1;
}

void enumerate_and_select_frame_size(int fd, struct v4l2_pix_format* pix) {
    struct { __u32 width, height; } supported_frame_sizes[10] = {};
    size_t supported_size_count = 0;
    size_t selected_index = 0;

    // enumerate supported frame sizes
    {
        struct v4l2_frmsizeenum frmsizeenum = {};
        frmsizeenum.index = 0;
        frmsizeenum.pixel_format = pix->pixelformat;
        if (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsizeenum) < 0) {
            fprintf(stderr, "Failed to enumerate device supported frame sizes: %s\n", strerror(errno));
            goto end;
        }
        switch (frmsizeenum.type) {
            case V4L2_FRMSIZE_TYPE_DISCRETE:
                do {
                    if (supported_size_count < sizeof(supported_frame_sizes)/sizeof(supported_frame_sizes[0])) {
                        supported_frame_sizes[supported_size_count].width  = frmsizeenum.discrete.width;
                        supported_frame_sizes[supported_size_count].height = frmsizeenum.discrete.height;
                        ++supported_size_count;
                    }

                    ++frmsizeenum.index;
                } while (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsizeenum) == 0);
                break;

            case V4L2_FRMSIZE_TYPE_STEPWISE:
            case V4L2_FRMSIZE_TYPE_CONTINUOUS:
                fprintf(stderr, "%s:%d: TODO: Not implemented\n", __FILE__, __LINE__);
                goto end;
        }
    }

    fprintf(stderr, "Supported frame sizes: %lu\n", supported_size_count);
    for (size_t i = 0; i < supported_size_count; ++i) {
        fprintf(stderr, "  [%lu] %ux%u\n", i, supported_frame_sizes[i].width, supported_frame_sizes[i].height);
    }

    fprintf(stderr, "Select a frame size: ");
    int ch = getchar();
    if (ch != '\n') {
        getchar(); // drop LF
        if (ch < '0' || ch > '9') {
            fprintf(stderr, "Expects a character from 0-9, got %c (%u)\n", ch, ch);
            goto end;
        }
        selected_index = ch - '0';
    }
    if (selected_index >= supported_size_count) {
        fprintf(stderr, "Selected index out of bounce\n");
        goto end;
    }

end:
    pix->width  = supported_frame_sizes[selected_index].width;
    pix->height = supported_frame_sizes[selected_index].height;
}

struct v4l2_fract enumerate_and_select_frame_interval(int fd, struct v4l2_pix_format pix) {
    struct v4l2_fract supported_frame_intervals[10] = {};
    size_t supported_interval_count = 0;
    size_t selected_index = 0;

    // enumerate supported frame intervals
    {
        struct v4l2_frmivalenum frmivalenum = {};
        frmivalenum.index = 0;
        frmivalenum.pixel_format = pix.pixelformat;
        frmivalenum.width = pix.width;
        frmivalenum.height = pix.height;
        if (ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &frmivalenum) < 0) {
            fprintf(stderr, "Failed to enumerate device supported frame frmivalenums: %s\n", strerror(errno));
            goto end;
        }
        switch (frmivalenum.type) {
            case V4L2_FRMIVAL_TYPE_DISCRETE:
                do {
                    if (supported_interval_count < sizeof(supported_frame_intervals)/sizeof(supported_frame_intervals[0])) {
                        supported_frame_intervals[supported_interval_count++] = frmivalenum.discrete;
                    }
                    ++frmivalenum.index;
                } while (ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &frmivalenum) == 0);
                if (errno != EINVAL) {
                    fprintf(stderr, "Failed to enumerate device supported frame frmivalenums: %s\n", strerror(errno));
                    goto end;
                }
                break;
            case V4L2_FRMIVAL_TYPE_STEPWISE:
            case V4L2_FRMIVAL_TYPE_CONTINUOUS:
                fprintf(stderr, "%s:%d: TODO: Not implemented\n", __FILE__, __LINE__);
                goto end;
        }
    }

    fprintf(stderr, "Supported frame intervals: %lu\n", supported_interval_count);
    for (size_t i = 0; i < supported_interval_count; ++i) {
        struct v4l2_fract fract = supported_frame_intervals[i];
        float sec = (float)fract.numerator   / (float)fract.denominator;
        float fps = (float)fract.denominator / (float)fract.numerator;
        fprintf(stderr, "  [%lu] %fsec, %ffps\n", i, sec, fps);
    }

    fprintf(stderr, "Select a frame interval: ");
    int ch = getchar();
    if (ch != '\n') {
        getchar(); // drop LF
        if (ch < '0' || ch > '9') {
            fprintf(stderr, "Expects a character from 0-9, got %c (%u)\n", ch, ch);
            goto end;
        }
        selected_index = ch - '0';
    }
    if (selected_index >= supported_interval_count) {
        fprintf(stderr, "Selected index out of bounce\n");
        goto end;
    }

end:
    return supported_frame_intervals[selected_index];
}

bool video_capture_set_pix_format(int fd, struct v4l2_pix_format* pix) {
    struct v4l2_format format = {};
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix = *pix;
    if (ioctl(fd, VIDIOC_S_FMT, &format) < 0) {
        fprintf(stderr, "Failed to set device pixel format: %s\n", strerror(errno));
        return false;
    }
    *pix = format.fmt.pix;
    return true;
}

bool video_capture_set_frame_interval(int fd, struct v4l2_fract* timeperframe) {
    struct v4l2_streamparm streamparm = {};
    streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    streamparm.parm.capture.timeperframe = *timeperframe;
    if (ioctl(fd, VIDIOC_S_PARM, &streamparm) < 0) {
        fprintf(stderr, "Failed to set device streaming params: %s\n", strerror(errno));
        return false;
    }
    *timeperframe = streamparm.parm.capture.timeperframe;
    return true;
}

void unmap_buffers(__u32 count, struct mapped_buffer* buffers) {
    for (__u32 i = 0; i < count; ++i) {
        if (buffers[i].start != 0) {
            munmap(buffers[i].start, buffers[i].length);
        }
    }
}

__u32 video_capture_request_and_map_buffers(int fd, __u32 request_count, struct mapped_buffer* buffers) {
    fprintf(stderr, "requesting %u buffers.\n", request_count);

    struct v4l2_requestbuffers requestbuffers = {};
    requestbuffers.count = request_count;
    requestbuffers.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    requestbuffers.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_REQBUFS, &requestbuffers) < 0) {
        fprintf(stderr, "Failed to reqest buffer from device: %s\n", strerror(errno));
        goto error;
    }

    fprintf(stderr, "%d buffers allocated.\n", requestbuffers.count);

    if (requestbuffers.count < 2) {
        fprintf(stderr, "Insufficient buffer count\n");
        goto error;
    }

    for (__u32 i = 0; i < requestbuffers.count; ++i) {
        struct v4l2_buffer buf = {};
        buf.index = i;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            fprintf(stderr, "Failed to query mmapped buffer location: %s\n", strerror(errno));
            goto error;
        }
        void* addr = mmap(NULL, buf.length, PROT_READ|PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
        if (addr == MAP_FAILED) {
            fprintf(stderr, "mmap: Failed to map buffer: %s\n", strerror(errno));
            goto error;
        }

        buffers[i].start = addr;
        buffers[i].length = buf.length;
    }

    return requestbuffers.count;

error:
    unmap_buffers(requestbuffers.count, buffers);
    return 0;
}

bool video_capture_queue_buffer(int fd, __u32 index) {
    struct v4l2_buffer buf = {};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = index;
    if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
        fprintf(stderr, "Failed to queue buffer: %s\n", strerror(errno));
        return false;
    }
    return true;
}

bool video_capture_dequeue_buffer(int fd, struct v4l2_buffer* buf) {
    buf->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf->memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_DQBUF, buf) < 0) {
        fprintf(stderr, "Failed to dequeue buffer: %s\n", strerror(errno));
        return false;
    }
    return true;
}

bool camera_queue_buffer(struct camera* cam, int index){
    if (index < 0) return false;
    return video_capture_queue_buffer(cam->fd, index);
}

int camera_dequeue_buffer(struct camera* cam, struct mapped_buffer* buffer){
    struct v4l2_buffer buf = {};
    if (!video_capture_dequeue_buffer(cam->fd, &buf)) {
        return -1;
    }
    buffer->start = cam->buffers[buf.index].start;
    buffer->length = buf.bytesused;
    return buf.index;
}

bool camera_start_streaming(struct camera* cam) {
    if (!cam->started) {
        if (ioctl(cam->fd, VIDIOC_STREAMON, &(enum v4l2_buf_type){V4L2_BUF_TYPE_VIDEO_CAPTURE}) < 0) {
            fprintf(stderr, "Failed to start streaming: %s\n", strerror(errno));
            return false;
        }
        cam->started = true;
    }
    return true;
}

bool camera_stop_streaming(struct camera* cam) {
    if (cam->started) {
        if (ioctl(cam->fd, VIDIOC_STREAMOFF, &(enum v4l2_buf_type){V4L2_BUF_TYPE_VIDEO_CAPTURE}) < 0) {
            fprintf(stderr, "Failed to stop streaming: %s\n", strerror(errno));
            return false;
        }
        cam->started = false;
    }
    return true;
}

void camera_deinit(struct camera* cam) {
    camera_stop_streaming(cam);
    unmap_buffers(cam->mapped_buffer_count, cam->buffers);
    if (cam->fd >= 0) close(cam->fd);
}

bool camera_init(struct camera* cam, const char* device_name) {
    fprintf(stderr, "Open device: %s\n", device_name);

    *cam = (struct camera){};
    cam->fd = open_video_capture_device(device_name);
    if (cam->fd < 0) goto error;

    bool supports_mjpeg = false;

    struct v4l2_fmtdesc fmtdesc = {};
    fmtdesc.index = 0;
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    while (ioctl(cam->fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
        fprintf(stderr, "Supported format: "PIX_FMT_PRI" (%s)\n", PIX_FMT_ARG(fmtdesc.pixelformat), fmtdesc.description);

        if (fmtdesc.pixelformat == V4L2_PIX_FMT_MJPEG) {
            supports_mjpeg = true;
        }

        ++fmtdesc.index;
    }
    if (errno != EINVAL) {
        fprintf(stderr, "Failed to enumerate device supported formats: %s\n", strerror(errno));
    }

    if (!supports_mjpeg) {
        fprintf(stderr, "Only supports pixel format V4L2_PIX_FMT_MJPEG for now\n");
        goto error;
    }

    struct v4l2_pix_format pix = { .pixelformat = V4L2_PIX_FMT_MJPEG };

    fprintf(stderr, "Using format: "PIX_FMT_PRI"\n", PIX_FMT_ARG(pix.pixelformat));

    enumerate_and_select_frame_size(cam->fd, &pix);

    fprintf(stderr, "Selected frame size: %ux%u\n", pix.width, pix.height);

    video_capture_set_pix_format(cam->fd, &pix);

    fprintf(stderr, "Current pixel format:\n");
    fprintf(stderr, "  width:  %u\n", pix.width);
    fprintf(stderr, "  height: %u\n", pix.height);
    fprintf(stderr, "  format: "PIX_FMT_PRI"\n", PIX_FMT_ARG(pix.pixelformat));
    fprintf(stderr, "  field: %d\n", pix.field);
    fprintf(stderr, "  bytesperline: %d\n", pix.bytesperline);
    fprintf(stderr, "  sizeimage: %d\n", pix.sizeimage);
    fprintf(stderr, "  colorspace: %d\n", pix.colorspace);
    fprintf(stderr, "  priv: %d\n", pix.priv == V4L2_PIX_FMT_PRIV_MAGIC);

    if (pix.pixelformat != V4L2_PIX_FMT_MJPEG) {
        fprintf(stderr, "Only supports pixel format V4L2_PIX_FMT_MJPEG for now\n");
        goto error;
    }

    struct v4l2_fract timeperframe = enumerate_and_select_frame_interval(cam->fd, pix);

    video_capture_set_frame_interval(cam->fd, &timeperframe);

    float sec = (float)timeperframe.numerator   / (float)timeperframe.denominator;
    float fps = (float)timeperframe.denominator / (float)timeperframe.numerator;
    fprintf(stderr, "Current frame interval: %fsec, %ffps\n", sec, fps);

    cam->mapped_buffer_count = video_capture_request_and_map_buffers(cam->fd, sizeof(cam->buffers)/sizeof(cam->buffers[0]), cam->buffers);
    for (__u32 i = 0; i < cam->mapped_buffer_count; ++i) {
        if (!video_capture_queue_buffer(cam->fd, i)) goto error;
    }

    if (!camera_start_streaming(cam)) goto error;

    return true;

error:
    camera_deinit(cam);
    return false;
}

