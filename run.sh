#!/bin/sh

set -xe

sudo LD_LIBRARY_PATH=./rknpu2/Linux/aarch64 $@ # > log-$(date -Iseconds).txt
