#!/bin/sh

set -xe

ip a
sudo LD_LIBRARY_PATH=./rknpu2/Linux/aarch64 $@ # > log-$(date -Iseconds).txt
