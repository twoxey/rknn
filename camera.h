#include <stdbool.h>
#include <stddef.h>

#define PIX_FMT_PRI "%c%c%c%c"
#define PIX_FMT_ARG(f) (f), (f)>>8, (f)>>16, (f)>>24

struct mapped_buffer {
    void* start;
    size_t length;
};

struct pixel_format {
    unsigned int width;
    unsigned int height;
    unsigned int fourcc;
};

struct fraction {
    unsigned int numerator;
    unsigned int denominator;
};

static const unsigned int FORMAT_MJPG;

struct camera* camera_init(const char* device_name);
void camera_deinit(struct camera* cam);
bool camera_start_streaming(struct camera* cam);
bool camera_stop_streaming(struct camera* cam);
int  camera_dequeue_buffer(struct camera* cam, struct mapped_buffer* buffer);
bool camera_queue_buffer(struct camera* cam, int index);

bool camera_enumerate_pixel_formats(struct camera* cam, struct pixel_format* formats, size_t* format_count, unsigned int match_fourcc /*optional*/);
bool camera_enumerate_frame_sizes(struct camera* cam, unsigned int pixelformat, struct pixel_format* formats, size_t* format_count);
bool camera_enumerate_frame_intervals(struct camera* cam, struct pixel_format format, struct fraction* intervals, size_t* interval_count);

//
// Implementation
//

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <linux/videodev2.h>

struct camera {
    bool started;
    int fd;
    struct mapped_buffer buffers[4];
    unsigned int mapped_buffer_count;
    struct v4l2_pix_format device_format;
};

static const unsigned int FORMAT_MJPG = V4L2_PIX_FMT_MJPEG;

static int open_video_capture_device(const char* device_name) {
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

bool camera_enumerate_frame_sizes(struct camera* cam, unsigned int pixelformat, struct pixel_format* formats, size_t* format_count) {
    size_t input_array_count = *format_count;
    *format_count = 0;

    size_t supported_size_count = 0;

    struct v4l2_frmsizeenum frmsizeenum = {};
    frmsizeenum.index = 0;
    frmsizeenum.pixel_format = pixelformat;
    if (ioctl(cam->fd, VIDIOC_ENUM_FRAMESIZES, &frmsizeenum) < 0) {
        fprintf(stderr, "Failed to enumerate device supported frame sizes: %s\n", strerror(errno));
        return false;
    }
    switch (frmsizeenum.type) {
        case V4L2_FRMSIZE_TYPE_DISCRETE:
            while (supported_size_count < input_array_count) {
                formats[supported_size_count].width  = frmsizeenum.discrete.width;
                formats[supported_size_count].height = frmsizeenum.discrete.height;
                formats[supported_size_count].fourcc = frmsizeenum.pixel_format;
                ++supported_size_count;

                ++frmsizeenum.index;
                if (ioctl(cam->fd, VIDIOC_ENUM_FRAMESIZES, &frmsizeenum) < 0) {
                    if (errno != EINVAL) {
                        fprintf(stderr, "Failed to enumerate device supported frame sizes: %s\n", strerror(errno));
                        return false;
                    }
                    break;
                }
            }
            break;

        case V4L2_FRMSIZE_TYPE_STEPWISE:
        case V4L2_FRMSIZE_TYPE_CONTINUOUS:
            fprintf(stderr, "%s:%d: TODO: Not implemented\n", __FILE__, __LINE__);
            return false;
    }

    *format_count = supported_size_count;
    return true;
}

bool camera_enumerate_pixel_formats(struct camera* cam, struct pixel_format* formats, size_t* format_count, unsigned int match_fourcc /*optional*/) {
    bool result = true;

    size_t input_array_count = *format_count;
    *format_count = 0;

    size_t collected_count = 0;

    struct v4l2_fmtdesc fmtdesc = {};
    fmtdesc.index = 0;
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    while (collected_count < input_array_count && ioctl(cam->fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
        if (match_fourcc == 0 || fmtdesc.pixelformat == match_fourcc) {
            size_t array_count = input_array_count - collected_count;
            if (!camera_enumerate_frame_sizes(cam, fmtdesc.pixelformat, &formats[collected_count], &array_count)) {
                fprintf(stderr, "Failed to enumerate frame sizes for format "PIX_FMT_PRI"\n", PIX_FMT_ARG(fmtdesc.pixelformat));
                result = false;
            }
            collected_count += array_count;
        }
        ++fmtdesc.index;
    }
    if (errno != EINVAL) {
        fprintf(stderr, "Failed to enumerate device supported formats: %s\n", strerror(errno));
        result = false;
    }

    *format_count = collected_count;
    return result;
}

bool camera_enumerate_frame_intervals(struct camera* cam, struct pixel_format format, struct fraction* intervals, size_t* interval_count) {
    size_t input_array_count = *interval_count;
    *interval_count = 0;
    size_t supported_interval_count = 0;

    struct v4l2_frmivalenum frmivalenum = {};
    frmivalenum.index = 0;
    frmivalenum.pixel_format = format.fourcc;
    frmivalenum.width = format.width;
    frmivalenum.height = format.height;
    if (ioctl(cam->fd, VIDIOC_ENUM_FRAMEINTERVALS, &frmivalenum) < 0) {
        fprintf(stderr, "Failed to enumerate device supported frame intervals: %s\n", strerror(errno));
        return false;
    }
    switch (frmivalenum.type) {
        case V4L2_FRMIVAL_TYPE_DISCRETE:
            while (supported_interval_count < input_array_count) {
                intervals[supported_interval_count].numerator = frmivalenum.discrete.numerator;
                intervals[supported_interval_count].denominator = frmivalenum.discrete.denominator;
                ++supported_interval_count;

                ++frmivalenum.index;
                if (ioctl(cam->fd, VIDIOC_ENUM_FRAMEINTERVALS, &frmivalenum) < 0) {
                    if (errno != EINVAL) {
                        fprintf(stderr, "Failed to enumerate device supported frame intervals: %s\n", strerror(errno));
                        return false;
                    }
                    break;
                }
            }
            break;
        case V4L2_FRMIVAL_TYPE_STEPWISE:
        case V4L2_FRMIVAL_TYPE_CONTINUOUS:
            fprintf(stderr, "%s:%d: TODO: Not implemented\n", __FILE__, __LINE__);
            return false;
    }

    *interval_count = supported_interval_count;
    return true;
}

static bool video_capture_set_pix_format(int fd, __u32 width, __u32 height, __u32 pixelformat, struct v4l2_pix_format* out_fmt /*optional*/) {
    struct v4l2_format format = {};
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;
    format.fmt.pix.pixelformat = pixelformat;
    if (ioctl(fd, VIDIOC_S_FMT, &format) < 0) {
        fprintf(stderr, "Failed to set device pixel format: %s\n", strerror(errno));
        return false;
    }
    if (out_fmt) *out_fmt = format.fmt.pix;
    return true;
}

static bool video_capture_set_frame_interval(int fd, struct v4l2_fract* timeperframe) {
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

static bool camera_unmap_and_free_buffers(struct camera* cam) {
    for (__u32 i = 0; i < cam->mapped_buffer_count; ++i) {
        if (cam->buffers[i].start) {
            munmap(cam->buffers[i].start, cam->buffers[i].length);
        }
    }
    cam->mapped_buffer_count = 0;

    struct v4l2_requestbuffers requestbuffers = {};
    requestbuffers.count = 0;
    requestbuffers.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    requestbuffers.memory = V4L2_MEMORY_MMAP;
    if (ioctl(cam->fd, VIDIOC_REQBUFS, &requestbuffers) < 0) {
        fprintf(stderr, "Failed to free buffers from device: %s\n", strerror(errno));
        return false;
    }
    return true;
}

static bool camera_request_and_map_buffers(struct camera* cam) {
    __u32 request_count = sizeof(cam->buffers)/sizeof(cam->buffers[0]);
    fprintf(stderr, "requesting %u buffers.\n", request_count);

    struct v4l2_requestbuffers requestbuffers = {};
    requestbuffers.count = request_count;
    requestbuffers.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    requestbuffers.memory = V4L2_MEMORY_MMAP;
    if (ioctl(cam->fd, VIDIOC_REQBUFS, &requestbuffers) < 0) {
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
        if (ioctl(cam->fd, VIDIOC_QUERYBUF, &buf) < 0) {
            fprintf(stderr, "Failed to query mmapped buffer location: %s\n", strerror(errno));
            goto error;
        }
        void* addr = mmap(NULL, buf.length, PROT_READ|PROT_WRITE, MAP_SHARED, cam->fd, buf.m.offset);
        if (addr == MAP_FAILED) {
            fprintf(stderr, "mmap: Failed to map buffer: %s\n", strerror(errno));
            goto error;
        }

        cam->buffers[i].start = addr;
        cam->buffers[i].length = buf.length;
    }

    cam->mapped_buffer_count = requestbuffers.count;
    return true;

error:
    camera_unmap_and_free_buffers(cam);
    return false;
}

static bool video_capture_queue_buffer(int fd, __u32 index) {
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

static bool video_capture_dequeue_buffer(int fd, struct v4l2_buffer* buf) {
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
    if (!cam->started) return -1;
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
        if (!cam->mapped_buffer_count) {
            if (!camera_request_and_map_buffers(cam)) {
                fprintf(stderr, "Failed to request and map buffers from device\n");
                return false;
            }
            for (__u32 i = 0; i < cam->mapped_buffer_count; ++i) {
                if (!video_capture_queue_buffer(cam->fd, i)) {
                    return false;
                }
            }
        }
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

bool camera_set_pixel_format(struct camera* cam, struct pixel_format format) {
    if (cam->started) {
        if (!camera_stop_streaming(cam)) return false;
        if (!camera_set_pixel_format(cam, format)) return false;
        camera_start_streaming(cam);
        return true;
    }

    struct v4l2_pix_format pix;
    if (!video_capture_set_pix_format(cam->fd, format.width, format.height, format.fourcc, &pix)) {
        if (cam->mapped_buffer_count == 0) {
            // we have no mapped buffers
            fprintf(stderr, "Error: camera failed to set pixel format\n");
            return false;
        }
        // we have mapped buffers, free them first an try again
        // https://docs.kernel.org/userspace-api/media/v4l/buffers.html
        fprintf(stderr, "camera_set_pixel_format(): Failed to change format directly, try to free first and set again.\n");
        if (!camera_unmap_and_free_buffers(cam)) {
            fprintf(stderr, "camera_set_pixel_format(): Failed to unmap and release the buffers\n");
            return false;
        }
        return camera_set_pixel_format(cam, format);
    }

    fprintf(stderr, "[INFO] Camera set pixel format, current format:\n");
    fprintf(stderr, "  width:  %u\n", pix.width);
    fprintf(stderr, "  height: %u\n", pix.height);
    fprintf(stderr, "  format: "PIX_FMT_PRI"\n", PIX_FMT_ARG(pix.pixelformat));
    fprintf(stderr, "  field: %d\n", pix.field);
    fprintf(stderr, "  bytesperline: %d\n", pix.bytesperline);
    fprintf(stderr, "  sizeimage: %d\n", pix.sizeimage);
    fprintf(stderr, "  colorspace: %d\n", pix.colorspace);
    fprintf(stderr, "  priv: %d\n", pix.priv == V4L2_PIX_FMT_PRIV_MAGIC);

    return true;
}

void camera_deinit(struct camera* cam) {
    if (!cam) return;
    camera_stop_streaming(cam);
    camera_unmap_and_free_buffers(cam);
    if (cam->fd >= 0) close(cam->fd);
    free(cam);
}

static int stdin_get_number(int max) {
    int result = 0;

    int ch = getchar();
    if (ch != '\n') {
        getchar(); // drop LF

        int n = ch - '0';
        if (n < 0 || n > 9) {
            fprintf(stderr, "Expects a character from 0-9, got %c (%u)\n", ch, ch);
            goto end;
        }
        if (n >= max) {
            fprintf(stderr, "Selected index out of bounce\n");
            goto end;
        }
        result = n;
    }
end:
    return result;
}

struct camera* camera_init(const char* device_name) {
    fprintf(stderr, "Open device: %s\n", device_name);

    struct camera* cam = calloc(1, sizeof(struct camera));
    if (!cam) {
        fprintf(stderr, "calloc() failed: %s\n", strerror(errno));
        goto error;
    }

    cam->fd = open_video_capture_device(device_name);
    if (cam->fd < 0) goto error;

    struct pixel_format formats[20];
    size_t formats_count = 20;
    camera_enumerate_pixel_formats(cam, formats, &formats_count, FORMAT_MJPG);
    if (formats_count == 0) {
        fprintf(stderr, "Camera does not support MJPG stream format\n");
        fprintf(stderr, "Only supports pixel format V4L2_PIX_FMT_MJPEG for now\n");
        goto error;
    }

    fprintf(stderr, "Supported format count: %zu\n", formats_count);
    for (size_t i = 0; i < formats_count; ++i) {
        fprintf(stderr, "  [%zu] "PIX_FMT_PRI", %ux%u\n", i, PIX_FMT_ARG(formats[i].fourcc), formats[i].width, formats[i].height);
    }

    fprintf(stderr, "Select a pixel format [0]: \n");
    //int idx = stdin_get_number(formats_count);
    int idx = 0;
    struct pixel_format fmt = formats[idx];
    fprintf(stderr, "Selected frame size: %ux%u\n", fmt.width, fmt.height);

    if (!camera_set_pixel_format(cam, fmt)) {
        fprintf(stderr, "Only supports pixel format V4L2_PIX_FMT_MJPEG for now\n");
        goto error;
    }

    struct fraction intervals[10];
    size_t interval_count = 10;
    if (!camera_enumerate_frame_intervals(cam, fmt, intervals, &interval_count)) {
        goto error;
    }

    fprintf(stderr, "Supported frame intervals: %lu\n", interval_count);
    for (size_t i = 0; i < interval_count; ++i) {
        struct fraction fract = intervals[i];
        float sec = (float)fract.numerator   / (float)fract.denominator;
        float fps = (float)fract.denominator / (float)fract.numerator;
        fprintf(stderr, "  [%lu] %fsec, %ffps\n", i, sec, fps);
    }

    fprintf(stderr, "Select a frame interval: ");
    //idx = stdin_get_number(interval_count);
    idx = 0;

    struct v4l2_fract timeperframe = {intervals[idx].numerator, intervals[idx].denominator};
    video_capture_set_frame_interval(cam->fd, &timeperframe);
    float sec = (float)timeperframe.numerator   / (float)timeperframe.denominator;
    float fps = (float)timeperframe.denominator / (float)timeperframe.numerator;
    fprintf(stderr, "Current frame interval: %fsec, %ffps\n", sec, fps);

    if (!camera_start_streaming(cam)) goto error;

    return cam;

error:
    camera_deinit(cam);
    return NULL;
}

