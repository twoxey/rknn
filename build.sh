#!/bin/sh

set -xe

cd $(dirname $0)

COMMON_FLAGS="-Wall -Wextra -D DISABLE_LIBJPEG -Wno-unused"

# incompatible pointer error: utils/image_utils.c:634
gcc -c -x c -o utils.o \
    $COMMON_FLAGS \
    -Wno-incompatible-pointer-types \
    -include math.h \
    -I librga/include \
    -I stb_image \
    -I utils \
    -include file_utils.c \
    -include image_utils.c \
    -include image_drawing.c \
    /dev/null

g++ -o main \
    $COMMON_FLAGS \
    -I utils \
    -I rknpu2/include \
    main.cpp \
    yolo11.cc \
    postprocess.cc \
    utils.o \
    librga/Linux/aarch64/librga.a \
    rknpu2/Linux/aarch64/librknnrt.so \
