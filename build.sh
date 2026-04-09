#!/bin/sh

set -xe

cd $(dirname $0)

COMMON_FLAGS="-Wall -Wextra"
INCLUDES_PATHS="-I rknpu2/include -I utils -I stb_image"

if 0
then

g++ $COMMON_FLAGS -Wno-unused $INCLUDES_PATHS -c yolo11.cc
g++ $COMMON_FLAGS -Wno-unused $INCLUDES_PATHS -c postprocess.cc
gcc $COMMON_FLAGS -Wno-unused $INCLUDES_PATHS -c utils/file_utils.c
gcc $COMMON_FLAGS -Wno-unused $INCLUDES_PATHS -c utils/image_utils.c -I librga/include -D DISABLE_LIBJPEG -Wno-incompatible-pointer-types
gcc $COMMON_FLAGS -Wno-unused $INCLUDES_PATHS -c utils/image_drawing.c -include math.h

echo "Building test..."

g++ $COMMON_FLAGS $INCLUDES_PATHS -o yolo11-test \
    yolo11-test.cpp \
    yolo11.o \
    postprocess.o \
    file_utils.o \
    image_utils.o \
    image_drawing.o \
    librga/Linux/aarch64/librga.a \
    rknpu2/Linux/aarch64/librknnrt.so

echo "Building camera..."

gcc $COMMON_FLAGS $INCLUDES_PATHS -c camera.c

g++ -o camera \
    camera.o \
    image_utils.o \
    file_utils.o \
    yolo11.o \
    postprocess.o \
    librga/Linux/aarch64/librga.a \
    rknpu2/Linux/aarch64/librknnrt.so

fi

echo "Building camera server..."

gcc $COMMON_FLAGS -Wno-unused -o camera_webserver camera_webserver.c


