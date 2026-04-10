#include "camera.h"
#include "yolo11.h"
#include "stb_image.h"

bool load_jpeg_image_from_memory(const unsigned char* buf, size_t len, image_buffer_t* image) {
    *image = (image_buffer_t){};

    int w, h, c;
    stbi_uc *pixeldata = stbi_load_from_memory(buf, len, &w, &h, &c, 0);
    if (!pixeldata) {
        printf("error: failed to load jpeg image from memory\n");
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

int main(void) {
    const char* model_path = "yolo11.rknn";

    rknn_app_context_t rknn_app_ctx = {};
    int ret = init_yolo11_model(model_path, &rknn_app_ctx);
    if (ret != 0) {
        printf("init_yolo11_model fail! ret=%d model_path=%s\n", ret, model_path);
        goto end;
    }

    struct camera* cam = camera_init("/dev/video0");
    if (cam) goto end;

    while (true) {
        struct mapped_buffer buffer;
        int index = camera_dequeue_buffer(cam, &buffer);
        if (index < 0) break;

        image_buffer_t image;
        if (!load_jpeg_image_from_memory((unsigned char*)buffer.start, buffer.length, &image)) break;

        if (!camera_queue_buffer(cam, index)) break;

        object_detect_result_list od_results;
        int ret = inference_yolo11_model(&rknn_app_ctx, &image, &od_results);
        if (ret != 0) {
            printf("init_yolo11_model fail! ret=%d\n", ret);
            break;
        }

        printf("od result count: %d\n", od_results.count);
        for (int i = 0; i < od_results.count; i++) {
            object_detect_result *det_result = &(od_results.results[i]);
            printf("  id: %d @ (%d %d %d %d) %.3f\n",
                   det_result->cls_id,
                   det_result->box.left, det_result->box.top,
                   det_result->box.right, det_result->box.bottom,
                   det_result->prop);
        }

        if (image.virt_addr) {
            free(image.virt_addr);
        }
    }

end:
    camera_deinit(cam);

    ret = release_yolo11_model(&rknn_app_ctx);
    if (ret != 0) {
        printf("release_yolo11_model fail! ret=%d\n", ret);
    }

    return 0;
}
